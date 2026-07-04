/* memstats.c - Allocation-statistics sampling (mimalloc-backed) */

#include <yetty/ycore/memstats.h>

#if defined(YETTY_HAS_MIMALLOC)

#include <mimalloc.h>

struct yetty_ycore_memstats_result yetty_ycore_memstats_sample(void)
{
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
        .committed_bytes = (uint64_t)current_commit,
        .peak_committed_bytes = (uint64_t)peak_commit,
        .resident_bytes = (uint64_t)current_rss,
        .peak_resident_bytes = (uint64_t)peak_rss,
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
