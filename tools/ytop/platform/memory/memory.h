/*
 * memory.h — memory abstraction: physical RAM and swap usage.
 *
 * A snapshot is a self-contained reading — memory has no per-tick deltas, so
 * there is no monitor struct; ytop_memory_sample() reads current totals each
 * call. All figures are in bytes so the presentation layer formats them
 * uniformly.
 */
#ifndef YTOP_PLATFORM_MEMORY_MEMORY_H
#define YTOP_PLATFORM_MEMORY_MEMORY_H

#include <stdint.h>

#include <yetty/ycore/result.h>

struct ytop_memory_snapshot {
    uint64_t ram_total;     /* bytes */
    uint64_t ram_used;      /* total - available (btop's "used") */
    uint64_t ram_available; /* MemAvailable */
    uint64_t ram_free;      /* MemFree */
    uint64_t ram_cached;    /* Cached + Buffers + SReclaimable */
    uint64_t swap_total;    /* bytes */
    uint64_t swap_used;     /* bytes */
};

/* Fill *out with a fresh memory reading. */
struct yetty_ycore_void_result ytop_memory_sample(struct ytop_memory_snapshot *out);

#endif /* YTOP_PLATFORM_MEMORY_MEMORY_H */
