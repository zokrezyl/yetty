/*
 * QuickJS JIT groundwork — opt-in public API for the Stage 0 profiler.
 *
 * Kept out of quickjs.h on purpose: hosts that do not opt in never see
 * these declarations. The functions exist in every build; when the
 * engine is compiled without QJS_ENABLE_PROFILE (or on platforms
 * without the sampler backend) JS_ProfileStart returns -1 and the rest
 * are no-ops, so callers need no conditional compilation.
 */
#ifndef QUICKJS_JIT_H
#define QUICKJS_JIT_H

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the per-thread CPU-time sampler at sample_hz on the CALLING
   thread, which must be the thread that runs this runtime's JavaScript.
   One active profile per thread. Returns 0 on success, -1 if profiling
   is unavailable (compiled out, unsupported platform, already active,
   or timer creation failed). */
JS_EXTERN int JS_ProfileStart(JSRuntime *rt, int sample_hz);

/* Disarm the sampler. Counters are kept for JS_ProfileDump. Returns 0,
   or -1 if no profile is active. */
JS_EXTERN int JS_ProfileStop(JSRuntime *rt);

/* Whether a profile is active on this runtime (1/0). */
JS_EXTERN int JS_ProfileActive(JSRuntime *rt);

/* Whether a profile EXISTS on the calling thread (armed or stopped but
   not yet torn down), regardless of which runtime owns it. Every
   runtime executing JavaScript on the sampled thread contributes to
   that profile (iframe child runtimes included) and should dump its
   rows at teardown while this returns 1. */
JS_EXTERN int JS_ProfileThreadActive(void);

/* Write the per-function histogram as TSV to `path`. Header comment
   lines (#) carry the bytecode fingerprint, sample rate and totals;
   then one row per live JSFunctionBytecode: name, source location,
   bytecode size, function kind, exact call/backedge counts, samples
   per category, JIT eligibility verdict and reason. May be called
   while running or after JS_ProfileStop, on the JS thread only.
   Returns 0 on success, -1 on error. */
JS_EXTERN int JS_ProfileDump(JSContext *ctx, const char *path);

/* FNV-1a fingerprint of the compiled-in bytecode ABI: opcode table
   (sizes, stack effects, formats), opcode count, JSValue size and the
   pinned tag constants. Changes whenever a rebase alters anything the
   future translator would hardcode. Available in every build. */
JS_EXTERN uint64_t JS_JITBytecodeFingerprint(void);

/* Baseline JIT runtime policy. 0 = off (interpreter only), 1 = baseline
   (compile hot functions past the call/backedge thresholds), 2 = eager
   (compile every compilable function on first call — tests/benchmarks).
   Returns 0 on success, -1 if the mode is invalid or the JIT is
   compiled out / unavailable on this platform. */
#define JS_JIT_MODE_OFF 0
#define JS_JIT_MODE_BASELINE 1
#define JS_JIT_MODE_EAGER 2
JS_EXTERN int JS_JITSetMode(JSRuntime *rt, int mode);

/* Whether the JIT is compiled in and available on this platform (1/0). */
JS_EXTERN int JS_JITAvailable(void);

/* Read JIT counters (each pointer may be NULL). Returns 0, or -1 when
   the JIT is compiled out. */
JS_EXTERN int JS_JITGetStats(JSRuntime *rt, uint64_t *compiled, uint64_t *unsupported,
                             uint64_t *failed, uint64_t *jit_calls);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* QUICKJS_JIT_H */
