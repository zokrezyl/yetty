#ifndef YETTY_YCORE_MEMSTATS_H
#define YETTY_YCORE_MEMSTATS_H

#include <yetty/ycore/result.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Snapshot of process allocation statistics, sampled from the linked
 * allocator (mimalloc) plus the OS process accounting. Heap figures
 * cover memory routed through the allocator; resident figures are the
 * OS view of the whole process (GPU driver mappings included). */
struct yetty_ycore_memstats {
    uint64_t allocated_bytes;      /* live heap bytes handed out */
    uint64_t peak_allocated_bytes; /* high-water mark of the above */
    uint64_t committed_bytes;      /* OS memory committed by the allocator */
    uint64_t resident_bytes;       /* process RSS */
    uint64_t peak_resident_bytes;  /* high-water mark of the RSS */
    uint64_t allocation_count;     /* total allocations since start */
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
