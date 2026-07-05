#ifndef YETTY_YCORE_MEMTAG_H
#define YETTY_YCORE_MEMTAG_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_buffer;

/* Per-owner allocation accounting: a tagged wrapper around the allocator at
 * bulk-owner choke points (grid line cells/arenas, scrollback segments, image
 * pixels, decoder contexts, ...). Two atomics per call, exact byte accounting
 * via the allocator's usable-size query on free (no headers), allocator-
 * agnostic, and immune to the cross-thread accounting loss in mimalloc's own
 * live-bytes stats (which drop decrements for cross-thread frees).
 *
 * A tag lives inside its owning object (never at file scope) and its name
 * must outlive it (string literals). All wrappers accept a NULL tag and then
 * behave exactly like the plain allocator calls — call sites stay branch-free
 * about whether accounting is wired up. */
struct yetty_ycore_memtag {
    const char *name; /* "yvterm/ring", "yvterm/archive", "yimage/pixels", ... */
    _Atomic int64_t live_bytes;
    _Atomic int64_t peak_bytes;
    _Atomic int64_t total_allocs;
};

void *yetty_ycore_memtag_alloc(struct yetty_ycore_memtag *tag, size_t size);
void *yetty_ycore_memtag_calloc(struct yetty_ycore_memtag *tag, size_t member_count,
                                size_t member_size);
void *yetty_ycore_memtag_realloc(struct yetty_ycore_memtag *tag, void *ptr, size_t size);
void yetty_ycore_memtag_free(struct yetty_ycore_memtag *tag, void *ptr);

/* Registry: a flat list of live tags feeding the yctl `memtags` dump (and any
 * future statusbar top-consumers readout). Owned by the framework; add/remove
 * and dump run on the loop thread (the tag counters themselves update from
 * any thread). Tags must be removed before their owning object dies. */
struct yetty_ycore_memtag_registry {
    struct yetty_ycore_memtag **tags;
    uint32_t count;
    uint32_t capacity;
};

YETTY_YRESULT_DECLARE(yetty_ycore_memtag_registry_ptr, struct yetty_ycore_memtag_registry *);

struct yetty_ycore_memtag_registry_ptr_result yetty_ycore_memtag_registry_create(void);
void yetty_ycore_memtag_registry_destroy(struct yetty_ycore_memtag_registry *registry);

struct yetty_ycore_void_result yetty_ycore_memtag_registry_add(
    struct yetty_ycore_memtag_registry *registry, struct yetty_ycore_memtag *tag);
void yetty_ycore_memtag_registry_remove(struct yetty_ycore_memtag_registry *registry,
                                        const struct yetty_ycore_memtag *tag);

/* Append a human-readable table of every registered tag (name, live, peak,
 * allocs) to `out`, sorted by live bytes descending. */
struct yetty_ycore_void_result yetty_ycore_memtag_registry_format(
    const struct yetty_ycore_memtag_registry *registry, struct yetty_ycore_buffer *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_MEMTAG_H */
