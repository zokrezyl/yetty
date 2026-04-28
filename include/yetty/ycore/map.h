#ifndef YETTY_YCORE_MAP_H
#define YETTY_YCORE_MAP_H

/*
 * yetty_ycore_map - Generic open-addressing hash map
 *
 * Fixed-capacity, uint32_t keys, uint32_t values.
 * Uses linear probing with power-of-2 table size.
 * Sentinel key 0xFFFFFFFF = empty slot.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YCORE_MAP_EMPTY_KEY 0xFFFFFFFF

struct yetty_ycore_map_entry {
    uint32_t key;
    uint32_t value;
};

struct yetty_ycore_map {
    struct yetty_ycore_map_entry *entries;
    uint32_t capacity; /* power of 2 */
    uint32_t count;
    uint32_t mask; /* capacity - 1 */
};

/* Create map with given capacity (rounded up to power of 2) */
static inline int yetty_ycore_map_init(struct yetty_ycore_map *m, uint32_t capacity)
{
    /* Round up to power of 2 */
    uint32_t cap = 16;
    while (cap < capacity) {
        cap <<= 1;
    }

    m->entries = (struct yetty_ycore_map_entry *)calloc(cap, sizeof(struct yetty_ycore_map_entry));
    if (!m->entries) {
        return -1;
    }

    m->capacity = cap;
    m->count = 0;
    m->mask = cap - 1;

    /* Fill with empty sentinel */
    for (uint32_t i = 0; i < cap; i++) {
        m->entries[i].key = YETTY_YCORE_MAP_EMPTY_KEY;
    }

    return 0;
}

static inline void yetty_ycore_map_destroy(struct yetty_ycore_map *m)
{
    free(m->entries);
    m->entries = NULL;
    m->capacity = 0;
    m->count = 0;
}

/* FNV-1a hash for uint32_t key */
static inline uint32_t yetty_ycore_map_hash(uint32_t key)
{
    uint32_t h = 2166136261u;
    h ^= key & 0xFF;
    h *= 16777619u;
    h ^= (key >> 8) & 0xFF;
    h *= 16777619u;
    h ^= (key >> 16) & 0xFF;
    h *= 16777619u;
    h ^= (key >> 24) & 0xFF;
    h *= 16777619u;
    return h;
}

/* Put key-value. Returns 0 on success, -1 if full. */
static inline int yetty_ycore_map_put(struct yetty_ycore_map *m, uint32_t key, uint32_t value)
{
    if (m->count >= (m->capacity * 3 / 4)) {
        return -1; /* load factor exceeded */
    }

    uint32_t idx = yetty_ycore_map_hash(key) & m->mask;
    for (;;) {
        uint32_t k = m->entries[idx].key;
        if (k == YETTY_YCORE_MAP_EMPTY_KEY) {
            m->entries[idx].key = key;
            m->entries[idx].value = value;
            m->count++;
            return 0;
        }
        if (k == key) {
            m->entries[idx].value = value;
            return 0;
        }
        idx = (idx + 1) & m->mask;
    }
}

/* Get value for key. Returns pointer to value, or NULL if not found. */
static inline const uint32_t *yetty_ycore_map_get(const struct yetty_ycore_map *m, uint32_t key)
{
    uint32_t idx = yetty_ycore_map_hash(key) & m->mask;
    for (;;) {
        uint32_t k = m->entries[idx].key;
        if (k == YETTY_YCORE_MAP_EMPTY_KEY) {
            return NULL;
        }
        if (k == key) {
            return &m->entries[idx].value;
        }
        idx = (idx + 1) & m->mask;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_MAP_H */
