/*
 * QuickJS JIT groundwork — Stage 0 implementation.
 *
 * This file is #included at the bottom of quickjs.c (single-TU spike,
 * per the JIT design) so it can use the interpreter's private types
 * (JSRuntime, JSFunctionBytecode, opcode_info, atoms). It provides:
 *
 *   - the per-thread CPU-time sampler (Linux) and its signal handler;
 *   - the histogram dump (JS_ProfileDump);
 *   - the decode/classify-only bytecode walker and the whole-function
 *     JIT eligibility predicate (reused unchanged by the later
 *     emission stages);
 *   - the bytecode ABI fingerprint;
 *   - the value-representation pin (_Static_asserts).
 *
 * Public entry points are declared in quickjs-jit.h; internal
 * structures in quickjs-jit-internal.h.
 */
#ifndef QJS_JIT_INCLUDED_FROM_QUICKJS_C
#error "quickjs-jit.c is compiled via #include from quickjs.c, not standalone"
#endif

#include "quickjs-jit.h"

/* ------------------------------------------------------------------ */
/* Value-representation pin                                            */
/*                                                                     */
/* The future translator hardcodes tag checks and value moves. Pin the */
/* exact layout so an upstream rebase that changes any of it fails at  */
/* compile time instead of miscompiling.                               */
/* ------------------------------------------------------------------ */

_Static_assert(JS_TAG_FIRST == -9,          "JSValue tag layout changed");
_Static_assert(JS_TAG_OBJECT == -1,         "JSValue tag layout changed");
_Static_assert(JS_TAG_INT == 0,             "JSValue tag layout changed");
_Static_assert(JS_TAG_BOOL == 1,            "JSValue tag layout changed");
_Static_assert(JS_TAG_NULL == 2,            "JSValue tag layout changed");
_Static_assert(JS_TAG_UNDEFINED == 3,       "JSValue tag layout changed");
_Static_assert(JS_TAG_SHORT_BIG_INT == 7,   "JSValue tag layout changed");
_Static_assert(JS_TAG_FLOAT64 == 8,         "JSValue tag layout changed");

#if INTPTR_MAX == INT64_MAX && !defined(JS_NAN_BOXING)
/* 64-bit two-word representation: 8-byte payload union + 8-byte tag. */
_Static_assert(sizeof(JSValue) == 16,       "64-bit JSValue is not two words");
_Static_assert(offsetof(JSValue, tag) == 8, "JSValue tag offset changed");
/* JSFunctionBytecode fields the profiler/translator touch (the Stage 0
   profile block sits at the struct tail, so these hold with or without
   QJS_ENABLE_PROFILE). A rebase moving them must update these pins. */
_Static_assert(offsetof(JSFunctionBytecode, byte_code_buf) == 32,
               "JSFunctionBytecode.byte_code_buf offset changed");
_Static_assert(offsetof(JSFunctionBytecode, byte_code_len) == 40,
               "JSFunctionBytecode.byte_code_len offset changed");
_Static_assert(offsetof(JSFunctionBytecode, arg_count) == 64,
               "JSFunctionBytecode.arg_count offset changed");
_Static_assert(offsetof(JSFunctionBytecode, var_count) == 66,
               "JSFunctionBytecode.var_count offset changed");
_Static_assert(offsetof(JSFunctionBytecode, stack_size) == 70,
               "JSFunctionBytecode.stack_size offset changed");
_Static_assert(offsetof(JSFunctionBytecode, cpool) == 88,
               "JSFunctionBytecode.cpool offset changed");
#endif

/* ------------------------------------------------------------------ */
/* Bytecode ABI fingerprint                                            */
/* ------------------------------------------------------------------ */

uint64_t JS_JITBytecodeFingerprint(void)
{
    /* FNV-1a over everything the translator would hardcode. */
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
#define QJS_FP_MIX(byte_expr)                                  \
    do {                                                       \
        hash ^= (uint8_t)(byte_expr);                          \
        hash *= UINT64_C(0x100000001b3);                       \
    } while (0)
    int op;
    QJS_FP_MIX(OP_COUNT);
    QJS_FP_MIX(OP_TEMP_START);
    QJS_FP_MIX(OP_TEMP_END);
    QJS_FP_MIX(sizeof(JSValue));
    QJS_FP_MIX(JS_TAG_FIRST & 0xff);
    QJS_FP_MIX(JS_TAG_FLOAT64);
    /* JSFunctionBytecode fields the translator/profiler touch: a rebase
       that moves them must change the fingerprint. */
    QJS_FP_MIX(offsetof(JSFunctionBytecode, byte_code_buf));
    QJS_FP_MIX(offsetof(JSFunctionBytecode, byte_code_len));
    QJS_FP_MIX(offsetof(JSFunctionBytecode, cpool));
    QJS_FP_MIX(offsetof(JSFunctionBytecode, arg_count));
    QJS_FP_MIX(offsetof(JSFunctionBytecode, var_count));
    QJS_FP_MIX(offsetof(JSFunctionBytecode, stack_size));
    QJS_FP_MIX(sizeof(JSFunctionBytecode));
    for (op = 0; op < OP_COUNT + (OP_TEMP_END - OP_TEMP_START); op++) {
        QJS_FP_MIX(opcode_info[op].size);
        QJS_FP_MIX(opcode_info[op].n_pop);
        QJS_FP_MIX(opcode_info[op].n_push);
        QJS_FP_MIX(opcode_info[op].fmt);
    }
#undef QJS_FP_MIX
    return hash;
}

/* ------------------------------------------------------------------ */
/* Decode/classify-only bytecode walker + eligibility predicate        */
/* ------------------------------------------------------------------ */

/* Opcodes the baseline tier can never compile. Everything else is
   lowerable either directly or through a slow-path helper that reuses
   the interpreter's own implementation (the design's structural
   position); a rebase that adds opcodes shows up in the fingerprint,
   which is the drift guard for this default. */
static bool qjs_jit_opcode_unsupported(uint16_t op)
{
    switch (op) {
    /* suspension/resumption — needs OSR-style mid-function entry */
    case OP_initial_yield:
    case OP_yield:
    case OP_yield_star:
    case OP_async_yield_star:
    case OP_await:
        return true;
    /* direct eval — dynamic scope materialization */
    case OP_eval:
    case OP_apply_eval:
        return true;
    /* `with` — dynamic scope chain */
    case OP_with_get_var:
    case OP_with_put_var:
    case OP_with_delete_var:
    case OP_with_make_ref:
    case OP_with_get_ref:
    case OP_with_get_ref_undef:
        return true;
    default:
        return false;
    }
}

enum {
    QJS_JIT_ELIGIBLE = 0,
    QJS_JIT_INELIGIBLE_FUNC_KIND,     /* generator / async / async generator */
    QJS_JIT_INELIGIBLE_OPCODE,        /* contains an unsupported opcode */
    QJS_JIT_INELIGIBLE_MALFORMED,     /* decode error (should not happen) */
};

/* Walk the final (short-opcode) bytecode of `b`, validating sizes and
   classifying. Decode-only: no emission, no stack modeling yet. On
   ineligibility, *detail receives the offending opcode (or 0).
   The later emission stages reuse exactly this walk. */
static int qjs_jit_classify_function(const JSFunctionBytecode *b, int *detail,
                                     int *detail_offset)
{
    const uint8_t *bytecode = b->byte_code_buf;
    int position = 0;

    *detail = 0;
    *detail_offset = 0;
    if (b->func_kind != JS_FUNC_NORMAL)
        return QJS_JIT_INELIGIBLE_FUNC_KIND;

    while (position < b->byte_code_len) {
        uint8_t op = bytecode[position];
        int size;
        if (op >= OP_COUNT) {
            *detail = op;
            *detail_offset = position;
            return QJS_JIT_INELIGIBLE_MALFORMED;
        }
        /* Final bytecode uses short opcodes, whose table rows sit after
           the interleaved temporary-opcode rows — same adjustment the
           interpreter's dumpers use. */
        size = short_opcode_info(op).size;
        if (size <= 0 || position + size > b->byte_code_len) {
            *detail = op;
            *detail_offset = position;
            return QJS_JIT_INELIGIBLE_MALFORMED;
        }
        if (qjs_jit_opcode_unsupported(op)) {
            *detail = op;
            *detail_offset = position;
            return QJS_JIT_INELIGIBLE_OPCODE;
        }
        position += size;
    }
    return QJS_JIT_ELIGIBLE;
}

static const char *qjs_jit_verdict_name(int verdict)
{
    switch (verdict) {
    case QJS_JIT_ELIGIBLE:              return "eligible";
    case QJS_JIT_INELIGIBLE_FUNC_KIND:  return "async-or-generator";
    case QJS_JIT_INELIGIBLE_OPCODE:     return "unsupported-opcode";
    default:                            return "malformed";
    }
}

#ifdef QJS_ENABLE_PROFILE

/* ------------------------------------------------------------------ */
/* Per-opcode stage-3 sample categories                                */
/*                                                                     */
/* Classifies every FINAL-bytecode opcode against the stage-3 lowering */
/* table: DISPATCH only for ops the baseline tier inlines directly;    */
/* helper-bound ops get their helper category; anything not            */
/* consciously classified lands in UNKNOWN so uncertain time can never */
/* inflate the inlineable share. Filled into                           */
/* qjs_prof_opcode_category[] by JS_ProfileStart.                      */
/* ------------------------------------------------------------------ */

static uint8_t qjs_prof_stage3_opcode_category(unsigned op)
{
    switch (op) {
    /* --- stage-3 inlineable: constants ------------------------------ */
    case OP_push_i32:
    case OP_push_const:
    case OP_push_atom_value:
    case OP_undefined:
    case OP_null:
    case OP_push_this:
    case OP_push_false:
    case OP_push_true:
    case OP_push_bigint_i32:
    case OP_push_minus1 ... OP_push_7:
    case OP_push_i8:
    case OP_push_i16:
    case OP_push_const8:
    case OP_push_empty_string:
    /* --- stack manipulation ---------------------------------------- */
    case OP_drop:
    case OP_nip:
    case OP_nip1:
    case OP_dup ... OP_dup3:
    case OP_insert2 ... OP_insert4:
    case OP_perm3 ... OP_perm5:
    case OP_swap:
    case OP_swap2:
    case OP_rot3l ... OP_rot5l:
    /* --- arguments / locals / closure variable slots ---------------- */
    case OP_get_loc ... OP_set_var_ref:            /* long forms */
    case OP_set_loc_uninitialized:
    case OP_get_loc_check:
    case OP_put_loc_check:
    case OP_put_loc_check_init:
    case OP_get_var_ref_check:
    case OP_put_var_ref_check:
    case OP_put_var_ref_check_init:
    case OP_get_loc8:
    case OP_put_loc8:
    case OP_set_loc8:
    case OP_get_loc0_loc1:
    case OP_get_loc0 ... OP_set_var_ref3:          /* short forms */
    /* --- branches ---------------------------------------------------- */
    case OP_if_false:
    case OP_if_true:
    case OP_goto:
    case OP_gosub:
    case OP_ret:
    case OP_if_false8:
    case OP_if_true8:
    case OP_goto8:
    case OP_goto16:
    /* --- arithmetic/logic/compare fast paths (slow paths re-mark VM) - */
    case OP_neg:
    case OP_plus:
    case OP_dec:
    case OP_inc:
    case OP_post_dec:
    case OP_post_inc:
    case OP_dec_loc:
    case OP_inc_loc:
    case OP_add_loc:
    case OP_not:
    case OP_lnot:
    case OP_mul ... OP_or:            /* mul div mod add sub shl sar shr and xor or */
    case OP_lt ... OP_gte:
    case OP_eq ... OP_strict_neq:
    case OP_is_undefined_or_null:
    case OP_is_undefined:
    case OP_is_null:
    case OP_typeof_is_undefined:
    case OP_typeof_is_function:
    /* --- return ------------------------------------------------------ */
    case OP_return:
    case OP_return_undef:
    case OP_nop:
        return QJS_PROF_CAT_DISPATCH;

    /* --- property/element loads -------------------------------------- */
    case OP_get_ref_value:
    case OP_get_field:
    case OP_get_field2:
    case OP_get_private_field:
    case OP_get_array_el:
    case OP_get_array_el2:
    case OP_get_super_value:
    case OP_get_length:
        return QJS_PROF_CAT_PROP_LOAD;

    /* --- property/element writes ------------------------------------- */
    case OP_put_ref_value:
    case OP_put_field:
    case OP_put_private_field:
    case OP_put_array_el:
    case OP_put_super_value:
        return QJS_PROF_CAT_PROP_WRITE;

    /* --- call/constructor dispatch ----------------------------------- */
    case OP_call_constructor:
    case OP_call:
    case OP_tail_call:
    case OP_call_method:
    case OP_tail_call_method:
    case OP_apply:
    case OP_eval:
    case OP_apply_eval:
    case OP_call0 ... OP_call3:
        return QJS_PROF_CAT_CALL;

    /* --- known VM helpers -------------------------------------------- */
    case OP_fclosure:
    case OP_fclosure8:
    case OP_private_symbol:
    case OP_object:
    case OP_special_object:
    case OP_rest:
    case OP_array_from:
    case OP_check_ctor_return:
    case OP_check_ctor:
    case OP_init_ctor:
    case OP_check_brand:
    case OP_add_brand:
    case OP_return_async:
    case OP_throw:
    case OP_throw_error:
    case OP_regexp:
    case OP_get_super:
    case OP_import:
    case OP_get_var_undef:
    case OP_get_var:
    case OP_put_var:
    case OP_put_var_init:
    case OP_define_var:
    case OP_check_define_var:
    case OP_define_func:
    case OP_define_private_field:
    case OP_define_field:
    case OP_set_name:
    case OP_set_name_computed:
    case OP_set_proto:
    case OP_set_home_object:
    case OP_define_array_el:
    case OP_append:
    case OP_copy_data_properties:
    case OP_define_method:
    case OP_define_method_computed:
    case OP_define_class:
    case OP_define_class_computed:
    case OP_close_loc:
    case OP_catch:
    case OP_nip_catch:
    case OP_check_object:
    case OP_to_object:
    case OP_to_propkey:
    case OP_to_propkey2:
    case OP_with_get_var ... OP_with_get_ref_undef:
    case OP_make_loc_ref ... OP_make_var_ref:
    case OP_for_in_start ... OP_iterator_call:
    case OP_initial_yield ... OP_await:
    case OP_typeof:
    case OP_delete:
    case OP_delete_var:
    case OP_pow:
    case OP_instanceof:
    case OP_in:
    case OP_private_in:
    case OP_using_dispose_init ... OP_using_check:
        return QJS_PROF_CAT_VM;

    default:
        /* OP_invalid, out-of-range bytes, and any opcode a future
           rebase adds without a conscious classification. */
        return QJS_PROF_CAT_UNKNOWN;
    }
}

/* ------------------------------------------------------------------ */
/* Sampler                                                             */
/* ------------------------------------------------------------------ */

/* Thread-wide profile state: every runtime executing JavaScript on the
   sampled thread contributes through the hooks (top document and
   iframe child runtimes alike). The handler runs on the sampled
   JavaScript thread itself (SIGEV delivery targets that thread), so
   reads of cur/cat never race the mutator. One active profile per
   thread; the starting runtime owns it. */
__thread QJSProfileState *qjs_prof_thread_state;
uint8_t qjs_prof_opcode_category[256];

#if defined(__linux__)

#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <pthread.h>

#ifndef sigev_notify_thread_id
#define sigev_notify_thread_id _sigev_un._tid
#endif

static void qjs_prof_signal_handler(int signum)
{
    QJSProfileState *state = qjs_prof_thread_state;
    volatile QJSProfileFuncCounters *func_counters;
    int cat;

    (void)signum;
    if (!state || !state->running)
        return;
    func_counters = state->cur;
    cat = state->cat;
    if (cat < 0 || cat >= QJS_PROF_CAT_COUNT)
        cat = QJS_PROF_CAT_DISPATCH;
    state->samples_total++;
    if (func_counters) {
        func_counters->samples[cat]++;
        state->samples_by_cat[cat]++;
    } else {
        state->samples_no_frame++;
    }
}

static int qjs_prof_sampler_start(QJSProfileState *state, int sample_hz)
{
    struct sigaction action;
    struct sigevent event;
    struct itimerspec interval;
    clockid_t thread_clock;
    long period_ns;

    if (pthread_getcpuclockid(pthread_self(), &thread_clock) != 0)
        return -1;

    memset(&action, 0, sizeof(action));
    action.sa_handler = qjs_prof_signal_handler;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPROF, &action, NULL) != 0)
        return -1;

    memset(&event, 0, sizeof(event));
    event.sigev_notify = SIGEV_THREAD_ID;
    event.sigev_signo = SIGPROF;
    event.sigev_notify_thread_id = (pid_t)syscall(SYS_gettid);
    if (timer_create(thread_clock, &event, &state->timer_id) != 0)
        return -1;
    state->timer_created = 1;

    period_ns = 1000000000L / sample_hz;
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_nsec = period_ns;
    interval.it_value = interval.it_interval;
    if (timer_settime(state->timer_id, 0, &interval, NULL) != 0) {
        timer_delete(state->timer_id);
        state->timer_created = 0;
        return -1;
    }
    return 0;
}

static void qjs_prof_sampler_stop(QJSProfileState *state)
{
    if (state->timer_created) {
        timer_delete(state->timer_id);
        state->timer_created = 0;
    }
}

#endif /* __linux__ */

int JS_ProfileStart(JSRuntime *rt, int sample_hz)
{
#if defined(__linux__)
    QJSProfileState *state;
    int op;

    if (rt->prof || sample_hz <= 0 || sample_hz > 100000)
        return -1;
    if (qjs_prof_thread_state)
        return -1;  /* one active profile per thread */

    for (op = 0; op < 256; op++)
        qjs_prof_opcode_category[op] = qjs_prof_stage3_opcode_category(op);

    state = js_mallocz_rt(rt, sizeof(*state));
    if (!state)
        return -1;
    state->owner = rt;
    state->sample_hz = sample_hz;
    state->cur = NULL;
    state->cat = QJS_PROF_CAT_DISPATCH;
    state->force_tombstone_oom =
        getenv("QJS_PROFILE_FORCE_TOMBSTONE_OOM") != NULL;

    qjs_prof_thread_state = state;
    if (qjs_prof_sampler_start(state, sample_hz) != 0) {
        qjs_prof_thread_state = NULL;
        js_free_rt(rt, state);
        return -1;
    }
    state->running = 1;
    rt->prof = state;
    return 0;
#else
    (void)rt;
    (void)sample_hz;
    return -1;
#endif
}

int JS_ProfileStop(JSRuntime *rt)
{
#if defined(__linux__)
    QJSProfileState *state = rt->prof;
    if (!state)
        return -1;
    state->running = 0;
    qjs_prof_sampler_stop(state);
    return 0;
#else
    (void)rt;
    return -1;
#endif
}

int JS_ProfileActive(JSRuntime *rt)
{
    return rt->prof != NULL && rt->prof->running;
}

int JS_ProfileThreadActive(void)
{
    /* Existence, not armed-ness: a stopped-but-undumped profile still
       needs late runtimes (child frames destroyed after the owner
       stopped) to dump their rows. The state lives until the owning
       runtime is freed. */
    return qjs_prof_thread_state != NULL;
}

/* Called from JS_FreeRuntime: disarm and release. */
static void qjs_prof_free(JSRuntime *rt)
{
    QJSProfileState *state = rt->prof;
    QJSProfileTombstone *tombstone, *next;
    if (!state)
        return;
    state->running = 0;
#if defined(__linux__)
    qjs_prof_sampler_stop(state);
    if (qjs_prof_thread_state == state)
        qjs_prof_thread_state = NULL;
#endif
    for (tombstone = state->tombstones; tombstone; tombstone = next) {
        next = tombstone->next;
        js_free_rt(rt, tombstone);
    }
    rt->prof = NULL;
    js_free_rt(rt, state);
}

/* Fallback when a tombstone cannot be allocated: fold the dying
   function's counters into the reclaimed totals so whole-run
   accounting stays consistent even without the per-function row. */
static void qjs_prof_reclaim_fold(QJSProfileState *state,
                                  const QJSProfileFuncCounters *func_counters)
{
    int cat;
    state->reclaimed_functions++;
    state->reclaimed_calls += func_counters->call_count;
    state->reclaimed_backedges += func_counters->backedge_count;
    for (cat = 0; cat < QJS_PROF_CAT_COUNT; cat++)
        state->reclaimed_samples[cat] += func_counters->samples[cat];
}

/* Called from free_function_bytecode while a thread profile exists: a
   function dying before its row was dumped becomes a tombstone that
   preserves identity, exact counters, category samples, and the
   eligibility verdict — the gate metric is per-function, so aggregate
   totals are not enough. The bytecode is still intact at this point,
   so the classifier can run. */
static void qjs_prof_function_freed(QJSProfileState *state, JSRuntime *rt,
                                    JSFunctionBytecode *b)
{
    QJSProfileTombstone *tombstone;
    QJSProfileFuncCounters *fc = &b->prof;
    char atom_buf[64];
    const char *text;
    uint32_t activity = fc->call_count + fc->backedge_count;
    int cat;

    for (cat = 0; cat < QJS_PROF_CAT_COUNT; cat++)
        activity += fc->samples[cat];
    if (activity == 0)
        return; /* never executed (or already drained by a dump) */

    if (state->force_tombstone_oom)
        tombstone = NULL; /* fault injection: exercise the loss path */
    else
        tombstone = js_mallocz_rt(state->owner, sizeof(*tombstone));
    if (!tombstone) {
        /* Sticky: per-function data is now incomplete; the analyzer
           must reject the dataset for go/no-go purposes. The aggregate
           fold below keeps totals diagnostic, nothing more. */
        state->incomplete = 1;
        qjs_prof_reclaim_fold(state, fc);
        return;
    }
    tombstone->runtime = rt;
    text = JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf), b->func_name);
    snprintf(tombstone->name, sizeof(tombstone->name), "%s",
             (text && text[0]) ? text : "<anonymous>");
    text = JS_AtomGetStrRT(rt, atom_buf, sizeof(atom_buf), b->filename);
    snprintf(tombstone->source, sizeof(tombstone->source), "%s",
             (text && text[0]) ? text : "<unknown>");
    tombstone->line = b->line_num;
    tombstone->bc_size = b->byte_code_len;
    tombstone->func_kind = b->func_kind;
    tombstone->counters = *fc;
    tombstone->verdict = qjs_jit_classify_function(b, &tombstone->detail,
                                                   &tombstone->detail_offset);
    tombstone->next = state->tombstones;
    state->tombstones = tombstone;
}

/* ------------------------------------------------------------------ */
/* Histogram dump                                                      */
/* ------------------------------------------------------------------ */

static void qjs_prof_dump_atom(FILE *out, JSContext *ctx, JSAtom atom,
                               const char *fallback)
{
    const char *cstring;
    const char *cursor;
    if (atom == JS_ATOM_NULL) {
        fputs(fallback, out);
        return;
    }
    cstring = JS_AtomToCString(ctx, atom);
    if (!cstring) {
        fputs(fallback, out);
        return;
    }
    /* TSV field: neutralize separators. */
    for (cursor = cstring; *cursor; cursor++)
        fputc((*cursor == '\t' || *cursor == '\n') ? ' ' : *cursor, out);
    JS_FreeCString(ctx, cstring);
}

int JS_ProfileDump(JSContext *ctx, const char *path)
{
    JSRuntime *rt = ctx->rt;
    QJSProfileState *state = qjs_prof_thread_state;
    struct list_head *el;
    FILE *out;
    int cat;

    if (!state || !path)
        return -1;

    out = fopen(path, "w");
    if (!out)
        return -1;

    fprintf(out, "# qjs-profile v3\n");
    fprintf(out, "# fingerprint\t%016" PRIx64 "\n", JS_JITBytecodeFingerprint());
    fprintf(out, "# runtime\t%p\towner=%d\n", (void *)rt, rt == state->owner);
    fprintf(out, "# layout\tjsvalue=%zu\tbc_buf=%zu\tbc_len=%zu\tcpool=%zu"
                 "\targ_count=%zu\tvar_count=%zu\tstack_size=%zu\n",
            sizeof(JSValue),
            offsetof(JSFunctionBytecode, byte_code_buf),
            offsetof(JSFunctionBytecode, byte_code_len),
            offsetof(JSFunctionBytecode, cpool),
            offsetof(JSFunctionBytecode, arg_count),
            offsetof(JSFunctionBytecode, var_count),
            offsetof(JSFunctionBytecode, stack_size));
    fprintf(out, "# sample_hz\t%d\n", state->sample_hz);
    fprintf(out, "# samples_total\t%" PRIu64 "\n", state->samples_total);
    fprintf(out, "# samples_no_frame\t%" PRIu64 "\n", state->samples_no_frame);
    fprintf(out, "# samples_by_cat\tdispatch=%" PRIu64 "\tprop_load=%" PRIu64
                 "\tprop_write=%" PRIu64 "\tcall=%" PRIu64 "\tstring=%" PRIu64
                 "\tvm=%" PRIu64 "\tnative=%" PRIu64 "\tunknown=%" PRIu64 "\n",
            state->samples_by_cat[0], state->samples_by_cat[1],
            state->samples_by_cat[2], state->samples_by_cat[3],
            state->samples_by_cat[4], state->samples_by_cat[5],
            state->samples_by_cat[6], state->samples_by_cat[7]);
    fprintf(out, "# incomplete\t%d\n", state->incomplete);
    fprintf(out, "# reclaimed\tfunctions=%" PRIu64 "\tcalls=%" PRIu64
                 "\tbackedges=%" PRIu64 "\n",
            state->reclaimed_functions, state->reclaimed_calls,
            state->reclaimed_backedges);
    fprintf(out, "name\tsource\tbc_size\tfunc_kind\tcalls\tbackedges"
                 "\ts_dispatch\ts_prop_load\ts_prop_write\ts_call\ts_string"
                 "\ts_vm\ts_native\ts_unknown\teligible\treason\truntime\n");

    list_for_each(el, &rt->gc_obj_list) {
        JSGCObjectHeader *gc_header = list_entry(el, JSGCObjectHeader, link);
        JSFunctionBytecode *b;
        int verdict, detail, detail_offset;

        if (gc_header->gc_obj_type != JS_GC_OBJ_TYPE_FUNCTION_BYTECODE)
            continue;
        b = (JSFunctionBytecode *)gc_header;
        if (b->prof.call_count == 0 && b->prof.backedge_count == 0) {
            uint32_t sample_sum = 0;
            for (cat = 0; cat < QJS_PROF_CAT_COUNT; cat++)
                sample_sum += b->prof.samples[cat];
            if (sample_sum == 0)
                continue; /* never executed while profiled — skip the row */
        }

        verdict = qjs_jit_classify_function(b, &detail, &detail_offset);

        qjs_prof_dump_atom(out, ctx, b->func_name, "<anonymous>");
        fputc('\t', out);
        qjs_prof_dump_atom(out, ctx, b->filename, "<unknown>");
        fprintf(out, ":%d", b->line_num);
        fprintf(out, "\t%d\t%d\t%u\t%u",
                b->byte_code_len, (int)b->func_kind,
                b->prof.call_count, b->prof.backedge_count);
        for (cat = 0; cat < QJS_PROF_CAT_COUNT; cat++)
            fprintf(out, "\t%u", b->prof.samples[cat]);
        fprintf(out, "\t%d\t%s", verdict == QJS_JIT_ELIGIBLE,
                qjs_jit_verdict_name(verdict));
        if (verdict == QJS_JIT_INELIGIBLE_OPCODE ||
            verdict == QJS_JIT_INELIGIBLE_MALFORMED)
            fprintf(out, ":%d@%d", detail, detail_offset);
        fprintf(out, "\t%p", (void *)rt);
        /* Drain: a dumped row is consumed, so later tombstones/dumps
           never double-count this function. */
        memset(&b->prof, 0, sizeof(b->prof));
        /* Gated triage aid: hex bytecode for small functions. */
        if (getenv("QJS_PROFILE_DUMP_BC") && b->byte_code_len <= 128) {
            int bc_index;
            fputs("\tbc=", out);
            for (bc_index = 0; bc_index < b->byte_code_len; bc_index++)
                fprintf(out, "%02x", b->byte_code_buf[bc_index]);
        }
        fputc('\n', out);
    }

    /* Flush tombstones (functions freed before a dump reached them) —
       exactly once, into whichever dump runs first. */
    while (state->tombstones) {
        QJSProfileTombstone *tombstone = state->tombstones;
        const char *cursor;
        state->tombstones = tombstone->next;
        for (cursor = tombstone->name; *cursor; cursor++)
            fputc((*cursor == '\t' || *cursor == '\n') ? ' ' : *cursor, out);
        fputc('\t', out);
        for (cursor = tombstone->source; *cursor; cursor++)
            fputc((*cursor == '\t' || *cursor == '\n') ? ' ' : *cursor, out);
        fprintf(out, ":%d", tombstone->line);
        fprintf(out, "\t%d\t%d\t%u\t%u",
                tombstone->bc_size, tombstone->func_kind,
                tombstone->counters.call_count,
                tombstone->counters.backedge_count);
        for (cat = 0; cat < QJS_PROF_CAT_COUNT; cat++)
            fprintf(out, "\t%u", tombstone->counters.samples[cat]);
        fprintf(out, "\t%d\t%s", tombstone->verdict == QJS_JIT_ELIGIBLE,
                qjs_jit_verdict_name(tombstone->verdict));
        if (tombstone->verdict == QJS_JIT_INELIGIBLE_OPCODE ||
            tombstone->verdict == QJS_JIT_INELIGIBLE_MALFORMED)
            fprintf(out, ":%d@%d", tombstone->detail,
                    tombstone->detail_offset);
        fprintf(out, "\t%p\n", tombstone->runtime);
        js_free_rt(state->owner, tombstone);
    }

    if (fclose(out) != 0)
        return -1;
    return 0;
}

#else /* !QJS_ENABLE_PROFILE — API stubs so hosts build unconditionally */

int JS_ProfileStart(JSRuntime *rt, int sample_hz)
{
    (void)rt;
    (void)sample_hz;
    return -1;
}

int JS_ProfileStop(JSRuntime *rt)
{
    (void)rt;
    return -1;
}

int JS_ProfileActive(JSRuntime *rt)
{
    (void)rt;
    return 0;
}

int JS_ProfileThreadActive(void)
{
    return 0;
}

int JS_ProfileDump(JSContext *ctx, const char *path)
{
    (void)ctx;
    (void)path;
    return -1;
}

#endif /* QJS_ENABLE_PROFILE */

/* Baseline JIT compiler (SLJIT backend). Uses this TU's private types
   and static helpers. */
#ifdef QJS_ENABLE_JIT
#include "quickjs-jit-compile.c"
#else
/* JIT compiled out: stub the public control API so hosts link
   unconditionally. */
int JS_JITSetMode(JSRuntime *rt, int mode)
{
    (void)rt;
    (void)mode;
    return -1;
}

int JS_JITAvailable(void)
{
    return 0;
}

int JS_JITGetStats(JSRuntime *rt, uint64_t *compiled, uint64_t *unsupported,
                   uint64_t *failed, uint64_t *jit_calls)
{
    (void)rt;
    (void)compiled;
    (void)unsupported;
    (void)failed;
    (void)jit_calls;
    return -1;
}
#endif
