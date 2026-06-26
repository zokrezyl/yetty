/*
 * yfspy compiler — Python-subset AST -> yfsvm bytecode, in C.
 *
 * Parses the shader with the embedded libpython-free CPython parser
 * (yetty_cpython) and lowers the supported subset to a yfsvm program. This is
 * the C port of the reference Python frontend (tools/yfspy/assets/): same
 * language, same lowering, same register/label model — the two are
 * cross-checked to emit byte-identical bytecode.
 *
 * The cpython runtime headers shadow <Python.h>; this translation unit is the
 * single deliberate bridge that includes them (kept off the rest of yetty via
 * PRIVATE include dirs on the yetty_yfspy_compile target).
 */

#include <yetty/yfspy/compile.h>
#include <yetty/yfsvm/compiler.h>

/* Embedded CPython parser (private shadow headers — see CMakeLists). */
#include "Python.h"
#include "arena.h"
#include "pycore_ast.h"
#include "pycore_parser.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *yetty_cpython_last_error(void);

#define YFSPY_MAX_LABELS 1024
#define YFSPY_NOT_FOUND ((YfsvmOpcode)0xFFFFu)

struct yfspy_fixup {
    uint32_t code_index; /* absolute index into prog->code */
    int label;
};

struct codegen {
    struct yetty_yfsvm_program *prog;
    PyArena *arena;

    /* current function */
    uint32_t func_start; /* prog->code index where the function began */
    const char *local_names[YFSVM_MAX_REGISTERS];
    uint8_t local_count; /* named locals occupy r1..r_local_count */
    uint8_t first_temp;  /* 1 + local_count */
    uint8_t temp_used[YFSVM_MAX_REGISTERS];

    int label_pos[YFSPY_MAX_LABELS]; /* function-relative position, -1 until placed */
    int label_count;
    struct yfspy_fixup fixups[YFSPY_MAX_LABELS];
    int fixup_count;

    /* known top-level function names, for inter-function-call rejection */
    const char *func_names[YFSVM_MAX_FUNCTIONS];
    uint32_t func_name_count;

    int uses_x, uses_y, uses_time;
    int cur_line;

    int had_error;
    char *errbuf;
    size_t errcap;
};

/* --- errors --------------------------------------------------------------- */

static void cg_errorf(struct codegen *cg, int line, const char *fmt, ...)
{
    if (cg->had_error) {
        return; /* keep the first error */
    }
    cg->had_error = 1;
    char detail[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    if (line > 0) {
        snprintf(cg->errbuf, cg->errcap, "line %d: %s", line, detail);
    } else {
        snprintf(cg->errbuf, cg->errcap, "%s", detail);
    }
}

/* --- AST leaf helpers ----------------------------------------------------- */

static const char *ident_str(identifier id)
{
    return id ? PyUnicode_AsUTF8(id) : NULL;
}

static bool name_is(const char *name, const char *literal)
{
    return name && strcmp(name, literal) == 0;
}

/* Numeric value of a Constant node's value box. Returns 1 and sets *out for
 * int/float/bool; returns 0 for anything else (e.g. strings). */
static int const_to_double(constant value, double *out)
{
    PyObject *object = (PyObject *)value;
    if (!object) {
        return 0;
    }
    if (object->kind == PYP_FLOAT) {
        *out = object->dval;
        return 1;
    }
    if (object->kind == PYP_LONG) {
        *out = (double)object->ival;
        return 1;
    }
    if (object->kind == PYP_BOOL) {
        *out = object->ival ? 1.0 : 0.0;
        return 1;
    }
    return 0;
}

static int const_is_string(constant value)
{
    PyObject *object = (PyObject *)value;
    return object && object->kind == PYP_STR;
}

/* --- register / constant / emit ------------------------------------------- */

static uint8_t cg_alloc_temp(struct codegen *cg)
{
    for (uint8_t reg = cg->first_temp; reg < YFSVM_MAX_REGISTERS; reg++) {
        if (!cg->temp_used[reg]) {
            cg->temp_used[reg] = 1;
            return reg;
        }
    }
    cg_errorf(cg, cg->cur_line, "out of registers: expression is too complex");
    return 0;
}

static void cg_free_temp(struct codegen *cg, uint8_t reg)
{
    if (reg >= cg->first_temp) {
        cg->temp_used[reg] = 0;
    }
}

static void cg_emit(struct codegen *cg, YfsvmOpcode op, uint8_t dst, uint8_t src1, uint8_t src2,
                    uint16_t imm)
{
    if (cg->had_error) {
        return;
    }
    if (cg->prog->code_count >= YFSVM_MAX_INSTRUCTIONS) {
        cg_errorf(cg, cg->cur_line, "program too large (max %u instructions)",
                  YFSVM_MAX_INSTRUCTIONS);
        return;
    }
    cg->prog->code[cg->prog->code_count++] = yfsvm_encode(op, dst, src1, src2, imm);
}

static uint16_t cg_add_constant(struct codegen *cg, float value)
{
    for (uint32_t i = 0; i < cg->prog->constant_count; i++) {
        if (cg->prog->constants[i] == value) {
            return (uint16_t)i;
        }
    }
    if (cg->prog->constant_count >= YFSVM_MAX_CONSTANTS) {
        cg_errorf(cg, cg->cur_line, "too many constants");
        return 0;
    }
    uint16_t idx = (uint16_t)cg->prog->constant_count;
    cg->prog->constants[cg->prog->constant_count++] = value;
    return idx;
}

static uint8_t cg_load_const(struct codegen *cg, double value)
{
    uint16_t idx = cg_add_constant(cg, (float)value);
    uint8_t reg = cg_alloc_temp(cg);
    cg_emit(cg, YETTY_YFSVM_OP_LOAD_C, reg, 0, 0, idx);
    return reg;
}

/* left OP right -> fresh temp; frees both operands (if temps). */
static uint8_t cg_combine(struct codegen *cg, YfsvmOpcode op, uint8_t left, uint8_t right)
{
    uint8_t dst = cg_alloc_temp(cg);
    cg_emit(cg, op, dst, left, right, 0);
    cg_free_temp(cg, left);
    cg_free_temp(cg, right);
    return dst;
}

/* OP operand -> fresh temp; frees the operand (if temp). */
static uint8_t cg_apply(struct codegen *cg, YfsvmOpcode op, uint8_t operand)
{
    uint8_t dst = cg_alloc_temp(cg);
    cg_emit(cg, op, dst, operand, 0, 0);
    cg_free_temp(cg, operand);
    return dst;
}

/* --- labels / branches ---------------------------------------------------- */

static int cg_new_label(struct codegen *cg)
{
    if (cg->label_count >= YFSPY_MAX_LABELS) {
        cg_errorf(cg, cg->cur_line, "too many labels");
        return 0;
    }
    cg->label_pos[cg->label_count] = -1;
    return cg->label_count++;
}

static void cg_place(struct codegen *cg, int label)
{
    cg->label_pos[label] = (int)(cg->prog->code_count - cg->func_start);
}

static void cg_emit_branch(struct codegen *cg, YfsvmOpcode op, int label, uint8_t src1,
                           uint8_t src2)
{
    if (cg->had_error) {
        return;
    }
    if (cg->fixup_count >= YFSPY_MAX_LABELS) {
        cg_errorf(cg, cg->cur_line, "too many branches");
        return;
    }
    cg->fixups[cg->fixup_count].code_index = cg->prog->code_count;
    cg->fixups[cg->fixup_count].label = label;
    cg->fixup_count++;
    cg_emit(cg, op, 0, src1, src2, 0);
}

static void cg_emit_jump(struct codegen *cg, int label)
{
    cg_emit_branch(cg, YETTY_YFSVM_OP_JMP, label, 0, 0);
}

static void cg_resolve_fixups(struct codegen *cg)
{
    for (int i = 0; i < cg->fixup_count; i++) {
        int pos = cg->label_pos[cg->fixups[i].label];
        if (pos < 0) {
            cg_errorf(cg, cg->cur_line, "internal: unresolved branch label");
            return;
        }
        uint32_t word = cg->prog->code[cg->fixups[i].code_index];
        cg->prog->code[cg->fixups[i].code_index] =
            yfsvm_encode(yfsvm_decode_opcode(word), yfsvm_decode_dst(word), yfsvm_decode_src1(word),
                         yfsvm_decode_src2(word), (uint16_t)pos);
    }
}

/* --- builtin tables (static const locals, never file scope) --------------- */

struct func_entry {
    const char *name;
    YfsvmOpcode op;
};

static YfsvmOpcode lookup_func1(const char *name)
{
    static const struct func_entry table[] = {
        {"sin", YETTY_YFSVM_OP_SIN},
        {"cos", YETTY_YFSVM_OP_COS},
        {"tan", YETTY_YFSVM_OP_TAN},
        {"asin", YETTY_YFSVM_OP_ASIN},
        {"acos", YETTY_YFSVM_OP_ACOS},
        {"atan", YETTY_YFSVM_OP_ATAN},
        {"sinh", YETTY_YFSVM_OP_SINH},
        {"cosh", YETTY_YFSVM_OP_COSH},
        {"tanh", YETTY_YFSVM_OP_TANH},
        {"asinh", YETTY_YFSVM_OP_ASINH},
        {"acosh", YETTY_YFSVM_OP_ACOSH},
        {"atanh", YETTY_YFSVM_OP_ATANH},
        {"sinc", YETTY_YFSVM_OP_SINC},
        {"exp", YETTY_YFSVM_OP_EXP},
        {"exp2", YETTY_YFSVM_OP_EXP2},
        {"log", YETTY_YFSVM_OP_LOG},
        {"ln", YETTY_YFSVM_OP_LOG},
        {"log2", YETTY_YFSVM_OP_LOG2},
        {"sqrt", YETTY_YFSVM_OP_SQRT},
        {"rsqrt", YETTY_YFSVM_OP_RSQRT},
        {"inverseSqrt", YETTY_YFSVM_OP_RSQRT},
        {"abs", YETTY_YFSVM_OP_ABS},
        {"floor", YETTY_YFSVM_OP_FLOOR},
        {"ceil", YETTY_YFSVM_OP_CEIL},
        {"round", YETTY_YFSVM_OP_ROUND},
        {"trunc", YETTY_YFSVM_OP_TRUNC},
        {"fract", YETTY_YFSVM_OP_FRACT},
        {"frac", YETTY_YFSVM_OP_FRACT},
        {"sign", YETTY_YFSVM_OP_SIGN},
        {"saturate", YETTY_YFSVM_OP_CLAMP01},
        {"radians", YETTY_YFSVM_OP_RADIANS},
        {"degrees", YETTY_YFSVM_OP_DEGREES},
        {"erf", YETTY_YFSVM_OP_ERF},
        {"erfc", YETTY_YFSVM_OP_ERFC},
        {"rand", YETTY_YFSVM_OP_RAND},
        {"noise", YETTY_YFSVM_OP_NOISE},
        {NULL, 0},
    };
    for (const struct func_entry *entry = table; entry->name; entry++) {
        if (strcmp(name, entry->name) == 0) {
            return entry->op;
        }
    }
    return YFSPY_NOT_FOUND;
}

static YfsvmOpcode lookup_func2(const char *name)
{
    static const struct func_entry table[] = {
        {"pow", YETTY_YFSVM_OP_POW},       {"atan2", YETTY_YFSVM_OP_ATAN2},
        {"min", YETTY_YFSVM_OP_MIN},       {"max", YETTY_YFSVM_OP_MAX},
        {"mod", YETTY_YFSVM_OP_MOD},       {"fmod", YETTY_YFSVM_OP_MOD},
        {"step", YETTY_YFSVM_OP_STEP},     {"lt", YETTY_YFSVM_OP_LT},
        {"gt", YETTY_YFSVM_OP_GT},         {"le", YETTY_YFSVM_OP_LE},
        {"ge", YETTY_YFSVM_OP_GE},         {"eq", YETTY_YFSVM_OP_EQ},
        {"ne", YETTY_YFSVM_OP_NE},         {"rand2", YETTY_YFSVM_OP_RAND2},
        {"noise2", YETTY_YFSVM_OP_NOISE2}, {NULL, 0},
    };
    for (const struct func_entry *entry = table; entry->name; entry++) {
        if (strcmp(name, entry->name) == 0) {
            return entry->op;
        }
    }
    return YFSPY_NOT_FOUND;
}

static int named_constant(const char *name, double *out)
{
    if (name_is(name, "pi") || name_is(name, "PI")) {
        *out = 3.14159265358979323846;
        return 1;
    }
    if (name_is(name, "e") || name_is(name, "E")) {
        *out = 2.71828182845904523536;
        return 1;
    }
    if (name_is(name, "tau") || name_is(name, "TAU")) {
        *out = 6.28318530717958647693;
        return 1;
    }
    return 0;
}

/* sampler index for "s0".."s7", or -1. */
static int sampler_index(const char *name)
{
    if (name && name[0] == 's' && name[1] >= '0' && name[1] <= '7' && name[2] == '\0') {
        return name[1] - '0';
    }
    return -1;
}

/* --- forward decls -------------------------------------------------------- */

static uint8_t compile_expr(struct codegen *cg, expr_ty node);
static uint8_t compile_bool(struct codegen *cg, expr_ty node);
static void emit_jump_if_false(struct codegen *cg, expr_ty test, int target);
static void compile_stmt(struct codegen *cg, stmt_ty node);

/* --- expressions ---------------------------------------------------------- */

static int local_reg(struct codegen *cg, const char *name)
{
    for (uint8_t i = 0; i < cg->local_count; i++) {
        if (strcmp(cg->local_names[i], name) == 0) {
            return (uint8_t)(1 + i);
        }
    }
    return -1;
}

/* Returns LOAD_X/Y/T/S opcode for a bare reserved input name, else NOT_FOUND.
 * For LOAD_S, *sampler is set to the slot. */
static YfsvmOpcode bare_input(const char *name, int *sampler)
{
    *sampler = 0;
    if (name_is(name, "x")) {
        return YETTY_YFSVM_OP_LOAD_X;
    }
    if (name_is(name, "y")) {
        return YETTY_YFSVM_OP_LOAD_Y;
    }
    if (name_is(name, "t") || name_is(name, "time")) {
        return YETTY_YFSVM_OP_LOAD_T;
    }
    int slot = sampler_index(name);
    if (slot >= 0) {
        *sampler = slot;
        return YETTY_YFSVM_OP_LOAD_S;
    }
    return YFSPY_NOT_FOUND;
}

static uint8_t resolve_name(struct codegen *cg, expr_ty node)
{
    const char *name = ident_str(node->v.Name.id);
    int reg = local_reg(cg, name);
    if (reg >= 0) {
        return (uint8_t)reg;
    }
    double constant_value;
    if (named_constant(name, &constant_value)) {
        return cg_load_const(cg, constant_value);
    }
    int sampler;
    YfsvmOpcode load = bare_input(name, &sampler);
    if (load != YFSPY_NOT_FOUND) {
        uint8_t dst = cg_alloc_temp(cg);
        if (load == YETTY_YFSVM_OP_LOAD_S) {
            cg_emit(cg, load, dst, 0, 0, (uint16_t)sampler);
        } else {
            cg_emit(cg, load, dst, 0, 0, 0);
            if (load == YETTY_YFSVM_OP_LOAD_X) {
                cg->uses_x = 1;
            } else if (load == YETTY_YFSVM_OP_LOAD_Y) {
                cg->uses_y = 1;
            } else if (load == YETTY_YFSVM_OP_LOAD_T) {
                cg->uses_time = 1;
            }
        }
        return dst;
    }
    cg_errorf(cg, cg->cur_line, "unknown identifier: %s", name ? name : "?");
    return 0;
}

static int binop_opcode(operator_ty op, YfsvmOpcode *out)
{
    switch (op) {
    case Add:
        *out = YETTY_YFSVM_OP_ADD;
        return 1;
    case Sub:
        *out = YETTY_YFSVM_OP_SUB;
        return 1;
    case Mult:
        *out = YETTY_YFSVM_OP_MUL;
        return 1;
    case Div:
        *out = YETTY_YFSVM_OP_DIV;
        return 1;
    case Mod:
        *out = YETTY_YFSVM_OP_MOD;
        return 1;
    case Pow:
        *out = YETTY_YFSVM_OP_POW;
        return 1;
    default:
        return 0;
    }
}

static int compare_opcode(int cmpop, YfsvmOpcode *out)
{
    switch (cmpop) {
    case Lt:
        *out = YETTY_YFSVM_OP_LT;
        return 1;
    case Gt:
        *out = YETTY_YFSVM_OP_GT;
        return 1;
    case LtE:
        *out = YETTY_YFSVM_OP_LE;
        return 1;
    case GtE:
        *out = YETTY_YFSVM_OP_GE;
        return 1;
    case Eq:
        *out = YETTY_YFSVM_OP_EQ;
        return 1;
    case NotEq:
        *out = YETTY_YFSVM_OP_NE;
        return 1;
    default:
        return 0;
    }
}

/* --- macros (port of frontend _macro_*) ----------------------------------- */

static uint8_t cg_mul_const(struct codegen *cg, uint8_t reg, double value)
{
    uint8_t constant = cg_load_const(cg, value);
    return cg_combine(cg, YETTY_YFSVM_OP_MUL, reg, constant);
}

static uint8_t macro_length2(struct codegen *cg, uint8_t x_reg, uint8_t y_reg)
{
    uint8_t xx = cg_alloc_temp(cg);
    cg_emit(cg, YETTY_YFSVM_OP_MUL, xx, x_reg, x_reg, 0);
    cg_free_temp(cg, x_reg);
    uint8_t yy = cg_alloc_temp(cg);
    cg_emit(cg, YETTY_YFSVM_OP_MUL, yy, y_reg, y_reg, 0);
    cg_free_temp(cg, y_reg);
    uint8_t total = cg_combine(cg, YETTY_YFSVM_OP_ADD, xx, yy);
    return cg_apply(cg, YETTY_YFSVM_OP_SQRT, total);
}

static int expect_argc(struct codegen *cg, const char *name, int got, int want)
{
    if (got != want) {
        cg_errorf(cg, cg->cur_line, "%s() takes %d arguments, got %d", name, want, got);
        return 0;
    }
    return 1;
}

static int const_int_arg(struct codegen *cg, expr_ty node, int *out)
{
    double value;
    if (node->kind == Constant_kind && const_to_double(node->v.Constant.value, &value)) {
        /* ok */
    } else if (node->kind == UnaryOp_kind && node->v.UnaryOp.op == USub &&
               node->v.UnaryOp.operand->kind == Constant_kind &&
               const_to_double(node->v.UnaryOp.operand->v.Constant.value, &value)) {
        value = -value;
    } else {
        cg_errorf(cg, cg->cur_line, "expected a constant integer argument");
        return 0;
    }
    if (value != (double)(int)value) {
        cg_errorf(cg, cg->cur_line, "expected a whole-number argument");
        return 0;
    }
    *out = (int)value;
    return 1;
}

static uint8_t compile_macro(struct codegen *cg, const char *name, asdl_expr_seq *args)
{
    int argc = (int)asdl_seq_LEN(args);

    if (name_is(name, "length2") || name_is(name, "polar_radius")) {
        if (!expect_argc(cg, name, argc, 2)) {
            return 0;
        }
        return macro_length2(cg, compile_expr(cg, asdl_seq_GET(args, 0)),
                             compile_expr(cg, asdl_seq_GET(args, 1)));
    }
    if (name_is(name, "polar_angle")) {
        if (!expect_argc(cg, name, argc, 2)) {
            return 0;
        }
        uint8_t x = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t y = compile_expr(cg, asdl_seq_GET(args, 1));
        return cg_combine(cg, YETTY_YFSVM_OP_ATAN2, y, x);
    }
    if (name_is(name, "dist2")) {
        if (!expect_argc(cg, name, argc, 4)) {
            return 0;
        }
        uint8_t ax = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t ay = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t bx = compile_expr(cg, asdl_seq_GET(args, 2));
        uint8_t by = compile_expr(cg, asdl_seq_GET(args, 3));
        uint8_t dx = cg_combine(cg, YETTY_YFSVM_OP_SUB, ax, bx);
        uint8_t dy = cg_combine(cg, YETTY_YFSVM_OP_SUB, ay, by);
        return macro_length2(cg, dx, dy);
    }
    if (name_is(name, "safe_div")) {
        if (!expect_argc(cg, name, argc, 3)) {
            return 0;
        }
        uint8_t numerator = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t denominator = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t fallback = compile_expr(cg, asdl_seq_GET(args, 2));
        uint8_t quotient = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_DIV, quotient, numerator, denominator, 0);
        uint8_t magnitude = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_ABS, magnitude, denominator, 0, 0);
        cg_free_temp(cg, numerator);
        cg_free_temp(cg, denominator);
        uint8_t eps = cg_load_const(cg, 1e-12);
        uint8_t condition = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_GT, condition, magnitude, eps, 0);
        cg_free_temp(cg, magnitude);
        cg_free_temp(cg, eps);
        uint8_t dst = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_SELECT, dst, fallback, quotient, (uint16_t)(condition & 0xF));
        cg_free_temp(cg, quotient);
        cg_free_temp(cg, fallback);
        cg_free_temp(cg, condition);
        return dst;
    }
    if (name_is(name, "safe_log")) {
        if (!expect_argc(cg, name, argc, 1)) {
            return 0;
        }
        uint8_t value = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t eps = cg_load_const(cg, 1e-12);
        uint8_t guarded = cg_combine(cg, YETTY_YFSVM_OP_MAX, value, eps);
        return cg_apply(cg, YETTY_YFSVM_OP_LOG, guarded);
    }
    if (name_is(name, "safe_sqrt")) {
        if (!expect_argc(cg, name, argc, 1)) {
            return 0;
        }
        uint8_t value = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t zero = cg_load_const(cg, 0.0);
        uint8_t guarded = cg_combine(cg, YETTY_YFSVM_OP_MAX, value, zero);
        return cg_apply(cg, YETTY_YFSVM_OP_SQRT, guarded);
    }
    if (name_is(name, "remap")) {
        if (!expect_argc(cg, name, argc, 5)) {
            return 0;
        }
        uint8_t value = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t in0 = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t in1 = compile_expr(cg, asdl_seq_GET(args, 2));
        uint8_t out0 = compile_expr(cg, asdl_seq_GET(args, 3));
        uint8_t out1 = compile_expr(cg, asdl_seq_GET(args, 4));
        uint8_t numerator = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_SUB, numerator, value, in0, 0);
        cg_free_temp(cg, value);
        uint8_t in_span = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_SUB, in_span, in1, in0, 0);
        cg_free_temp(cg, in1);
        cg_free_temp(cg, in0);
        uint8_t out_span = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_SUB, out_span, out1, out0, 0);
        cg_free_temp(cg, out1);
        uint8_t ratio = cg_combine(cg, YETTY_YFSVM_OP_DIV, numerator, in_span);
        uint8_t scaled = cg_combine(cg, YETTY_YFSVM_OP_MUL, ratio, out_span);
        return cg_combine(cg, YETTY_YFSVM_OP_ADD, out0, scaled);
    }
    if (name_is(name, "remap01")) {
        if (!expect_argc(cg, name, argc, 3)) {
            return 0;
        }
        uint8_t value = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t low = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t high = compile_expr(cg, asdl_seq_GET(args, 2));
        uint8_t numerator = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_SUB, numerator, value, low, 0);
        cg_free_temp(cg, value);
        uint8_t span = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_SUB, span, high, low, 0);
        cg_free_temp(cg, high);
        cg_free_temp(cg, low);
        uint8_t ratio = cg_combine(cg, YETTY_YFSVM_OP_DIV, numerator, span);
        return cg_apply(cg, YETTY_YFSVM_OP_CLAMP01, ratio);
    }
    if (name_is(name, "checker")) {
        if (!expect_argc(cg, name, argc, 3)) {
            return 0;
        }
        uint8_t x = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t y = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t scale = compile_expr(cg, asdl_seq_GET(args, 2));
        uint8_t scaled_x = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_MUL, scaled_x, x, scale, 0);
        cg_free_temp(cg, x);
        uint8_t scaled_y = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_MUL, scaled_y, y, scale, 0);
        cg_free_temp(cg, y);
        cg_free_temp(cg, scale);
        uint8_t floor_x = cg_apply(cg, YETTY_YFSVM_OP_FLOOR, scaled_x);
        uint8_t floor_y = cg_apply(cg, YETTY_YFSVM_OP_FLOOR, scaled_y);
        uint8_t total = cg_combine(cg, YETTY_YFSVM_OP_ADD, floor_x, floor_y);
        uint8_t two = cg_load_const(cg, 2.0);
        return cg_combine(cg, YETTY_YFSVM_OP_MOD, total, two);
    }
    if (name_is(name, "stripe")) {
        if (!expect_argc(cg, name, argc, 2)) {
            return 0;
        }
        uint8_t x = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t scale = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t scaled = cg_combine(cg, YETTY_YFSVM_OP_MUL, x, scale);
        uint8_t fraction = cg_apply(cg, YETTY_YFSVM_OP_FRACT, scaled);
        uint8_t edge = cg_load_const(cg, 0.5);
        return cg_combine(cg, YETTY_YFSVM_OP_STEP, edge, fraction);
    }
    if (name_is(name, "domain_warp_x") || name_is(name, "domain_warp_y")) {
        int is_x = name_is(name, "domain_warp_x");
        if (!expect_argc(cg, name, argc, 3)) {
            return 0;
        }
        double seed_x = is_x ? 0.0 : 5.2;
        double seed_y = is_x ? 0.0 : 1.3;
        uint8_t x = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t y = compile_expr(cg, asdl_seq_GET(args, 1));
        uint8_t amount = compile_expr(cg, asdl_seq_GET(args, 2));
        uint8_t seed_x_reg = cg_load_const(cg, seed_x);
        uint8_t warp_x = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_ADD, warp_x, x, seed_x_reg, 0);
        cg_free_temp(cg, seed_x_reg);
        uint8_t seed_y_reg = cg_load_const(cg, seed_y);
        uint8_t warp_y = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_ADD, warp_y, y, seed_y_reg, 0);
        cg_free_temp(cg, seed_y_reg);
        uint8_t noise = cg_combine(cg, YETTY_YFSVM_OP_NOISE2, warp_x, warp_y);
        uint8_t delta = cg_combine(cg, YETTY_YFSVM_OP_MUL, noise, amount);
        uint8_t base = is_x ? x : y;
        uint8_t other = is_x ? y : x;
        cg_free_temp(cg, other);
        return cg_combine(cg, YETTY_YFSVM_OP_ADD, base, delta);
    }
    if (name_is(name, "fbm2") || name_is(name, "ridge2")) {
        int ridged = name_is(name, "ridge2");
        if (!expect_argc(cg, name, argc, 3)) {
            return 0;
        }
        uint8_t x = compile_expr(cg, asdl_seq_GET(args, 0));
        uint8_t y = compile_expr(cg, asdl_seq_GET(args, 1));
        int octaves;
        if (!const_int_arg(cg, asdl_seq_GET(args, 2), &octaves)) {
            return 0;
        }
        if (octaves < 1 || octaves > 8) {
            cg_errorf(cg, cg->cur_line, "%s() octaves must be between 1 and 8", name);
            return 0;
        }
        uint8_t accumulator = cg_load_const(cg, 0.0);
        double norm = 0.0;
        double amplitude = 0.5;
        double frequency = 1.0;
        for (int octave = 0; octave < octaves; octave++) {
            uint8_t frequency_reg = cg_load_const(cg, frequency);
            uint8_t sample_x = cg_alloc_temp(cg);
            cg_emit(cg, YETTY_YFSVM_OP_MUL, sample_x, x, frequency_reg, 0);
            uint8_t sample_y = cg_alloc_temp(cg);
            cg_emit(cg, YETTY_YFSVM_OP_MUL, sample_y, y, frequency_reg, 0);
            cg_free_temp(cg, frequency_reg);
            uint8_t noise = cg_combine(cg, YETTY_YFSVM_OP_NOISE2, sample_x, sample_y);
            if (ridged) {
                uint8_t shifted = cg_mul_const(cg, noise, 2.0);
                uint8_t one = cg_load_const(cg, 1.0);
                uint8_t centered = cg_alloc_temp(cg);
                cg_emit(cg, YETTY_YFSVM_OP_SUB, centered, shifted, one, 0);
                cg_free_temp(cg, shifted);
                uint8_t magnitude = cg_apply(cg, YETTY_YFSVM_OP_ABS, centered);
                noise = cg_combine(cg, YETTY_YFSVM_OP_SUB, one, magnitude);
            }
            uint8_t contribution = cg_mul_const(cg, noise, amplitude);
            accumulator = cg_combine(cg, YETTY_YFSVM_OP_ADD, accumulator, contribution);
            norm += amplitude;
            amplitude *= 0.5;
            frequency *= 2.0;
        }
        cg_free_temp(cg, x);
        cg_free_temp(cg, y);
        return cg_mul_const(cg, accumulator, 1.0 / norm);
    }
    cg_errorf(cg, cg->cur_line, "unknown macro: %s", name);
    return 0;
}

static int is_macro_name(const char *name)
{
    static const char *const macros[] = {
        "safe_div",      "safe_log",     "safe_sqrt",   "length2", "dist2",  "remap",
        "remap01",       "polar_radius", "polar_angle", "fbm2",    "ridge2", "domain_warp_x",
        "domain_warp_y", "checker",      "stripe",      NULL,
    };
    for (const char *const *macro = macros; *macro; macro++) {
        if (strcmp(name, *macro) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_function_name(struct codegen *cg, const char *name)
{
    for (uint32_t i = 0; i < cg->func_name_count; i++) {
        if (strcmp(cg->func_names[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static uint8_t compile_three_arg(struct codegen *cg, const char *name, asdl_expr_seq *args)
{
    if (!expect_argc(cg, name, (int)asdl_seq_LEN(args), 3)) {
        return 0;
    }
    uint8_t first = compile_expr(cg, asdl_seq_GET(args, 0));
    uint8_t second = compile_expr(cg, asdl_seq_GET(args, 1));
    uint8_t third = compile_expr(cg, asdl_seq_GET(args, 2));
    uint8_t dst = cg_alloc_temp(cg);
    if (name_is(name, "mix") || name_is(name, "lerp")) {
        cg_emit(cg, YETTY_YFSVM_OP_MIX, dst, first, second, (uint16_t)(third & 0xF));
    } else if (name_is(name, "smoothstep")) {
        cg_emit(cg, YETTY_YFSVM_OP_SMOOTHSTEP, dst, first, second, (uint16_t)(third & 0xF));
    } else if (name_is(name, "select")) {
        cg_emit(cg, YETTY_YFSVM_OP_SELECT, dst, first, second, (uint16_t)(third & 0xF));
    } else { /* clamp(value, low, high) = min(max(value, low), high) */
        uint8_t lower = cg_alloc_temp(cg);
        cg_emit(cg, YETTY_YFSVM_OP_MAX, lower, first, second, 0);
        cg_emit(cg, YETTY_YFSVM_OP_MIN, dst, lower, third, 0);
        cg_free_temp(cg, lower);
    }
    cg_free_temp(cg, first);
    cg_free_temp(cg, second);
    cg_free_temp(cg, third);
    return dst;
}

static int is_three_arg(const char *name)
{
    return name_is(name, "mix") || name_is(name, "lerp") || name_is(name, "smoothstep") ||
           name_is(name, "select") || name_is(name, "clamp");
}

static uint8_t compile_call(struct codegen *cg, expr_ty node)
{
    if (node->v.Call.func->kind != Name_kind) {
        cg_errorf(cg, cg->cur_line,
                  "only direct calls to builtins are supported (no attribute or method calls)");
        return 0;
    }
    const char *name = ident_str(node->v.Call.func->v.Name.id);
    if (asdl_seq_LEN(node->v.Call.keywords) > 0) {
        cg_errorf(cg, cg->cur_line, "keyword arguments are not supported");
        return 0;
    }
    asdl_expr_seq *args = node->v.Call.args;
    int argc = (int)asdl_seq_LEN(args);
    for (int i = 0; i < argc; i++) {
        if (((expr_ty)asdl_seq_GET(args, i))->kind == Starred_kind) {
            cg_errorf(cg, cg->cur_line, "argument unpacking is not supported");
            return 0;
        }
    }

    if (is_function_name(cg, name)) {
        cg_errorf(cg, cg->cur_line,
                  "the VM has no call instruction: functions are independent entry "
                  "points and cannot call one another");
        return 0;
    }
    if (name_is(name, "range")) {
        cg_errorf(cg, cg->cur_line, "range() is only valid as the iterable of a for-loop");
        return 0;
    }

    if (is_macro_name(name)) {
        return compile_macro(cg, name, args);
    }
    if (is_three_arg(name)) {
        return compile_three_arg(cg, name, args);
    }
    if (argc == 1) {
        YfsvmOpcode op = lookup_func1(name);
        if (op != YFSPY_NOT_FOUND) {
            return cg_apply(cg, op, compile_expr(cg, asdl_seq_GET(args, 0)));
        }
    }
    if (argc == 2) {
        YfsvmOpcode op = lookup_func2(name);
        if (op != YFSPY_NOT_FOUND) {
            uint8_t left = compile_expr(cg, asdl_seq_GET(args, 0));
            uint8_t right = compile_expr(cg, asdl_seq_GET(args, 1));
            return cg_combine(cg, op, left, right);
        }
    }
    if (lookup_func1(name) != YFSPY_NOT_FOUND) {
        cg_errorf(cg, cg->cur_line, "%s() takes 1 argument, got %d", name, argc);
        return 0;
    }
    if (lookup_func2(name) != YFSPY_NOT_FOUND) {
        cg_errorf(cg, cg->cur_line, "%s() takes 2 arguments, got %d", name, argc);
        return 0;
    }
    cg_errorf(cg, cg->cur_line, "unknown function: %s", name ? name : "?");
    return 0;
}

static uint8_t compile_ifexp(struct codegen *cg, expr_ty node)
{
    uint8_t condition = compile_bool(cg, node->v.IfExp.test);
    uint8_t if_true = compile_expr(cg, node->v.IfExp.body);
    uint8_t if_false = compile_expr(cg, node->v.IfExp.orelse);
    uint8_t dst = cg_alloc_temp(cg);
    cg_emit(cg, YETTY_YFSVM_OP_SELECT, dst, if_false, if_true, (uint16_t)(condition & 0xF));
    cg_free_temp(cg, if_true);
    cg_free_temp(cg, if_false);
    cg_free_temp(cg, condition);
    return dst;
}

static uint8_t compile_expr(struct codegen *cg, expr_ty node)
{
    if (cg->had_error) {
        return 0;
    }
    cg->cur_line = node->lineno;
    switch (node->kind) {
    case Constant_kind: {
        double value;
        if (((PyObject *)node->v.Constant.value) &&
            ((PyObject *)node->v.Constant.value)->kind == PYP_BOOL) {
            return cg_load_const(cg, ((PyObject *)node->v.Constant.value)->ival ? 1.0 : 0.0);
        }
        if (const_to_double(node->v.Constant.value, &value)) {
            return cg_load_const(cg, value);
        }
        cg_errorf(cg, cg->cur_line, "only numeric literals are supported");
        return 0;
    }
    case Name_kind:
        return resolve_name(cg, node);
    case BinOp_kind: {
        YfsvmOpcode op;
        if (!binop_opcode(node->v.BinOp.op, &op)) {
            cg_errorf(cg, cg->cur_line, "unsupported operator");
            return 0;
        }
        uint8_t left = compile_expr(cg, node->v.BinOp.left);
        uint8_t right = compile_expr(cg, node->v.BinOp.right);
        return cg_combine(cg, op, left, right);
    }
    case UnaryOp_kind:
        if (node->v.UnaryOp.op == USub) {
            return cg_apply(cg, YETTY_YFSVM_OP_NEG, compile_expr(cg, node->v.UnaryOp.operand));
        }
        if (node->v.UnaryOp.op == UAdd) {
            return compile_expr(cg, node->v.UnaryOp.operand);
        }
        if (node->v.UnaryOp.op == Not) {
            return compile_bool(cg, node);
        }
        cg_errorf(cg, cg->cur_line, "unsupported unary operator");
        return 0;
    case Compare_kind:
    case BoolOp_kind:
        return compile_bool(cg, node);
    case Call_kind:
        return compile_call(cg, node);
    case IfExp_kind:
        return compile_ifexp(cg, node);
    default:
        cg_errorf(cg, cg->cur_line, "unsupported expression");
        return 0;
    }
}

/* --- conditions ----------------------------------------------------------- */

static uint8_t single_compare(struct codegen *cg, int cmpop, expr_ty left, expr_ty right)
{
    YfsvmOpcode op;
    if (!compare_opcode(cmpop, &op)) {
        cg_errorf(cg, cg->cur_line, "unsupported comparison");
        return 0;
    }
    uint8_t a = compile_expr(cg, left);
    uint8_t b = compile_expr(cg, right);
    return cg_combine(cg, op, a, b);
}

static uint8_t compile_compare(struct codegen *cg, expr_ty node)
{
    asdl_int_seq *ops = node->v.Compare.ops;
    asdl_expr_seq *comparators = node->v.Compare.comparators;
    int count = (int)asdl_seq_LEN(ops);
    expr_ty left = node->v.Compare.left;
    int have_result = 0;
    uint8_t result = 0;
    for (int i = 0; i < count; i++) {
        int cmpop = (int)asdl_seq_GET(ops, i);
        expr_ty right = asdl_seq_GET(comparators, i);
        uint8_t pair = single_compare(cg, cmpop, left, right);
        if (!have_result) {
            result = pair;
            have_result = 1;
        } else {
            result = cg_combine(cg, YETTY_YFSVM_OP_MUL, result, pair);
        }
        left = right;
    }
    return result;
}

static uint8_t compile_bool(struct codegen *cg, expr_ty node)
{
    if (cg->had_error) {
        return 0;
    }
    if (node->kind == Compare_kind) {
        return compile_compare(cg, node);
    }
    if (node->kind == BoolOp_kind) {
        YfsvmOpcode fold = (node->v.BoolOp.op == And) ? YETTY_YFSVM_OP_MUL : YETTY_YFSVM_OP_MAX;
        asdl_expr_seq *values = node->v.BoolOp.values;
        int count = (int)asdl_seq_LEN(values);
        uint8_t accumulator = compile_bool(cg, asdl_seq_GET(values, 0));
        for (int i = 1; i < count; i++) {
            uint8_t operand = compile_bool(cg, asdl_seq_GET(values, i));
            accumulator = cg_combine(cg, fold, accumulator, operand);
        }
        return accumulator;
    }
    if (node->kind == UnaryOp_kind && node->v.UnaryOp.op == Not) {
        uint8_t inner = compile_bool(cg, node->v.UnaryOp.operand);
        uint8_t one = cg_load_const(cg, 1.0);
        return cg_combine(cg, YETTY_YFSVM_OP_SUB, one, inner);
    }
    /* general expression: truthiness is "value != 0" */
    uint8_t value_reg = compile_expr(cg, node);
    uint8_t zero = cg_load_const(cg, 0.0);
    return cg_combine(cg, YETTY_YFSVM_OP_NE, value_reg, zero);
}

static void emit_jump_if_false(struct codegen *cg, expr_ty test, int target)
{
    if (test->kind == Compare_kind && asdl_seq_LEN(test->v.Compare.ops) == 1) {
        int cmpop = (int)asdl_seq_GET(test->v.Compare.ops, 0);
        expr_ty left = test->v.Compare.left;
        expr_ty right = asdl_seq_GET(test->v.Compare.comparators, 0);
        uint8_t a, b;
        switch (cmpop) {
        case Lt: /* false when a >= b */
            a = compile_expr(cg, left);
            b = compile_expr(cg, right);
            cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_GE, target, a, b);
            cg_free_temp(cg, a);
            cg_free_temp(cg, b);
            return;
        case Gt: /* a > b false when b >= a */
            a = compile_expr(cg, left);
            b = compile_expr(cg, right);
            cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_GE, target, b, a);
            cg_free_temp(cg, a);
            cg_free_temp(cg, b);
            return;
        case GtE: /* false when a < b */
            a = compile_expr(cg, left);
            b = compile_expr(cg, right);
            cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_LT, target, a, b);
            cg_free_temp(cg, a);
            cg_free_temp(cg, b);
            return;
        case LtE: /* a <= b false when b < a */
            a = compile_expr(cg, left);
            b = compile_expr(cg, right);
            cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_LT, target, b, a);
            cg_free_temp(cg, a);
            cg_free_temp(cg, b);
            return;
        default:
            break; /* Eq / NotEq fall through to the general path */
        }
    }
    uint8_t condition = compile_bool(cg, test);
    cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_ZERO, target, condition, 0);
    cg_free_temp(cg, condition);
}

/* --- statements ----------------------------------------------------------- */

static void compile_body(struct codegen *cg, asdl_stmt_seq *body)
{
    int count = (int)asdl_seq_LEN(body);
    for (int i = 0; i < count && !cg->had_error; i++) {
        compile_stmt(cg, asdl_seq_GET(body, i));
    }
}

static int const_int_for_range(struct codegen *cg, expr_ty node, int *out)
{
    return const_int_arg(cg, node, out);
}

static void compile_for(struct codegen *cg, stmt_ty node)
{
    if (asdl_seq_LEN(node->v.For.orelse) > 0) {
        cg_errorf(cg, cg->cur_line, "for-else is not supported");
        return;
    }
    if (node->v.For.target->kind != Name_kind) {
        cg_errorf(cg, cg->cur_line, "for-loop variable must be a simple name");
        return;
    }
    expr_ty iter = node->v.For.iter;
    if (iter->kind != Call_kind || iter->v.Call.func->kind != Name_kind ||
        !name_is(ident_str(iter->v.Call.func->v.Name.id), "range")) {
        cg_errorf(cg, cg->cur_line, "for-loops must iterate over range(...)");
        return;
    }
    asdl_expr_seq *range_args = iter->v.Call.args;
    int range_argc = (int)asdl_seq_LEN(range_args);
    if (range_argc < 1 || range_argc > 3) {
        cg_errorf(cg, cg->cur_line, "range() takes 1 to 3 arguments");
        return;
    }
    int values[3] = {0, 0, 0};
    for (int i = 0; i < range_argc; i++) {
        if (!const_int_for_range(cg, asdl_seq_GET(range_args, i), &values[i])) {
            return;
        }
    }
    int start, stop, step;
    if (range_argc == 1) {
        start = 0;
        stop = values[0];
        step = 1;
    } else if (range_argc == 2) {
        start = values[0];
        stop = values[1];
        step = 1;
    } else {
        start = values[0];
        stop = values[1];
        step = values[2];
    }
    if (step == 0) {
        cg_errorf(cg, cg->cur_line, "range() step must not be zero");
        return;
    }

    int var_reg = local_reg(cg, ident_str(node->v.For.target->v.Name.id));
    uint8_t start_reg = cg_load_const(cg, (double)start);
    cg_emit(cg, YETTY_YFSVM_OP_MOV, (uint8_t)var_reg, start_reg, 0, 0);
    cg_free_temp(cg, start_reg);

    int loop_label = cg_new_label(cg);
    int end_label = cg_new_label(cg);
    cg_place(cg, loop_label);

    uint8_t stop_reg = cg_load_const(cg, (double)stop);
    if (step > 0) {
        cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_GE, end_label, (uint8_t)var_reg, stop_reg);
    } else {
        cg_emit_branch(cg, YETTY_YFSVM_OP_JMP_IF_GE, end_label, stop_reg, (uint8_t)var_reg);
    }
    cg_free_temp(cg, stop_reg);

    compile_body(cg, node->v.For.body);

    uint8_t step_reg = cg_load_const(cg, (double)step);
    uint8_t next_reg = cg_alloc_temp(cg);
    cg_emit(cg, YETTY_YFSVM_OP_ADD, next_reg, (uint8_t)var_reg, step_reg, 0);
    cg_emit(cg, YETTY_YFSVM_OP_MOV, (uint8_t)var_reg, next_reg, 0, 0);
    cg_free_temp(cg, step_reg);
    cg_free_temp(cg, next_reg);

    cg_emit_jump(cg, loop_label);
    cg_place(cg, end_label);
}

static void compile_stmt(struct codegen *cg, stmt_ty node)
{
    if (cg->had_error) {
        return;
    }
    cg->cur_line = node->lineno;
    switch (node->kind) {
    case Assign_kind: {
        if (asdl_seq_LEN(node->v.Assign.targets) != 1) {
            cg_errorf(cg, cg->cur_line, "chained assignment is not supported");
            return;
        }
        expr_ty target = asdl_seq_GET(node->v.Assign.targets, 0);
        if (target->kind != Name_kind) {
            cg_errorf(cg, cg->cur_line, "assignment target must be a simple variable");
            return;
        }
        uint8_t value_reg = compile_expr(cg, node->v.Assign.value);
        int reg = local_reg(cg, ident_str(target->v.Name.id));
        cg_emit(cg, YETTY_YFSVM_OP_MOV, (uint8_t)reg, value_reg, 0, 0);
        cg_free_temp(cg, value_reg);
        return;
    }
    case AugAssign_kind: {
        if (node->v.AugAssign.target->kind != Name_kind) {
            cg_errorf(cg, cg->cur_line, "augmented-assignment target must be a simple variable");
            return;
        }
        YfsvmOpcode op;
        if (!binop_opcode(node->v.AugAssign.op, &op)) {
            cg_errorf(cg, cg->cur_line, "unsupported augmented operator");
            return;
        }
        int target_reg = local_reg(cg, ident_str(node->v.AugAssign.target->v.Name.id));
        uint8_t value_reg = compile_expr(cg, node->v.AugAssign.value);
        uint8_t result = cg_alloc_temp(cg);
        cg_emit(cg, op, result, (uint8_t)target_reg, value_reg, 0);
        cg_free_temp(cg, value_reg);
        cg_emit(cg, YETTY_YFSVM_OP_MOV, (uint8_t)target_reg, result, 0, 0);
        cg_free_temp(cg, result);
        return;
    }
    case Return_kind: {
        if (node->v.Return.value) {
            uint8_t value_reg = compile_expr(cg, node->v.Return.value);
            cg_emit(cg, YETTY_YFSVM_OP_MOV, 0, value_reg, 0, 0);
            cg_free_temp(cg, value_reg);
        }
        cg_emit(cg, YETTY_YFSVM_OP_RET, 0, 0, 0, 0);
        return;
    }
    case If_kind: {
        int else_label = cg_new_label(cg);
        emit_jump_if_false(cg, node->v.If.test, else_label);
        compile_body(cg, node->v.If.body);
        if (asdl_seq_LEN(node->v.If.orelse) > 0) {
            int end_label = cg_new_label(cg);
            cg_emit_jump(cg, end_label);
            cg_place(cg, else_label);
            compile_body(cg, node->v.If.orelse);
            cg_place(cg, end_label);
        } else {
            cg_place(cg, else_label);
        }
        return;
    }
    case While_kind: {
        if (asdl_seq_LEN(node->v.While.orelse) > 0) {
            cg_errorf(cg, cg->cur_line, "while-else is not supported");
            return;
        }
        int start_label = cg_new_label(cg);
        int end_label = cg_new_label(cg);
        cg_place(cg, start_label);
        emit_jump_if_false(cg, node->v.While.test, end_label);
        compile_body(cg, node->v.While.body);
        cg_emit_jump(cg, start_label);
        cg_place(cg, end_label);
        return;
    }
    case For_kind:
        compile_for(cg, node);
        return;
    case Expr_kind:
        /* A bare string (docstring) has no runtime meaning; ignore it. */
        if (node->v.Expr.value->kind == Constant_kind &&
            const_is_string(node->v.Expr.value->v.Constant.value)) {
            return;
        }
        cg_free_temp(cg, compile_expr(cg, node->v.Expr.value));
        return;
    case Pass_kind:
        return;
    default:
        cg_errorf(cg, cg->cur_line, "unsupported statement");
        return;
    }
}

/* --- local collection (mirror of _LocalCollector) ------------------------- */

static void add_local(struct codegen *cg, const char *name)
{
    if (!name) {
        return;
    }
    if (local_reg(cg, name) >= 0) {
        return;
    }
    if (cg->local_count + 1 >= YFSVM_MAX_REGISTERS) {
        cg_errorf(cg, cg->cur_line, "too many local variables (max %u)", YFSVM_MAX_REGISTERS - 1);
        return;
    }
    cg->local_names[cg->local_count++] = name;
}

static void collect_locals_body(struct codegen *cg, asdl_stmt_seq *body);

static void collect_locals_stmt(struct codegen *cg, stmt_ty node)
{
    switch (node->kind) {
    case Assign_kind: {
        int count = (int)asdl_seq_LEN(node->v.Assign.targets);
        for (int i = 0; i < count; i++) {
            expr_ty target = asdl_seq_GET(node->v.Assign.targets, i);
            if (target->kind == Name_kind) {
                add_local(cg, ident_str(target->v.Name.id));
            }
        }
        return;
    }
    case AugAssign_kind:
        if (node->v.AugAssign.target->kind == Name_kind) {
            add_local(cg, ident_str(node->v.AugAssign.target->v.Name.id));
        }
        return;
    case For_kind:
        if (node->v.For.target->kind == Name_kind) {
            add_local(cg, ident_str(node->v.For.target->v.Name.id));
        }
        collect_locals_body(cg, node->v.For.body);
        collect_locals_body(cg, node->v.For.orelse);
        return;
    case If_kind:
        collect_locals_body(cg, node->v.If.body);
        collect_locals_body(cg, node->v.If.orelse);
        return;
    case While_kind:
        collect_locals_body(cg, node->v.While.body);
        collect_locals_body(cg, node->v.While.orelse);
        return;
    default:
        return;
    }
}

static void collect_locals_body(struct codegen *cg, asdl_stmt_seq *body)
{
    int count = (int)asdl_seq_LEN(body);
    for (int i = 0; i < count; i++) {
        collect_locals_stmt(cg, asdl_seq_GET(body, i));
    }
}

/* --- function compile ----------------------------------------------------- */

/* Bind a parameter to its VM input: reserved name, else positional. */
static void emit_parameter_load(struct codegen *cg, const char *name, int position, uint8_t reg)
{
    YfsvmOpcode load;
    int sampler = 0;
    int reserved_sampler = sampler_index(name);
    if (name_is(name, "x")) {
        load = YETTY_YFSVM_OP_LOAD_X;
    } else if (name_is(name, "y")) {
        load = YETTY_YFSVM_OP_LOAD_Y;
    } else if (name_is(name, "t") || name_is(name, "time")) {
        load = YETTY_YFSVM_OP_LOAD_T;
    } else if (reserved_sampler >= 0) {
        load = YETTY_YFSVM_OP_LOAD_S;
        sampler = reserved_sampler;
    } else if (position == 0) {
        load = YETTY_YFSVM_OP_LOAD_X;
    } else if (position == 1) {
        load = YETTY_YFSVM_OP_LOAD_Y;
    } else if (position == 2) {
        load = YETTY_YFSVM_OP_LOAD_T;
    } else {
        sampler = position - 3;
        if (sampler > 7) {
            cg_errorf(cg, cg->cur_line,
                      "too many parameters: the VM provides x, y, time and s0..s7 (max 11)");
            return;
        }
        load = YETTY_YFSVM_OP_LOAD_S;
    }
    if (load == YETTY_YFSVM_OP_LOAD_S) {
        cg_emit(cg, YETTY_YFSVM_OP_LOAD_S, reg, 0, 0, (uint16_t)sampler);
    } else {
        cg_emit(cg, load, reg, 0, 0, 0);
        if (load == YETTY_YFSVM_OP_LOAD_X) {
            cg->uses_x = 1;
        } else if (load == YETTY_YFSVM_OP_LOAD_Y) {
            cg->uses_y = 1;
        } else if (load == YETTY_YFSVM_OP_LOAD_T) {
            cg->uses_time = 1;
        }
    }
}

static void compile_function(struct codegen *cg, stmt_ty def)
{
    cg->cur_line = def->lineno;
    arguments_ty args = def->v.FunctionDef.args;

    if (asdl_seq_LEN(def->v.FunctionDef.decorator_list) > 0) {
        cg_errorf(cg, def->lineno, "decorators are not supported");
        return;
    }
    if (args->vararg || args->kwarg || asdl_seq_LEN(args->kwonlyargs) > 0 ||
        asdl_seq_LEN(args->kw_defaults) > 0 || asdl_seq_LEN(args->defaults) > 0) {
        cg_errorf(cg, def->lineno,
                  "only positional scalar parameters are supported "
                  "(no *args, **kwargs, keyword-only, or default arguments)");
        return;
    }

    /* begin function */
    cg->func_start = cg->prog->code_count;
    cg->local_count = 0;
    cg->label_count = 0;
    cg->fixup_count = 0;
    memset(cg->temp_used, 0, sizeof(cg->temp_used));

    /* parameters become the first locals (in order) */
    int param_count = (int)asdl_seq_LEN(args->posonlyargs) + (int)asdl_seq_LEN(args->args);
    const char *param_names[YFSVM_MAX_REGISTERS];
    int collected = 0;
    int posonly = (int)asdl_seq_LEN(args->posonlyargs);
    for (int i = 0; i < posonly; i++) {
        param_names[collected++] = ident_str(((arg_ty)asdl_seq_GET(args->posonlyargs, i))->arg);
    }
    int normal = (int)asdl_seq_LEN(args->args);
    for (int i = 0; i < normal; i++) {
        param_names[collected++] = ident_str(((arg_ty)asdl_seq_GET(args->args, i))->arg);
    }
    for (int i = 0; i < param_count; i++) {
        add_local(cg, param_names[i]);
    }

    /* assigned names follow, in first-seen order */
    collect_locals_body(cg, def->v.FunctionDef.body);
    if (cg->had_error) {
        return;
    }
    cg->first_temp = (uint8_t)(1 + cg->local_count);

    /* load each parameter from its VM input */
    for (int i = 0; i < param_count; i++) {
        emit_parameter_load(cg, param_names[i], i, (uint8_t)(1 + i));
    }

    compile_body(cg, def->v.FunctionDef.body);

    /* fallthrough return */
    cg_emit(cg, YETTY_YFSVM_OP_RET, 0, 0, 0, 0);

    if (cg->had_error) {
        return;
    }
    cg_resolve_fixups(cg);

    /* record the function table entry */
    struct yetty_yfsvm_function *fn = &cg->prog->functions[cg->prog->function_count++];
    fn->code_offset = cg->func_start;
    fn->code_length = cg->prog->code_count - cg->func_start;
}

/* --- public entry --------------------------------------------------------- */

static int is_module_docstring(stmt_ty node)
{
    return node->kind == Expr_kind && node->v.Expr.value->kind == Constant_kind &&
           const_is_string(node->v.Expr.value->v.Constant.value);
}

int yetty_yfspy_compile(const char *source, const char *filename, struct yetty_yfspy_program *out,
                        char *errbuf, size_t errcap)
{
    if (errcap > 0) {
        errbuf[0] = '\0';
    }
    memset(out, 0, sizeof(*out));

    PyArena *arena = _PyArena_New();
    if (!arena) {
        snprintf(errbuf, errcap, "out of memory");
        return YETTY_YFSPY_OOM;
    }
    PyObject *filename_obj = PyUnicode_FromString(filename ? filename : "<shader>");
    PyCompilerFlags flags = _PyCompilerFlags_INIT;
    mod_ty mod = _PyParser_ASTFromString(source, filename_obj, Py_file_input, &flags, arena);
    if (!mod) {
        const char *message = yetty_cpython_last_error();
        snprintf(errbuf, errcap, "%s", message ? message : "syntax error");
        _PyArena_Free(arena);
        return YETTY_YFSPY_PARSE_ERROR;
    }
    if (mod->kind != Module_kind) {
        snprintf(errbuf, errcap, "expected a module");
        _PyArena_Free(arena);
        return YETTY_YFSPY_COMPILE_ERROR;
    }

    struct codegen cg;
    memset(&cg, 0, sizeof(cg));
    cg.prog = &out->program;
    cg.arena = arena;
    cg.errbuf = errbuf;
    cg.errcap = errcap;

    /* gather top-level function names first (for inter-function-call rejection) */
    asdl_stmt_seq *body = mod->v.Module.body;
    int top_count = (int)asdl_seq_LEN(body);
    for (int i = 0; i < top_count; i++) {
        stmt_ty node = asdl_seq_GET(body, i);
        if (node->kind == FunctionDef_kind) {
            if (cg.func_name_count < YFSVM_MAX_FUNCTIONS) {
                cg.func_names[cg.func_name_count++] = ident_str(node->v.FunctionDef.name);
            }
        } else if (!is_module_docstring(node)) {
            cg_errorf(&cg, node->lineno,
                      "top-level statement is not allowed; only function definitions");
        }
    }
    if (cg.had_error) {
        _PyArena_Free(arena);
        return YETTY_YFSPY_COMPILE_ERROR;
    }
    if (cg.func_name_count == 0) {
        snprintf(errbuf, errcap, "no function definitions found");
        _PyArena_Free(arena);
        return YETTY_YFSPY_COMPILE_ERROR;
    }
    if (cg.func_name_count > YFSVM_MAX_FUNCTIONS) {
        snprintf(errbuf, errcap, "too many functions (max %u)", YFSVM_MAX_FUNCTIONS);
        _PyArena_Free(arena);
        return YETTY_YFSPY_COMPILE_ERROR;
    }

    for (int i = 0; i < top_count && !cg.had_error; i++) {
        stmt_ty node = asdl_seq_GET(body, i);
        if (node->kind == FunctionDef_kind) {
            compile_function(&cg, node);
        }
    }
    _PyArena_Free(arena);

    if (cg.had_error) {
        return YETTY_YFSPY_COMPILE_ERROR;
    }

    /* validate the assembled program before handing it back */
    struct yetty_ycore_void_result valid = yetty_yfsvm_program_validate(&out->program);
    if (YETTY_IS_ERR(valid)) {
        snprintf(errbuf, errcap, "%s", valid.error.msg);
        return YETTY_YFSPY_COMPILE_ERROR;
    }

    out->function_count = out->program.function_count;
    out->uses_x = cg.uses_x;
    out->uses_y = cg.uses_y;
    out->uses_time = cg.uses_time;
    return YETTY_YFSPY_OK;
}
