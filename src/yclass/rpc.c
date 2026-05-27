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

void yetty_yclass_rpc_init(void)
{
    struct rpc_server_state *s = server();
    s->object_count = 0;
    s->next_handle = 1;
}

void yetty_yclass_rpc_add_skel_lookup(yetty_yclass_rpc_skel_lookup_fn fn)
{
    if (!fn)
        return;
    struct skel_lookup_node *node = calloc(1, sizeof(*node));
    if (!node)
        return;
    struct rpc_server_state *s = server();
    node->fn = fn;
    node->next = s->lookup_chain;
    s->lookup_chain = node;
}

yetty_yclass_rpc_skel_fn yetty_yclass_rpc_skel_for(yetty_yclass_method_slot slot)
{
    struct rpc_server_state *s = server();
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED)
        return NULL;

    struct skel_cache_entry *e = NULL;
    HASH_FIND(hh, s->skel_cache, &slot, sizeof(slot), e);
    if (e)
        return e->fn;

    /* Walk the chain. First hit wins. */
    yetty_yclass_rpc_skel_fn fn = NULL;
    for (struct skel_lookup_node *n = s->lookup_chain; n; n = n->next) {
        fn = n->fn(slot);
        if (fn)
            break;
    }
    if (!fn)
        return NULL;

    e = calloc(1, sizeof(*e));
    if (!e)
        return fn;
    e->slot = slot;
    e->fn = fn;
    HASH_ADD(hh, s->skel_cache, slot, sizeof(slot), e);
    return fn;
}

uint64_t yetty_yclass_rpc_register_object(void *obj)
{
    struct rpc_server_state *s = server();
    if (s->object_count >= MAX_OBJECTS)
        return 0;
    uint64_t h = s->next_handle++;
    s->objects[s->object_count].handle = h;
    s->objects[s->object_count].ptr = obj;
    s->object_count++;
    return h;
}

void *yetty_yclass_rpc_handle_resolve(uint64_t h)
{
    if (!h)
        return NULL;
    struct rpc_server_state *s = server();
    for (size_t i = 0; i < s->object_count; ++i)
        if (s->objects[i].handle == h)
            return s->objects[i].ptr;
    return NULL;
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
 * Returns NULL on alloc failure; the caller should treat that the
 * same as a lookup miss (return 0 from the handler). */
static char *dup_wire_name(const void *bytes, size_t len)
{
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
    yetty_yclass_for_each_slot(cr.value, get_class_emit, &gc);
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
    uint64_t h = yetty_yclass_rpc_register_object(obj_r.value);
    if (!h) {
        /* Object table full — the registry orphans the alloc otherwise.
         * Free it here so handle=0 (client failure path) doesn't leak. */
        yetty_yclass_object_free(obj_r.value);
        ywarn("create('%s'): rpc_register_object full, allocation freed", name);
        free(name);
        return 0;
    }
    if (resp_max < sizeof(h)) {
        free(name);
        return 0;
    }
    memcpy(resp, &h, sizeof(h));
    ydebug("create('%s') -> handle=%llu", name, (unsigned long long)h);
    free(name);
    return sizeof(h);
}

/* -------- server loop ---------------------------------------------- */

void yetty_yclass_rpc_server_run(struct yetty_yclass_transport *transport)
{
    struct rpc_server_state *gs = server();
    static uint8_t body[BUF_MAX];
    static uint8_t resp[BUF_MAX];

    (void)gs;
    if (!transport)
        return;
    for (;;) {
        uint32_t header = 0, body_len = 0;
        if (read_full(transport, &header, 4) < 0)
            return;
        if (read_full(transport, &body_len, 4) < 0)
            return;
        if (body_len > BUF_MAX)
            return;
        if (body_len && read_full(transport, body, body_len) < 0)
            return;

        enum yetty_yclass_rpc_op op = YETTY_YCLASS_RPC_HDR_OP(header);
        uint32_t id = YETTY_YCLASS_RPC_HDR_ID(header);
        uint32_t resp_len = 0;

        switch (op) {
        case YETTY_YCLASS_RPC_OP_CALL: {
            yetty_yclass_rpc_skel_fn fn =
                yetty_yclass_rpc_skel_for((yetty_yclass_method_slot)id);
            if (fn) {
                ydebug("CALL slot=%u body_len=%u", id, body_len);
                resp_len = (uint32_t)fn(body, body_len, resp, BUF_MAX);
            } else {
                ywarn("CALL slot=%u — no skel", id);
            }
            break;
        }
        case YETTY_YCLASS_RPC_OP_RESOLVE_SLOT:
            resp_len = (uint32_t)handle_resolve_slot(body, body_len, resp, BUF_MAX);
            break;
        case YETTY_YCLASS_RPC_OP_GET_CLASS:
            resp_len = (uint32_t)handle_get_class(body, body_len, resp, BUF_MAX);
            break;
        case YETTY_YCLASS_RPC_OP_CREATE:
            resp_len = (uint32_t)handle_create(body, body_len, resp, BUF_MAX);
            break;
        default:
            ywarn("unknown op=%u", op);
            break;
        }

        if (write_full(transport, &resp_len, 4) < 0)
            return;
        if (resp_len && write_full(transport, resp, resp_len) < 0)
            return;
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

struct yetty_yclass_rpc_session *
yetty_yclass_rpc_session_create(struct yetty_yclass_transport *transport)
{
    if (!transport)
        return NULL;
    struct yetty_yclass_rpc_session *s = calloc(1, sizeof(*s));
    if (s)
        s->transport = transport;
    return s;
}

void yetty_yclass_rpc_session_destroy(struct yetty_yclass_rpc_session *s)
{
    if (!s)
        return;
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
    if (s->transport && s->transport->ops->destroy)
        s->transport->ops->destroy(s->transport);
    free(s);
}

uint32_t yetty_yclass_rpc_session_remote_id(struct yetty_yclass_rpc_session *s,
                                            yetty_yclass_method_slot slot)
{
    if (!s)
        return YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    return e ? e->remote_id : YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
}

void yetty_yclass_rpc_session_set_remote_id(struct yetty_yclass_rpc_session *s,
                                            yetty_yclass_method_slot slot, uint32_t remote_id)
{
    if (!s)
        return;
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e)
            return;
        e->local_slot = slot;
        HASH_ADD(hh, s->remote_ids, local_slot, sizeof(yetty_yclass_method_slot), e);
    }
    e->remote_id = remote_id;
}

size_t yetty_yclass_rpc_call(struct yetty_yclass_rpc_session *s, enum yetty_yclass_rpc_op op,
                             uint32_t id, const void *body, size_t body_len, void *resp,
                             size_t resp_max)
{
    if (!s)
        return 0;
    /* The wire body_len is a u32, and the peer reads body bytes into
     * a BUF_MAX-sized static buffer. Reject anything that would
     * truncate (cast to u32) or overrun the peer's buffer BEFORE
     * writing the header, so the stream stays framed on rejection. */
    if (body_len > UINT32_MAX || body_len > BUF_MAX) {
        ywarn("rpc_call: body_len=%zu exceeds wire/buffer limit (BUF_MAX=%u)", body_len,
              BUF_MAX);
        return 0;
    }
    /* The wire id field is 28 bits — RPC_HDR_MAKE silently masks
     * anything larger. Reject loudly instead so the caller sees the
     * bug rather than the peer receiving a corrupted id. */
    if (id > YETTY_YCLASS_RPC_ID_MASK) {
        ywarn("rpc_call: id=0x%08x exceeds 28-bit wire field", id);
        return 0;
    }
    /* op occupies a 4-bit field. An out-of-range enum value (whether
     * from a caller bug or wire-protocol drift) would either be
     * silently masked into a different op by RPC_HDR_MAKE, or land on
     * the server's `default:` branch — neither is what callers
     * expect. Cap at the highest defined op rather than just the
     * mask width so unknown-but-fits-in-4-bits values are also caught. */
    if (op > YETTY_YCLASS_RPC_OP_CREATE) {
        ywarn("rpc_call: op=%u is not a defined yetty_yclass_rpc_op", op);
        return 0;
    }
    uint32_t header = YETTY_YCLASS_RPC_HDR_MAKE(op, id);
    ydebug("op=%u id=%u body_len=%zu", op, id, body_len);

    uint32_t bl = (uint32_t)body_len;
    if (write_full(s->transport, &header, 4) < 0)
        return 0;
    if (write_full(s->transport, &bl, 4) < 0)
        return 0;
    if (body_len && write_full(s->transport, body, body_len) < 0)
        return 0;

    uint32_t resp_len = 0;
    if (read_full(s->transport, &resp_len, 4) < 0)
        return 0;
    if (resp_len > resp_max) {
        /* Drain the oversized payload so the next frame read starts
         * aligned. Without this we'd parse garbage from mid-stream. */
        uint8_t drain[256];
        size_t remain = resp_len;
        while (remain) {
            size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
            if (read_full(s->transport, drain, chunk) < 0)
                return 0;
            remain -= chunk;
        }
        return 0;
    }
    if (resp_len && read_full(s->transport, resp, resp_len) < 0)
        return 0;
    return resp_len;
}

uint32_t yetty_yclass_rpc_session_ensure_remote_id(struct yetty_yclass_rpc_session *s,
                                                   yetty_yclass_method_slot local_slot)
{
    if (!s)
        return YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    uint32_t cached = yetty_yclass_rpc_session_remote_id(s, local_slot);
    if (cached != YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED)
        return cached;

    struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(local_slot);
    if (YETTY_IS_ERR(nr)) {
        yetty_ycore_error_destroy(nr.error);
        return YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    }
    const char *name = nr.value;

    uint32_t remote = YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
    size_t n = yetty_yclass_rpc_call(s, YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0, name,
                                     strlen(name), &remote, sizeof(remote));
    if (n != sizeof(remote) || remote == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED)
        return YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;

    yetty_yclass_rpc_session_set_remote_id(s, local_slot, remote);
    ydebug("lazy resolve '%s' local=%u remote=%u", name, local_slot, remote);
    return remote;
}

int yetty_yclass_rpc_session_translate_class(struct yetty_yclass_rpc_session *s,
                                             const char *class_name)
{
    if (!s || !class_name)
        return -1;
    struct translated_class *t = NULL;
    HASH_FIND_STR(s->translated, class_name, t);
    if (t)
        return 0;

    uint8_t buf[BUF_MAX];
    size_t name_len = strlen(class_name);
    size_t resp_len = yetty_yclass_rpc_call(s, YETTY_YCLASS_RPC_OP_GET_CLASS, 0, class_name,
                                            name_len, buf, sizeof(buf));
    if (resp_len == 0)
        return -1;

    size_t off = 0;
    while (off + 2 + 4 <= resp_len) {
        uint16_t nl;
        memcpy(&nl, buf + off, 2);
        off += 2;
        if (off + nl + 4 > resp_len)
            break;
        char *slot_name = dup_wire_name(buf + off, nl);
        if (!slot_name)
            break; /* alloc fail mid-parse; skip the rest, keep what we got */
        off += nl;
        uint32_t rid;
        memcpy(&rid, buf + off, 4);
        off += 4;

        struct yetty_yclass_method_slot_result lr =
            yetty_yclass_method_slot_by_qname(slot_name);
        if (YETTY_IS_OK(lr)) {
            yetty_yclass_rpc_session_set_remote_id(s, lr.value, rid);
            ydebug("xlat['%s'] local=%u remote=%u", slot_name, lr.value, rid);
        } else {
            yetty_ycore_error_destroy(lr.error);
        }
        free(slot_name);
    }

    t = calloc(1, sizeof(*t));
    if (!t)
        return 0;
    t->name = strdup(class_name);
    if (!t->name) {
        free(t);
        return 0;
    }
    HASH_ADD_KEYPTR(hh, s->translated, t->name, strlen(t->name), t);
    return 0;
}
