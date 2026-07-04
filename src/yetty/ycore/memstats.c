/* memstats.c - Allocation-statistics sampling (mimalloc-backed) */

#include <yetty/ycore/memstats.h>

#if defined(YETTY_HAS_MIMALLOC)

#include <mimalloc-stats.h>
#include <mimalloc.h>
#include <string.h>

static uint64_t memstats_nonnegative(int64_t value)
{
    return value > 0 ? (uint64_t)value : 0;
}

struct yetty_ycore_memstats_result yetty_ycore_memstats_sample(void)
{
    mi_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    stats.size = sizeof(stats);
    stats.version = MI_STAT_VERSION;
    if (!mi_stats_get(&stats)) {
        return YETTY_ERR(yetty_ycore_memstats, "memstats_sample: mi_stats_get failed");
    }

    size_t elapsed_msecs = 0;
    size_t user_msecs = 0;
    size_t system_msecs = 0;
    size_t current_rss = 0;
    size_t peak_rss = 0;
    size_t current_commit = 0;
    size_t peak_commit = 0;
    size_t page_faults = 0;
    mi_process_info(&elapsed_msecs, &user_msecs, &system_msecs, &current_rss, &peak_rss,
                    &current_commit, &peak_commit, &page_faults);

    struct yetty_ycore_memstats sample = {
        .allocated_bytes = memstats_nonnegative(stats.malloc_normal.current) +
                           memstats_nonnegative(stats.malloc_huge.current),
        .peak_allocated_bytes = memstats_nonnegative(stats.malloc_normal.peak) +
                                memstats_nonnegative(stats.malloc_huge.peak),
        .committed_bytes = memstats_nonnegative(stats.committed.current),
        .resident_bytes = (uint64_t)current_rss,
        .peak_resident_bytes = (uint64_t)peak_rss,
        .allocation_count = memstats_nonnegative(stats.malloc_normal_count.total) +
                            memstats_nonnegative(stats.malloc_huge_count.total),
    };
    return YETTY_OK(yetty_ycore_memstats, sample);
}

#else /* !YETTY_HAS_MIMALLOC */

struct yetty_ycore_memstats_result yetty_ycore_memstats_sample(void)
{
    return YETTY_ERR(yetty_ycore_memstats,
                     "memstats_sample: built without an instrumented allocator");
}

#endif /* YETTY_HAS_MIMALLOC */
