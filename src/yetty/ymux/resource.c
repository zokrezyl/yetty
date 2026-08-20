/*
 * resource.c — the server-side content-addressed resource store (#695).
 *
 * Heavy rich payloads (images, PDF pages, fonts) are stored ONCE keyed by a
 * 64-bit content hash and referenced by that hash, so a payload shared across
 * figures or re-projected every generation is not re-transmitted (#695
 * acceptance: "Heavy payloads are content-addressed and not resent on every
 * projection"; ties to #690/#691). This is the GPU-free daemon-side store; the
 * projector emits references and the independent resource channel + client cache
 * are wired on top.
 *
 * Module-internal plain-C (the tty-render.c precedent): owned by the daemon
 * history/rich model, passed explicitly. Dedup is hash + full byte compare, so a
 * hash collision can never alias distinct content.
 */
#include "resource.h"

#include <stdlib.h>
#include <string.h>

/* FNV-1a 64-bit content hash. */
static uint64_t resource_hash(const uint8_t *bytes, size_t len)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (size_t index = 0; index < len; ++index) {
        hash ^= bytes[index];
        hash *= 0x00000100000001b3ull;
    }
    return hash;
}

void yetty_ymux_resource_store_init(struct yetty_ymux_resource_store *store)
{
    store->entries = NULL;
    store->count = 0;
    store->capacity = 0;
}

void yetty_ymux_resource_store_free(struct yetty_ymux_resource_store *store)
{
    for (uint32_t index = 0; index < store->count; ++index) {
        free(store->entries[index].bytes);
    }
    free(store->entries);
    store->entries = NULL;
    store->count = 0;
    store->capacity = 0;
}

static struct yetty_ymux_resource_entry *resource_find(struct yetty_ymux_resource_store *store,
                                                       uint64_t hash, const uint8_t *bytes,
                                                       size_t len)
{
    for (uint32_t index = 0; index < store->count; ++index) {
        struct yetty_ymux_resource_entry *entry = &store->entries[index];
        if (entry->hash == hash && entry->len == len &&
            (len == 0 || memcmp(entry->bytes, bytes, len) == 0)) {
            return entry;
        }
    }
    return NULL;
}

struct yetty_ycore_void_result yetty_ymux_resource_add(struct yetty_ymux_resource_store *store,
                                                       const uint8_t *bytes, size_t len,
                                                       uint64_t *out_hash)
{
    if (!store || (!bytes && len > 0)) {
        return YETTY_ERR(yetty_ycore_void, "ymux resource_add: bad args");
    }
    uint64_t hash = resource_hash(bytes, len);
    struct yetty_ymux_resource_entry *existing = resource_find(store, hash, bytes, len);
    if (existing) {
        ++existing->refcount; /* dedup: shared payload, one copy */
        if (out_hash) {
            *out_hash = hash;
        }
        return YETTY_OK_VOID();
    }
    if (store->count == store->capacity) {
        uint32_t new_capacity = store->capacity ? store->capacity * 2 : 8;
        struct yetty_ymux_resource_entry *grown = realloc(
            store->entries, (size_t)new_capacity * sizeof(struct yetty_ymux_resource_entry));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux resource_add: grow");
        }
        store->entries = grown;
        store->capacity = new_capacity;
    }
    uint8_t *copy = NULL;
    if (len > 0) {
        copy = malloc(len);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "ymux resource_add: copy");
        }
        memcpy(copy, bytes, len);
    }
    struct yetty_ymux_resource_entry *entry = &store->entries[store->count++];
    entry->hash = hash;
    entry->bytes = copy;
    entry->len = len;
    entry->refcount = 1;
    if (out_hash) {
        *out_hash = hash;
    }
    return YETTY_OK_VOID();
}

const uint8_t *yetty_ymux_resource_get(const struct yetty_ymux_resource_store *store, uint64_t hash,
                                       size_t *out_len)
{
    for (uint32_t index = 0; index < store->count; ++index) {
        if (store->entries[index].hash == hash) {
            if (out_len) {
                *out_len = store->entries[index].len;
            }
            return store->entries[index].bytes;
        }
    }
    if (out_len) {
        *out_len = 0;
    }
    return NULL;
}

int yetty_ymux_resource_has(const struct yetty_ymux_resource_store *store, uint64_t hash)
{
    for (uint32_t index = 0; index < store->count; ++index) {
        if (store->entries[index].hash == hash) {
            return 1;
        }
    }
    return 0;
}

/* Drop one reference; the payload is freed and removed when the last reference
 * goes (retention: evict what nothing references — the #23 hook). */
void yetty_ymux_resource_release(struct yetty_ymux_resource_store *store, uint64_t hash)
{
    for (uint32_t index = 0; index < store->count; ++index) {
        struct yetty_ymux_resource_entry *entry = &store->entries[index];
        if (entry->hash != hash) {
            continue;
        }
        if (entry->refcount > 1) {
            --entry->refcount;
            return;
        }
        free(entry->bytes);
        store->entries[index] = store->entries[store->count - 1]; /* swap-remove */
        --store->count;
        return;
    }
}

uint32_t yetty_ymux_resource_count(const struct yetty_ymux_resource_store *store)
{
    return store->count;
}
