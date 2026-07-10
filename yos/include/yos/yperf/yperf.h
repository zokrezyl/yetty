/*
 * yperf — perf-like per-call profiling for wasm3 guest functions.
 *
 * Hooks fire from wasm3's op_Call / op_CallIndirect / op_Compile in
 * the vendored interpreter (src/wasm3/source/m3_exec.h). Each enter
 * pushes (fn_pc, ts_ns) onto a per-host-thread call stack; each exit
 * pops the top and emits one fixed-size record into a per-thread
 * ring buffer. On shutdown (atexit), every thread's ring is dumped
 * to a single binary file, followed by a function-name table built
 * by walking the runtime's modules to resolve the recorded pc_t
 * pointers back to readable names.
 *
 * Storage shape (per-record, 24 bytes):
 *    uint64_t enter_ts_ns
 *    uint64_t exit_ts_ns
 *    uintptr_t fn_pc          (M3Function->compiled pointer = id)
 *
 * The pc value is opaque at record time. The dumper later walks all
 * loaded modules and emits a `(pc, name)` table appended after the
 * records; the reader joins records on pc.
 *
 * Runtime control via YPERF env (same shape as YTRACE_DEFAULT_ON):
 *    YPERF=yes      enable from process start
 *    YPERF_FILE=/path   override default /tmp/yperf-<pid>.bin
 *
 * The hot path (yperf_enter/exit) is one branch when disabled,
 * one timestamp read + one bounded store when enabled. No locks,
 * no allocations, no syscalls beyond clock_gettime.
 */
#ifndef YOS_YPERF_H
#define YOS_YPERF_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile-time master switch — yperf is opt-in but always linked. */
#ifndef YPERF_C_ENABLED
#define YPERF_C_ENABLED 1
#endif

#if YPERF_C_ENABLED

/* Opaque function handle. The wasm3 hook passes the M3Function*
 * (cast to const void*); the dumper later resolves it to a name
 * by walking the runtime's modules via the registered walker. */
typedef const void *yperf_fn_t;

/* Initialise once; safe to call repeatedly. Reads YPERF / YPERF_FILE
 * env vars, installs the atexit hook. */
void yperf_init(void);

/* Toggle enable state at runtime. Mirrors ytrace_set_all_enabled —
 * env.c uses it to handle setenv("YPERF", "yes") from the guest. */
void yperf_set_enabled(bool on);
bool yperf_is_enabled(void);

/* Externally-readable enable flag — used by wasm3's op_Entry to
 * short-circuit the hot path without an atomic load through a
 * function call. Yes it's a global; this is the perf hot path. */
extern _Atomic _Bool g_yperf_enabled;

/* Hot-path hooks fired from wasm3's op_Entry. Caller already gates
 * on g_yperf_enabled so these don't re-check; both are inlined as
 * empty when YPERF_C_ENABLED is 0. yperf_enter also lazily snapshots
 * the function's name on first sight so that the dumper still has
 * symbols after the originating proc has been reaped. */
void yperf_enter(yperf_fn_t fn, const char *name);
void yperf_exit (void);

/* Symbol-walker registration. yos installs one at startup; on dump
 * yperf calls it with an emit callback so we can write the
 * (M3Function* → name) table to the file. Decouples yperf from
 * wasm3 internals — wasm3 doesn't have to include yperf headers,
 * and yperf doesn't have to know about M3Runtime layout. */
typedef void (*yperf_emit_fn)(yperf_fn_t fn, const char *name);
typedef void (*yperf_walker_fn)(yperf_emit_fn emit);
void yperf_set_walker(yperf_walker_fn walker);

/* Flush every thread's ring buffer to the configured output file and
 * append the symbol table. Called from atexit; also exposed so
 * tests / explicit shutdown paths can force a flush. */
void yperf_dump(void);

/* Same as yperf_dump but also clears every per-thread ring + call
 * stack so a subsequent enable starts capturing a fresh trace. Used
 * by the `yperf` guest wrapper to scope a profile to one child:
 *
 *     setenv(YPERF_FILE, "/tmp/x.bin");
 *     setenv(YPERF, "yes");
 *     fork+exec+wait <child>
 *     setenv(YPERF, "stop");    // → yperf_dump_and_reset
 *
 * After this call yperf is disabled; re-enable via setenv("YPERF","yes")
 * for the next capture. The wasm function-id space is preserved (we
 * keep the malloc'd struct yperf_thread blocks on the list), so the
 * second capture's symbol resolution doesn't lose anything either. */
void yperf_dump_and_reset(void);

#else /* !YPERF_C_ENABLED */

static inline void yperf_init(void) {}
static inline void yperf_set_enabled(bool on) { (void)on; }
static inline bool yperf_is_enabled(void) { return false; }
static inline void yperf_enter(const void *fn, const char *n) {
    (void)fn; (void)n;
}
static inline void yperf_exit(void) {}
typedef void (*yperf_emit_fn)(const void *, const char *);
typedef void (*yperf_walker_fn)(yperf_emit_fn);
static inline void yperf_set_walker(yperf_walker_fn w) { (void)w; }
static inline void yperf_dump(void) {}
static inline void yperf_dump_and_reset(void) {}

#endif /* YPERF_C_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* YOS_YPERF_H */
