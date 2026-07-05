/* memstats.c - Allocation-statistics sampling (mimalloc- or ASAN-backed) */

#include <yetty/ycore/memstats.h>

/* Detect an address-sanitized build (clang spells it via __has_feature, gcc
 * defines __SANITIZE_ADDRESS__). */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define YETTY_MEMSTATS_HAS_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define YETTY_MEMSTATS_HAS_ASAN 1
#endif

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

#elif defined(YETTY_MEMSTATS_HAS_ASAN)

/* ASAN builds run without mimalloc (the override would blind the sanitizer),
 * but the sanitizer runtime exposes its own allocator statistics — so the
 * statusbar readout keeps working during ASAN soaks. committed = live heap
 * bytes the sanitizer tracks; peak committed = the sanitizer's mapped heap
 * (monotonic, a high-water proxy). Resident figures come from the OS. */
#include <sanitizer/allocator_interface.h>
#if defined(__linux__)
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

struct yetty_ycore_memstats_result yetty_ycore_memstats_sample(void)
{
    struct yetty_ycore_memstats sample = {
        .committed_bytes = (uint64_t)__sanitizer_get_current_allocated_bytes(),
        .peak_committed_bytes = (uint64_t)__sanitizer_get_heap_size(),
    };
#if defined(__linux__)
    FILE *statm = fopen("/proc/self/statm", "r");
    if (statm) {
        unsigned long total_pages = 0;
        unsigned long resident_pages = 0;
        if (fscanf(statm, "%lu %lu", &total_pages, &resident_pages) == 2) {
            sample.resident_bytes = (uint64_t)resident_pages * (uint64_t)sysconf(_SC_PAGESIZE);
        }
        fclose(statm);
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        sample.peak_resident_bytes = (uint64_t)usage.ru_maxrss * 1024u;
    }
#endif
    return YETTY_OK(yetty_ycore_memstats, sample);
}

#else /* no instrumented allocator */

struct yetty_ycore_memstats_result yetty_ycore_memstats_sample(void)
{
    return YETTY_ERR(yetty_ycore_memstats,
                     "memstats_sample: built without an instrumented allocator");
}

#endif /* YETTY_HAS_MIMALLOC */
