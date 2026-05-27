/* RPC runtime — packed-header wire, op enum, uthash translations. */

#include "rpc.h"
#include "uthash.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_OBJECTS 256
#define BUF_MAX 65536

/* -------- server (process-global) state ---------------------------- */

struct object_entry { uint64_t handle; void *ptr; };

struct skel_lookup_node {
    skel_lookup_fn fn;
    struct skel_lookup_node *next;
};

struct skel_cache_entry {
    method_slot slot;
    rpc_skel_fn fn;
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

void rpc_init(void)
{
    struct rpc_server_state *s = server();
    s->object_count = 0;
    s->next_handle = 1;
}

void rpc_add_skel_lookup(skel_lookup_fn fn)
{
    if (!fn) return;
    struct skel_lookup_node *node = calloc(1, sizeof(*node));
    if (!node) return;
    struct rpc_server_state *s = server();
    node->fn = fn;
    node->next = s->lookup_chain;
    s->lookup_chain = node;
}

rpc_skel_fn rpc_skel_for(method_slot slot)
{
    struct rpc_server_state *s = server();
    if (slot == METHOD_SLOT_UNDEFINED) return NULL;

    struct skel_cache_entry *e = NULL;
    HASH_FIND(hh, s->skel_cache, &slot, sizeof(slot), e);
    if (e) return e->fn;

    /* Walk the chain. First hit wins. */
    rpc_skel_fn fn = NULL;
    for (struct skel_lookup_node *n = s->lookup_chain; n; n = n->next) {
        fn = n->fn(slot);
        if (fn) break;
    }
    if (!fn) return NULL;

    e = calloc(1, sizeof(*e));
    if (!e) return fn;
    e->slot = slot;
    e->fn = fn;
    HASH_ADD(hh, s->skel_cache, slot, sizeof(slot), e);
    return fn;
}

uint64_t rpc_register_object(void *obj)
{
    struct rpc_server_state *s = server();
    if (s->object_count >= MAX_OBJECTS) return 0;
    uint64_t h = s->next_handle++;
    s->objects[s->object_count].handle = h;
    s->objects[s->object_count].ptr = obj;
    s->object_count++;
    return h;
}

void *rpc_handle_resolve(uint64_t h)
{
    if (!h) return NULL;
    struct rpc_server_state *s = server();
    for (size_t i = 0; i < s->object_count; ++i)
        if (s->objects[i].handle == h) return s->objects[i].ptr;
    return NULL;
}

/* -------- io helpers ----------------------------------------------- */

static int read_full(int fd, void *buf, size_t n)
{
    char *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r > 0) { p += r; n -= (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

static int write_full(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w > 0) { p += w; n -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}

/* -------- admin handlers (server side) ----------------------------- */

static size_t handle_resolve_slot(const void *body, size_t body_len,
                                  void *resp, size_t resp_max)
{
    char name[128];
    size_t n = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
    memcpy(name, body, n);
    name[n] = 0;
    method_slot slot = method_slot_by_qname(name);
    uint32_t out = (slot == METHOD_SLOT_UNDEFINED) ? UINT32_MAX : (uint32_t)slot;
    if (resp_max < sizeof(out)) return 0;
    memcpy(resp, &out, sizeof(out));
    fprintf(stderr, "[server] resolve_slot('%s') -> %u\n", name, out);
    return sizeof(out);
}

struct get_class_ctx { uint8_t *out; size_t off; size_t cap; };

static void get_class_emit(const char *name, method_slot slot, void *ud)
{
    struct get_class_ctx *gc = ud;
    size_t name_len = strlen(name);
    size_t need = 2 + name_len + 4;
    if (gc->off + need > gc->cap) return;
    uint16_t nl = (uint16_t)name_len;
    memcpy(gc->out + gc->off, &nl, 2);          gc->off += 2;
    memcpy(gc->out + gc->off, name, name_len);  gc->off += name_len;
    uint32_t rid = (uint32_t)slot;
    memcpy(gc->out + gc->off, &rid, 4);         gc->off += 4;
}

static size_t handle_get_class(const void *body, size_t body_len,
                               void *resp, size_t resp_max)
{
    char name[128];
    size_t n = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
    memcpy(name, body, n);
    name[n] = 0;
    const struct class *cls = class_by_name(name);
    if (!cls) return 0;
    struct get_class_ctx gc = { resp, 0, resp_max };
    class_for_each_slot(cls, get_class_emit, &gc);
    fprintf(stderr, "[server] get_class('%s') -> %zu entries (%zu bytes)\n",
            name, gc.off / 6, gc.off);
    return gc.off;
}

static size_t handle_create(const void *body, size_t body_len,
                            void *resp, size_t resp_max)
{
    char name[128];
    size_t n = body_len < sizeof(name) - 1 ? body_len : sizeof(name) - 1;
    memcpy(name, body, n);
    name[n] = 0;
    const struct class *cls = class_by_name(name);
    struct object *obj = cls ? object_alloc(cls) : NULL;
    uint64_t h = obj ? rpc_register_object(obj) : 0;
    if (resp_max < sizeof(h)) return 0;
    memcpy(resp, &h, sizeof(h));
    fprintf(stderr, "[server] create('%s') -> handle=%llu\n",
            name, (unsigned long long)h);
    return sizeof(h);
}

/* -------- server loop ---------------------------------------------- */

void rpc_server_run(int fd)
{
    struct rpc_server_state *gs = server();
    static uint8_t body[BUF_MAX];
    static uint8_t resp[BUF_MAX];

    (void)gs;
    for (;;) {
        uint32_t header = 0, body_len = 0;
        if (read_full(fd, &header, 4) < 0) return;
        if (read_full(fd, &body_len, 4) < 0) return;
        if (body_len > BUF_MAX) return;
        if (body_len && read_full(fd, body, body_len) < 0) return;

        enum rpc_op op = RPC_HDR_OP(header);
        uint32_t id   = RPC_HDR_ID(header);
        uint32_t resp_len = 0;

        switch (op) {
        case RPC_OP_CALL: {
            rpc_skel_fn fn = rpc_skel_for((method_slot)id);
            if (fn) {
                fprintf(stderr, "[server] CALL slot=%u body_len=%u\n", id, body_len);
                resp_len = (uint32_t)fn(body, body_len, resp, BUF_MAX);
            } else {
                fprintf(stderr, "[server] CALL slot=%u — no skel\n", id);
            }
            break;
        }
        case RPC_OP_RESOLVE_SLOT:
            resp_len = (uint32_t)handle_resolve_slot(body, body_len, resp, BUF_MAX);
            break;
        case RPC_OP_GET_CLASS:
            resp_len = (uint32_t)handle_get_class(body, body_len, resp, BUF_MAX);
            break;
        case RPC_OP_CREATE:
            resp_len = (uint32_t)handle_create(body, body_len, resp, BUF_MAX);
            break;
        default:
            fprintf(stderr, "[server] unknown op=%u\n", op);
            break;
        }

        if (write_full(fd, &resp_len, 4) < 0) return;
        if (resp_len && write_full(fd, resp, resp_len) < 0) return;
    }
}

/* -------- client session ------------------------------------------- */

struct translated_class { char *name; UT_hash_handle hh; };

struct remote_id_entry {
    method_slot local_slot;
    uint32_t remote_id;
    UT_hash_handle hh;
};

struct rpc_session {
    int fd;
    /* Local slot → remote id. Local slots are sparse (domain id in
     * upper bits), so hash by slot rather than flat-array. */
    struct remote_id_entry *remote_ids;
    struct translated_class *translated;  /* by class name */
};

struct rpc_session *rpc_session_create(int fd)
{
    struct rpc_session *s = calloc(1, sizeof(*s));
    if (s) s->fd = fd;
    return s;
}

void rpc_session_destroy(struct rpc_session *s)
{
    if (!s) return;
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
    if (s->fd >= 0) close(s->fd);
    free(s);
}

/* Sentinel for "this slot has no remote_id yet on this session". Distinct
 * from any valid wire id since the wire id is only 28 bits — anything
 * >= 2^28 (let alone UINT32_MAX) cannot be a real slot. */
#define REMOTE_ID_UNRESOLVED UINT32_MAX

uint32_t rpc_session_remote_id(struct rpc_session *s, method_slot slot)
{
    if (!s) return REMOTE_ID_UNRESOLVED;
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    return e ? e->remote_id : REMOTE_ID_UNRESOLVED;
}

void rpc_session_set_remote_id(struct rpc_session *s, method_slot slot,
                               uint32_t remote_id)
{
    if (!s) return;
    struct remote_id_entry *e = NULL;
    HASH_FIND(hh, s->remote_ids, &slot, sizeof(slot), e);
    if (!e) {
        e = calloc(1, sizeof(*e));
        if (!e) return;
        e->local_slot = slot;
        HASH_ADD(hh, s->remote_ids, local_slot, sizeof(method_slot), e);
    }
    e->remote_id = remote_id;
}

size_t rpc_call(struct rpc_session *s, enum rpc_op op, uint32_t id,
                const void *body, size_t body_len, void *resp, size_t resp_max)
{
    if (!s) return 0;
    uint32_t header = RPC_HDR_MAKE(op, id);
    fprintf(stderr, "[client] op=%u id=%u body_len=%zu\n", op, id, body_len);

    uint32_t bl = (uint32_t)body_len;
    if (write_full(s->fd, &header, 4) < 0) return 0;
    if (write_full(s->fd, &bl, 4) < 0) return 0;
    if (body_len && write_full(s->fd, body, body_len) < 0) return 0;

    uint32_t resp_len = 0;
    if (read_full(s->fd, &resp_len, 4) < 0) return 0;
    if (resp_len > resp_max) {
        /* Drain the oversized payload so the next frame read starts
         * aligned. Without this we'd parse garbage from mid-stream. */
        uint8_t drain[256];
        size_t remain = resp_len;
        while (remain) {
            size_t chunk = remain > sizeof(drain) ? sizeof(drain) : remain;
            if (read_full(s->fd, drain, chunk) < 0) return 0;
            remain -= chunk;
        }
        return 0;
    }
    if (resp_len && read_full(s->fd, resp, resp_len) < 0) return 0;
    return resp_len;
}

uint32_t rpc_session_ensure_remote_id(struct rpc_session *s, method_slot local_slot)
{
    if (!s) return REMOTE_ID_UNRESOLVED;
    uint32_t cached = rpc_session_remote_id(s, local_slot);
    if (cached != REMOTE_ID_UNRESOLVED) return cached;

    const char *name = method_slot_name(local_slot);
    if (!name) return REMOTE_ID_UNRESOLVED;

    uint32_t remote = REMOTE_ID_UNRESOLVED;
    size_t n = rpc_call(s, RPC_OP_RESOLVE_SLOT, 0,
                        name, strlen(name), &remote, sizeof(remote));
    if (n != sizeof(remote) || remote == REMOTE_ID_UNRESOLVED)
        return REMOTE_ID_UNRESOLVED;

    rpc_session_set_remote_id(s, local_slot, remote);
    fprintf(stderr, "[client] lazy resolve '%s' local=%u remote=%u\n",
            name, local_slot, remote);
    return remote;
}

int rpc_session_translate_class(struct rpc_session *s, const char *class_name)
{
    if (!s || !class_name) return -1;
    struct translated_class *t = NULL;
    HASH_FIND_STR(s->translated, class_name, t);
    if (t) return 0;

    uint8_t buf[BUF_MAX];
    size_t name_len = strlen(class_name);
    size_t resp_len = rpc_call(s, RPC_OP_GET_CLASS, 0, class_name, name_len,
                               buf, sizeof(buf));
    if (resp_len == 0) return -1;

    size_t off = 0;
    while (off + 2 + 4 <= resp_len) {
        uint16_t nl;
        memcpy(&nl, buf + off, 2); off += 2;
        if (off + nl + 4 > resp_len) break;
        char slot_name[128];
        size_t copy = nl < sizeof(slot_name) - 1 ? nl : sizeof(slot_name) - 1;
        memcpy(slot_name, buf + off, copy);
        slot_name[copy] = 0;
        off += nl;
        uint32_t rid;
        memcpy(&rid, buf + off, 4); off += 4;

        method_slot local = method_slot_by_qname(slot_name);
        if (local != METHOD_SLOT_UNDEFINED) {
            rpc_session_set_remote_id(s, local, rid);
            fprintf(stderr, "[client] xlat['%s'] local=%u remote=%u\n",
                    slot_name, local, rid);
        }
    }

    t = calloc(1, sizeof(*t));
    if (!t) return 0;
    t->name = strdup(class_name);
    if (!t->name) { free(t); return 0; }
    HASH_ADD_KEYPTR(hh, s->translated, t->name, strlen(t->name), t);
    return 0;
}
