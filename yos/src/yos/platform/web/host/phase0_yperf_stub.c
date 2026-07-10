/* yperf link stub for the Phase 0 host.
 *
 * The vendored wasm3 fork's op_Entry (src/wasm3/source/m3_exec.h) references
 * three yperf symbols unconditionally — g_yperf_enabled, yperf_enter,
 * yperf_exit — expecting them to come from libyos at link time. yperf is
 * host-side per-call profiling instrumentation; it is disabled by default and
 * is not part of the guest ABI, so the Phase 0 host provides empty no-op
 * definitions rather than dragging in the real yperf.c (which pulls ytrace,
 * threading, and a symbol table).
 *
 * g_yperf_enabled stays false, so wasm3's inlined relaxed-atomic gate never
 * calls into yperf_enter/yperf_exit — the branch is never taken.
 *
 * A later phase that wants real profiling in the browser host can drop this
 * stub and compile src/yos/yperf/yperf.c instead.
 */

#include <stdatomic.h>
#include <stdbool.h>

/* Read inline by wasm3's op_Entry via a relaxed atomic load. */
_Atomic bool g_yperf_enabled = false;

void yperf_enter(const void *function, const char *name)
{
	(void)function;
	(void)name;
}

void yperf_exit(void)
{
}
