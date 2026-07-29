/*
 * QuickJS baseline JIT — bytecode -> SLJIT translator.
 *
 * Compiled into the quickjs.c TU (single-TU spike per the design) via
 * #include from quickjs-jit.c, so it uses the interpreter's private
 * types and static helpers (js_dup, set_value, JSValue layout,
 * opcode_info, JS_CallInternal). Only present under QJS_ENABLE_JIT.
 *
 * Model: the generated code manages the operand stack in the frame's
 * stack_buf by static depth (QuickJS bytecode has a deterministic
 * operand-stack depth at every pc, recovered here by a worklist
 * pre-pass). Every owned JSValue lives in a memory slot; the only
 * live reference to a heap value never sits solely in a register
 * across a call that can allocate or throw. Simple opcodes lower to a
 * narrow helper that reuses the interpreter's own operation; nothing
 * reimplements semantics.
 *
 * Exception model matches the interpreter's `exception:` label: on a
 * throwing op's failure, the live operands (stack_buf[0..depth)) are
 * freed and the entry returns -1. Helpers either leave their inputs
 * live on failure (freed by the unwind) or null consumed slots to
 * JS_UNDEFINED (freed harmlessly) — never a dangling slot.
 *
 * Stage 1: constants + return. Stage 2: locals, args, stack
 * manipulation, branches, calls, and the exception unwind. Later
 * stages extend qjs_jit_op_supported() and the lowering switch.
 */
#ifndef QJS_JIT_INCLUDED_FROM_QUICKJS_C
#error "quickjs-jit-compile.c is compiled via #include from quickjs.c"
#endif

#define SLJIT_CONFIG_AUTO 1
#include "sljitLir.h"

_Static_assert(sizeof(JSValue) == 2 * sizeof(sljit_sw), "JIT assumes a two-word JSValue");

#define JIT_FRAME SLJIT_S0 /* QJSJitFrame *   (incoming arg 1) */
#define JIT_OUT SLJIT_S1   /* JSValue *out    (incoming arg 2) */
#define JIT_STACK SLJIT_S2 /* JSValue *stack_buf (cached) */
#define JIT_VAR SLJIT_S3   /* JSValue *var_buf   (cached) */
#define JIT_ARG SLJIT_S4   /* JSValue *arg_buf   (cached) */
#define JIT_CTX SLJIT_S5   /* JSContext *        (cached) */

/* JSValue slot layout: payload word at +0, tag word at +8, size 16. */
#define VAL_W(slot) ((sljit_sw)(slot) * 16)
#define TAG_W(slot) ((sljit_sw)(slot) * 16 + 8)

/* Thread-wide native-code accounting. Every runtime that JITs on the JS
   thread (top document + iframe child runtimes) shares this budget — the
   process-wide cap of the design, realized with the same thread-local
   model as the profiler (qjs_prof_thread_state). The per-runtime cap in
   QJSJitRuntime.code_limit still applies; compilation stops when EITHER
   limit is reached. */
static __thread size_t qjs_jit_thread_code_bytes;
#define QJS_JIT_THREAD_CODE_LIMIT ((size_t)64 * 1024 * 1024)

/* ================================================================== */
/* Generated-code helpers (reuse interpreter semantics)                */
/* ================================================================== */

/* --- constants --- */
static void qjs_jh_set_int(QJSJitFrame *frame, sljit_sw slot, sljit_sw value)
{
    frame->stack_buf[slot] = js_int32((int32_t)value);
}

static void qjs_jh_set_special(QJSJitFrame *frame, sljit_sw slot, sljit_sw tag)
{
    switch (tag) {
    case JS_TAG_UNDEFINED:
        frame->stack_buf[slot] = JS_UNDEFINED;
        break;
    case JS_TAG_NULL:
        frame->stack_buf[slot] = JS_NULL;
        break;
    default:
        frame->stack_buf[slot] = js_bool(tag == 1);
        break;
    }
}

static void qjs_jh_dup_const(QJSJitFrame *frame, sljit_sw slot, sljit_sw index)
{
    frame->stack_buf[slot] = js_dup(frame->b->cpool[index]);
}

/* --- return --- */
static void qjs_jh_return(QJSJitFrame *frame, JSValue *out, sljit_sw slot)
{
    *out = frame->stack_buf[slot]; /* ownership transfer, no dup */
}

static void qjs_jh_return_undef(QJSJitFrame *frame, JSValue *out, sljit_sw unused)
{
    (void)frame;
    (void)unused;
    *out = JS_UNDEFINED;
}

/* --- stack manipulation --- */
static void qjs_jh_dup(QJSJitFrame *frame, sljit_sw src, sljit_sw dst)
{
    frame->stack_buf[dst] = js_dup(frame->stack_buf[src]);
}

static void qjs_jh_drop(QJSJitFrame *frame, sljit_sw slot, sljit_sw unused)
{
    (void)unused;
    JS_FreeValue(frame->ctx, frame->stack_buf[slot]);
}

/* nip: [a b] -> [b]. Free a, move b down. (src=b slot, dst=a slot) */
static void qjs_jh_nip(QJSJitFrame *frame, sljit_sw dst, sljit_sw src)
{
    JS_FreeValue(frame->ctx, frame->stack_buf[dst]);
    frame->stack_buf[dst] = frame->stack_buf[src];
}

static void qjs_jh_swap(QJSJitFrame *frame, sljit_sw a, sljit_sw b)
{
    JSValue tmp = frame->stack_buf[a];
    frame->stack_buf[a] = frame->stack_buf[b];
    frame->stack_buf[b] = tmp;
}

/* --- locals / args --- */
static void qjs_jh_get_loc(QJSJitFrame *frame, sljit_sw dst, sljit_sw idx)
{
    frame->stack_buf[dst] = js_dup(frame->var_buf[idx]);
}

static void qjs_jh_put_loc(QJSJitFrame *frame, sljit_sw idx, sljit_sw src)
{
    set_value(frame->ctx, &frame->var_buf[idx], frame->stack_buf[src]);
}

static void qjs_jh_set_loc(QJSJitFrame *frame, sljit_sw idx, sljit_sw src)
{
    set_value(frame->ctx, &frame->var_buf[idx], js_dup(frame->stack_buf[src]));
}

static void qjs_jh_set_loc_uninit(QJSJitFrame *frame, sljit_sw idx, sljit_sw unused)
{
    (void)unused;
    set_value(frame->ctx, &frame->var_buf[idx], JS_UNINITIALIZED);
}

static void qjs_jh_get_loc01(QJSJitFrame *frame, sljit_sw dst, sljit_sw unused)
{
    (void)unused;
    frame->stack_buf[dst] = js_dup(frame->var_buf[0]);
    frame->stack_buf[dst + 1] = js_dup(frame->var_buf[1]);
}

static void qjs_jh_get_arg(QJSJitFrame *frame, sljit_sw dst, sljit_sw idx)
{
    frame->stack_buf[dst] = js_dup(frame->arg_buf[idx]);
}

static void qjs_jh_put_arg(QJSJitFrame *frame, sljit_sw idx, sljit_sw src)
{
    set_value(frame->ctx, &frame->arg_buf[idx], frame->stack_buf[src]);
}

static void qjs_jh_set_arg(QJSJitFrame *frame, sljit_sw idx, sljit_sw src)
{
    set_value(frame->ctx, &frame->arg_buf[idx], js_dup(frame->stack_buf[src]));
}

/* get_loc_check: throws ReferenceError on the TDZ. pc_off names the op
   for the backtrace. Leaves nothing pushed on failure. */
static sljit_sw qjs_jh_get_loc_check(QJSJitFrame *frame, sljit_sw dst_and_idx, sljit_sw pc_off)
{
    int dst = (int)(dst_and_idx >> 20);
    int idx = (int)(dst_and_idx & 0xfffff);
    if (unlikely(JS_IsUninitialized(frame->var_buf[idx]))) {
        frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
        JS_ThrowReferenceErrorUninitialized2(frame->ctx, frame->b, idx, false);
        return -1;
    }
    frame->stack_buf[dst] = js_dup(frame->var_buf[idx]);
    return 0;
}

static sljit_sw qjs_jh_put_loc_check(QJSJitFrame *frame, sljit_sw idx_and_src, sljit_sw pc_off)
{
    int idx = (int)(idx_and_src >> 20);
    int src = (int)(idx_and_src & 0xfffff);
    if (unlikely(JS_IsUninitialized(frame->var_buf[idx]))) {
        frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
        JS_ThrowReferenceErrorUninitialized2(frame->ctx, frame->b, idx, false);
        return -1; /* the value stays live on the stack; unwind frees it */
    }
    set_value(frame->ctx, &frame->var_buf[idx], frame->stack_buf[src]);
    return 0;
}

/* --- truthiness (if_true / if_false) --- */
static sljit_sw qjs_jh_to_bool_free(QJSJitFrame *frame, sljit_sw slot, sljit_sw unused)
{
    JSValue op1 = frame->stack_buf[slot];
    (void)unused;
    if ((uint32_t)JS_VALUE_GET_TAG(op1) <= JS_TAG_UNDEFINED) {
        return JS_VALUE_GET_INT(op1);
    }
    return JS_ToBoolFree(frame->ctx, op1);
}

/* --- interrupt safe-point (backedges) --- */
static sljit_sw qjs_jh_poll_interrupts(QJSJitFrame *frame, sljit_sw pc_off, sljit_sw unused)
{
    (void)unused;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    return js_poll_interrupts(frame->ctx) ? -1 : 0;
}

/* --- calls --- */
static sljit_sw qjs_jh_call(QJSJitFrame *frame, sljit_sw func_slot_argc, sljit_sw pc_off)
{
    int func_slot = (int)(func_slot_argc >> 20);
    int argc = (int)(func_slot_argc & 0xfffff);
    JSValue *base = &frame->stack_buf[func_slot]; /* base[0]=func, base[1..]=args */
    JSValue ret;
    int i;

    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    ret = JS_CallInternal(frame->ctx, base[0], JS_UNDEFINED, JS_UNDEFINED, argc, vc(base + 1), 0);
    if (unlikely(JS_IsException(ret))) {
        return -1; /* func + args stay live; unwind frees them */
    }
    for (i = 0; i <= argc; i++) {
        JS_FreeValue(frame->ctx, base[i]);
    }
    base[0] = ret;
    return 0;
}

/* --- exception unwind: free the live operand stack --- */
static void qjs_jh_unwind(QJSJitFrame *frame, sljit_sw count, sljit_sw unused)
{
    sljit_sw i;
    (void)unused;
    for (i = 0; i < count; i++) {
        JS_FreeValue(frame->ctx, frame->stack_buf[i]);
    }
}

/* Slow half of a value free, called by inline code only when a tag check
   has already shown the value is heap. sel: 0=stack, 1=var, 2=arg. */
static void qjs_jh_free_at(QJSJitFrame *frame, sljit_sw sel, sljit_sw slot)
{
    JSValue *base = sel == 0 ? frame->stack_buf : sel == 1 ? frame->var_buf : frame->arg_buf;
    JS_FreeValue(frame->ctx, base[slot]);
}

/* Slow half of the backedge interrupt check (counter already decremented
   to <= 0 inline). Returns -1 if the runtime interrupt fired. */
static sljit_sw qjs_jh_poll_slow(QJSJitFrame *frame, sljit_sw pc_off, sljit_sw unused)
{
    (void)unused;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    return __js_poll_interrupts(frame->ctx) ? -1 : 0;
}

/* ================================================================== */
/* Stage 3: arithmetic / comparison / bitwise                          */
/*                                                                     */
/* Faithful replicas of the interpreter cases: the both-int fast path  */
/* inline, everything else via the interpreter's own slow function     */
/* (js_add_slow, js_binary_arith_slow, ...). op_pc packs               */
/* (opcode << 20) | pc_off. sp_slot is the interpreter `sp` position   */
/* (one past the top operand): sp[-1] = top, sp[-2] = next.            */
/* ================================================================== */

/* binary: reads sp[-2],sp[-1]; writes sp[-2]; caller does depth -= 1. */
static sljit_sw qjs_jh_binary(QJSJitFrame *frame, sljit_sw sp_slot, sljit_sw op_pc)
{
    JSValue *sp = &frame->stack_buf[sp_slot];
    JSValue op1 = sp[-2], op2 = sp[-1];
    int op = (int)(op_pc >> 20);
    int both_int = JS_VALUE_IS_BOTH_INT(op1, op2);

    if (both_int) {
        int32_t a = JS_VALUE_GET_INT(op1), b = JS_VALUE_GET_INT(op2);
        int64_t r;
        switch (op) {
        case OP_add:
            r = (int64_t)a + b;
            sp[-2] = (r < INT32_MIN || r > INT32_MAX) ? js_float64((double)r) : js_int32((int)r);
            return 0;
        case OP_sub:
            r = (int64_t)a - b;
            sp[-2] = (r < INT32_MIN || r > INT32_MAX) ? js_float64((double)r) : js_int32((int)r);
            return 0;
        case OP_mul:
            r = (int64_t)a * b;
            if (r != 0 && (int)r == r) { /* r==0 goes slow (handles -0) */
                sp[-2] = js_int32((int)r);
                return 0;
            }
            break;
        case OP_and:
            sp[-2] = js_int32(a & b);
            return 0;
        case OP_or:
            sp[-2] = js_int32(a | b);
            return 0;
        case OP_xor:
            sp[-2] = js_int32(a ^ b);
            return 0;
        case OP_shl:
            sp[-2] = js_int32(a << (b & 0x1f));
            return 0;
        case OP_sar:
            sp[-2] = js_int32(a >> (b & 0x1f));
            return 0;
        case OP_shr:
            sp[-2] = js_uint32((uint32_t)a >> (b & 0x1f));
            return 0;
        case OP_lt:
            sp[-2] = js_bool(a < b);
            return 0;
        case OP_lte:
            sp[-2] = js_bool(a <= b);
            return 0;
        case OP_gt:
            sp[-2] = js_bool(a > b);
            return 0;
        case OP_gte:
            sp[-2] = js_bool(a >= b);
            return 0;
        case OP_eq:
        case OP_strict_eq:
            sp[-2] = js_bool(a == b);
            return 0;
        case OP_neq:
        case OP_strict_neq:
            sp[-2] = js_bool(a != b);
            return 0;
        default:
            break; /* div/mod/pow: slow */
        }
    }

    frame->sf->cur_pc = frame->b->byte_code_buf + (op_pc & 0xfffff);
    switch (op) {
    case OP_add:
        return js_add_slow(frame->ctx, sp);
    case OP_sub:
    case OP_mul:
    case OP_div:
    case OP_mod:
    case OP_pow:
        return js_binary_arith_slow(frame->ctx, sp, op);
    case OP_and:
    case OP_or:
    case OP_xor:
    case OP_shl:
    case OP_sar:
        return js_binary_logic_slow(frame->ctx, sp, op);
    case OP_shr:
        return js_shr_slow(frame->ctx, sp);
    case OP_lt:
    case OP_lte:
    case OP_gt:
    case OP_gte:
        return js_relational_slow(frame->ctx, sp, op);
    case OP_eq:
        return js_eq_slow(frame->ctx, sp, 0);
    case OP_neq:
        return js_eq_slow(frame->ctx, sp, 1);
    case OP_strict_eq:
        return js_strict_eq_slow(frame->ctx, sp, 0);
    case OP_strict_neq:
        return js_strict_eq_slow(frame->ctx, sp, 1);
    default:
        return -1;
    }
}

/* unary in place: neg/plus/not/lnot/inc/dec. reads/writes sp[-1]. */
static sljit_sw qjs_jh_unary(QJSJitFrame *frame, sljit_sw sp_slot, sljit_sw op_pc)
{
    JSValue *sp = &frame->stack_buf[sp_slot];
    JSValue op1 = sp[-1];
    int op = (int)(op_pc >> 20);
    uint32_t tag = JS_VALUE_GET_TAG(op1);

    switch (op) {
    case OP_inc:
        if (tag == JS_TAG_INT) {
            int val = JS_VALUE_GET_INT(op1);
            if (val != INT32_MAX) {
                sp[-1] = js_int32(val + 1);
                return 0;
            }
        }
        break;
    case OP_dec:
        if (tag == JS_TAG_INT) {
            int val = JS_VALUE_GET_INT(op1);
            if (val != INT32_MIN) {
                sp[-1] = js_int32(val - 1);
                return 0;
            }
        }
        break;
    case OP_neg:
        if (tag == JS_TAG_INT) {
            int val = JS_VALUE_GET_INT(op1);
            if (val != 0 && val != INT32_MIN) {
                sp[-1] = js_int32(-val);
                return 0;
            }
        }
        break;
    case OP_plus:
        if (tag == JS_TAG_INT || JS_TAG_IS_FLOAT64(tag)) {
            return 0; /* no-op for numbers */
        }
        break;
    case OP_not:
        if (tag == JS_TAG_INT) {
            sp[-1] = js_int32(~JS_VALUE_GET_INT(op1));
            return 0;
        }
        frame->sf->cur_pc = frame->b->byte_code_buf + (op_pc & 0xfffff);
        return js_not_slow(frame->ctx, sp);
    case OP_lnot: {
        int res;
        if ((uint32_t)JS_VALUE_GET_TAG(op1) <= JS_TAG_UNDEFINED) {
            res = JS_VALUE_GET_INT(op1) != 0;
        } else {
            res = JS_ToBoolFree(frame->ctx, op1);
        }
        sp[-1] = js_bool(!res);
        return 0; /* lnot never throws */
    }
    default:
        break;
    }

    frame->sf->cur_pc = frame->b->byte_code_buf + (op_pc & 0xfffff);
    return js_unary_arith_slow(frame->ctx, sp, op);
}

/* post_inc / post_dec: reads sp[-1], writes sp[0]; caller does depth += 1. */
static sljit_sw qjs_jh_post_incdec(QJSJitFrame *frame, sljit_sw sp_slot, sljit_sw op_pc)
{
    JSValue *sp = &frame->stack_buf[sp_slot];
    JSValue op1 = sp[-1];
    int op = (int)(op_pc >> 20);

    if (JS_VALUE_GET_TAG(op1) == JS_TAG_INT) {
        int val = JS_VALUE_GET_INT(op1);
        if (op == OP_post_inc) {
            if (val != INT32_MAX) {
                sp[0] = js_int32(val + 1);
                return 0;
            }
        } else {
            if (val != INT32_MIN) {
                sp[0] = js_int32(val - 1);
                return 0;
            }
        }
    }
    frame->sf->cur_pc = frame->b->byte_code_buf + (op_pc & 0xfffff);
    return js_post_inc_slow(frame->ctx, sp, op);
}

/* inc_loc / dec_loc: var_buf[idx] in place. idx_op packs (idx<<8)|is_dec. */
static sljit_sw qjs_jh_incdec_loc(QJSJitFrame *frame, sljit_sw idx_isdec, sljit_sw pc_off)
{
    int idx = (int)(idx_isdec >> 8);
    int is_dec = (int)(idx_isdec & 0xff);
    JSValue op1 = frame->var_buf[idx];

    if (JS_VALUE_GET_TAG(op1) == JS_TAG_INT) {
        int val = JS_VALUE_GET_INT(op1);
        if (!is_dec && val != INT32_MAX) {
            frame->var_buf[idx] = js_int32(val + 1);
            return 0;
        }
        if (is_dec && val != INT32_MIN) {
            frame->var_buf[idx] = js_int32(val - 1);
            return 0;
        }
    }
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    op1 = js_dup(op1);
    if (js_unary_arith_slow(frame->ctx, &op1 + 1, is_dec ? OP_dec : OP_inc)) {
        return -1;
    }
    set_value(frame->ctx, &frame->var_buf[idx], op1);
    return 0;
}

/* add_loc: var_buf[idx] += sp[-1]; caller does depth -= 1. The operand
   is captured out of the stack slot (nulled to JS_UNDEFINED) so a throw
   on the string/slow path is safe for the pre-op-depth unwind. */
static sljit_sw qjs_jh_add_loc(QJSJitFrame *frame, sljit_sw idx_and_sp, sljit_sw pc_off)
{
    int idx = (int)(idx_and_sp >> 20);
    int sp_slot = (int)(idx_and_sp & 0xfffff);
    JSValue *pv = &frame->var_buf[idx];
    JSValue top = frame->stack_buf[sp_slot - 1];
    frame->stack_buf[sp_slot - 1] = JS_UNDEFINED; /* consumed */

    if (JS_VALUE_IS_BOTH_INT(*pv, top)) {
        int64_t r = (int64_t)JS_VALUE_GET_INT(*pv) + JS_VALUE_GET_INT(top);
        *pv = ((int)r != r) ? js_float64((double)r) : js_int32((int)r);
        return 0;
    }
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    if (JS_VALUE_GET_TAG(*pv) == JS_TAG_STRING) {
        top = JS_ToPrimitiveFree(frame->ctx, top, HINT_NONE);
        if (JS_IsException(top)) {
            return -1;
        }
        top = JS_ConcatString(frame->ctx, js_dup(*pv), top);
        if (JS_IsException(top)) {
            return -1;
        }
        set_value(frame->ctx, pv, top);
        return 0;
    }
    {
        JSValue ops[2];
        ops[0] = js_dup(*pv);
        ops[1] = top;
        if (js_add_slow(frame->ctx, ops + 2)) {
            return -1;
        }
        set_value(frame->ctx, pv, ops[0]);
        return 0;
    }
}

/* ================================================================== */
/* Stage 4: property access, closure vars, method calls                */
/*                                                                     */
/* All faithful to the interpreter's free/consume order. The atom or   */
/* index operand is re-read from the bytecode at pc_off (like the       */
/* interpreter), so only slot + pc_off need packing. Consumed slots     */
/* are nulled to JS_UNDEFINED so the pre-op-depth unwind is safe.       */
/* ================================================================== */

/* obj[top] . atom  ->  val (in place). */
static sljit_sw qjs_jh_get_field(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    JSAtom atom = get_u32(frame->b->byte_code_buf + pc_off + 1);
    JSValue obj = frame->stack_buf[top];
    JSValue val;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    val = JS_GetPropertyInternal(frame->ctx, obj, atom, obj, false);
    if (JS_IsException(val)) {
        return -1; /* obj stays live; unwind frees */
    }
    JS_FreeValue(frame->ctx, obj);
    frame->stack_buf[top] = val;
    return 0;
}

/* obj[top] . length  ->  val (in place). */
static sljit_sw qjs_jh_get_length(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    JSValue obj = frame->stack_buf[top];
    JSValue val;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    val = JS_GetPropertyInternal(frame->ctx, obj, JS_ATOM_length, obj, false);
    if (JS_IsException(val)) {
        return -1;
    }
    JS_FreeValue(frame->ctx, obj);
    frame->stack_buf[top] = val;
    return 0;
}

/* obj[top] . atom  ->  obj, val  (keep obj; for method dispatch). */
static sljit_sw qjs_jh_get_field2(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    JSAtom atom = get_u32(frame->b->byte_code_buf + pc_off + 1);
    JSValue obj = frame->stack_buf[top];
    JSValue val;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    val = JS_GetPropertyInternal(frame->ctx, obj, atom, obj, false);
    if (JS_IsException(val)) {
        return -1; /* obj stays live */
    }
    frame->stack_buf[top + 1] = val;
    return 0;
}

/* obj[top-1] . atom = val[top]  ->  nothing. */
static sljit_sw qjs_jh_put_field(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    JSAtom atom = get_u32(frame->b->byte_code_buf + pc_off + 1);
    JSValue obj = frame->stack_buf[top - 1];
    JSValue val = frame->stack_buf[top];
    int ret;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    ret = JS_SetPropertyInternal2(frame->ctx, obj, atom, val, obj,
                                  JS_PROP_THROW_STRICT); /* consumes val */
    frame->stack_buf[top] = JS_UNDEFINED;
    JS_FreeValue(frame->ctx, obj);
    frame->stack_buf[top - 1] = JS_UNDEFINED;
    return ret < 0 ? -1 : 0;
}

/* obj[top-1] [ prop[top] ]  ->  val (in place at top-1). */
static sljit_sw qjs_jh_get_array_el(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    JSValue val;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    val = JS_GetPropertyValue(frame->ctx, frame->stack_buf[top - 1],
                              frame->stack_buf[top]); /* consumes prop */
    frame->stack_buf[top] = JS_UNDEFINED;
    JS_FreeValue(frame->ctx, frame->stack_buf[top - 1]);
    frame->stack_buf[top - 1] = val;
    return JS_IsException(val) ? -1 : 0;
}

/* obj[top-1] [ prop[top] ]  ->  obj, val (keep obj). */
static sljit_sw qjs_jh_get_array_el2(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    JSValue val;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    val = JS_GetPropertyValue(frame->ctx, frame->stack_buf[top - 1],
                              frame->stack_buf[top]); /* consumes prop */
    frame->stack_buf[top] = val;
    return JS_IsException(val) ? -1 : 0;
}

/* obj[top-2] [ idx[top-1] ] = val[top]  ->  nothing. */
static sljit_sw qjs_jh_put_array_el(QJSJitFrame *frame, sljit_sw top, sljit_sw pc_off)
{
    int ret;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    ret = JS_SetPropertyValue(frame->ctx, frame->stack_buf[top - 2], frame->stack_buf[top - 1],
                              frame->stack_buf[top], JS_PROP_THROW_STRICT); /* consumes idx + val */
    frame->stack_buf[top] = JS_UNDEFINED;
    frame->stack_buf[top - 1] = JS_UNDEFINED;
    JS_FreeValue(frame->ctx, frame->stack_buf[top - 2]);
    frame->stack_buf[top - 2] = JS_UNDEFINED;
    return ret < 0 ? -1 : 0;
}

/* --- closure variables (var_refs) --- */
static void qjs_jh_get_var_ref(QJSJitFrame *frame, sljit_sw dst, sljit_sw idx)
{
    frame->stack_buf[dst] = js_dup(*frame->var_refs[idx]->pvalue);
}

static void qjs_jh_put_var_ref(QJSJitFrame *frame, sljit_sw idx, sljit_sw src)
{
    set_value(frame->ctx, frame->var_refs[idx]->pvalue, frame->stack_buf[src]);
}

static void qjs_jh_set_var_ref(QJSJitFrame *frame, sljit_sw idx, sljit_sw src)
{
    set_value(frame->ctx, frame->var_refs[idx]->pvalue, js_dup(frame->stack_buf[src]));
}

static sljit_sw qjs_jh_get_var_ref_check(QJSJitFrame *frame, sljit_sw dst_idx, sljit_sw pc_off)
{
    int dst = (int)(dst_idx >> 20);
    int idx = (int)(dst_idx & 0xfffff);
    JSValue val = *frame->var_refs[idx]->pvalue;
    if (unlikely(JS_IsUninitialized(val))) {
        frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
        JS_ThrowReferenceErrorUninitialized2(frame->ctx, frame->b, idx, true);
        return -1;
    }
    frame->stack_buf[dst] = js_dup(val);
    return 0;
}

static sljit_sw qjs_jh_put_var_ref_check(QJSJitFrame *frame, sljit_sw idx_src, sljit_sw pc_off)
{
    int idx = (int)(idx_src >> 20);
    int src = (int)(idx_src & 0xfffff);
    if (unlikely(JS_IsUninitialized(*frame->var_refs[idx]->pvalue))) {
        frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
        JS_ThrowReferenceErrorUninitialized2(frame->ctx, frame->b, idx, true);
        return -1; /* value stays live; unwind frees */
    }
    set_value(frame->ctx, frame->var_refs[idx]->pvalue, frame->stack_buf[src]);
    return 0;
}

/* constructor call: [func, new_target, args...] -> [result].
   func_slot_argc packs (func_slot << 20) | argc. */
static sljit_sw qjs_jh_call_constructor(QJSJitFrame *frame, sljit_sw func_slot_argc,
                                        sljit_sw pc_off)
{
    int func_slot = (int)(func_slot_argc >> 20);
    int argc = (int)(func_slot_argc & 0xfffff);
    JSValue *base = &frame->stack_buf[func_slot];
    JSValue ret;
    int i;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    ret = JS_CallConstructorInternal(frame->ctx, base[0], base[1], argc, vc(base + 2), 0);
    if (unlikely(JS_IsException(ret))) {
        return -1; /* func + new_target + args stay live */
    }
    for (i = 0; i < argc + 2; i++) {
        JS_FreeValue(frame->ctx, base[i]);
    }
    base[0] = ret;
    return 0;
}

/* method call: [this, func, args...] -> [result].  this_slot_argc packs
   (this_slot << 20) | argc.  base[0]=this, base[1]=func, base[2..]=args. */
static sljit_sw qjs_jh_call_method(QJSJitFrame *frame, sljit_sw this_slot_argc, sljit_sw pc_off)
{
    int this_slot = (int)(this_slot_argc >> 20);
    int argc = (int)(this_slot_argc & 0xfffff);
    JSValue *base = &frame->stack_buf[this_slot];
    JSValue ret;
    int i;
    frame->sf->cur_pc = frame->b->byte_code_buf + pc_off;
    ret = JS_CallInternal(frame->ctx, base[1], base[0], JS_UNDEFINED, argc, vc(base + 2), 0);
    if (unlikely(JS_IsException(ret))) {
        return -1; /* this + func + args stay live */
    }
    for (i = 0; i < argc + 2; i++) {
        JS_FreeValue(frame->ctx, base[i]);
    }
    base[0] = ret;
    return 0;
}

/* ================================================================== */
/* Compiler context                                                    */
/* ================================================================== */

struct qjs_jit_ctx {
    struct sljit_compiler *compiler;
    JSContext *ctx;
    JSRuntime *rt;
    JSFunctionBytecode *b;
    int *depth_at;                 /* operand depth at each byte, -1 = unreached */
    struct sljit_label **labels;   /* label per branch-target byte, or NULL */
    struct sljit_jump **pend_jump; /* pending forward/backward jumps */
    int *pend_target;              /* target byte for each pending jump */
    int pend_count;
    int pend_cap;
    int failed;
};

static void qjs_jit_fail(struct qjs_jit_ctx *jc)
{
    jc->failed = 1;
}

static void qjs_jit_check(struct qjs_jit_ctx *jc, sljit_s32 status)
{
    if (status != SLJIT_SUCCESS) {
        jc->failed = 1;
    }
}

static void qjs_jit_add_pending(struct qjs_jit_ctx *jc, struct sljit_jump *jump, int target)
{
    if (!jump) {
        qjs_jit_fail(jc);
        return;
    }
    if (jc->pend_count >= jc->pend_cap) {
        qjs_jit_fail(jc);
        return;
    }
    jc->pend_jump[jc->pend_count] = jump;
    jc->pend_target[jc->pend_count] = target;
    jc->pend_count++;
}

/* ================================================================== */
/* Emission primitives                                                 */
/* ================================================================== */

/* helper(frame, imm1, imm2) with void return */
static void qjs_jit_call_fss(struct qjs_jit_ctx *jc, void *helper, sljit_sw imm1, sljit_sw imm2)
{
    struct sljit_compiler *c = jc->compiler;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, JIT_FRAME, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, imm1));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, imm2));
    qjs_jit_check(
        jc, sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3V(P, W, W), SLJIT_IMM, (sljit_sw)helper));
}

/* value-returning helper(frame, imm1, imm2) -> sljit_sw in R0 */
static void qjs_jit_call_fss_ret(struct qjs_jit_ctx *jc, void *helper, sljit_sw imm1, sljit_sw imm2)
{
    struct sljit_compiler *c = jc->compiler;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, JIT_FRAME, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, imm1));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, imm2));
    qjs_jit_check(
        jc, sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3(W, P, W, W), SLJIT_IMM, (sljit_sw)helper));
}

/* Emit: free the live operand stack [0..depth) and return -1. */
static void qjs_jit_emit_unwind_return(struct qjs_jit_ctx *jc, int depth)
{
    qjs_jit_call_fss(jc, (void *)qjs_jh_unwind, depth, 0);
    qjs_jit_check(jc, sljit_emit_return(jc->compiler, SLJIT_MOV, SLJIT_IMM, (sljit_sw)-1));
}

/* Normal return of the operand at `slot`: free any live operands below
   it, transfer the result to *out, return 0. */
static void qjs_jit_emit_return_slot(struct qjs_jit_ctx *jc, int slot)
{
    struct sljit_compiler *c = jc->compiler;
    if (slot > 0) {
        qjs_jit_call_fss(jc, (void *)qjs_jh_unwind, slot, 0);
    }
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, JIT_FRAME, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, JIT_OUT, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, slot));
    qjs_jit_check(jc, sljit_emit_icall(c, SLJIT_CALL, SLJIT_ARGS3V(P, P, W), SLJIT_IMM,
                                       (sljit_sw)qjs_jh_return));
    qjs_jit_check(jc, sljit_emit_return(c, SLJIT_MOV, SLJIT_IMM, 0));
}

/* After a throwing helper (status in R0): on status < 0, unwind the
   pre-op live depth and return -1; else continue. */
static void qjs_jit_check_exception(struct qjs_jit_ctx *jc, int pre_op_depth)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *ok = sljit_emit_cmp(c, SLJIT_SIG_GREATER_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);
    if (!ok) {
        qjs_jit_fail(jc);
        return;
    }
    qjs_jit_emit_unwind_return(jc, pre_op_depth);
    {
        struct sljit_label *cont = sljit_emit_label(c);
        if (!cont) {
            qjs_jit_fail(jc);
            return;
        }
        sljit_set_label(ok, cont);
    }
}

/* ================================================================== */
/* Inline codegen — the hot-loop path runs with no per-op call         */
/* ================================================================== */

/* Store an int32 result into a slot: payload (sign-extended) + INT tag. */
static void qjs_jit_store_int(struct qjs_jit_ctx *jc, int slot, sljit_s32 reg)
{
    struct sljit_compiler *c = jc->compiler;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV_S32, reg, 0, reg, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(JIT_STACK), VAL_W(slot), reg, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(JIT_STACK), TAG_W(slot), SLJIT_IMM,
                                     JS_TAG_INT));
}

static void qjs_jit_store_bool(struct qjs_jit_ctx *jc, int slot, sljit_s32 reg)
{
    struct sljit_compiler *c = jc->compiler;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(JIT_STACK), VAL_W(slot), reg, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(JIT_STACK), TAG_W(slot), SLJIT_IMM,
                                     JS_TAG_BOOL));
}

/* Materialize a small immediate (int/bool/null/undefined) into a slot. */
static void qjs_jit_emit_imm(struct qjs_jit_ctx *jc, int slot, sljit_sw val, sljit_sw tag)
{
    struct sljit_compiler *c = jc->compiler;
    qjs_jit_check(jc,
                  sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(JIT_STACK), VAL_W(slot), SLJIT_IMM, val));
    qjs_jit_check(jc,
                  sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(JIT_STACK), TAG_W(slot), SLJIT_IMM, tag));
}

/* Copy a 16-byte JSValue between slots of two cached bases (no refcount). */
static void qjs_jit_emit_copy(struct qjs_jit_ctx *jc, sljit_s32 dbase, int dslot, sljit_s32 sbase,
                              int sslot)
{
    struct sljit_compiler *c = jc->compiler;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(sbase), VAL_W(sslot)));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(dbase), VAL_W(dslot), SLJIT_R0, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(sbase), TAG_W(sslot)));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(dbase), TAG_W(dslot), SLJIT_R0, 0));
}

/* Inline refcount++ on the value at (base, slot) if it is heap (js_dup). */
static void qjs_jit_emit_incref(struct qjs_jit_ctx *jc, sljit_s32 base, int slot)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *skip;
    struct sljit_label *cont;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(base), TAG_W(slot)));
    /* JS_VALUE_HAS_REF_COUNT: (unsigned)tag >= (unsigned)JS_TAG_FIRST */
    skip = sljit_emit_cmp(c, SLJIT_LESS, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)JS_TAG_FIRST);
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(base), VAL_W(slot)));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1), 0));
    qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_ADD32, SLJIT_R2, 0, SLJIT_R2, 0, SLJIT_IMM, 1));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(SLJIT_R1), 0, SLJIT_R2, 0));
    cont = sljit_emit_label(c);
    if (skip && cont) {
        sljit_set_label(skip, cont);
    } else {
        qjs_jit_fail(jc);
    }
}

/* Inline free of the value at (base, slot) if heap: tag check, then the
   slow helper does the actual JS_FreeValue (rare for int hot loops). */
static void qjs_jit_emit_free(struct qjs_jit_ctx *jc, sljit_s32 base, int slot, int sel)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *skip;
    struct sljit_label *cont;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(base), TAG_W(slot)));
    skip = sljit_emit_cmp(c, SLJIT_LESS, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)JS_TAG_FIRST);
    qjs_jit_call_fss(jc, (void *)qjs_jh_free_at, sel, slot);
    cont = sljit_emit_label(c);
    if (skip && cont) {
        sljit_set_label(skip, cont);
    } else {
        qjs_jit_fail(jc);
    }
}

/* Which binary ops have an inline both-int fast path. */
static int qjs_jit_binary_inlinable(int op)
{
    switch (op) {
    case OP_add:
    case OP_sub:
    case OP_and:
    case OP_or:
    case OP_xor:
    case OP_lt:
    case OP_lte:
    case OP_gt:
    case OP_gte:
    case OP_eq:
    case OP_neq:
    case OP_strict_eq:
    case OP_strict_neq:
        return 1;
    default:
        return 0; /* mul/div/mod/pow/shl/sar/shr -> helper */
    }
}

/* Emit a binary op: inline both-int fast path (when inlinable) with a
   slow fall-through to qjs_jh_binary; otherwise the plain helper call. */
static void qjs_jit_emit_binary(struct qjs_jit_ctx *jc, int op, int depth, int pos)
{
    struct sljit_compiler *c = jc->compiler;
    int s1 = depth - 2, s2 = depth - 1;
    struct sljit_jump *not_int, *overflow = NULL, *done;
    struct sljit_label *slow_lbl, *done_lbl;
    sljit_s32 cond;

    if (!qjs_jit_binary_inlinable(op)) {
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_binary, depth, ((sljit_sw)op << 20) | pos);
        qjs_jit_check_exception(jc, depth);
        return;
    }

    /* both int? tag(s1) | tag(s2) == 0 iff both JS_TAG_INT. */
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(JIT_STACK), TAG_W(s1)));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_MEM1(JIT_STACK), TAG_W(s2)));
    qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_OR, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_R1, 0));
    not_int = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, 0);

    qjs_jit_check(jc,
                  sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R2, 0, SLJIT_MEM1(JIT_STACK), VAL_W(s1)));
    qjs_jit_check(jc,
                  sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R3, 0, SLJIT_MEM1(JIT_STACK), VAL_W(s2)));

    switch (op) {
    case OP_add:
        qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_ADD32 | SLJIT_SET(SLJIT_OVERFLOW), SLJIT_R4, 0,
                                         SLJIT_R2, 0, SLJIT_R3, 0));
        overflow = sljit_emit_jump(c, SLJIT_OVERFLOW);
        qjs_jit_store_int(jc, s1, SLJIT_R4);
        break;
    case OP_sub:
        qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_SUB32 | SLJIT_SET(SLJIT_OVERFLOW), SLJIT_R4, 0,
                                         SLJIT_R2, 0, SLJIT_R3, 0));
        overflow = sljit_emit_jump(c, SLJIT_OVERFLOW);
        qjs_jit_store_int(jc, s1, SLJIT_R4);
        break;
    case OP_and:
        qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_AND32, SLJIT_R4, 0, SLJIT_R2, 0, SLJIT_R3, 0));
        qjs_jit_store_int(jc, s1, SLJIT_R4);
        break;
    case OP_or:
        qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_OR32, SLJIT_R4, 0, SLJIT_R2, 0, SLJIT_R3, 0));
        qjs_jit_store_int(jc, s1, SLJIT_R4);
        break;
    case OP_xor:
        qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_XOR32, SLJIT_R4, 0, SLJIT_R2, 0, SLJIT_R3, 0));
        qjs_jit_store_int(jc, s1, SLJIT_R4);
        break;
    default: { /* comparisons */
        sljit_s32 set_flag, read_cond;
        /* SUB sets ONE flag per pair; the inverse condition (…_EQUAL) is
           read off the same flag. So SET with the canonical base
           (SIG_LESS / SIG_GREATER / zero), READ with the real condition. */
        switch (op) {
        case OP_lt:
            set_flag = SLJIT_SET(SLJIT_SIG_LESS);
            read_cond = SLJIT_SIG_LESS;
            break;
        case OP_gte:
            set_flag = SLJIT_SET(SLJIT_SIG_LESS);
            read_cond = SLJIT_SIG_GREATER_EQUAL;
            break;
        case OP_gt:
            set_flag = SLJIT_SET(SLJIT_SIG_GREATER);
            read_cond = SLJIT_SIG_GREATER;
            break;
        case OP_lte:
            set_flag = SLJIT_SET(SLJIT_SIG_GREATER);
            read_cond = SLJIT_SIG_LESS_EQUAL;
            break;
        case OP_eq:
        case OP_strict_eq:
            set_flag = SLJIT_SET_Z;
            read_cond = SLJIT_EQUAL;
            break;
        default:
            set_flag = SLJIT_SET_Z;
            read_cond = SLJIT_NOT_EQUAL;
            break;
        }
        (void)cond;
        qjs_jit_check(jc, sljit_emit_op2u(c, SLJIT_SUB32 | set_flag, SLJIT_R2, 0, SLJIT_R3, 0));
        qjs_jit_check(jc, sljit_emit_op_flags(c, SLJIT_MOV, SLJIT_R4, 0, read_cond));
        qjs_jit_store_bool(jc, s1, SLJIT_R4);
        break;
    }
    }
    done = sljit_emit_jump(c, SLJIT_JUMP);

    /* slow path */
    slow_lbl = sljit_emit_label(c);
    if (!not_int || !done || !slow_lbl) {
        qjs_jit_fail(jc);
        return;
    }
    sljit_set_label(not_int, slow_lbl);
    if (overflow) {
        sljit_set_label(overflow, slow_lbl);
    }
    qjs_jit_call_fss_ret(jc, (void *)qjs_jh_binary, depth, ((sljit_sw)op << 20) | pos);
    qjs_jit_check_exception(jc, depth);

    done_lbl = sljit_emit_label(c);
    if (done_lbl) {
        sljit_set_label(done, done_lbl);
    } else {
        qjs_jit_fail(jc);
    }
}

/* Emit inc/dec on the stack top (slot) or a local var (base=JIT_VAR).
   Inline int fast path with overflow guard; else the helper. */
static void qjs_jit_emit_incdec(struct qjs_jit_ctx *jc, int is_dec, sljit_s32 base, int slot,
                                void *helper, sljit_sw helper_a, sljit_sw pc_off, int depth)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *not_int, *overflow, *done;
    struct sljit_label *slow_lbl, *done_lbl;

    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(base), TAG_W(slot)));
    not_int = sljit_emit_cmp(c, SLJIT_NOT_EQUAL, SLJIT_R0, 0, SLJIT_IMM, JS_TAG_INT);
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R2, 0, SLJIT_MEM1(base), VAL_W(slot)));
    qjs_jit_check(
        jc, sljit_emit_op2(c, (is_dec ? SLJIT_SUB32 : SLJIT_ADD32) | SLJIT_SET(SLJIT_OVERFLOW),
                           SLJIT_R4, 0, SLJIT_R2, 0, SLJIT_IMM, 1));
    overflow = sljit_emit_jump(c, SLJIT_OVERFLOW);
    /* store int result to (base, slot) */
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R4, 0, SLJIT_R4, 0));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(base), VAL_W(slot), SLJIT_R4, 0));
    qjs_jit_check(
        jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(base), TAG_W(slot), SLJIT_IMM, JS_TAG_INT));
    done = sljit_emit_jump(c, SLJIT_JUMP);

    slow_lbl = sljit_emit_label(c);
    if (!not_int || !overflow || !done || !slow_lbl) {
        qjs_jit_fail(jc);
        return;
    }
    sljit_set_label(not_int, slow_lbl);
    sljit_set_label(overflow, slow_lbl);
    qjs_jit_call_fss_ret(jc, helper, helper_a, pc_off);
    qjs_jit_check_exception(jc, depth);

    done_lbl = sljit_emit_label(c);
    if (done_lbl) {
        sljit_set_label(done, done_lbl);
    } else {
        qjs_jit_fail(jc);
    }
}

/* put var (move stack top into local): free-old fast path inline. If the
   OLD value is heap, the helper does the correct set_value; else (int/
   non-heap old) just move the new value in — no free, no call. Consumes
   the stack top either way. */
static void qjs_jit_emit_put_var(struct qjs_jit_ctx *jc, sljit_s32 base, int idx, int top,
                                 void *helper)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *heap, *done;
    struct sljit_label *heap_lbl, *done_lbl;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(base), TAG_W(idx)));
    heap = sljit_emit_cmp(c, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)JS_TAG_FIRST);
    qjs_jit_emit_copy(jc, base, idx, JIT_STACK, top); /* move new in (old was int) */
    done = sljit_emit_jump(c, SLJIT_JUMP);
    heap_lbl = sljit_emit_label(c);
    if (!heap || !done || !heap_lbl) {
        qjs_jit_fail(jc);
        return;
    }
    sljit_set_label(heap, heap_lbl);
    qjs_jit_call_fss(jc, helper, idx, top); /* set_value handles the free */
    done_lbl = sljit_emit_label(c);
    if (done_lbl) {
        sljit_set_label(done, done_lbl);
    } else {
        qjs_jit_fail(jc);
    }
}

/* set var (var = dup(stack top), stack kept): inline only when BOTH the
   old var value and the new stack value are non-heap; else helper. */
static void qjs_jit_emit_set_var(struct qjs_jit_ctx *jc, sljit_s32 base, int idx, int top,
                                 void *helper)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *heap_old, *heap_new, *done;
    struct sljit_label *heap_lbl, *done_lbl;
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(base), TAG_W(idx)));
    heap_old =
        sljit_emit_cmp(c, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)JS_TAG_FIRST);
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(JIT_STACK), TAG_W(top)));
    heap_new =
        sljit_emit_cmp(c, SLJIT_GREATER_EQUAL, SLJIT_R0, 0, SLJIT_IMM, (sljit_sw)JS_TAG_FIRST);
    qjs_jit_emit_copy(jc, base, idx, JIT_STACK, top); /* both int: plain copy, stack kept */
    done = sljit_emit_jump(c, SLJIT_JUMP);
    heap_lbl = sljit_emit_label(c);
    if (!heap_old || !heap_new || !done || !heap_lbl) {
        qjs_jit_fail(jc);
        return;
    }
    sljit_set_label(heap_old, heap_lbl);
    sljit_set_label(heap_new, heap_lbl);
    qjs_jit_call_fss(jc, helper, idx, top);
    done_lbl = sljit_emit_label(c);
    if (done_lbl) {
        sljit_set_label(done, done_lbl);
    } else {
        qjs_jit_fail(jc);
    }
}

/* Inline conditional branch (if_true/if_false): the truthiness of an
   int/bool/null/undefined operand is its payload (no call); heap/float
   falls to JS_ToBoolFree. Emits the conditional jump to `target`. */
static void qjs_jit_emit_branch(struct qjs_jit_ctx *jc, int cond_true, int top, int target)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *slow, *past, *brj;
    struct sljit_label *slow_lbl, *test_lbl;

    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_MEM1(JIT_STACK), TAG_W(top)));
    /* (uint32)tag <= JS_TAG_UNDEFINED  ->  res = payload (int/bool/null/undef) */
    slow = sljit_emit_cmp(c, SLJIT_GREATER, SLJIT_R0, 0, SLJIT_IMM, JS_TAG_UNDEFINED);
    qjs_jit_check(jc,
                  sljit_emit_op1(c, SLJIT_MOV_S32, SLJIT_R1, 0, SLJIT_MEM1(JIT_STACK), VAL_W(top)));
    past = sljit_emit_jump(c, SLJIT_JUMP);

    slow_lbl = sljit_emit_label(c);
    if (!slow || !past || !slow_lbl) {
        qjs_jit_fail(jc);
        return;
    }
    sljit_set_label(slow, slow_lbl);
    qjs_jit_call_fss_ret(jc, (void *)qjs_jh_to_bool_free, top, 0); /* R0 = bool */
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_R0, 0));

    test_lbl = sljit_emit_label(c);
    if (!test_lbl) {
        qjs_jit_fail(jc);
        return;
    }
    sljit_set_label(past, test_lbl);
    brj = sljit_emit_cmp(c, cond_true ? SLJIT_NOT_EQUAL : SLJIT_EQUAL, SLJIT_R1, 0, SLJIT_IMM, 0);
    qjs_jit_add_pending(jc, brj, target);
}

/* Inline backedge interrupt: decrement ctx->interrupt_counter; call the
   slow poll only when it reaches <= 0. */
static void qjs_jit_emit_backedge_poll(struct qjs_jit_ctx *jc, int pos, int depth)
{
    struct sljit_compiler *c = jc->compiler;
    struct sljit_jump *no_poll;
    struct sljit_label *cont;
    sljit_sw off = offsetof(struct JSContext, interrupt_counter);

    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV32, SLJIT_R0, 0, SLJIT_MEM1(JIT_CTX), off));
    qjs_jit_check(jc, sljit_emit_op2(c, SLJIT_SUB32, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1));
    qjs_jit_check(jc, sljit_emit_op1(c, SLJIT_MOV32, SLJIT_MEM1(JIT_CTX), off, SLJIT_R0, 0));
    /* separate compare so the store above cannot clobber the flag */
    no_poll = sljit_emit_cmp(c, SLJIT_SIG_GREATER | SLJIT_32, SLJIT_R0, 0, SLJIT_IMM, 0);
    qjs_jit_call_fss_ret(jc, (void *)qjs_jh_poll_slow, pos, 0);
    qjs_jit_check_exception(jc, depth);
    cont = sljit_emit_label(c);
    if (no_poll && cont) {
        sljit_set_label(no_poll, cont);
    } else {
        qjs_jit_fail(jc);
    }
}

/* ================================================================== */
/* Stack-depth model                                                   */
/* ================================================================== */

/* Signed operand-stack delta and control-flow shape of a supported op.
   branch_target is the absolute byte for a (conditional) branch, else
   -1; *is_uncond set for an unconditional goto; *is_terminal for
   return/return_undef. */
static int qjs_jit_op_delta(uint8_t op, const uint8_t *pc, int pos, int *branch_target,
                            int *is_uncond, int *is_terminal)
{
    *branch_target = -1;
    *is_uncond = 0;
    *is_terminal = 0;
    switch (op) {
    case OP_push_i32:
    case OP_push_i8:
    case OP_push_i16:
    case OP_push_minus1:
    case OP_push_0:
    case OP_push_1:
    case OP_push_2:
    case OP_push_3:
    case OP_push_4:
    case OP_push_5:
    case OP_push_6:
    case OP_push_7:
    case OP_push_const:
    case OP_push_const8:
    case OP_undefined:
    case OP_null:
    case OP_push_false:
    case OP_push_true:
    case OP_get_loc:
    case OP_get_loc8:
    case OP_get_loc0:
    case OP_get_loc1:
    case OP_get_loc2:
    case OP_get_loc3:
    case OP_get_loc_check:
    case OP_get_arg:
    case OP_get_arg0:
    case OP_get_arg1:
    case OP_get_arg2:
    case OP_get_arg3:
    case OP_dup:
        return 1;
    case OP_get_loc0_loc1:
        return 2;
    case OP_drop:
    case OP_put_loc:
    case OP_put_loc8:
    case OP_put_loc0:
    case OP_put_loc1:
    case OP_put_loc2:
    case OP_put_loc3:
    case OP_put_loc_check:
    case OP_put_arg:
    case OP_nip:
        return -1;
    case OP_set_loc:
    case OP_set_loc8:
    case OP_set_loc0:
    case OP_set_loc1:
    case OP_set_loc2:
    case OP_set_loc3:
    case OP_set_loc_uninitialized:
    case OP_set_arg:
    case OP_swap:
    case OP_nop:
        return 0;
    /* binary arith/logic/compare: pop 2, push 1 */
    case OP_add:
    case OP_sub:
    case OP_mul:
    case OP_div:
    case OP_mod:
    case OP_pow:
    case OP_and:
    case OP_or:
    case OP_xor:
    case OP_shl:
    case OP_sar:
    case OP_shr:
    case OP_lt:
    case OP_lte:
    case OP_gt:
    case OP_gte:
    case OP_eq:
    case OP_neq:
    case OP_strict_eq:
    case OP_strict_neq:
    case OP_add_loc:
        return -1;
    /* unary in place: pop 1, push 1 */
    case OP_inc:
    case OP_dec:
    case OP_neg:
    case OP_plus:
    case OP_not:
    case OP_lnot:
        return 0;
    case OP_inc_loc:
    case OP_dec_loc:
        return 0;
    case OP_post_inc:
    case OP_post_dec:
        return 1;
    case OP_if_true:
    case OP_if_false:
        *branch_target = pos + 1 + (int32_t)get_u32(pc);
        return -1;
    case OP_if_true8:
    case OP_if_false8:
        *branch_target = pos + 1 + (int8_t)pc[0];
        return -1;
    case OP_goto:
        *branch_target = pos + 1 + (int32_t)get_u32(pc);
        *is_uncond = 1;
        return 0;
    case OP_goto8:
        *branch_target = pos + 1 + (int8_t)pc[0];
        *is_uncond = 1;
        return 0;
    case OP_goto16:
        *branch_target = pos + 1 + (int16_t)get_u16(pc);
        *is_uncond = 1;
        return 0;
    case OP_call0:
        return -0;
    case OP_call1:
        return -1;
    case OP_call2:
        return -2;
    case OP_call3:
        return -3;
    case OP_call:
        return -(int)get_u16(pc);
    case OP_call_method:
        return -((int)get_u16(pc) + 1); /* this+func+args -> result */
    case OP_call_constructor:
        return -((int)get_u16(pc) + 1); /* func+nt+args -> result */
    case OP_tail_call:
        *is_terminal = 1;
        return 0; /* returns the call result */
    case OP_tail_call_method:
        *is_terminal = 1;
        return 0;
    case OP_return:
        *is_terminal = 1;
        return -1;
    case OP_return_undef:
        *is_terminal = 1;
        return 0;
    /* Stage 4: property access */
    case OP_get_field:
    case OP_get_length:
    case OP_get_array_el2:
        return 0; /* obj -> val, or obj,prop -> obj,val */
    case OP_get_field2:
        return 1; /* obj -> obj, val */
    case OP_get_array_el:
        return -1; /* obj, prop -> val */
    case OP_put_field:
        return -2; /* obj, val -> */
    case OP_put_array_el:
        return -3; /* obj, idx, val -> */
    /* Stage 4: closure variables */
    case OP_get_var_ref:
    case OP_get_var_ref0:
    case OP_get_var_ref1:
    case OP_get_var_ref2:
    case OP_get_var_ref3:
    case OP_get_var_ref_check:
        return 1;
    case OP_put_var_ref:
    case OP_put_var_ref0:
    case OP_put_var_ref1:
    case OP_put_var_ref2:
    case OP_put_var_ref3:
    case OP_put_var_ref_check:
        return -1;
    case OP_set_var_ref:
    case OP_set_var_ref0:
    case OP_set_var_ref1:
    case OP_set_var_ref2:
    case OP_set_var_ref3:
        return 0;
    default:
        return 0;
    }
}

/* ================================================================== */
/* Opcode support test (what THIS stage's compiler emits)              */
/* ================================================================== */

static bool qjs_jit_op_supported(uint16_t op)
{
    switch (op) {
    case OP_push_i32:
    case OP_push_i8:
    case OP_push_i16:
    case OP_push_minus1:
    case OP_push_0:
    case OP_push_1:
    case OP_push_2:
    case OP_push_3:
    case OP_push_4:
    case OP_push_5:
    case OP_push_6:
    case OP_push_7:
    case OP_push_const:
    case OP_push_const8:
    case OP_undefined:
    case OP_null:
    case OP_push_false:
    case OP_push_true:
    case OP_drop:
    case OP_nip:
    case OP_dup:
    case OP_swap:
    case OP_get_loc:
    case OP_get_loc8:
    case OP_get_loc0:
    case OP_get_loc1:
    case OP_get_loc2:
    case OP_get_loc3:
    case OP_get_loc_check:
    case OP_get_loc0_loc1:
    case OP_put_loc:
    case OP_put_loc8:
    case OP_put_loc0:
    case OP_put_loc1:
    case OP_put_loc2:
    case OP_put_loc3:
    case OP_put_loc_check:
    case OP_set_loc:
    case OP_set_loc8:
    case OP_set_loc0:
    case OP_set_loc1:
    case OP_set_loc2:
    case OP_set_loc3:
    case OP_set_loc_uninitialized:
    case OP_get_arg:
    case OP_get_arg0:
    case OP_get_arg1:
    case OP_get_arg2:
    case OP_get_arg3:
    case OP_put_arg:
    case OP_set_arg:
    case OP_if_true:
    case OP_if_false:
    case OP_if_true8:
    case OP_if_false8:
    case OP_goto:
    case OP_goto8:
    case OP_goto16:
    case OP_call:
    case OP_call0:
    case OP_call1:
    case OP_call2:
    case OP_call3:
    case OP_return:
    case OP_return_undef:
    case OP_nop:
    /* Stage 3 arithmetic / comparison / bitwise / unary */
    case OP_add:
    case OP_sub:
    case OP_mul:
    case OP_div:
    case OP_mod:
    case OP_pow:
    case OP_and:
    case OP_or:
    case OP_xor:
    case OP_shl:
    case OP_sar:
    case OP_shr:
    case OP_lt:
    case OP_lte:
    case OP_gt:
    case OP_gte:
    case OP_eq:
    case OP_neq:
    case OP_strict_eq:
    case OP_strict_neq:
    case OP_add_loc:
    case OP_inc:
    case OP_dec:
    case OP_neg:
    case OP_plus:
    case OP_not:
    case OP_lnot:
    case OP_inc_loc:
    case OP_dec_loc:
    case OP_post_inc:
    case OP_post_dec:
    /* Stage 4: property / element access, closures, method calls */
    case OP_get_field:
    case OP_get_field2:
    case OP_get_length:
    case OP_put_field:
    case OP_get_array_el:
    case OP_get_array_el2:
    case OP_put_array_el:
    case OP_get_var_ref:
    case OP_get_var_ref0:
    case OP_get_var_ref1:
    case OP_get_var_ref2:
    case OP_get_var_ref3:
    case OP_get_var_ref_check:
    case OP_put_var_ref:
    case OP_put_var_ref0:
    case OP_put_var_ref1:
    case OP_put_var_ref2:
    case OP_put_var_ref3:
    case OP_put_var_ref_check:
    case OP_set_var_ref:
    case OP_set_var_ref0:
    case OP_set_var_ref1:
    case OP_set_var_ref2:
    case OP_set_var_ref3:
    case OP_call_method:
    case OP_tail_call:
    case OP_tail_call_method:
    case OP_call_constructor:
        return true;
    default:
        return false;
    }
}

/* ================================================================== */
/* Depth pre-pass (worklist) + support/wellformedness check            */
/* ================================================================== */

static bool qjs_jit_compute_depths(struct qjs_jit_ctx *jc)
{
    JSFunctionBytecode *b = jc->b;
    const uint8_t *bytecode = b->byte_code_buf;
    int len = b->byte_code_len;
    int *worklist;
    int wl_count = 0;
    int i;

    for (i = 0; i < len; i++) {
        jc->depth_at[i] = -1;
    }

    worklist = js_malloc_rt(jc->rt, sizeof(int) * (len > 0 ? len : 1));
    if (!worklist) {
        return false;
    }

    jc->depth_at[0] = 0;
    worklist[wl_count++] = 0;

    while (wl_count > 0) {
        int pos = worklist[--wl_count];
        uint8_t op = bytecode[pos];
        int size = short_opcode_info(op).size;
        int delta, branch_target, is_uncond, is_terminal;
        int out_depth;

        if (op >= OP_COUNT || size <= 0 || pos + size > len) {
            js_free_rt(jc->rt, worklist);
            return false;
        }
        if (!qjs_jit_op_supported(op)) {
            if (getenv("QJS_JIT_DEBUG")) {
                fprintf(stderr, "JIT: unsupported op %d at %d\n", op, pos);
            }
            js_free_rt(jc->rt, worklist);
            return false;
        }
        delta =
            qjs_jit_op_delta(op, bytecode + pos + 1, pos, &branch_target, &is_uncond, &is_terminal);
        out_depth = jc->depth_at[pos] + delta;
        if (out_depth < 0 || out_depth > b->stack_size) {
            js_free_rt(jc->rt, worklist);
            return false;
        }

        /* successors */
        if (branch_target >= 0) {
            if (branch_target < 0 || branch_target >= len || bytecode[branch_target] >= OP_COUNT) {
                js_free_rt(jc->rt, worklist);
                return false;
            }
            if (jc->depth_at[branch_target] < 0) {
                jc->depth_at[branch_target] = out_depth;
                worklist[wl_count++] = branch_target;
            } else if (jc->depth_at[branch_target] != out_depth) {
                js_free_rt(jc->rt, worklist);
                return false;
            }
        }
        if (!is_uncond && !is_terminal) {
            int next = pos + size;
            if (next < len) {
                if (jc->depth_at[next] < 0) {
                    jc->depth_at[next] = out_depth;
                    worklist[wl_count++] = next;
                } else if (jc->depth_at[next] != out_depth) {
                    js_free_rt(jc->rt, worklist);
                    return false;
                }
            }
        }
    }

    js_free_rt(jc->rt, worklist);
    return true;
}

/* ================================================================== */
/* Lowering                                                            */
/* ================================================================== */

/* pack (a<<20)|b for helpers taking two small ints in one word */
static sljit_sw qjs_jit_pack2(int a, int b)
{
    return ((sljit_sw)a << 20) | b;
}

static void qjs_jit_lower_op(struct qjs_jit_ctx *jc, uint8_t op, const uint8_t *pc, int pos)
{
    int depth = jc->depth_at[pos]; /* pre-op operand depth */
    int top = depth - 1;           /* index of the top operand */

    switch (op) {
    /* --- constants (inline: write value + tag directly) --- */
    case OP_push_i32:
        qjs_jit_emit_imm(jc, depth, (int32_t)get_u32(pc), JS_TAG_INT);
        break;
    case OP_push_i8:
        qjs_jit_emit_imm(jc, depth, get_i8(pc), JS_TAG_INT);
        break;
    case OP_push_i16:
        qjs_jit_emit_imm(jc, depth, get_i16(pc), JS_TAG_INT);
        break;
    case OP_push_minus1:
        qjs_jit_emit_imm(jc, depth, -1, JS_TAG_INT);
        break;
    case OP_push_0:
    case OP_push_1:
    case OP_push_2:
    case OP_push_3:
    case OP_push_4:
    case OP_push_5:
    case OP_push_6:
    case OP_push_7:
        qjs_jit_emit_imm(jc, depth, op - OP_push_0, JS_TAG_INT);
        break;
    case OP_push_const:
        qjs_jit_call_fss(jc, (void *)qjs_jh_dup_const, depth, get_u32(pc));
        break;
    case OP_push_const8:
        qjs_jit_call_fss(jc, (void *)qjs_jh_dup_const, depth, pc[0]);
        break;
    case OP_undefined:
        qjs_jit_emit_imm(jc, depth, 0, JS_TAG_UNDEFINED);
        break;
    case OP_null:
        qjs_jit_emit_imm(jc, depth, 0, JS_TAG_NULL);
        break;
    case OP_push_false:
        qjs_jit_emit_imm(jc, depth, 0, JS_TAG_BOOL);
        break;
    case OP_push_true:
        qjs_jit_emit_imm(jc, depth, 1, JS_TAG_BOOL);
        break;

    /* --- stack manipulation --- */
    case OP_drop:
        qjs_jit_emit_free(jc, JIT_STACK, top, 0);
        break;
    case OP_dup:
        qjs_jit_emit_copy(jc, JIT_STACK, depth, JIT_STACK, top);
        qjs_jit_emit_incref(jc, JIT_STACK, depth);
        break;
    case OP_nip:
        qjs_jit_emit_free(jc, JIT_STACK, top - 1, 0);
        qjs_jit_emit_copy(jc, JIT_STACK, top - 1, JIT_STACK, top);
        break;
    case OP_swap:
        qjs_jit_call_fss(jc, (void *)qjs_jh_swap, top - 1, top);
        break;

    /* --- locals (inline copy + refcount) --- */
    case OP_get_loc:
        qjs_jit_emit_copy(jc, JIT_STACK, depth, JIT_VAR, get_u16(pc));
        qjs_jit_emit_incref(jc, JIT_STACK, depth);
        break;
    case OP_get_loc8:
        qjs_jit_emit_copy(jc, JIT_STACK, depth, JIT_VAR, pc[0]);
        qjs_jit_emit_incref(jc, JIT_STACK, depth);
        break;
    case OP_get_loc0:
    case OP_get_loc1:
    case OP_get_loc2:
    case OP_get_loc3:
        qjs_jit_emit_copy(jc, JIT_STACK, depth, JIT_VAR, op - OP_get_loc0);
        qjs_jit_emit_incref(jc, JIT_STACK, depth);
        break;
    case OP_get_loc0_loc1:
        qjs_jit_emit_copy(jc, JIT_STACK, depth, JIT_VAR, 0);
        qjs_jit_emit_incref(jc, JIT_STACK, depth);
        qjs_jit_emit_copy(jc, JIT_STACK, depth + 1, JIT_VAR, 1);
        qjs_jit_emit_incref(jc, JIT_STACK, depth + 1);
        break;
    case OP_put_loc:
        qjs_jit_emit_put_var(jc, JIT_VAR, get_u16(pc), top, (void *)qjs_jh_put_loc);
        break;
    case OP_put_loc8:
        qjs_jit_emit_put_var(jc, JIT_VAR, pc[0], top, (void *)qjs_jh_put_loc);
        break;
    case OP_put_loc0:
    case OP_put_loc1:
    case OP_put_loc2:
    case OP_put_loc3:
        qjs_jit_emit_put_var(jc, JIT_VAR, op - OP_put_loc0, top, (void *)qjs_jh_put_loc);
        break;
    case OP_set_loc:
        qjs_jit_emit_set_var(jc, JIT_VAR, get_u16(pc), top, (void *)qjs_jh_set_loc);
        break;
    case OP_set_loc8:
        qjs_jit_emit_set_var(jc, JIT_VAR, pc[0], top, (void *)qjs_jh_set_loc);
        break;
    case OP_set_loc0:
    case OP_set_loc1:
    case OP_set_loc2:
    case OP_set_loc3:
        qjs_jit_emit_set_var(jc, JIT_VAR, op - OP_set_loc0, top, (void *)qjs_jh_set_loc);
        break;
    case OP_set_loc_uninitialized:
        qjs_jit_call_fss(jc, (void *)qjs_jh_set_loc_uninit, get_u16(pc), 0);
        break;
    case OP_get_loc_check:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_loc_check, qjs_jit_pack2(depth, get_u16(pc)),
                             pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_put_loc_check:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_put_loc_check, qjs_jit_pack2(get_u16(pc), top),
                             pos);
        qjs_jit_check_exception(jc, depth);
        break;

    /* --- args --- */
    case OP_get_arg:
        qjs_jit_call_fss(jc, (void *)qjs_jh_get_arg, depth, get_u16(pc));
        break;
    case OP_get_arg0:
    case OP_get_arg1:
    case OP_get_arg2:
    case OP_get_arg3:
        qjs_jit_call_fss(jc, (void *)qjs_jh_get_arg, depth, op - OP_get_arg0);
        break;
    case OP_put_arg:
        qjs_jit_call_fss(jc, (void *)qjs_jh_put_arg, get_u16(pc), top);
        break;
    case OP_set_arg:
        qjs_jit_call_fss(jc, (void *)qjs_jh_set_arg, get_u16(pc), top);
        break;

    case OP_nop:
        break;

    /* --- branches --- */
    case OP_if_true:
    case OP_if_false:
    case OP_if_true8:
    case OP_if_false8: {
        int target, uncond, terminal;
        int cond_true = (op == OP_if_true || op == OP_if_true8);
        qjs_jit_op_delta(op, pc, pos, &target, &uncond, &terminal);
        qjs_jit_emit_branch(jc, cond_true, top, target);
        break;
    }
    case OP_goto:
    case OP_goto8:
    case OP_goto16: {
        int target, uncond, terminal;
        qjs_jit_op_delta(op, pc, pos, &target, &uncond, &terminal);
        /* backward edge: inline interrupt safe-point */
        if (target <= pos) {
            qjs_jit_emit_backedge_poll(jc, pos, jc->depth_at[pos]);
        }
        {
            struct sljit_jump *j = sljit_emit_jump(jc->compiler, SLJIT_JUMP);
            qjs_jit_add_pending(jc, j, target);
        }
        break;
    }

    /* --- calls --- */
    case OP_call:
    case OP_call0:
    case OP_call1:
    case OP_call2:
    case OP_call3: {
        int argc = (op == OP_call) ? (int)get_u16(pc) : (op - OP_call0);
        int func_slot = depth - argc - 1;
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_call, qjs_jit_pack2(func_slot, argc), pos);
        qjs_jit_check_exception(jc, depth);
        break;
    }
    case OP_call_method: {
        int argc = (int)get_u16(pc);
        int this_slot = depth - argc - 2;
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_call_method, qjs_jit_pack2(this_slot, argc), pos);
        qjs_jit_check_exception(jc, depth);
        break;
    }
    case OP_call_constructor: {
        int argc = (int)get_u16(pc);
        int func_slot = depth - argc - 2;
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_call_constructor, qjs_jit_pack2(func_slot, argc),
                             pos);
        qjs_jit_check_exception(jc, depth);
        break;
    }
    case OP_tail_call: {
        int argc = (int)get_u16(pc);
        int func_slot = depth - argc - 1;
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_call, qjs_jit_pack2(func_slot, argc), pos);
        qjs_jit_check_exception(jc, depth);
        qjs_jit_emit_return_slot(jc, func_slot); /* result at func_slot */
        break;
    }
    case OP_tail_call_method: {
        int argc = (int)get_u16(pc);
        int this_slot = depth - argc - 2;
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_call_method, qjs_jit_pack2(this_slot, argc), pos);
        qjs_jit_check_exception(jc, depth);
        qjs_jit_emit_return_slot(jc, this_slot); /* result at this_slot */
        break;
    }

    /* --- Stage 4: property / element access --- */
    case OP_get_field:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_field, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_get_length:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_length, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_get_field2:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_field2, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_put_field:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_put_field, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_get_array_el:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_array_el, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_get_array_el2:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_array_el2, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_put_array_el:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_put_array_el, top, pos);
        qjs_jit_check_exception(jc, depth);
        break;

    /* --- Stage 4: closure variables --- */
    case OP_get_var_ref:
        qjs_jit_call_fss(jc, (void *)qjs_jh_get_var_ref, depth, get_u16(pc));
        break;
    case OP_get_var_ref0:
    case OP_get_var_ref1:
    case OP_get_var_ref2:
    case OP_get_var_ref3:
        qjs_jit_call_fss(jc, (void *)qjs_jh_get_var_ref, depth, op - OP_get_var_ref0);
        break;
    case OP_put_var_ref:
        qjs_jit_call_fss(jc, (void *)qjs_jh_put_var_ref, get_u16(pc), top);
        break;
    case OP_put_var_ref0:
    case OP_put_var_ref1:
    case OP_put_var_ref2:
    case OP_put_var_ref3:
        qjs_jit_call_fss(jc, (void *)qjs_jh_put_var_ref, op - OP_put_var_ref0, top);
        break;
    case OP_set_var_ref:
        qjs_jit_call_fss(jc, (void *)qjs_jh_set_var_ref, get_u16(pc), top);
        break;
    case OP_set_var_ref0:
    case OP_set_var_ref1:
    case OP_set_var_ref2:
    case OP_set_var_ref3:
        qjs_jit_call_fss(jc, (void *)qjs_jh_set_var_ref, op - OP_set_var_ref0, top);
        break;
    case OP_get_var_ref_check:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_get_var_ref_check,
                             qjs_jit_pack2(depth, get_u16(pc)), pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_put_var_ref_check:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_put_var_ref_check, qjs_jit_pack2(get_u16(pc), top),
                             pos);
        qjs_jit_check_exception(jc, depth);
        break;

    /* --- Stage 3: binary arith / logic / compare (inline int fast path) --- */
    case OP_add:
    case OP_sub:
    case OP_mul:
    case OP_div:
    case OP_mod:
    case OP_pow:
    case OP_and:
    case OP_or:
    case OP_xor:
    case OP_shl:
    case OP_sar:
    case OP_shr:
    case OP_lt:
    case OP_lte:
    case OP_gt:
    case OP_gte:
    case OP_eq:
    case OP_neq:
    case OP_strict_eq:
    case OP_strict_neq:
        qjs_jit_emit_binary(jc, op, depth, pos);
        break;
    case OP_add_loc:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_add_loc, ((sljit_sw)pc[0] << 20) | depth, pos);
        qjs_jit_check_exception(jc, depth);
        break;

    /* --- Stage 3: unary in place (inline int fast path) --- */
    case OP_inc:
    case OP_dec:
        qjs_jit_emit_incdec(jc, op == OP_dec, JIT_STACK, top, (void *)qjs_jh_unary, depth,
                            ((sljit_sw)op << 20) | pos, depth);
        break;
    case OP_neg:
    case OP_plus:
    case OP_not:
    case OP_lnot:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_unary, depth, ((sljit_sw)op << 20) | pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_post_inc:
    case OP_post_dec:
        qjs_jit_call_fss_ret(jc, (void *)qjs_jh_post_incdec, depth, ((sljit_sw)op << 20) | pos);
        qjs_jit_check_exception(jc, depth);
        break;
    case OP_inc_loc:
    case OP_dec_loc:
        qjs_jit_emit_incdec(jc, op == OP_dec_loc, JIT_VAR, pc[0], (void *)qjs_jh_incdec_loc,
                            ((sljit_sw)pc[0] << 8) | (op == OP_dec_loc), pos, depth);
        break;

    /* --- return --- */
    case OP_return:
        qjs_jit_emit_return_slot(jc, top);
        break;
    case OP_return_undef:
        qjs_jit_check(jc, sljit_emit_op1(jc->compiler, SLJIT_MOV, SLJIT_R0, 0, JIT_FRAME, 0));
        qjs_jit_check(jc, sljit_emit_op1(jc->compiler, SLJIT_MOV, SLJIT_R1, 0, JIT_OUT, 0));
        qjs_jit_check(jc, sljit_emit_op1(jc->compiler, SLJIT_MOV, SLJIT_R2, 0, SLJIT_IMM, 0));
        qjs_jit_check(jc, sljit_emit_icall(jc->compiler, SLJIT_CALL, SLJIT_ARGS3V(P, P, W),
                                           SLJIT_IMM, (sljit_sw)qjs_jh_return_undef));
        qjs_jit_check(jc, sljit_emit_return(jc->compiler, SLJIT_MOV, SLJIT_IMM, 0));
        break;

    default:
        qjs_jit_fail(jc);
        break;
    }
}

/* ================================================================== */
/* Runtime glue                                                        */
/* ================================================================== */

static void qjs_jit_runtime_init(JSRuntime *rt)
{
    memset(&rt->jit, 0, sizeof(rt->jit));
    rt->jit.mode = QJS_JIT_MODE_OFF;
    rt->jit.call_threshold = 16;
    rt->jit.backedge_threshold = 1000;
    rt->jit.code_limit = (size_t)16 * 1024 * 1024;
}

static void qjs_jit_function_freed(JSRuntime *rt, JSFunctionBytecode *b)
{
    QJSJitCode *code = b->jitc_code;
    if (!code) {
        return;
    }
    if (code->sljit_code) {
        sljit_free_code(code->sljit_code, NULL);
    }
    if (rt->jit.code_bytes >= code->code_size) {
        rt->jit.code_bytes -= code->code_size;
    }
    if (qjs_jit_thread_code_bytes >= code->code_size) {
        qjs_jit_thread_code_bytes -= code->code_size;
    }
    js_free_rt(rt, code);
    b->jitc_code = NULL;
}

static void qjs_jit_compile(JSContext *ctx, JSFunctionBytecode *b)
{
    JSRuntime *rt = ctx->rt;
    struct qjs_jit_ctx jc;
    struct sljit_compiler *compiler = NULL;
    const uint8_t *bytecode = b->byte_code_buf;
    int len = b->byte_code_len;
    int position = 0;
    void *code = NULL;
    QJSJitCode *jit_code;

    if (b->func_kind != JS_FUNC_NORMAL || len <= 0 || len >= (1 << 20)) {
        /* len bound: pc offsets are packed into the low 20 bits of a
           helper argument word. */
        b->jitc_state = QJS_JITC_UNSUPPORTED;
        rt->jit.unsupported++;
        return;
    }
    if (rt->jit.code_bytes >= rt->jit.code_limit ||
        qjs_jit_thread_code_bytes >= QJS_JIT_THREAD_CODE_LIMIT) {
        /* Over budget (per-runtime or thread-wide): do not retry. */
        b->jitc_state = QJS_JITC_FAILED;
        return;
    }

    memset(&jc, 0, sizeof(jc));
    jc.ctx = ctx;
    jc.rt = rt;
    jc.b = b;
    jc.depth_at = js_malloc_rt(rt, sizeof(int) * len);
    jc.labels = js_mallocz_rt(rt, sizeof(*jc.labels) * len);
    jc.pend_cap = len;
    jc.pend_jump = js_malloc_rt(rt, sizeof(*jc.pend_jump) * len);
    jc.pend_target = js_malloc_rt(rt, sizeof(int) * len);
    if (!jc.depth_at || !jc.labels || !jc.pend_jump || !jc.pend_target) {
        goto oom;
    }

    /* Depth pre-pass also validates support + well-formedness. */
    if (!qjs_jit_compute_depths(&jc)) {
        b->jitc_state = QJS_JITC_UNSUPPORTED;
        rt->jit.unsupported++;
        goto cleanup;
    }

    compiler = sljit_create_compiler(NULL);
    if (!compiler) {
        goto fail;
    }
    jc.compiler = compiler;

    /* 6 scratch (R0-R5), 6 saved (S0-S5). Cache the frame's base
       pointers so inline code addresses slots directly. */
    qjs_jit_check(&jc, sljit_emit_enter(compiler, 0, SLJIT_ARGS2(W, P, P), 6, 6, 0));
    qjs_jit_check(&jc, sljit_emit_op1(compiler, SLJIT_MOV, JIT_STACK, 0, SLJIT_MEM1(JIT_FRAME),
                                      offsetof(QJSJitFrame, stack_buf)));
    qjs_jit_check(&jc, sljit_emit_op1(compiler, SLJIT_MOV, JIT_VAR, 0, SLJIT_MEM1(JIT_FRAME),
                                      offsetof(QJSJitFrame, var_buf)));
    qjs_jit_check(&jc, sljit_emit_op1(compiler, SLJIT_MOV, JIT_ARG, 0, SLJIT_MEM1(JIT_FRAME),
                                      offsetof(QJSJitFrame, arg_buf)));
    qjs_jit_check(&jc, sljit_emit_op1(compiler, SLJIT_MOV, JIT_CTX, 0, SLJIT_MEM1(JIT_FRAME),
                                      offsetof(QJSJitFrame, ctx)));

    while (position < len && !jc.failed) {
        uint8_t op = bytecode[position];
        int size = short_opcode_info(op).size;
        /* Emit a label if this byte is a branch target that was reached. */
        if (jc.depth_at[position] >= 0) {
            struct sljit_label *lbl = sljit_emit_label(compiler);
            if (!lbl) {
                qjs_jit_fail(&jc);
                break;
            }
            jc.labels[position] = lbl;
        }
        if (jc.depth_at[position] < 0) {
            /* Unreachable byte between blocks — skip without emitting. */
            position += size;
            continue;
        }
        qjs_jit_lower_op(&jc, op, bytecode + position + 1, position);
        position += size;
    }

    if (jc.failed) {
        goto fail;
    }

    /* Patch pending jumps to their target labels. */
    {
        int i;
        for (i = 0; i < jc.pend_count; i++) {
            struct sljit_label *lbl = jc.labels[jc.pend_target[i]];
            if (!lbl) {
                qjs_jit_fail(&jc);
                break;
            }
            sljit_set_label(jc.pend_jump[i], lbl);
        }
    }
    if (jc.failed) {
        goto fail;
    }

    code = sljit_generate_code(compiler, 0, NULL);
    if (!code) {
        goto fail;
    }

    jit_code = js_mallocz_rt(rt, sizeof(*jit_code));
    if (!jit_code) {
        sljit_free_code(code, NULL);
        goto fail;
    }
    jit_code->entry = code;
    jit_code->sljit_code = code;
    jit_code->code_size = sljit_get_generated_code_size(compiler);

    sljit_free_compiler(compiler);
    b->jitc_code = jit_code;
    b->jitc_state = QJS_JITC_COMPILED;
    rt->jit.code_bytes += jit_code->code_size;
    qjs_jit_thread_code_bytes += jit_code->code_size;
    rt->jit.compiled++;
    goto cleanup;

fail:
    if (compiler) {
        sljit_free_compiler(compiler);
    }
    b->jitc_state = QJS_JITC_FAILED;
    rt->jit.failed++;
    goto cleanup;
oom:
    b->jitc_state = QJS_JITC_FAILED;
    rt->jit.failed++;
cleanup:
    js_free_rt(rt, jc.depth_at);
    js_free_rt(rt, jc.labels);
    js_free_rt(rt, jc.pend_jump);
    js_free_rt(rt, jc.pend_target);
}

/* ================================================================== */
/* Public control API                                                  */
/* ================================================================== */

int JS_JITSetMode(JSRuntime *rt, int mode)
{
    if (mode < QJS_JIT_MODE_OFF || mode > QJS_JIT_MODE_EAGER) {
        return -1;
    }
    rt->jit.mode = mode;
    return 0;
}

int JS_JITAvailable(void)
{
    return 1;
}

int JS_JITGetStats(JSRuntime *rt, uint64_t *compiled, uint64_t *unsupported, uint64_t *failed,
                   uint64_t *jit_calls)
{
    if (compiled) {
        *compiled = rt->jit.compiled;
    }
    if (unsupported) {
        *unsupported = rt->jit.unsupported;
    }
    if (failed) {
        *failed = rt->jit.failed;
    }
    if (jit_calls) {
        *jit_calls = rt->jit.jit_calls;
    }
    return 0;
}
