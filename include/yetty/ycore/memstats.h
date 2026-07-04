#ifndef YETTY_YCORE_MEMSTATS_H
#define YETTY_YCORE_MEMSTATS_H

#include <yetty/ycore/result.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Snapshot of process allocation statistics, sampled from the linked
 * allocator (mimalloc) plus the OS process accounting. Committed bytes
 * are the allocator's real OS footprint; resident figures are the OS
 * view of the whole process (GPU driver mappings included).
 *
 * Deliberately NOT exposed: mimalloc's live-bytes counters
 * (malloc_normal/malloc_huge "current"). Their accounting loses the
 * decrement for blocks freed on a different thread than the one that
 * allocated them, so in a multi-threaded process they climb without
 * bound and read as a fictional leak. Committed bytes cannot drift
 * from reality: leaked blocks keep their pages committed, so genuine
 * leaks still show. */
struct yetty_ycore_memstats {
    uint64_t committed_bytes;      /* OS memory committed by the allocator */
    uint64_t peak_committed_bytes; /* high-water mark of the above */
    uint64_t resident_bytes;       /* process RSS */
    uint64_t peak_resident_bytes;  /* high-water mark of the RSS */
};

YETTY_YRESULT_DECLARE(yetty_ycore_memstats, struct yetty_ycore_memstats);

/* Sample the current allocation statistics. Fails when the build does
 * not carry an instrumented allocator (YETTY_ENABLE_LIB_MIMALLOC off —
 * webasm, windows); callers should degrade gracefully, e.g. skip the
 * statusbar readout. */
struct yetty_ycore_memstats_result yetty_ycore_memstats_sample(void);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCORE_MEMSTATS_H */
