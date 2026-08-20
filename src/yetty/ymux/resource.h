/*
 * resource.h — the server-side content-addressed resource store (#695).
 * See resource.c. Module-internal plain-C, owned by the daemon rich/history
 * model. Heavy payloads (images/PDF/fonts) are stored once by 64-bit content
 * hash and referenced by that hash so they are not re-sent per projection.
 */
#ifndef YETTY_YMUX_RESOURCE_H
#define YETTY_YMUX_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

struct yetty_ymux_resource_entry {
    uint64_t hash;
    uint8_t *bytes;
    size_t len;
    uint32_t refcount;
};

struct yetty_ymux_resource_store {
    struct yetty_ymux_resource_entry *entries;
    uint32_t count;
    uint32_t capacity;
};

void yetty_ymux_resource_store_init(struct yetty_ymux_resource_store *store);
void yetty_ymux_resource_store_free(struct yetty_ymux_resource_store *store);

/* Store `bytes` (deduped by content hash + full byte compare) and return its
 * 64-bit hash in *out_hash. An already-present payload just bumps its refcount;
 * one copy is kept regardless of how many figures reference it. */
struct yetty_ycore_void_result yetty_ymux_resource_add(struct yetty_ymux_resource_store *store,
                                                       const uint8_t *bytes, size_t len,
                                                       uint64_t *out_hash);

/* Borrowed payload for `hash` (+ its length), or NULL if absent. */
const uint8_t *yetty_ymux_resource_get(const struct yetty_ymux_resource_store *store, uint64_t hash,
                                       size_t *out_len);

int yetty_ymux_resource_has(const struct yetty_ymux_resource_store *store, uint64_t hash);

/* Drop one reference; frees + removes the payload when the last reference goes. */
void yetty_ymux_resource_release(struct yetty_ymux_resource_store *store, uint64_t hash);

uint32_t yetty_ymux_resource_count(const struct yetty_ymux_resource_store *store);

#endif /* YETTY_YMUX_RESOURCE_H */
