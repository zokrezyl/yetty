/* font-cache.c — refcounted MSDF font pool, per canvas. */

#include <yetty/yfont/font-cache.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ytrace/ytrace.h>

#define KEY_MAX 64

struct yetty_yfont_cache_entry {
    char key[KEY_MAX];
    struct yetty_ydraw_font *font;
    uint32_t refcount;
    bool in_use;
};

struct yetty_yfont_cache {
    char shaders_dir[1024];

    struct yetty_yfont_cache_entry *entries;
    uint32_t count;    /* slots ever allocated (including freed) */
    uint32_t capacity;

    yetty_yfont_cache_handle *free_slots;
    uint32_t free_count;
    uint32_t free_capacity;

    /* Bumped on every slot alloc AND every slot release. Lets consumers
     * (ypaint-layer's dispatcher rebuild) detect any change in the active
     * slot set — `count` alone is a high watermark and stays unchanged
     * when a slot drops. */
    uint32_t generation;
};

struct yetty_yfont_cache_ptr_result yetty_yfont_cache_create(const char *shaders_dir)
{
    if (!shaders_dir) {
        return YETTY_ERR(yetty_yfont_cache_ptr, "shaders_dir is NULL");
    }

    struct yetty_yfont_cache *cache = calloc(1, sizeof(struct yetty_yfont_cache));
    if (!cache) {
        return YETTY_ERR(yetty_yfont_cache_ptr, "calloc failed");
    }
    strncpy(cache->shaders_dir, shaders_dir, sizeof(cache->shaders_dir) - 1);
    cache->shaders_dir[sizeof(cache->shaders_dir) - 1] = '\0';

    return YETTY_OK(yetty_yfont_cache_ptr, cache);
}

void yetty_yfont_cache_destroy(struct yetty_yfont_cache *cache)
{
    if (!cache) {
        return;
    }
    for (uint32_t i = 0; i < cache->count; i++) {
        struct yetty_yfont_cache_entry *e = &cache->entries[i];
        if (e->in_use && e->font && e->font->ops && e->font->ops->destroy) {
            e->font->ops->destroy(e->font);
        }
    }
    free(cache->entries);
    free(cache->free_slots);
    free(cache);
}

static yetty_yfont_cache_handle find_handle_by_key(const struct yetty_yfont_cache *cache,
                                                   const char *key)
{
    for (uint32_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].in_use && strcmp(cache->entries[i].key, key) == 0) {
            return i;
        }
    }
    return YETTY_YFONT_CACHE_HANDLE_INVALID;
}

/* Return an unused slot index. Reuses one from free_slots, or grows entries[]. */
static struct yetty_ycore_int_result alloc_slot(struct yetty_yfont_cache *cache)
{
    if (cache->free_count > 0) {
        yetty_yfont_cache_handle h = cache->free_slots[--cache->free_count];
        return YETTY_OK(yetty_ycore_int, (int)h);
    }
    if (cache->count == cache->capacity) {
        uint32_t nc = cache->capacity ? cache->capacity * 2 : 8;
        struct yetty_yfont_cache_entry *ne =
            realloc(cache->entries, nc * sizeof(struct yetty_yfont_cache_entry));
        if (!ne) {
            return YETTY_ERR(yetty_ycore_int, "entries realloc failed");
        }
        memset(ne + cache->capacity, 0,
               (nc - cache->capacity) * sizeof(struct yetty_yfont_cache_entry));
        cache->entries = ne;
        cache->capacity = nc;
    }
    return YETTY_OK(yetty_ycore_int, (int)cache->count++);
}

struct yetty_yfont_cache_ref_result yetty_yfont_cache_get_font(struct yetty_yfont_cache *cache,
                                                               const char *key,
                                                               const char *cdb_path)
{
    if (!cache) {
        return YETTY_ERR(yetty_yfont_cache_ref, "cache is NULL");
    }
    if (!key) {
        return YETTY_ERR(yetty_yfont_cache_ref, "key is NULL");
    }
    if (strlen(key) >= KEY_MAX) {
        return YETTY_ERR(yetty_yfont_cache_ref, "key too long");
    }

    /* Hit */
    yetty_yfont_cache_handle h = find_handle_by_key(cache, key);
    if (h != YETTY_YFONT_CACHE_HANDLE_INVALID) {
        cache->entries[h].refcount++;
        struct yetty_yfont_cache_ref ref = {.font = cache->entries[h].font, .handle = h};
        ydebug("yfont_cache: hit key='%s' handle=%u refcount=%u", key, h,
               cache->entries[h].refcount);
        return YETTY_OK(yetty_yfont_cache_ref, ref);
    }

    /* Miss — construct */
    if (!cdb_path) {
        return YETTY_ERR(yetty_yfont_cache_ref, "cdb_path is NULL on miss");
    }

    char shader_path[1280];
    snprintf(shader_path, sizeof(shader_path), "%s/msdf-font.wgsl", cache->shaders_dir);

    struct yetty_font_font_result fr = yetty_yfont_msdf_font_create(cdb_path, shader_path, key);
    YETTY_RETURN_IF_ERR(yetty_yfont_cache_ref, fr, "msdf_font_create failed");

    struct yetty_ycore_int_result sr = alloc_slot(cache);
    if (YETTY_IS_ERR(sr)) {
        fr.value->ops->destroy(fr.value);
        return YETTY_ERR(yetty_yfont_cache_ref, "slot alloc failed", sr);
    }
    yetty_yfont_cache_handle slot = (yetty_yfont_cache_handle)sr.value;

    struct yetty_yfont_cache_entry *e = &cache->entries[slot];
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    e->font = fr.value;
    e->refcount = 1;
    e->in_use = true;
    cache->generation++;

    ydebug("yfont_cache: miss key='%s' -> slot=%u (constructed)", key, slot);
    struct yetty_yfont_cache_ref ref = {.font = fr.value, .handle = slot};
    return YETTY_OK(yetty_yfont_cache_ref, ref);
}

void yetty_yfont_cache_retain(struct yetty_yfont_cache *cache, yetty_yfont_cache_handle h)
{
    if (!cache || h == YETTY_YFONT_CACHE_HANDLE_INVALID || h >= cache->count) {
        return;
    }
    if (!cache->entries[h].in_use) {
        return;
    }
    cache->entries[h].refcount++;
}

void yetty_yfont_cache_release_font(struct yetty_yfont_cache *cache, yetty_yfont_cache_handle h)
{
    if (!cache || h == YETTY_YFONT_CACHE_HANDLE_INVALID || h >= cache->count) {
        return;
    }
    struct yetty_yfont_cache_entry *e = &cache->entries[h];
    if (!e->in_use || e->refcount == 0) {
        return;
    }
    if (--e->refcount > 0) {
        return;
    }

    ydebug("yfont_cache: drop slot=%u key='%s' (refcount hit 0)", h, e->key);
    if (e->font && e->font->ops && e->font->ops->destroy) {
        e->font->ops->destroy(e->font);
    }
    e->font = NULL;
    e->key[0] = '\0';
    e->in_use = false;
    cache->generation++;

    /* Push to free list. On OOM growing the free list, leak the slot — the
     * font is already gone and the caller can keep operating. */
    if (cache->free_count == cache->free_capacity) {
        uint32_t nc = cache->free_capacity ? cache->free_capacity * 2 : 8;
        yetty_yfont_cache_handle *ns =
            realloc(cache->free_slots, nc * sizeof(yetty_yfont_cache_handle));
        if (!ns) {
            ywarn("yfont_cache: free-list realloc failed; slot %u leaked", h);
            return;
        }
        cache->free_slots = ns;
        cache->free_capacity = nc;
    }
    cache->free_slots[cache->free_count++] = h;
}

struct yetty_ydraw_font *yetty_yfont_cache_font_at(const struct yetty_yfont_cache *cache,
                                                    yetty_yfont_cache_handle h)
{
    if (!cache || h == YETTY_YFONT_CACHE_HANDLE_INVALID || h >= cache->count) {
        return NULL;
    }
    if (!cache->entries[h].in_use) {
        return NULL;
    }
    return cache->entries[h].font;
}

uint32_t yetty_yfont_cache_count(const struct yetty_yfont_cache *cache)
{
    return cache ? cache->count : 0;
}

uint32_t yetty_yfont_cache_generation(const struct yetty_yfont_cache *cache)
{
    return cache ? cache->generation : 0;
}
