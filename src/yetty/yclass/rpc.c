/* RPC runtime — packed-header wire, op enum, uthash translations. */

#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport-pty.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/channel.h>

#include <ut/uthash.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECTS 256
/* Max wire body for one RPC frame. Figure bodies are whole drawable-list /
 * complex payloads shipped one-way (a yplot/yimage figure is multi-MB:
 * 800x800 RGBA alone is ~2.5 MB), so this must comfortably exceed 64 KB or
 * rpc_write_request rejects them. The two static scratch buffers in
 * rpc_server_run are sized from this — they are BSS (demand-paged), and the
 * production receiver is the DCS server, which reads the body straight from
 * the wire statemachine rather than into a fixed buffer. */
#define BUF_MAX (64u * 1024u * 1024u)
/* GET_CLASS responses are small (a class's method name->id table), and the
 * translate_class parser reads them into a STACK buffer — so this must stay
 * modest regardless of BUF_MAX, or raising BUF_MAX overflows the stack. */
#define GET_CLASS_RESP_MAX 65536u

/* -------- server (process-global) state ---------------------------- */

struct object_entry {
    uint64_t handle;
    void *ptr;
};

struct skel_lookup_node {
    yetty_yclass_rpc_skel_lookup_fn fn;
    struct skel_lookup_node *next;
};

struct skel_cache_entry {
    yetty_yclass_method_slot slot;
    yetty_yclass_rpc_skel_fn fn;
    UT_hash_handle hh;
};

struct rpc_server_state {
    struct object_entry objects[MAX_OBJECTS];
    size_t object_count;
    uint64_t next_handle;
    /* Handle of the server's designated root object (0 = none). Returned by
     * RPC_OP_GET_ROOT so remote producers can proxy the host's pre-existing
     * root rather than minting a detached one. Set via rpc_set_root. */
    uint64_t root_handle;

    /* Per-module skel lookups, chained. First one that returns
     * non-NULL wins. Result cached per-slot. slot values are sparse
     * (domain id in bits 27..24), so use a hash, not a flat array. */
    struct skel_lookup_node *lookup_chain;
    struct skel_cache_entry *skel_cache;
};

static struct rpc_server_state *server(void)
{
    static struct rpc_server_state s = {0};
    return &s;
}

struct yetty_ycore_void_result yetty_yclass_rpc_init(void)
{
    struct rpc_server_state *s = server();
    /* Idempotent one-shot bootstrap: the object table holds caller-
     * owned objects we cannot free here (we never took ownership), and
     * the skel-cache / lookup-chain are populated by the per-module
     * yetty_<module>_register() calls at server bring-up — clearing them
     * would orphan the mappings. Initialise next_handle to 1 on the
     * first call only; subsequent calls are no-ops so a stale "rpc_init
     * looks like a reset" expectation can't silently invalidate live
     * handles. */
    if (s->next_handle != 0) {
        return YETTY_OK_VOID();
    }
    s->next_handle = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yclass_rpc_add_skel_lookup(yetty_yclass_rpc_skel_lookup_fn fn)
{
    if (!fn) {
        return YETTY_ERR(yetty_ycore_void, "rpc_add_skel_lookup: NULL fn");
    }
    struct skel_lookup_node *node = calloc(1, sizeof(*node));
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "rpc_add_skel_lookup: calloc failed");
    }
    struct rpc_server_state *s = server();
    node->fn = fn;
    node->next = s->lookup_chain;
    s->lookup_chain = node;
    return YETTY_OK_VOID();
}

struct yetty_yclass_rpc_skel_fn_result yetty_yclass_rpc_skel_for(yetty_yclass_method_slot slot)
{
    struct rpc_server_state *s = server();
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_yclass_rpc_skel_fn, "skel_for: METHOD_SLOT_UNDEFINED");
    }

    struct skel_cache_entry *e = NULL;
    HASH_FIND(hh, s->skel_cache, &slot, sizeof(slot), e);
    if (e) {
        return YETTY_OK(yetty_yclass_rpc_skel_fn, e->fn);
    }

    /* Walk the chain. First hit wins. */
    yetty_yclass_rpc_skel_fn fn = NULL;
    for (struct skel_lookup_node *n = s->lookup_chain; n; n = n->next) {
        fn = n->fn(slot);
        if (fn) {
            break;
        }
    }
    if (!fn) {
        return YETTY_ERR(yetty_yclass_rpc_skel_fn, "skel_for: no skel registered for this slot");
    }

    e = calloc(1, sizeof(*e));
    if (!e) {
        /* Cache alloc failed but we have the fn — return it OK; next
         * call will retry the chain walk and try caching again. */
        return YETTY_OK(yetty_yclass_rpc_skel_fn, fn);
    }
    e->slot = slot;
    e->fn = fn;
    HASH_ADD(hh, s->skel_cache, slot, sizeof(slot), e);
    return YETTY_OK(yetty_yclass_rpc_skel_fn, fn);
}

struct yetty_yclass_handle_result yetty_yclass_rpc_register_object(void *obj)
{
    struct rpc_server_state *s = server();
    if (s->object_count >= MAX_OBJECTS) {
        return YETTY_ERR(yetty_yclass_handle, "rpc_register_object: object table full");
    }
    uint64_t h = s->next_handle++;
    s->objects[s->object_count].handle = h;
    s->objects[s->object_count].ptr = obj;
    s->object_count++;
    return YETTY_OK(yetty_yclass_handle, h);
}

struct yetty_yclass_handle_result yetty_yclass_rpc_register_object_dedup(void *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_yclass_handle, "rpc_register_object_dedup: NULL object");
    }
    struct rpc_server_state *s = server();
    for (size_t i = 0; i < s->object_count; ++i) {
        if (s->objects[i].ptr == obj) {
            return YETTY_OK(yetty_yclass_handle, s->objects[i].handle);
        }
    }
    return yetty_yclass_rpc_register_object(obj);
}

struct yetty_yclass_handle_result yetty_yclass_rpc_set_root(void *obj)
{
    struct yetty_yclass_handle_result reg_r = yetty_yclass_rpc_register_object(obj);
    if (YETTY_IS_ERR(reg_r)) {
        return reg_r;
    }
    server()->root_handle = reg_r.value;
    return reg_r;
}

struct yetty_yclass_void_ptr_result yetty_yclass_rpc_handle_resolve(uint64_t h)
{
    if (!h) {
        return YETTY_ERR(yetty_yclass_void_ptr, "rpc_handle_resolve: handle is 0");
    }
    struct rpc_server_state *s = server();
    for (size_t i = 0; i < s->object_count; ++i) {
        if (s->objects[i].handle == h) {
            return YETTY_OK(yetty_yclass_void_ptr, s->objects[i].ptr);
        }
    }
    return YETTY_ERR(yetty_yclass_void_ptr, "rpc_handle_resolve: handle not registered");
}

/* -------- io helpers (transport-routed) ---------------------------- */

/* Loop on transport.recv until exactly n bytes are filled. recv may
 * return short; coro-based transports yield internally. Returns 0 on
 * success, -1 on EOF / error / size_result error. */
static int read_full(struct yetty_yclass_transport *t, void *buf, size_t n)
{
    char *p = buf;
    while (n) {
        struct yetty_ycore_size_result r = t->ops->recv(t, p, n);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
            return -1;
        }
        if (r.value == 0) {
            return -1;
        }
        p += r.value;
        n -= r.value;
    }
    return 0;
}

/* Loop on transport.send until exactly n bytes are out. Same shape. */
static int write_full(struct yetty_yclass_transport *t, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        struct yetty_ycore_size_result w = t->ops->send(t, p, n);
        if (YETTY_IS_ERR(w)) {
            yetty_ycore_error_destroy(w.error);
            return -1;
        }
        if (w.value == 0) {
            return -1;
        }
        p += w.value;
        n -= w.value;
    }
    return 0;
}

/* -------- admin handlers (server side) ----------------------------- */

/* Copy `len` wire bytes into a freshly-allocated NUL-terminated C
 * string. No fixed cap — qualified yclass names can be long when a
 * module/class/slot chain is deep, and silent truncation was making
 * lookups fail invisibly. Caller owns the returned pointer.
 *
 * Rejects embedded NUL bytes: the registry hash keys are strlen()
 * /uthash string keys, so "valid\0junk" would silently resolve as
 * "valid" — a malformed wire body could masquerade as a shorter
 * legitimate name. Length-prefixed names with embedded NULs are
 * always malformed, so fail closed.
 *
 * Returns NULL on alloc failure OR embedded NUL; the caller should
 * treat that the same as a lookup miss (return 0 from the handler). */
static char *dup_wire_name(const void *bytes, size_t len)
{
    if (len && memchr(bytes, 0, len) != NULL) {
        return NULL;
    }
    char *s = malloc(len + 1);
    if (!s) {
        return NULL;
    }
    if (len) {
        memcpy(s, bytes, len);
    }
    s[len] = 0;
    return s;
}

static struct yetty_ycore_size_result handle_resolve_slot(const void *body, size_t body_len,
                                                          void *resp, size_t resp_max)
{
    char *name = dup_wire_name(body, body_len);
    if (!name) {
        return YETTY_ERR(yetty_ycore_size, "resolve_slot: dup_wire_name failed");
    }
    /* An unknown slot name is a valid result, not a transport error: the
     * wire carries UINT32_MAX to tell the client "no such slot". Only a
     * genuine inability to emit the response is an error. */
    struct yetty_yclass_method_slot_result sr = yetty_yclass_method_slot_by_qname(name);
    uint32_t out;
    if (YETTY_IS_ERR(sr)) {
        yetty_ycore_error_destroy(sr.error);
        out = UINT32_MAX;
    } else {
        out = (uint32_t)sr.value;
    }
    if (resp_max < sizeof(out)) {
        free(name);
        return YETTY_ERR(yetty_ycore_size, "resolve_slot: resp buffer too small");
    }
    memcpy(resp, &out, sizeof(out));
    ydebug("resolve_slot('%s') -> %u", name, out);
    free(name);
    return YETTY_OK(yetty_ycore_size, sizeof(out));
}

struct get_class_ctx {
    uint8_t *out;
    size_t off;
    size_t cap;
};

static void get_class_emit(const char *name, yetty_yclass_method_slot slot, void *ud)
{
    struct get_class_ctx *gc = ud;
    size_t name_len = strlen(name);
    /* Wire length field is u16 — silently truncating it while copying
     * the full name desyncs the client (it parses `nl` bytes as the
     * name, then reads u32 rid from the middle of what's actually
     * still name). Skip with a warning instead; the client falls back
     * to per-slot RESOLVE_SLOT for any entry we drop here. */
    if (name_len > UINT16_MAX) {
        ywarn("get_class_emit: slot name '%.32s...' (%zu bytes) exceeds u16 wire length; "
              "skipping entry",
              name, name_len);
        return;
    }
    size_t need = 2 + name_len + 4;
    if (gc->off + need > gc->cap) {
        return;
    }
    uint16_t nl = (uint16_t)name_len;
    memcpy(gc->out + gc->off, &nl, 2);
    gc->off += 2;
    memcpy(gc->out + gc->off, name, name_len);
    gc->off += name_len;
    uint32_t rid = (uint32_t)slot;
    memcpy(gc->out + gc->off, &rid, 4);
    gc->off += 4;
}

static struct yetty_ycore_size_result handle_get_class(const void *body, size_t body_len,
                                                       void *resp, size_t resp_max)
{
    char *name = dup_wire_name(body, body_len);
    if (!name) {
        return YETTY_ERR(yetty_ycore_size, "get_class: dup_wire_name failed");
    }
    struct yetty_yclass_ptr_result cr = yetty_yclass_by_name(name);
    if (YETTY_IS_ERR(cr)) {
        free(name);
        return YETTY_ERR(yetty_ycore_size, "get_class: class_by_name failed", cr);
    }
    struct get_class_ctx gc = {resp, 0, resp_max};
    struct yetty_ycore_void_result fe = yetty_yclass_for_each_slot(cr.value, get_class_emit, &gc);
    if (YETTY_IS_ERR(fe)) {
        free(name);
        return YETTY_ERR(yetty_ycore_size, "get_class: for_each_slot failed", fe);
    }
    ydebug("get_class('%s') -> %zu entries (%zu bytes)", name, gc.off / 6, gc.off);
    free(name);
    return YETTY_OK(yetty_ycore_size, gc.off);
}

static struct yetty_ycore_size_result handle_create(const void *body, size_t body_len, void *resp,
                                                    size_t resp_max)
{
    char *name = dup_wire_name(body, body_len);
    if (!name) {
        return YETTY_ERR(yetty_ycore_size, "create: dup_wire_name failed");
    }
    struct yetty_yclass_ptr_result cr = yetty_yclass_by_name(name);
    if (YETTY_IS_ERR(cr)) {
        free(name);
        return YETTY_ERR(yetty_ycore_size, "create: class_by_name failed", cr);
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yclass_object_alloc(cr.value);
    if (YETTY_IS_ERR(obj_r)) {
        free(name);
        return YETTY_ERR(yetty_ycore_size, "create: object_alloc failed", obj_r);
    }
    struct yetty_yclass_handle_result rh = yetty_yclass_rpc_register_object(obj_r.value);
    if (YETTY_IS_ERR(rh)) {
        /* Registry rejected (table full, …) — free the alloc so we
         * don't orphan it, then surface the failure. */
        struct yetty_ycore_void_result fr = yetty_yclass_object_free(obj_r.value);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
        ywarn("create('%s'): rpc_register_object failed, allocation freed", name);
        free(name);
        return YETTY_ERR(yetty_ycore_size, "create: rpc_register_object failed", rh);
    }
    uint64_t h = rh.value;
    if (resp_max < sizeof(h)) {
        free(name);
        return YETTY_ERR(yetty_ycore_size, "create: resp buffer too small");
    }
    memcpy(resp, &h, sizeof(h));
    ydebug("create('%s') -> handle=%llu", name, (unsigned long long)h);
    free(name);
    return YETTY_OK(yetty_ycore_size, sizeof(h));
}

/* -------- request dispatch (transport-agnostic) -------------------- */

/* Dispatch one already-assembled yrpc request frame. Used by
 * rpc_server_run (transport loop) and by the DCS-based server handler
 * (terminal-side, wire_statemachine integration) — same per-request
 * shape, just different transport plumbing. Returns resp_len. */
struct yetty_ycore_size_result yetty_yclass_rpc_dispatch_one(uint32_t header, const void *body,
                                                             size_t body_len, void *resp,
                                                             size_t resp_max)
{
    enum yetty_yclass_rpc_op op = YETTY_YCLASS_RPC_HDR_OP(header);
    uint32_t id = YETTY_YCLASS_RPC_HDR_ID(header);

    switch (op) {
    /* CALL and CALL_ONEWAY dispatch identically; they differ only in whether
     * the SERVER LOOP writes the response (it skips it for one-way). */
    case YETTY_YCLASS_RPC_OP_CALL:
    case YETTY_YCLASS_RPC_OP_CALL_ONEWAY: {
        struct yetty_yclass_rpc_skel_fn_result sr =
            yetty_yclass_rpc_skel_for((yetty_yclass_method_slot)id);
        if (YETTY_IS_OK(sr)) {
            ydebug("CALL slot=%u body_len=%zu oneway=%d", id, body_len,
                   op == YETTY_YCLASS_RPC_OP_CALL_ONEWAY);
            /* The skel is an externally-typed wire bridge returning a
             * raw resp length (size_t), not a Result. */
            return YETTY_OK(yetty_ycore_size, sr.value(body, body_len, resp, resp_max));
        }
        return YETTY_ERR(yetty_ycore_size, "dispatch_one: CALL skel_for failed", sr);
    }
    case YETTY_YCLASS_RPC_OP_RESOLVE_SLOT:
        return handle_resolve_slot(body, body_len, resp, resp_max);
    case YETTY_YCLASS_RPC_OP_GET_CLASS:
        return handle_get_class(body, body_len, resp, resp_max);
    case YETTY_YCLASS_RPC_OP_CREATE:
        return handle_create(body, body_len, resp, resp_max);
    case YETTY_YCLASS_RPC_OP_GET_ROOT: {
        uint64_t h = server()->root_handle;
        if (resp_max < sizeof(h)) {
            return YETTY_ERR(yetty_ycore_size, "get_root: resp buffer too small");
        }
        memcpy(resp, &h, sizeof(h));
        ydebug("get_root -> handle=%llu", (unsigned long long)h);
        return YETTY_OK(yetty_ycore_size, sizeof(h));
    }
    default:
        return YETTY_ERR(yetty_ycore_size, "dispatch_one: unknown op");
    }
}

/* -------- server loop ---------------------------------------------- */

struct yetty_ycore_void_result yetty_yclass_rpc_server_run(struct yetty_yclass_transport *transport)
{
    struct rpc_server_state *gs = server();
    static uint8_t body[BUF_MAX];
    static uint8_t resp[BUF_MAX];

    (void)gs;
    if (!transport) {
        return YETTY_ERR(yetty_ycore_void, "rpc_server_run: NULL transport");
    }
    for (;;) {
        uint32_t header = 0, body_len = 0;
        if (read_full(transport, &header, 4) < 0) {
            return YETTY_OK_VOID(); /* clean peer disconnect */
        }
        if (read_full(transport, &body_len, 4) < 0) {
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short read on body_len");
        }
        if (body_len > BUF_MAX) {
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: body_len exceeds BUF_MAX");
        }
        if (body_len && read_full(transport, body, body_len) < 0) {
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short read on body");
        }

        /* A dispatch error is per-request, not fatal to the serve loop:
         * log it server-side and reply with a zero-length response so the
         * client maps it to "remote impl returned error" and the loop
         * stays up for the next frame. */
        struct yetty_ycore_size_result dispatch_res =
            yetty_yclass_rpc_dispatch_one(header, body, body_len, resp, BUF_MAX);
        uint32_t resp_len = 0;
        if (YETTY_IS_OK(dispatch_res)) {
            resp_len = (uint32_t)dispatch_res.value;
        } else {
            yetty_ycore_error_print(stderr, "[server] dispatch", dispatch_res.error);
            yetty_ycore_error_destroy(dispatch_res.error);
        }

        /* One-way calls get NO response — the client never reads one, so
         * writing it would desync the lockstep stream. */
        if (YETTY_YCLASS_RPC_HDR_OP(header) == YETTY_YCLASS_RPC_OP_CALL_ONEWAY) {
            continue;
        }

        if (write_full(transport, &resp_len, 4) < 0) {
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short write on resp_len");
        }
        if (resp_len && write_full(transport, resp, resp_len) < 0) {
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short write on resp");
        }
    }
}

/* -------- client session ------------------------------------------- */

struct translated_class {
    char *name;
    UT_hash_handle hh;
};

struct remote_id_entry {
    yetty_yclass_method_slot local_slot;
    uint32_t remote_id;
    UT_hash_handle hh;
};

/* Qualified-name → remote id. The name-keyed cache backs
 * ensure_remote_id_by_name: a pure remote client resolves slots by the
 * canonical wire name compiled into its stubs and needs NO local slot
 * table at all (no class registration, no method_slot_get). */
struct remote_name_entry {
    char *qualified_name; /* owned key */
    uint32_t remote_id;
    UT_hash_handle hh;
};

/* One in-flight pipelined void call awaiting its response frame. The
 * qualified name is the stub's compile-time string literal — borrowed,
 * never freed. FIFO: the wire is one ordered stream, responses arrive in
 * request order. */
struct pending_call {
    const char *qualified_name;
    struct pending_call *next;
};

struct yetty_yclass_rpc_session {
    struct yetty_yclass_transport *transport; /* owned */
    /* Local slot → remote id. Local slots are sparse (domain id in
     * upper bits), so hash by slot rather than flat-array. */
    struct remote_id_entry *remote_ids;
    struct remote_name_entry *remote_names; /* by qualified slot name */
    struct translated_class *translated;    /* by class name */

    /* ASYNC pipelined mode — set when the transport declares
     * recv_available (its inbound bytes arrive via an external event
     * loop's pump; blocking recv after attach is forbidden). Void wire
     * calls send without waiting; completions drain non-blockingly in
     * request order and failures go to the error sink. */
    int async_mode;
    struct pending_call *pending_head;
    struct pending_call *pending_tail;
    size_t pending_count;
    /* Monotonic count of pipelined calls whose response frame has drained.
     * Responses arrive in request order, so an embedder that snapshots
     * (completed_total + pending_count) when it queues a call knows that
     * call is applied once completed_total reaches the snapshot — without
     * waiting for the WHOLE pipeline to empty (which a sustained feed never
     * lets happen). */
    uint64_t completed_total;
    /* Response-frame reassembly (u32 len | payload, back to back). */
    uint8_t *inbound_buf;
    size_t inbound_len;
    size_t inbound_cap;
    /* Failure delivery for pipelined calls. NULL → log + destroy. The
     * sink takes ownership of the error chain. */
    yetty_yclass_rpc_error_sink_fn error_sink;
    void *error_sink_userdata;

    /* Root proxy minted by session_attach_root — owned by the session,
     * freed in session_destroy (a proxy is a plain calloc'd wrapper; it
     * does not dereference the session on free). */
    struct yetty_yclass_object *root_proxy;

    /* Connection stack owned by a zero-arg yetty_yclass_rpc_connect(): the
     * session created the whole thing (pty transport + multiplexed
     * connection + its own dynamic RPC channel) and tears it down on
     * destroy. NULL when the caller supplied the transport/connection
     * (connect_transport / connect_channel) — then only the channel (if we
     * opened it) is ours to close. */
    struct yetty_ywire_channel *owned_channel;
    struct yetty_ywire_connection *owned_connection;
    struct yetty_yclass_transport_pty *owned_pty;
};

struct yetty_ywire_connection *yetty_yclass_rpc_session_connection(
    struct yetty_yclass_rpc_session *s)
{
    /* NULL when the caller supplied the transport/connection — then the
     * caller already holds the connection and pumps it itself. */
    return s ? s->owned_connection : NULL;
}

struct yetty_yclass_rpc_session_ptr_result yetty_yclass_rpc_session_create(
    struct yetty_yclass_transport *transport)
{
    if (!transport) {
        return YETTY_ERR(yetty_yclass_rpc_session_ptr, "session_create: NULL transport");
    }
    struct yetty_yclass_rpc_session *s = calloc(1, sizeof(*s));
    if (!s) {
        return YETTY_ERR(yetty_yclass_rpc_session_ptr, "session_create: calloc failed");
    }
    s->transport = transport;
    s->async_mode = transport->ops->recv_available != NULL;
    return YETTY_OK(yetty_yclass_rpc_session_ptr, s);
}

struct yetty_ycore_void_result yetty_yclass_rpc_session_destroy(struct yetty_yclass_rpc_session *s)
{
    if (!s) {
        return YETTY_OK_VOID();
    }
    struct translated_class *cur, *tmp;
    HASH_ITER(hh, s->translated, cur, tmp)
    {
        HASH_DEL(s->translated, cur);
        free(cur->name);
        free(cur);
    }
    struct remote_id_entry *rcur, *rtmp;
    HASH_ITER(hh, s->remote_ids, rcur, rtmp)
    {
        HASH_DEL(s->remote_ids, rcur);
        free(rcur);
    }
    struct remote_name_entry *ncur, *ntmp;
    HASH_ITER(hh, s->remote_names, ncur, ntmp)
    {
        HASH_DEL(s->remote_names, ncur);
        free(ncur->qualified_name);
        free(ncur);
    }
    /* Best-effort: surface completions that already arrived, then drop the
     * rest — the transport dies with the session, nothing more can come. */
    if (s->async_mode) {
        struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(s);
        if (YETTY_IS_ERR(pump_res)) {
            yetty_ycore_error_destroy(pump_res.error);
        }
    }
    while (s->pending_head) {
        struct pending_call *dead = s->pending_head;
        s->pending_head = dead->next;
        free(dead);
    }
    free(s->inbound_buf);
    free(s->root_proxy);
    /* Best-effort cleanup: still free the session even if transport
     * destroy fails — propagate the error after freeing so resources
     * are released either way (multi-step *_destroy pattern). */
    struct yetty_ycore_void_result td = YETTY_OK_VOID();
    if (s->transport && s->transport->ops->destroy) {
        td = s->transport->ops->destroy(s->transport);
    }
    /* Close our dynamic channel (graceful CLOSE), then tear down the
     * connection stack if this session owns it (zero-arg connect). */
    if (s->owned_channel) {
        struct yetty_ycore_void_result cc = yetty_ywire_channel_close(s->owned_channel);
        if (YETTY_IS_ERR(cc)) {
            yetty_ycore_error_destroy(cc.error);
        }
    }
    if (s->owned_connection) {
        struct yetty_ycore_void_result dc = yetty_ywire_connection_destroy(s->owned_connection);
        if (YETTY_IS_ERR(dc)) {
            yetty_ycore_error_destroy(dc.error);
        }
    }
    if (s->owned_pty) {
        struct yetty_ycore_void_result dp = yetty_yclass_transport_pty_destroy(s->owned_pty);
        if (YETTY_IS_ERR(dp)) {
            yetty_ycore_error_destroy(dp.error);
        }
    }
    free(s);
    if (YETTY_IS_ERR(td)) {
        return YETTY_ERR(yetty_ycore_void, "session_destroy: transport destroy failed", td);
    }
    return YETTY_OK_VOID();
}

struct uint32_result yetty_yclass_rpc_session_remote_id(struct yetty_yclass_rpc_session *s,
                                                        yetty_yclass_method_slot slot)
{
    if (!s) {
        return YETTY_ERR(uint32, "remote_id: NULL session");
    }
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e) {
        return YETTY_ERR(uint32, "remote_id: slot not cached in this session");
    }
    return YETTY_OK(uint32, e->remote_id);
}

struct yetty_ycore_void_result yetty_yclass_rpc_session_set_remote_id(
    struct yetty_yclass_rpc_session *s, yetty_yclass_method_slot slot, uint32_t remote_id)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "set_remote_id: NULL session");
    }
    /* Public API — direct callers can otherwise poison the xlat table
     * with values that translate_class now refuses to cache:
     *   - METHOD_SLOT_UNDEFINED as the local slot has nothing to map.
     *   - remote_id == UINT32_MAX collides with REMOTE_ID_UNRESOLVED,
     *     making ensure_remote_id retry forever (or worse, fail the
     *     stub if it's read back as cached).
     *   - remote_id > RPC_ID_MASK doesn't fit in the wire id field
     *     and would be rejected at rpc_call time, breaking dispatch
     *     for that slot. */
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_void, "set_remote_id: METHOD_SLOT_UNDEFINED local slot");
    }
    if (remote_id == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED ||
        remote_id > YETTY_YCLASS_RPC_ID_MASK) {
        return YETTY_ERR(yetty_ycore_void,
                         "set_remote_id: invalid remote_id (UNRESOLVED or > ID_MASK)");
    }
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) {
            return YETTY_ERR(yetty_ycore_void, "set_remote_id: calloc failed");
        }
        e->local_slot = slot;
        HASH_ADD(hh, s->remote_ids, local_slot, sizeof(yetty_yclass_method_slot), e);
    }
    e->remote_id = remote_id;
    return YETTY_OK_VOID();
}

/* Validate the (op, id, body) request parameters and write the request frame
 * (header + body_len + body) to the session transport. Shared by
 * yetty_yclass_rpc_call and yetty_yclass_rpc_call_alloc, which differ only in
 * how they receive the response. */
static struct yetty_ycore_void_result rpc_write_request(struct yetty_yclass_rpc_session *s,
                                                        enum yetty_yclass_rpc_op op, uint32_t id,
                                                        const void *body, size_t body_len)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: NULL session");
    }
    /* Reject before touching the wire: the body_len is a u32 on the
     * wire and the peer reads into a BUF_MAX-sized static buffer. */
    if (body_len > UINT32_MAX || body_len > BUF_MAX) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: body_len exceeds wire/buffer limit");
    }
    /* The wire id field is 28 bits — RPC_HDR_MAKE silently masks
     * anything larger. */
    if (id > YETTY_YCLASS_RPC_ID_MASK) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: id exceeds 28-bit wire field");
    }
    /* op occupies a 4-bit field; cap at the highest defined op so
     * undefined-but-fits-in-4-bits values are also caught. */
    if (op > YETTY_YCLASS_RPC_OP_CALL_ONEWAY) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: op is not a defined yetty_yclass_rpc_op");
    }
    if (body_len > 0 && body == NULL) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: body_len>0 but body is NULL");
    }

    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(op, id);
    ydebug("op=%u id=%u body_len=%zu", op, id, body_len);

    uint32_t bl = (uint32_t)body_len;
    if (write_full(s->transport, &header, 4) < 0) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: short write on header");
    }
    if (write_full(s->transport, &bl, 4) < 0) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: short write on body_len");
    }
    if (body_len && write_full(s->transport, body, body_len) < 0) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call: short write on body");
    }
    return YETTY_OK_VOID();
}

/* Read and discard `count` bytes from the transport so the next frame read
 * starts aligned after an unwanted/oversized response. */
static struct yetty_ycore_void_result rpc_drain(struct yetty_yclass_transport *transport,
                                                size_t count)
{
    uint8_t drain[256];
    while (count) {
        size_t chunk = count > sizeof(drain) ? sizeof(drain) : count;
        if (read_full(transport, drain, chunk) < 0) {
            return YETTY_ERR(yetty_ycore_void, "rpc_call: short read while draining resp");
        }
        count -= chunk;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_yclass_rpc_call(struct yetty_yclass_rpc_session *s,
                                                     enum yetty_yclass_rpc_op op, uint32_t id,
                                                     const void *body, size_t body_len, void *resp,
                                                     size_t resp_max)
{
    /* A lockstep round-trip while pipelined completions are in flight would
     * read THEIR response frames as its own — refuse loudly. Sync calls are
     * legal in async mode only while the pipeline is empty (the attach
     * window, or after an explicit drain). */
    if (s && s->async_mode && s->pending_count) {
        return YETTY_ERR(yetty_ycore_size,
                         "rpc_call: sync round-trip with pipelined completions pending");
    }
    if (resp_max > 0 && resp == NULL) {
        return YETTY_ERR(yetty_ycore_size, "rpc_call: resp_max>0 but resp is NULL");
    }
    struct yetty_ycore_void_result write_res = rpc_write_request(s, op, id, body, body_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, write_res, "rpc_call: request write failed");

    uint32_t resp_len = 0;
    if (read_full(s->transport, &resp_len, 4) < 0) {
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short read on resp_len");
    }
    if (resp_len > resp_max) {
        /* Drain the oversized payload so the next frame read starts
         * aligned. Without this we'd parse garbage from mid-stream. */
        struct yetty_ycore_void_result drain_res = rpc_drain(s->transport, resp_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, drain_res, "rpc_call: drain oversized resp failed");
        return YETTY_ERR(yetty_ycore_size, "rpc_call: response exceeds caller's resp_max");
    }
    if (resp_len && read_full(s->transport, resp, resp_len) < 0) {
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short read on resp body");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)resp_len);
}

struct yetty_ycore_void_result yetty_yclass_rpc_call_oneway(struct yetty_yclass_rpc_session *s,
                                                            uint32_t id, const void *body,
                                                            size_t body_len)
{
    struct yetty_ycore_void_result write_res =
        rpc_write_request(s, YETTY_YCLASS_RPC_OP_CALL_ONEWAY, id, body, body_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "rpc_call_oneway: request write failed");

    /* No response is read. A buffering transport (DCS) only flushes on recv(),
     * which never happens for a one-way call — flush explicitly so the request
     * actually reaches the wire. Transports that write through in send() expose
     * no flush op (NULL) and need none. */
    if (s->transport->ops->flush) {
        struct yetty_ycore_void_result flush_res = s->transport->ops->flush(s->transport);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "rpc_call_oneway: flush failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yclass_rpc_call_alloc(struct yetty_yclass_rpc_session *s,
                                                           enum yetty_yclass_rpc_op op, uint32_t id,
                                                           const void *body, size_t body_len,
                                                           uint8_t **resp_out, size_t *resp_len_out)
{
    /* Same lockstep guard as yetty_yclass_rpc_call — see there. */
    if (s && s->async_mode && s->pending_count) {
        return YETTY_ERR(yetty_ycore_void,
                         "rpc_call_alloc: sync round-trip with pipelined completions pending");
    }
    if (!resp_out || !resp_len_out) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call_alloc: NULL out parameter");
    }
    *resp_out = NULL;
    *resp_len_out = 0;

    struct yetty_ycore_void_result write_res = rpc_write_request(s, op, id, body, body_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "rpc_call_alloc: request write failed");

    uint32_t resp_len = 0;
    if (read_full(s->transport, &resp_len, 4) < 0) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call_alloc: short read on resp_len");
    }
    /* The peer writes from a BUF_MAX-sized buffer, so a larger length means a
     * corrupt frame; drain and reject rather than honour a hostile size. */
    if (resp_len > BUF_MAX) {
        struct yetty_ycore_void_result drain_res = rpc_drain(s->transport, resp_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res,
                            "rpc_call_alloc: drain oversized resp failed");
        return YETTY_ERR(yetty_ycore_void, "rpc_call_alloc: response exceeds BUF_MAX");
    }
    if (resp_len == 0) {
        return YETTY_OK_VOID(); /* empty response: *resp_out stays NULL */
    }

    uint8_t *resp = malloc(resp_len);
    if (!resp) {
        struct yetty_ycore_void_result drain_res = rpc_drain(s->transport, resp_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drain_res, "rpc_call_alloc: drain after oom failed");
        return YETTY_ERR(yetty_ycore_void, "rpc_call_alloc: response buffer oom");
    }
    if (read_full(s->transport, resp, resp_len) < 0) {
        free(resp);
        return YETTY_ERR(yetty_ycore_void, "rpc_call_alloc: short read on resp body");
    }
    *resp_out = resp;
    *resp_len_out = (size_t)resp_len;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_yclass_rpc_session_ensure_remote_id(struct yetty_yclass_rpc_session *s,
                                                               yetty_yclass_method_slot local_slot)
{
    if (!s) {
        return YETTY_ERR(uint32, "ensure_remote_id: NULL session");
    }
    struct uint32_result cached = yetty_yclass_rpc_session_remote_id(s, local_slot);
    if (YETTY_IS_OK(cached)) {
        return cached;
    }
    /* Not cached — drop the lookup error and do the round-trip. */
    yetty_ycore_error_destroy(cached.error);

    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(local_slot);
    if (YETTY_IS_ERR(nr)) {
        return YETTY_ERR(uint32, "ensure_remote_id: slot name lookup failed", nr);
    }
    const char *name = nr.value;

    uint32_t remote = YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    struct yetty_ycore_size_result nr2 = yetty_yclass_rpc_call(
        s, YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0, name, strlen(name), &remote, sizeof(remote));
    if (YETTY_IS_ERR(nr2)) {
        return YETTY_ERR(uint32, "ensure_remote_id: RESOLVE_SLOT call failed", nr2);
    }
    if (nr2.value != sizeof(remote) || remote == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED) {
        return YETTY_ERR(uint32, "ensure_remote_id: RESOLVE_SLOT returned unresolved");
    }

    struct yetty_ycore_void_result sr =
        yetty_yclass_rpc_session_set_remote_id(s, local_slot, remote);
    if (YETTY_IS_ERR(sr)) {
        /* Caching failed (validation or alloc) — return the resolved
         * id to the caller anyway so this one call can proceed;
         * future calls will retry the round-trip. Log so a persistent
         * cache failure doesn't silently degrade every call to a
         * round-trip. */
        ywarn("ensure_remote_id: cache write for slot=0x%08x rid=%u failed: %s", local_slot, remote,
              sr.error.msg ? sr.error.msg : "(no msg)");
        yetty_ycore_error_destroy(sr.error);
    }
    ydebug("lazy resolve '%s' local=%u remote=%u", name, local_slot, remote);
    return YETTY_OK(uint32, remote);
}

struct uint32_result yetty_yclass_rpc_session_ensure_remote_id_by_name(
    struct yetty_yclass_rpc_session *s, const char *qualified_name)
{
    if (!s) {
        return YETTY_ERR(uint32, "ensure_remote_id_by_name: NULL session");
    }
    if (!qualified_name || !qualified_name[0]) {
        return YETTY_ERR(uint32, "ensure_remote_id_by_name: NULL/empty name");
    }
    struct remote_name_entry *entry = NULL;
    HASH_FIND_STR(s->remote_names, qualified_name, entry);
    if (entry) {
        return YETTY_OK(uint32, entry->remote_id);
    }

    uint32_t remote = YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    struct yetty_ycore_size_result resolve_res =
        yetty_yclass_rpc_call(s, YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0, qualified_name,
                              strlen(qualified_name), &remote, sizeof(remote));
    if (YETTY_IS_ERR(resolve_res)) {
        return YETTY_ERR(uint32, "ensure_remote_id_by_name: RESOLVE_SLOT call failed", resolve_res);
    }
    if (resolve_res.value != sizeof(remote) || remote == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED) {
        return YETTY_ERR(uint32, "ensure_remote_id_by_name: RESOLVE_SLOT returned unresolved");
    }
    if (remote > YETTY_YCLASS_RPC_ID_MASK) {
        return YETTY_ERR(uint32, "ensure_remote_id_by_name: peer sent rid > id-mask");
    }

    /* Cache-add failure is non-fatal — return the resolved id so this call
     * proceeds; the next call simply retries the round-trip. */
    entry = calloc(1, sizeof(*entry));
    if (entry) {
        entry->qualified_name = strdup(qualified_name);
        if (!entry->qualified_name) {
            free(entry);
        } else {
            entry->remote_id = remote;
            HASH_ADD_KEYPTR(hh, s->remote_names, entry->qualified_name,
                            strlen(entry->qualified_name), entry);
        }
    }
    ydebug("lazy resolve by name '%s' remote=%u", qualified_name, remote);
    return YETTY_OK(uint32, remote);
}

struct yetty_ycore_void_result yetty_yclass_rpc_session_seed_remote_id_by_name(
    struct yetty_yclass_rpc_session *s, const char *qualified_name, uint32_t remote_id)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "seed_remote_id_by_name: NULL session");
    }
    if (!qualified_name || !qualified_name[0]) {
        return YETTY_ERR(yetty_ycore_void, "seed_remote_id_by_name: NULL/empty name");
    }
    if (remote_id > YETTY_YCLASS_RPC_ID_MASK) {
        return YETTY_ERR(yetty_ycore_void, "seed_remote_id_by_name: rid > id-mask");
    }
    struct remote_name_entry *entry = NULL;
    HASH_FIND_STR(s->remote_names, qualified_name, entry);
    if (entry) {
        entry->remote_id = remote_id;
        return YETTY_OK_VOID();
    }
    entry = calloc(1, sizeof(*entry));
    if (!entry) {
        return YETTY_ERR(yetty_ycore_void, "seed_remote_id_by_name: calloc failed");
    }
    entry->qualified_name = strdup(qualified_name);
    if (!entry->qualified_name) {
        free(entry);
        return YETTY_ERR(yetty_ycore_void, "seed_remote_id_by_name: strdup failed");
    }
    entry->remote_id = remote_id;
    HASH_ADD_KEYPTR(hh, s->remote_names, entry->qualified_name, strlen(entry->qualified_name),
                    entry);
    ydebug("seeded name '%s' remote=%u", qualified_name, remote_id);
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_yclass_rpc_connect_transport(
    struct yetty_yclass_transport *transport)
{
    if (!transport) {
        return YETTY_ERR(yetty_yclass_object_ptr, "rpc_connect: NULL transport");
    }
    struct yetty_yclass_rpc_session_ptr_result session_res =
        yetty_yclass_rpc_session_create(transport);
    if (YETTY_IS_ERR(session_res)) {
        struct yetty_ycore_void_result destroy_res = transport->ops->destroy(transport);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "rpc_connect: session_create", session_res);
    }
    /* NULL prime-class: the root's own slots resolve lazily by name (the
     * first typed call does one RESOLVE_SLOT while the pipeline is empty,
     * then caches). attach_root fetches + proxies the session root. */
    struct yetty_yclass_object_ptr_result root_res =
        yetty_yclass_rpc_session_attach_root(session_res.value, NULL);
    if (YETTY_IS_ERR(root_res)) {
        struct yetty_ycore_void_result session_destroy_res =
            yetty_yclass_rpc_session_destroy(session_res.value);
        if (YETTY_IS_ERR(session_destroy_res)) {
            yetty_ycore_error_destroy(session_destroy_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "rpc_connect: attach_root", root_res);
    }
    return root_res;
}

struct yetty_yclass_object_ptr_result yetty_yclass_rpc_connect_channel(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_yclass_object_ptr, "rpc_connect_channel: NULL connection");
    }
    /* Open our OWN dynamic channel on the (shared) connection — the SSH
     * CHANNEL_OPEN. Several clients on one connection each get a separate
     * channel; the host serves each as an independent RPC session. */
    struct yetty_ywire_channel_ptr_result channel_res =
        yetty_ywire_connection_open_channel(connection, /*initial_recv_window=*/0);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, channel_res, "rpc_connect_channel: open_channel");
    struct yetty_yclass_transport_ptr_result transport_res =
        yetty_ywire_channel_transport(channel_res.value);
    if (YETTY_IS_ERR(transport_res)) {
        struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(channel_res.value);
        if (YETTY_IS_ERR(close_res)) {
            yetty_ycore_error_destroy(close_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "rpc_connect_channel: channel_transport",
                         transport_res);
    }
    struct yetty_yclass_object_ptr_result root_res =
        yetty_yclass_rpc_connect_transport(transport_res.value);
    if (YETTY_IS_ERR(root_res)) {
        struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(channel_res.value);
        if (YETTY_IS_ERR(close_res)) {
            yetty_ycore_error_destroy(close_res.error);
        }
        return root_res;
    }
    /* The session (root->session) owns the channel so it CLOSEs it on
     * destroy; the connection stays the caller's. */
    root_res.value->session->owned_channel = channel_res.value;
    return root_res;
}

struct yetty_yclass_object_ptr_result yetty_yclass_rpc_connect_fds(int read_fd, int write_fd)
{
    /* The zero-configuration client bring-up over a chosen fd pair (STDIN/
     * STDOUT for the common in-terminal tool): own the whole stack — pty byte
     * transport, a multiplexed connection (initiator role), and one dynamic
     * RPC channel on it — then attach to the session root. Teardown is one
     * yetty_yclass_rpc_disconnect (session_destroy frees channel + connection
     * + pty). */
    struct yetty_yclass_transport_pty_ptr_result pty_res =
        yetty_yclass_transport_pty_create(read_fd, write_fd);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, pty_res, "rpc_connect: pty transport");
    struct yetty_yclass_transport_pty *pty = pty_res.value;

    /* Raw mode: our channel writes must not be echoed back by the tty. */
    struct yetty_ycore_void_result raw_res = yetty_yclass_transport_pty_enable_raw_mode(pty);
    if (YETTY_IS_ERR(raw_res)) {
        yetty_ycore_error_destroy(raw_res.error);
    }

    struct yetty_ywire_connection_ptr_result connection_res =
        yetty_ywire_connection_create(yetty_yclass_transport_pty_reactor(pty), /*compressed=*/1);
    if (YETTY_IS_ERR(connection_res)) {
        struct yetty_ycore_void_result dp = yetty_yclass_transport_pty_destroy(pty);
        if (YETTY_IS_ERR(dp)) {
            yetty_ycore_error_destroy(dp.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "rpc_connect: connection_create", connection_res);
    }
    struct yetty_ywire_connection *connection = connection_res.value;
    /* Client is the initiator (even dynamic ids); the host is the acceptor. */
    struct yetty_ycore_void_result role_res =
        yetty_ywire_connection_set_role(connection, /*acceptor=*/0);
    if (YETTY_IS_ERR(role_res)) {
        yetty_ycore_error_destroy(role_res.error);
    }

    struct yetty_yclass_object_ptr_result root_res = yetty_yclass_rpc_connect_channel(connection);
    if (YETTY_IS_ERR(root_res)) {
        struct yetty_ycore_void_result dc = yetty_ywire_connection_destroy(connection);
        if (YETTY_IS_ERR(dc)) {
            yetty_ycore_error_destroy(dc.error);
        }
        struct yetty_ycore_void_result dp = yetty_yclass_transport_pty_destroy(pty);
        if (YETTY_IS_ERR(dp)) {
            yetty_ycore_error_destroy(dp.error);
        }
        return root_res;
    }
    /* Hand ownership of the connection stack to the session (connect_channel
     * already gave it the channel). */
    root_res.value->session->owned_connection = connection;
    root_res.value->session->owned_pty = pty;
    return root_res;
}

struct yetty_yclass_object_ptr_result yetty_yclass_rpc_connect(void)
{
    return yetty_yclass_rpc_connect_fds(0 /*STDIN*/, 1 /*STDOUT*/);
}

struct yetty_ycore_void_result yetty_yclass_rpc_disconnect(struct yetty_yclass_object *root)
{
    if (!root) {
        return YETTY_OK_VOID();
    }
    if (!root->session) {
        return YETTY_ERR(yetty_ycore_void,
                         "rpc_disconnect: object has no session (not a connect() root)");
    }
    return yetty_yclass_rpc_session_destroy(root->session);
}

struct yetty_yclass_object_ptr_result yetty_yclass_rpc_session_attach_root(
    struct yetty_yclass_rpc_session *s, const char *root_class_name)
{
    if (!s) {
        return YETTY_ERR(yetty_yclass_object_ptr, "session_attach_root: NULL session");
    }
    if (s->root_proxy) {
        return YETTY_OK(yetty_yclass_object_ptr, s->root_proxy); /* idempotent */
    }
    /* Prime the name→remote-id cache while a sync round-trip is still legal
     * (attach window). Degraded-but-OK on failure for lockstep transports —
     * the lazy per-name fallback still resolves there; on an async
     * transport a missed prime surfaces loudly at the first call. */
    if (root_class_name) {
        struct yetty_ycore_void_result translate_res =
            yetty_yclass_rpc_session_translate_class(s, root_class_name);
        if (YETTY_IS_ERR(translate_res)) {
            ywarn("session_attach_root: translate_class('%s') failed: %s", root_class_name,
                  translate_res.error.msg ? translate_res.error.msg : "(no msg)");
            yetty_ycore_error_destroy(translate_res.error);
        }
    }

    struct yetty_yclass_handle_result root_res = yetty_yclass_rpc_session_get_root(s);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, root_res, "session_attach_root: get_root");
    if (root_res.value == 0) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "session_attach_root: peer published no root object");
    }
    struct yetty_yclass_object_ptr_result proxy_res =
        yetty_yclass_object_proxy_create(s, root_res.value, NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, proxy_res, "session_attach_root: proxy_create");
    s->root_proxy = proxy_res.value;
    return proxy_res;
}

void yetty_yclass_rpc_session_set_error_sink(struct yetty_yclass_rpc_session *s,
                                             yetty_yclass_rpc_error_sink_fn sink, void *userdata)
{
    if (!s) {
        return;
    }
    s->error_sink = sink;
    s->error_sink_userdata = userdata;
}

/* Complete the FIFO head with one fully-reassembled response payload.
 * status byte 0 = OK (nothing to do for a void call); 1 = ERR with the
 * serialized remote cause chain following — rebuild it and hand it to
 * the sink (which owns it), or log-and-destroy without one. */
static void complete_pending_call(struct yetty_yclass_rpc_session *s, const uint8_t *payload,
                                  size_t payload_len)
{
    struct pending_call *done = s->pending_head;
    if (!done) {
        ywarn("rpc pump: response frame with no pending call — stream desync");
        return;
    }
    s->pending_head = done->next;
    if (!s->pending_head) {
        s->pending_tail = NULL;
    }
    s->pending_count--;
    s->completed_total++;

    if (payload_len < 1) {
        ywarn("rpc pump: short response for '%s'", done->qualified_name);
        free(done);
        return;
    }
    if (payload[0] != 0) {
        struct yetty_ycore_error *remote_chain =
            yetty_ycore_error_deserialize(payload + 1, payload_len - 1);
        struct yetty_ycore_void_result failed =
            YETTY_ERR(yetty_ycore_void, "pipelined call: remote impl returned error");
        failed.error.cause = remote_chain;
        if (s->error_sink) {
            s->error_sink(done->qualified_name, &failed.error, s->error_sink_userdata);
        } else {
            yetty_ycore_error_print(stderr, done->qualified_name, failed.error);
            yetty_ycore_error_destroy(failed.error);
        }
    }
    free(done);
}

size_t yetty_yclass_rpc_session_pending(const struct yetty_yclass_rpc_session *s)
{
    return s ? s->pending_count : 0;
}

uint64_t yetty_yclass_rpc_session_completed_total(const struct yetty_yclass_rpc_session *s)
{
    return s ? s->completed_total : 0;
}

struct yetty_ycore_void_result yetty_yclass_rpc_session_pump(struct yetty_yclass_rpc_session *s)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "session_pump: NULL session");
    }
    if (!s->async_mode) {
        return YETTY_OK_VOID();
    }
    for (;;) {
        /* Top up the reassembly buffer from what the event loop's pump has
         * already delivered — strictly non-blocking. */
        if (s->inbound_cap - s->inbound_len < 4096) {
            size_t grown_cap = s->inbound_cap ? s->inbound_cap * 2 : 8192;
            uint8_t *grown = realloc(s->inbound_buf, grown_cap);
            if (!grown) {
                return YETTY_ERR(yetty_ycore_void, "session_pump: reassembly grow failed");
            }
            s->inbound_buf = grown;
            s->inbound_cap = grown_cap;
        }
        struct yetty_ycore_size_result read_res = s->transport->ops->recv_available(
            s->transport, s->inbound_buf + s->inbound_len, s->inbound_cap - s->inbound_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "session_pump: recv_available");
        if (read_res.value == 0) {
            break;
        }
        s->inbound_len += read_res.value;
    }
    /* Peel complete `u32 resp_len | payload` frames off the front. */
    size_t consumed = 0;
    while (s->inbound_len - consumed >= 4) {
        uint32_t frame_len;
        memcpy(&frame_len, s->inbound_buf + consumed, 4);
        if (frame_len > BUF_MAX) {
            return YETTY_ERR(yetty_ycore_void, "session_pump: response frame exceeds BUF_MAX");
        }
        if (s->inbound_len - consumed - 4 < frame_len) {
            break; /* incomplete — wait for more bytes */
        }
        complete_pending_call(s, s->inbound_buf + consumed + 4, frame_len);
        consumed += 4 + frame_len;
    }
    if (consumed) {
        memmove(s->inbound_buf, s->inbound_buf + consumed, s->inbound_len - consumed);
        s->inbound_len -= consumed;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yclass_rpc_call_void(struct yetty_yclass_rpc_session *s,
                                                          uint32_t remote_id,
                                                          const char *qualified_name,
                                                          const void *body, size_t body_len)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call_void: NULL session");
    }
    if (!s->async_mode) {
        /* Lockstep transport: ordinary request/response with the decode the
         * generated stubs used to carry inline. */
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result call_res = yetty_yclass_rpc_call_alloc(
            s, YETTY_YCLASS_RPC_OP_CALL, remote_id, body, body_len, &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, call_res, "rpc_call_void: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void, "rpc_call_void: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error =
                YETTY_ERR(yetty_ycore_void, "rpc_call_void: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    }

    /* Pipelined: surface whatever completions already landed, send, enqueue.
     * The response arrives through the event loop's pump; a failure reaches
     * the error sink with this call's qualified name. */
    struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(s);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pump_res, "rpc_call_void: completion pump");

    struct pending_call *pending = calloc(1, sizeof(*pending));
    if (!pending) {
        return YETTY_ERR(yetty_ycore_void, "rpc_call_void: pending alloc failed");
    }
    pending->qualified_name = qualified_name;

    struct yetty_ycore_void_result write_res =
        rpc_write_request(s, YETTY_YCLASS_RPC_OP_CALL, remote_id, body, body_len);
    if (YETTY_IS_ERR(write_res)) {
        free(pending);
        return YETTY_ERR(yetty_ycore_void, "rpc_call_void: request write failed", write_res);
    }
    /* Flush so buffering transports actually put the frame on the wire —
     * no recv follows to trigger their lazy flush. */
    if (s->transport->ops->flush) {
        struct yetty_ycore_void_result flush_res = s->transport->ops->flush(s->transport);
        if (YETTY_IS_ERR(flush_res)) {
            free(pending);
            return YETTY_ERR(yetty_ycore_void, "rpc_call_void: flush failed", flush_res);
        }
    }
    if (s->pending_tail) {
        s->pending_tail->next = pending;
    } else {
        s->pending_head = pending;
    }
    s->pending_tail = pending;
    s->pending_count++;
    return YETTY_OK_VOID();
}

struct yetty_yclass_handle_result yetty_yclass_rpc_session_get_root(
    struct yetty_yclass_rpc_session *s)
{
    if (!s) {
        return YETTY_ERR(yetty_yclass_handle, "session_get_root: NULL session");
    }
    uint64_t handle = 0;
    struct yetty_ycore_size_result call_r =
        yetty_yclass_rpc_call(s, YETTY_YCLASS_RPC_OP_GET_ROOT, 0, NULL, 0, &handle, sizeof(handle));
    if (YETTY_IS_ERR(call_r)) {
        return YETTY_ERR(yetty_yclass_handle, "session_get_root: GET_ROOT call failed", call_r);
    }
    if (call_r.value != sizeof(handle)) {
        return YETTY_ERR(yetty_yclass_handle, "session_get_root: GET_ROOT short response");
    }
    return YETTY_OK(yetty_yclass_handle, handle);
}

struct yetty_ycore_void_result yetty_yclass_rpc_session_translate_class(
    struct yetty_yclass_rpc_session *s, const char *class_name)
{
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "translate_class: NULL session");
    }
    if (!class_name) {
        return YETTY_ERR(yetty_ycore_void, "translate_class: NULL class_name");
    }
    struct translated_class *t = NULL;
    HASH_FIND_STR(s->translated, class_name, t);
    if (t) {
        return YETTY_OK_VOID();
    }

    uint8_t buf[GET_CLASS_RESP_MAX];
    size_t name_len = strlen(class_name);
    struct yetty_ycore_size_result rr = yetty_yclass_rpc_call(
        s, YETTY_YCLASS_RPC_OP_GET_CLASS, 0, class_name, name_len, buf, sizeof(buf));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "translate_class: GET_CLASS call failed");
    size_t resp_len = rr.value;
    if (resp_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "translate_class: empty GET_CLASS response");
    }

    size_t off = 0;
    while (off + 2 + 4 <= resp_len) {
        uint16_t nl;
        memcpy(&nl, buf + off, 2);
        off += 2;
        if (off + nl + 4 > resp_len) {
            break;
        }
        char *slot_name = dup_wire_name(buf + off, nl);
        if (!slot_name) {
            break; /* alloc fail mid-parse — bail and don't cache. */
        }
        off += nl;
        uint32_t rid;
        memcpy(&rid, buf + off, 4);
        off += 4;

        /* rid must fit in the wire's 28-bit id field — yetty_yclass_rpc_call
         * rejects anything larger. Caching a too-large rid would poison
         * the session: future stub calls would fail in rpc_call, and
         * ensure_remote_id's "is this UNRESOLVED?" check would see a
         * non-sentinel value and skip the RESOLVE_SLOT retry. Bail and
         * let the off != resp_len guard below drop the cache. */
        if (rid > YETTY_YCLASS_RPC_ID_MASK) {
            ywarn("translate_class('%s'): peer sent rid=0x%08x > id-mask for slot '%s'", class_name,
                  rid, slot_name);
            free(slot_name);
            break;
        }

        /* Prime the name-keyed cache too — the split-generated stubs resolve
         * by qualified name, and in async mode a mid-stream RESOLVE_SLOT
         * round-trip is forbidden, so this batch IS their resolution. */
        struct remote_name_entry *name_entry = NULL;
        HASH_FIND_STR(s->remote_names, slot_name, name_entry);
        if (!name_entry) {
            name_entry = calloc(1, sizeof(*name_entry));
            if (name_entry) {
                name_entry->qualified_name = strdup(slot_name);
                if (!name_entry->qualified_name) {
                    free(name_entry);
                } else {
                    name_entry->remote_id = rid;
                    HASH_ADD_KEYPTR(hh, s->remote_names, name_entry->qualified_name,
                                    strlen(name_entry->qualified_name), name_entry);
                }
            }
        }

        struct yetty_yclass_method_slot_result lr = yetty_yclass_method_slot_by_qname(slot_name);
        if (YETTY_IS_OK(lr)) {
            struct yetty_ycore_void_result sr =
                yetty_yclass_rpc_session_set_remote_id(s, lr.value, rid);
            if (YETTY_IS_ERR(sr)) {
                /* Per-entry cache failure — log and keep parsing the
                 * rest; per-slot RESOLVE_SLOT fallback can recover
                 * any entry we drop here. */
                ywarn("translate_class['%s']: cache write for slot '%s' rid=%u failed: %s",
                      class_name, slot_name, rid, sr.error.msg ? sr.error.msg : "(no msg)");
                yetty_ycore_error_destroy(sr.error);
            } else {
                ydebug("xlat['%s'] local=%u remote=%u", slot_name, lr.value, rid);
            }
        } else {
            yetty_ycore_error_destroy(lr.error);
        }
        free(slot_name);
    }

    /* A well-formed response consumes exactly resp_len bytes — entries
     * are packed back-to-back. If we bailed mid-loop (truncated entry,
     * dup_wire_name alloc failure, or trailing junk), don't mark the
     * class translated. The per-slot RESOLVE_SLOT fallback in
     * ensure_remote_id can still fill in any missed mappings, and a
     * later translate_class call gets another shot at the full table. */
    if (off != resp_len) {
        ywarn("translate_class('%s'): parse consumed %zu of %zu bytes — not caching", class_name,
              off, resp_len);
        return YETTY_ERR(yetty_ycore_void,
                         "translate_class: parse did not consume exactly resp_len bytes");
    }

    t = calloc(1, sizeof(*t));
    if (!t) {
        return YETTY_ERR(yetty_ycore_void, "translate_class: cache entry calloc failed");
    }
    t->name = strdup(class_name);
    if (!t->name) {
        free(t);
        return YETTY_ERR(yetty_ycore_void, "translate_class: cache entry strdup failed");
    }
    HASH_ADD_KEYPTR(hh, s->translated, t->name, strlen(t->name), t);
    return YETTY_OK_VOID();
}
