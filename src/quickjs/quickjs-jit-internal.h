/*
 * QuickJS JIT groundwork — private profiling structures and inline hooks.
 *
 * Stage 0 of the baseline-JIT plan: interpreter instrumentation only.
 * No code generation lives here yet. Everything is compiled into the
 * quickjs.c translation unit and is guarded by QJS_ENABLE_PROFILE; when
 * that macro is not defined none of these types or hooks exist.
 *
 * Threading model: the profile is THREAD-WIDE, held in the thread-local
 * `qjs_prof_thread_state`. Every runtime executing JavaScript on the
 * sampled thread contributes (top document AND iframe child runtimes) —
 * the page-level metric requires all of them. The runtime that called
 * JS_ProfileStart owns start/stop/teardown; any runtime on the thread
 * may dump its own functions.
 *
 * Concurrency model: all mutator stores (cur/cat, call/backedge counts)
 * and all sampler increments happen on the one JavaScript thread — the
 * sampler is a signal delivered to that same thread — so plain stores
 * suffice. `cur` and `cat` are volatile so the stores are real memory
 * writes visible at signal delivery; the sample counters are touched
 * only by the handler until the sampler is stopped.
 */
#ifndef QUICKJS_JIT_INTERNAL_H
#define QUICKJS_JIT_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#if defined(__linux__)
#include <time.h> /* timer_t */
#endif

/* ================================================================== */
/* Baseline JIT (QJS_ENABLE_JIT) — code-generation metadata            */
/*                                                                     */
/* Compiled into the quickjs.c TU alongside the profiler. When         */
/* QJS_ENABLE_JIT is not defined none of this exists and the engine    */
/* is interpreter-only. See tmp/qjs-sljit-jit.md.                      */
/* ================================================================== */
#ifdef QJS_ENABLE_JIT

/* Runtime JIT policy. OFF: interpreter only. BASELINE: compile a hot
   function once it crosses the call/backedge threshold. EAGER: compile
   every compilable function on first call (tests/benchmarks). */
enum { QJS_JIT_MODE_OFF = 0, QJS_JIT_MODE_BASELINE, QJS_JIT_MODE_EAGER };

/* Per-function compilation state machine. Terminal UNSUPPORTED/FAILED
   never retry. */
enum {
    QJS_JITC_COLD = 0,
    QJS_JITC_QUEUED,
    QJS_JITC_COMPILED,
    QJS_JITC_UNSUPPORTED,
    QJS_JITC_FAILED
};

/* Published native code for one function. */
typedef struct QJSJitCode {
    void *entry;      /* QJSJitEntry; call target */
    void *sljit_code; /* sljit_generate_code() result, for sljit_free_code */
    size_t code_size; /* charged against the runtime/process limits */
} QJSJitCode;

/* The generated-code ABI. A C wrapper in JS_CallInternal builds the
   normal JSStackFrame and operand-stack storage exactly as the
   interpreter does, fills this struct, and calls the entry. All owned
   JSValues live in the frame's memory slots (arg_buf/var_buf/stack_buf)
   — the generated code holds no owned reference solely in a register
   across any call that can allocate or throw. */
typedef struct QJSJitFrame {
    struct JSContext *ctx;
    struct JSFunctionBytecode *b;
    struct JSValue *arg_buf;
    struct JSValue *var_buf;
    struct JSValue *stack_buf;
    struct JSVarRef **var_refs;
    struct JSStackFrame *sf;
} QJSJitFrame;

/* 0: normal return, *out_ret holds the result (ownership transferred),
   all operands consumed. -1: exception left in the context state, the
   generated code has released its live operands. */
typedef int (*QJSJitEntry)(QJSJitFrame *frame, struct JSValue *out_ret);

/* Per-runtime JIT accounting/config, embedded in JSRuntime. */
typedef struct QJSJitRuntime {
    int mode;      /* QJS_JIT_MODE_* */
    int compiling; /* reentrancy guard: no nested compile */
    uint32_t call_threshold;
    uint32_t backedge_threshold;
    size_t code_bytes; /* live native-code bytes in this runtime */
    size_t code_limit; /* per-runtime cap */
    uint64_t compiled;
    uint64_t unsupported;
    uint64_t failed;
    uint64_t jit_calls; /* invocations that ran native code */
} QJSJitRuntime;

#endif /* QJS_ENABLE_JIT */

/* Sample categories, per the Stage 0 design split. The category is set
   from `qjs_prof_opcode_category[]` at every bytecode dispatch (so each
   op self-classifies against the stage-3 lowering table) and refined by
   the slow-path wrappers (VM/STRING) and the C-callee marks (NATIVE).
   UNKNOWN is the default for any opcode not consciously classified —
   uncertain time must never inflate the inlineable DISPATCH bucket. */
enum {
    QJS_PROF_CAT_DISPATCH = 0, /* stage-3 inlineable primitives + dispatch */
    QJS_PROF_CAT_PROP_LOAD,    /* property/element load ops */
    QJS_PROF_CAT_PROP_WRITE,   /* property/element write ops */
    QJS_PROF_CAT_CALL,         /* call/constructor dispatch machinery */
    QJS_PROF_CAT_STRING,       /* string helpers (concat) */
    QJS_PROF_CAT_VM,           /* other known VM slow helpers */
    QJS_PROF_CAT_NATIVE,       /* C-function callees (builtins + host bindings) */
    QJS_PROF_CAT_UNKNOWN,      /* opcode without a conscious classification */
    QJS_PROF_CAT_COUNT
};

/* Per-JSFunctionBytecode counters. Embedded directly in the bytecode
   object (design: counters inline, no pointer chase). Zeroed after a
   dump writes the row (drain semantics), so reclaim accounting never
   double-counts dumped functions. */
typedef struct QJSProfileFuncCounters {
    uint32_t call_count;                  /* exact, saturating */
    uint32_t backedge_count;              /* exact, saturating */
    uint32_t samples[QJS_PROF_CAT_COUNT]; /* incremented by the signal handler */
} QJSProfileFuncCounters;

/* Full per-function record snapshotted when a profiled
   JSFunctionBytecode is freed before it was dumped. The gate metric is
   per-function, so aggregate totals are not enough — the tombstone
   preserves identity, exact counters, samples, and the eligibility
   verdict. Flushed (exactly once) by the next JS_ProfileDump. */
typedef struct QJSProfileTombstone {
    struct QJSProfileTombstone *next;
    void *runtime; /* owning JSRuntime at free time */
    char name[48];
    char source[64];
    int line;
    int bc_size;
    int func_kind;
    QJSProfileFuncCounters counters;
    int verdict;
    int detail;
    int detail_offset;
} QJSProfileTombstone;

typedef struct QJSProfileState {
    /* Written by the mutator, read by the same-thread signal handler. */
    volatile QJSProfileFuncCounters *cur; /* top JS frame's counters, NULL outside JS */
    volatile int cat;                     /* current QJS_PROF_CAT_* */
    volatile int running;                 /* sampler armed */

    QJSProfileTombstone *tombstones; /* freed-before-dump functions */
    int incomplete;                  /* sticky: a tombstone was lost (OOM) —
                                                per-function data no longer complete */
    int force_tombstone_oom;         /* fault injection (env), tests only */

    struct JSRuntime *owner; /* runtime that called JS_ProfileStart */
    int sample_hz;
#if defined(__linux__)
    timer_t timer_id;
    int timer_created;
#endif

    /* Handler-side totals (whole-thread accounting). */
    volatile uint64_t samples_total;
    volatile uint64_t samples_by_cat[QJS_PROF_CAT_COUNT];
    volatile uint64_t samples_no_frame; /* cur == NULL: parser, GC, host glue */

    /* Fallback accounting for functions freed with undumped counts
       when a tombstone could not be allocated: totals stay consistent
       even though the per-function rows are lost. */
    uint64_t reclaimed_functions;
    uint64_t reclaimed_calls;
    uint64_t reclaimed_backedges;
    uint64_t reclaimed_samples[QJS_PROF_CAT_COUNT];
} QJSProfileState;

/* Thread-wide profile state + the 256-entry final-opcode category
   table. Defined in quickjs-jit.c; filled by JS_ProfileStart. */
extern __thread QJSProfileState *qjs_prof_thread_state;
extern uint8_t qjs_prof_opcode_category[256];

static inline void qjs_prof_sat_inc_u32(uint32_t *counter)
{
    if (*counter != UINT32_MAX) {
        (*counter)++;
    }
}

/* Function entry: count the call, make the callee current. The previous
   (function, category) pair is saved in interpreter locals and restored
   at the single interpreter exit, giving self-time attribution. */
static inline void qjs_prof_enter(QJSProfileState *ps, QJSProfileFuncCounters *func_counters,
                                  QJSProfileFuncCounters **prev_func, int *prev_cat)
{
    *prev_func = (QJSProfileFuncCounters *)ps->cur;
    *prev_cat = ps->cat;
    qjs_prof_sat_inc_u32(&func_counters->call_count);
    ps->cur = func_counters;
    ps->cat = QJS_PROF_CAT_DISPATCH;
}

static inline void qjs_prof_exit(QJSProfileState *ps, QJSProfileFuncCounters *prev_func,
                                 int prev_cat)
{
    ps->cur = prev_func;
    ps->cat = prev_cat;
}

static inline int qjs_prof_cat_push(QJSProfileState *ps, int cat)
{
    int prev = ps->cat;
    ps->cat = cat;
    return prev;
}

static inline void qjs_prof_cat_pop(QJSProfileState *ps, int prev)
{
    ps->cat = prev;
}

static inline void qjs_prof_backedge(QJSProfileFuncCounters *func_counters)
{
    qjs_prof_sat_inc_u32(&func_counters->backedge_count);
}

/* Dispatch tap: classifies every executed opcode against the stage-3
   lowering table. Wrapped around the `opcode = *pc++` fetch in the
   interpreter's SWITCH macros; evaluates the fetch exactly once and
   yields the opcode. Cost discipline: the thread-local state pointer is
   cached in the interpreter local `prof_state` (loaded once per frame
   entry — a profile started mid-frame applies from the next frame on),
   and the compare-before-store lets long runs of same-category ops
   (hot arithmetic loops are all DISPATCH) skip the volatile store.
   The interpreter local `prof_cat_local` shadows prof_state->cat so the
   per-op fast path is a register compare, no volatile load. Helper
   marks push/pop around their regions and always restore, so the
   shadow stays coherent; the exception label re-syncs both. */
#define QJS_PROF_OP_TAP(opcode_fetch)                                                              \
    ((opcode_fetch),                                                                               \
     (unlikely(prof_state) ? (void)(prof_cat_local == qjs_prof_opcode_category[(uint8_t)opcode]    \
                                        ? 0                                                        \
                                        : (prof_state->cat = prof_cat_local =                      \
                                               qjs_prof_opcode_category[(uint8_t)opcode]))         \
                           : (void)0),                                                             \
     opcode)

/* Scoped category marking for non-interpreter sites (C-callee paths). */
#define QJS_PROF_CAT_PUSH_DECL(rt, category, savevar)                                              \
    int savevar = 0;                                                                               \
    (void)(rt);                                                                                    \
    if (unlikely(qjs_prof_thread_state))                                                           \
    savevar = qjs_prof_cat_push(qjs_prof_thread_state, (category))

#define QJS_PROF_CAT_POP(rt, savevar)                                                              \
    do {                                                                                           \
        (void)(rt);                                                                                \
        if (unlikely(qjs_prof_thread_state))                                                       \
            qjs_prof_cat_pop(qjs_prof_thread_state, savevar);                                      \
    } while (0)

#endif /* QUICKJS_JIT_INTERNAL_H */
