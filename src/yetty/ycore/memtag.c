/*
 * memtag.c — per-owner allocation accounting (see memtag.h).
 *
 * Byte accounting uses the allocator's usable-size query at alloc/free time,
 * so no size header is prepended and the numbers reflect what the allocator
 * actually handed out. With mimalloc linked (the instrumented desktop builds)
 * that is mi_usable_size; elsewhere the platform allocator's query is used.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/memtag.h>
#include <yetty/ycore/types.h>

#if defined(YETTY_HAS_MIMALLOC)
#include <mimalloc.h>
static size_t memtag_usable_size(void *ptr)
{
    return mi_usable_size(ptr);
}
#elif defined(_WIN32)
#include <malloc.h>
static size_t memtag_usable_size(void *ptr)
{
    return ptr ? _msize(ptr) : 0u;
}
#else
#include <malloc.h>
static size_t memtag_usable_size(void *ptr)
{
    return malloc_usable_size(ptr);
}
#endif

static void memtag_account_alloc(struct yetty_ycore_memtag *tag, void *ptr)
{
    if (!tag || !ptr) {
        return;
    }
    int64_t usable = (int64_t)memtag_usable_size(ptr);
    int64_t live = atomic_fetch_add(&tag->live_bytes, usable) + usable;
    atomic_fetch_add(&tag->total_allocs, 1);
    /* Peak update: monotonic compare-exchange loop (racy peaks may lag one
     * concurrent allocation — accounting, not a lock). */
    int64_t peak = atomic_load(&tag->peak_bytes);
    while (live > peak && !atomic_compare_exchange_weak(&tag->peak_bytes, &peak, live)) {
    }
}

static void memtag_account_free(struct yetty_ycore_memtag *tag, void *ptr)
{
    if (!tag || !ptr) {
        return;
    }
    atomic_fetch_sub(&tag->live_bytes, (int64_t)memtag_usable_size(ptr));
}

void *yetty_ycore_memtag_alloc(struct yetty_ycore_memtag *tag, size_t size)
{
    void *ptr = malloc(size);
    memtag_account_alloc(tag, ptr);
    return ptr;
}

void *yetty_ycore_memtag_calloc(struct yetty_ycore_memtag *tag, size_t member_count,
                                size_t member_size)
{
    void *ptr = calloc(member_count, member_size);
    memtag_account_alloc(tag, ptr);
    return ptr;
}

void *yetty_ycore_memtag_realloc(struct yetty_ycore_memtag *tag, void *ptr, size_t size)
{
    memtag_account_free(tag, ptr);
    void *grown = realloc(ptr, size);
    if (!grown && size) {
        /* realloc failed: the old block is still live — re-account it. */
        memtag_account_alloc(tag, ptr);
        return NULL;
    }
    memtag_account_alloc(tag, grown);
    return grown;
}

void yetty_ycore_memtag_free(struct yetty_ycore_memtag *tag, void *ptr)
{
    memtag_account_free(tag, ptr);
    free(ptr);
}

/*===========================================================================
 * Registry.
 *=========================================================================*/

struct yetty_ycore_memtag_registry_ptr_result yetty_ycore_memtag_registry_create(void)
{
    struct yetty_ycore_memtag_registry *registry = calloc(1, sizeof(*registry));
    if (!registry) {
        return YETTY_ERR(yetty_ycore_memtag_registry_ptr, "memtag registry alloc failed");
    }
    return YETTY_OK(yetty_ycore_memtag_registry_ptr, registry);
}

void yetty_ycore_memtag_registry_destroy(struct yetty_ycore_memtag_registry *registry)
{
    if (!registry) {
        return;
    }
    free(registry->tags);
    free(registry);
}

struct yetty_ycore_void_result yetty_ycore_memtag_registry_add(
    struct yetty_ycore_memtag_registry *registry, struct yetty_ycore_memtag *tag)
{
    if (!registry || !tag) {
        return YETTY_ERR(yetty_ycore_void, "memtag registry add: NULL argument");
    }
    if (registry->count == registry->capacity) {
        uint32_t new_capacity = registry->capacity ? registry->capacity * 2u : 16u;
        struct yetty_ycore_memtag **grown =
            realloc(registry->tags, (size_t)new_capacity * sizeof(*grown));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "memtag registry grow failed");
        }
        registry->tags = grown;
        registry->capacity = new_capacity;
    }
    registry->tags[registry->count++] = tag;
    return YETTY_OK_VOID();
}

void yetty_ycore_memtag_registry_remove(struct yetty_ycore_memtag_registry *registry,
                                        const struct yetty_ycore_memtag *tag)
{
    if (!registry) {
        return;
    }
    for (uint32_t index = 0; index < registry->count; ++index) {
        if (registry->tags[index] == tag) {
            registry->tags[index] = registry->tags[registry->count - 1u];
            registry->count--;
            return;
        }
    }
}

struct yetty_ycore_void_result yetty_ycore_memtag_registry_format(
    const struct yetty_ycore_memtag_registry *registry, struct yetty_ycore_buffer *out)
{
    if (!registry || !out) {
        return YETTY_ERR(yetty_ycore_void, "memtag format: NULL argument");
    }
    char line[160];
    int header_len = snprintf(line, sizeof(line), "%-28s %14s %14s %12s\n", "tag", "live_bytes",
                              "peak_bytes", "allocs");
    struct yetty_ycore_void_result write_res =
        yetty_ycore_buffer_write(out, line, (size_t)header_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "memtag format: header");

    /* Selection-sort a local index array by live bytes descending — the
     * registry is a dozen entries; simplicity over asymptotics. */
    uint32_t order[256];
    uint32_t order_count = registry->count < 256u ? registry->count : 256u;
    for (uint32_t index = 0; index < order_count; ++index) {
        order[index] = index;
    }
    for (uint32_t outer = 0; outer + 1u < order_count; ++outer) {
        uint32_t best = outer;
        for (uint32_t inner = outer + 1u; inner < order_count; ++inner) {
            if (atomic_load(&registry->tags[order[inner]]->live_bytes) >
                atomic_load(&registry->tags[order[best]]->live_bytes)) {
                best = inner;
            }
        }
        uint32_t swap = order[outer];
        order[outer] = order[best];
        order[best] = swap;
    }
    for (uint32_t index = 0; index < order_count; ++index) {
        const struct yetty_ycore_memtag *tag = registry->tags[order[index]];
        int line_len = snprintf(
            line, sizeof(line), "%-28s %14lld %14lld %12lld\n", tag->name ? tag->name : "?",
            (long long)atomic_load(&tag->live_bytes), (long long)atomic_load(&tag->peak_bytes),
            (long long)atomic_load(&tag->total_allocs));
        write_res = yetty_ycore_buffer_write(out, line, (size_t)line_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, write_res, "memtag format: row");
    }
    return YETTY_OK_VOID();
}
