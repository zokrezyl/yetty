/* RPC runtime — packed-header wire, op enum, uthash translations. */

#include <yclass/rpc.h>

#include <ut/uthash.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OBJECTS 256
#define BUF_MAX 65536

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
     * the skel-cache / lookup-chain are populated by per-module
     * constructors before main() — clearing them would orphan the
     * mappings. Initialise next_handle to 1 on the first call only;
     * subsequent calls are no-ops so a stale "rpc_init looks like a
     * reset" expectation can't silently invalidate live handles. */
    if (s->next_handle != 0)
        return YETTY_OK_VOID();
    s->next_handle = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result
yetty_yclass_rpc_add_skel_lookup(yetty_yclass_rpc_skel_lookup_fn fn)
{
    if (!fn)
        return YETTY_ERR(yetty_ycore_void, "rpc_add_skel_lookup: NULL fn");
    struct skel_lookup_node *node = calloc(1, sizeof(*node));
    if (!node)
        return YETTY_ERR(yetty_ycore_void, "rpc_add_skel_lookup: calloc failed");
    struct rpc_server_state *s = server();
    node->fn = fn;
    node->next = s->lookup_chain;
    s->lookup_chain = node;
    return YETTY_OK_VOID();
}

struct yetty_yclass_rpc_skel_fn_result
yetty_yclass_rpc_skel_for(yetty_yclass_method_slot slot)
{
    struct rpc_server_state *s = server();
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED)
        return YETTY_ERR(yetty_yclass_rpc_skel_fn, "skel_for: METHOD_SLOT_UNDEFINED");

    struct skel_cache_entry *e = NULL;
    HASH_FIND(hh, s->skel_cache, &slot, sizeof(slot), e);
    if (e)
        return YETTY_OK(yetty_yclass_rpc_skel_fn, e->fn);

    /* Walk the chain. First hit wins. */
    yetty_yclass_rpc_skel_fn fn = NULL;
    for (struct skel_lookup_node *n = s->lookup_chain; n; n = n->next) {
        fn = n->fn(slot);
        if (fn)
            break;
    }
    if (!fn)
        return YETTY_ERR(yetty_yclass_rpc_skel_fn,
                         "skel_for: no skel registered for this slot");

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
    if (s->object_count >= MAX_OBJECTS)
        return YETTY_ERR(yetty_yclass_handle, "rpc_register_object: object table full");
    uint64_t h = s->next_handle++;
    s->objects[s->object_count].handle = h;
    s->objects[s->object_count].ptr = obj;
    s->object_count++;
    return YETTY_OK(yetty_yclass_handle, h);
}

struct yetty_yclass_void_ptr_result yetty_yclass_rpc_handle_resolve(uint64_t h)
{
    if (!h)
        return YETTY_ERR(yetty_yclass_void_ptr, "rpc_handle_resolve: handle is 0");
    struct rpc_server_state *s = server();
    for (size_t i = 0; i < s->object_count; ++i)
        if (s->objects[i].handle == h)
            return YETTY_OK(yetty_yclass_void_ptr, s->objects[i].ptr);
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
        if (r.value == 0)
            return -1;
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
        if (w.value == 0)
            return -1;
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
    if (len && memchr(bytes, 0, len) != NULL)
        return NULL;
    char *s = malloc(len + 1);
    if (!s)
        return NULL;
    if (len)
        memcpy(s, bytes, len);
    s[len] = 0;
    return s;
}

static size_t handle_resolve_slot(const void *body, size_t body_len, void *resp,
                                  size_t resp_max)
{
    char *name = dup_wire_name(body, body_len);
    if (!name)
        return 0;
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
        return 0;
    }
    memcpy(resp, &out, sizeof(out));
    ydebug("resolve_slot('%s') -> %u", name, out);
    free(name);
    return sizeof(out);
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
    if (gc->off + need > gc->cap)
        return;
    uint16_t nl = (uint16_t)name_len;
    memcpy(gc->out + gc->off, &nl, 2);
    gc->off += 2;
    memcpy(gc->out + gc->off, name, name_len);
    gc->off += name_len;
    uint32_t rid = (uint32_t)slot;
    memcpy(gc->out + gc->off, &rid, 4);
    gc->off += 4;
}

static size_t handle_get_class(const void *body, size_t body_len, void *resp, size_t resp_max)
{
    char *name = dup_wire_name(body, body_len);
    if (!name)
        return 0;
    struct yetty_yclass_ptr_result cr = yetty_yclass_by_name(name);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_print(stderr, "[server] get_class", cr.error);
        yetty_ycore_error_destroy(cr.error);
        free(name);
        return 0;
    }
    struct get_class_ctx gc = {resp, 0, resp_max};
    struct yetty_ycore_void_result fe = yetty_yclass_for_each_slot(cr.value, get_class_emit, &gc);
    if (YETTY_IS_ERR(fe)) {
        yetty_ycore_error_print(stderr, "[server] get_class for_each_slot", fe.error);
        yetty_ycore_error_destroy(fe.error);
        free(name);
        return 0;
    }
    ydebug("get_class('%s') -> %zu entries (%zu bytes)", name, gc.off / 6, gc.off);
    free(name);
    return gc.off;
}

static size_t handle_create(const void *body, size_t body_len, void *resp, size_t resp_max)
{
    char *name = dup_wire_name(body, body_len);
    if (!name)
        return 0;
    struct yetty_yclass_ptr_result cr = yetty_yclass_by_name(name);
    if (YETTY_IS_ERR(cr)) {
        yetty_ycore_error_print(stderr, "[server] create class_by_name", cr.error);
        yetty_ycore_error_destroy(cr.error);
        free(name);
        return 0;
    }
    struct yetty_yclass_object_ptr_result obj_r = yetty_yclass_object_alloc(cr.value);
    if (YETTY_IS_ERR(obj_r)) {
        yetty_ycore_error_print(stderr, "[server] create object_alloc", obj_r.error);
        yetty_ycore_error_destroy(obj_r.error);
        free(name);
        return 0;
    }
    struct yetty_yclass_handle_result rh = yetty_yclass_rpc_register_object(obj_r.value);
    if (YETTY_IS_ERR(rh)) {
        /* Registry rejected (table full, …) — free the alloc so we
         * don't orphan it, then surface the failure as zero bytes to
         * the client (handle=0 maps to client-side failure). */
        struct yetty_ycore_void_result fr = yetty_yclass_object_free(obj_r.value);
        if (YETTY_IS_ERR(fr))
            yetty_ycore_error_destroy(fr.error);
        ywarn("create('%s'): rpc_register_object failed, allocation freed", name);
        yetty_ycore_error_destroy(rh.error);
        free(name);
        return 0;
    }
    uint64_t h = rh.value;
    if (resp_max < sizeof(h)) {
        free(name);
        return 0;
    }
    memcpy(resp, &h, sizeof(h));
    ydebug("create('%s') -> handle=%llu", name, (unsigned long long)h);
    free(name);
    return sizeof(h);
}

/* -------- request dispatch (transport-agnostic) -------------------- */

/* Dispatch one already-assembled yrpc request frame. Used by
 * rpc_server_run (transport loop) and by the DCS-based server handler
 * (terminal-side, wire_statemachine integration) — same per-request
 * shape, just different transport plumbing. Returns resp_len. */
size_t yetty_yclass_rpc_dispatch_one(uint32_t header, const void *body, size_t body_len,
                                     void *resp, size_t resp_max)
{
    enum yetty_yclass_rpc_op op = YETTY_YCLASS_RPC_HDR_OP(header);
    uint32_t id = YETTY_YCLASS_RPC_HDR_ID(header);

    switch (op) {
    case YETTY_YCLASS_RPC_OP_CALL: {
        struct yetty_yclass_rpc_skel_fn_result sr =
            yetty_yclass_rpc_skel_for((yetty_yclass_method_slot)id);
        if (YETTY_IS_OK(sr)) {
            ydebug("CALL slot=%u body_len=%zu", id, body_len);
            return sr.value(body, body_len, resp, resp_max);
        }
        ywarn("CALL slot=%u — skel_for failed: %s", id,
              sr.error.msg ? sr.error.msg : "(no msg)");
        yetty_ycore_error_destroy(sr.error);
        return 0;
    }
    case YETTY_YCLASS_RPC_OP_RESOLVE_SLOT:
        return handle_resolve_slot(body, body_len, resp, resp_max);
    case YETTY_YCLASS_RPC_OP_GET_CLASS:
        return handle_get_class(body, body_len, resp, resp_max);
    case YETTY_YCLASS_RPC_OP_CREATE:
        return handle_create(body, body_len, resp, resp_max);
    default:
        ywarn("unknown op=%u", op);
        return 0;
    }
}

/* -------- server loop ---------------------------------------------- */

struct yetty_ycore_void_result
yetty_yclass_rpc_server_run(struct yetty_yclass_transport *transport)
{
    struct rpc_server_state *gs = server();
    static uint8_t body[BUF_MAX];
    static uint8_t resp[BUF_MAX];

    (void)gs;
    if (!transport)
        return YETTY_ERR(yetty_ycore_void, "rpc_server_run: NULL transport");
    for (;;) {
        uint32_t header = 0, body_len = 0;
        if (read_full(transport, &header, 4) < 0)
            return YETTY_OK_VOID(); /* clean peer disconnect */
        if (read_full(transport, &body_len, 4) < 0)
            return YETTY_ERR(yetty_ycore_void,
                             "rpc_server_run: short read on body_len");
        if (body_len > BUF_MAX)
            return YETTY_ERR(yetty_ycore_void,
                             "rpc_server_run: body_len exceeds BUF_MAX");
        if (body_len && read_full(transport, body, body_len) < 0)
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short read on body");

        uint32_t resp_len =
            (uint32_t)yetty_yclass_rpc_dispatch_one(header, body, body_len, resp, BUF_MAX);

        if (write_full(transport, &resp_len, 4) < 0)
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short write on resp_len");
        if (resp_len && write_full(transport, resp, resp_len) < 0)
            return YETTY_ERR(yetty_ycore_void, "rpc_server_run: short write on resp");
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

struct yetty_yclass_rpc_session {
    struct yetty_yclass_transport *transport; /* owned */
    /* Local slot → remote id. Local slots are sparse (domain id in
     * upper bits), so hash by slot rather than flat-array. */
    struct remote_id_entry *remote_ids;
    struct translated_class *translated; /* by class name */
};

struct yetty_yclass_rpc_session_ptr_result
yetty_yclass_rpc_session_create(struct yetty_yclass_transport *transport)
{
    if (!transport)
        return YETTY_ERR(yetty_yclass_rpc_session_ptr, "session_create: NULL transport");
    struct yetty_yclass_rpc_session *s = calloc(1, sizeof(*s));
    if (!s)
        return YETTY_ERR(yetty_yclass_rpc_session_ptr, "session_create: calloc failed");
    s->transport = transport;
    return YETTY_OK(yetty_yclass_rpc_session_ptr, s);
}

struct yetty_ycore_void_result
yetty_yclass_rpc_session_destroy(struct yetty_yclass_rpc_session *s)
{
    if (!s)
        return YETTY_OK_VOID();
    struct translated_class *cur, *tmp;
    HASH_ITER(hh, s->translated, cur, tmp) {
        HASH_DEL(s->translated, cur);
        free(cur->name);
        free(cur);
    }
    struct remote_id_entry *rcur, *rtmp;
    HASH_ITER(hh, s->remote_ids, rcur, rtmp) {
        HASH_DEL(s->remote_ids, rcur);
        free(rcur);
    }
    /* Best-effort cleanup: still free the session even if transport
     * destroy fails — propagate the error after freeing so resources
     * are released either way (multi-step *_destroy pattern). */
    struct yetty_ycore_void_result td = YETTY_OK_VOID();
    if (s->transport && s->transport->ops->destroy)
        td = s->transport->ops->destroy(s->transport);
    free(s);
    if (YETTY_IS_ERR(td))
        return YETTY_ERR(yetty_ycore_void, "session_destroy: transport destroy failed", td);
    return YETTY_OK_VOID();
}

struct uint32_result yetty_yclass_rpc_session_remote_id(struct yetty_yclass_rpc_session *s,
                                                         yetty_yclass_method_slot slot)
{
    if (!s)
        return YETTY_ERR(uint32, "remote_id: NULL session");
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e)
        return YETTY_ERR(uint32, "remote_id: slot not cached in this session");
    return YETTY_OK(uint32, e->remote_id);
}

struct yetty_ycore_void_result
yetty_yclass_rpc_session_set_remote_id(struct yetty_yclass_rpc_session *s,
                                       yetty_yclass_method_slot slot, uint32_t remote_id)
{
    if (!s)
        return YETTY_ERR(yetty_ycore_void, "set_remote_id: NULL session");
    /* Public API — direct callers can otherwise poison the xlat table
     * with values that translate_class now refuses to cache:
     *   - METHOD_SLOT_UNDEFINED as the local slot has nothing to map.
     *   - remote_id == UINT32_MAX collides with REMOTE_ID_UNRESOLVED,
     *     making ensure_remote_id retry forever (or worse, fail the
     *     stub if it's read back as cached).
     *   - remote_id > RPC_ID_MASK doesn't fit in the wire id field
     *     and would be rejected at rpc_call time, breaking dispatch
     *     for that slot. */
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED)
        return YETTY_ERR(yetty_ycore_void,
                         "set_remote_id: METHOD_SLOT_UNDEFINED local slot");
    if (remote_id == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED ||
        remote_id > YETTY_YCLASS_RPC_ID_MASK)
        return YETTY_ERR(yetty_ycore_void,
                         "set_remote_id: invalid remote_id (UNRESOLVED or > ID_MASK)");
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e)
            return YETTY_ERR(yetty_ycore_void, "set_remote_id: calloc failed");
        e->local_slot = slot;
        HASH_ADD(hh, s->remote_ids, local_slot, sizeof(yetty_yclass_method_slot), e);
    }
    e->remote_id = remote_id;
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result
yetty_yclass_rpc_call(struct yetty_yclass_rpc_session *s, enum yetty_yclass_rpc_op op,
                      uint32_t id, const void *body, size_t body_len, void *resp,
                      size_t resp_max)
{
    if (!s)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: NULL session");
    /* Reject before touching the wire: the body_len is a u32 on the
     * wire and the peer reads into a BUF_MAX-sized static buffer. */
    if (body_len > UINT32_MAX || body_len > BUF_MAX)
        return YETTY_ERR(yetty_ycore_size,
                         "rpc_call: body_len exceeds wire/buffer limit");
    /* The wire id field is 28 bits — RPC_HDR_MAKE silently masks
     * anything larger. */
    if (id > YETTY_YCLASS_RPC_ID_MASK)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: id exceeds 28-bit wire field");
    /* op occupies a 4-bit field; cap at the highest defined op so
     * undefined-but-fits-in-4-bits values are also caught. */
    if (op > YETTY_YCLASS_RPC_OP_CREATE)
        return YETTY_ERR(yetty_ycore_size,
                         "rpc_call: op is not a defined yetty_yclass_rpc_op");
    if (body_len > 0 && body == NULL)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: body_len>0 but body is NULL");
    if (resp_max > 0 && resp == NULL)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: resp_max>0 but resp is NULL");

    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(op, id);
    ydebug("op=%u id=%u body_len=%zu", op, id, body_len);

    uint32_t bl = (uint32_t)body_len;
    if (write_full(s->transport, &header, 4) < 0)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short write on header");
    if (write_full(s->transport, &bl, 4) < 0)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short write on body_len");
    if (body_len && write_full(s->transport, body, body_len) < 0)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short write on body");

    uint32_t resp_len = 0;
    if (read_full(s->transport, &resp_len, 4) < 0)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short read on resp_len");
    if (resp_len > resp_max) {
        /* Drain the oversized payload so the next frame read starts
         * aligned. Without this we'd parse garbage from mid-stream. */
        uint8_t drain[256];
        size_t remain = resp_len;
        while (remain) {
            size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
            if (read_full(s->transport, drain, chunk) < 0)
                return YETTY_ERR(yetty_ycore_size,
                                 "rpc_call: short read while draining oversized resp");
            remain -= chunk;
        }
        return YETTY_ERR(yetty_ycore_size, "rpc_call: response exceeds caller's resp_max");
    }
    if (resp_len && read_full(s->transport, resp, resp_len) < 0)
        return YETTY_ERR(yetty_ycore_size, "rpc_call: short read on resp body");
    return YETTY_OK(yetty_ycore_size, (size_t)resp_len);
}

struct uint32_result
yetty_yclass_rpc_session_ensure_remote_id(struct yetty_yclass_rpc_session *s,
                                          yetty_yclass_method_slot local_slot)
{
    if (!s)
        return YETTY_ERR(uint32, "ensure_remote_id: NULL session");
    struct uint32_result cached = yetty_yclass_rpc_session_remote_id(s, local_slot);
    if (YETTY_IS_OK(cached))
        return cached;
    /* Not cached — drop the lookup error and do the round-trip. */
    yetty_ycore_error_destroy(cached.error);

    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(local_slot);
    if (YETTY_IS_ERR(nr))
        return YETTY_ERR(uint32, "ensure_remote_id: slot name lookup failed", nr);
    const char *name = nr.value;

    uint32_t remote = YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    struct yetty_ycore_size_result nr2 =
        yetty_yclass_rpc_call(s, YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0, name, strlen(name),
                              &remote, sizeof(remote));
    if (YETTY_IS_ERR(nr2))
        return YETTY_ERR(uint32, "ensure_remote_id: RESOLVE_SLOT call failed", nr2);
    if (nr2.value != sizeof(remote) || remote == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED)
        return YETTY_ERR(uint32, "ensure_remote_id: RESOLVE_SLOT returned unresolved");

    struct yetty_ycore_void_result sr =
        yetty_yclass_rpc_session_set_remote_id(s, local_slot, remote);
    if (YETTY_IS_ERR(sr)) {
        /* Caching failed (validation or alloc) — return the resolved
         * id to the caller anyway so this one call can proceed;
         * future calls will retry the round-trip. Log so a persistent
         * cache failure doesn't silently degrade every call to a
         * round-trip. */
        ywarn("ensure_remote_id: cache write for slot=0x%08x rid=%u failed: %s",
              local_slot, remote, sr.error.msg ? sr.error.msg : "(no msg)");
        yetty_ycore_error_destroy(sr.error);
    }
    ydebug("lazy resolve '%s' local=%u remote=%u", name, local_slot, remote);
    return YETTY_OK(uint32, remote);
}

struct yetty_ycore_void_result
yetty_yclass_rpc_session_translate_class(struct yetty_yclass_rpc_session *s,
                                         const char *class_name)
{
    if (!s)
        return YETTY_ERR(yetty_ycore_void, "translate_class: NULL session");
    if (!class_name)
        return YETTY_ERR(yetty_ycore_void, "translate_class: NULL class_name");
    struct translated_class *t = NULL;
    HASH_FIND_STR(s->translated, class_name, t);
    if (t)
        return YETTY_OK_VOID();

    uint8_t buf[BUF_MAX];
    size_t name_len = strlen(class_name);
    struct yetty_ycore_size_result rr =
        yetty_yclass_rpc_call(s, YETTY_YCLASS_RPC_OP_GET_CLASS, 0, class_name, name_len, buf,
                              sizeof(buf));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "translate_class: GET_CLASS call failed");
    size_t resp_len = rr.value;
    if (resp_len == 0)
        return YETTY_ERR(yetty_ycore_void, "translate_class: empty GET_CLASS response");

    size_t off = 0;
    while (off + 2 + 4 <= resp_len) {
        uint16_t nl;
        memcpy(&nl, buf + off, 2);
        off += 2;
        if (off + nl + 4 > resp_len)
            break;
        char *slot_name = dup_wire_name(buf + off, nl);
        if (!slot_name)
            break; /* alloc fail mid-parse — bail and don't cache. */
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
            ywarn("translate_class('%s'): peer sent rid=0x%08x > id-mask for slot '%s'",
                  class_name, rid, slot_name);
            free(slot_name);
            break;
        }

        struct yetty_yclass_method_slot_result lr =
            yetty_yclass_method_slot_by_qname(slot_name);
        if (YETTY_IS_OK(lr)) {
            struct yetty_ycore_void_result sr =
                yetty_yclass_rpc_session_set_remote_id(s, lr.value, rid);
            if (YETTY_IS_ERR(sr)) {
                /* Per-entry cache failure — log and keep parsing the
                 * rest; per-slot RESOLVE_SLOT fallback can recover
                 * any entry we drop here. */
                ywarn("translate_class['%s']: cache write for slot '%s' rid=%u failed: %s",
                      class_name, slot_name, rid,
                      sr.error.msg ? sr.error.msg : "(no msg)");
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
        ywarn("translate_class('%s'): parse consumed %zu of %zu bytes — not caching",
              class_name, off, resp_len);
        return YETTY_ERR(yetty_ycore_void,
                         "translate_class: parse did not consume exactly resp_len bytes");
    }

    t = calloc(1, sizeof(*t));
    if (!t)
        return YETTY_ERR(yetty_ycore_void, "translate_class: cache entry calloc failed");
    t->name = strdup(class_name);
    if (!t->name) {
        free(t);
        return YETTY_ERR(yetty_ycore_void, "translate_class: cache entry strdup failed");
    }
    HASH_ADD_KEYPTR(hh, s->translated, t->name, strlen(t->name), t);
    return YETTY_OK_VOID();
}
