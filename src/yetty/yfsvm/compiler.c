#include <yetty/yfsvm/compiler.h>
#include <string.h>
#include <stdbool.h>

/* Shader resource set + embedded WGSL live in shader-rs.c (yetty_yfsvm).
 * This file is GPU-less so it can live in yetty_yfsvm_core. */

/*=============================================================================
 * Instruction builder (internal)
 *===========================================================================*/

struct yetty_yfsvm_builder {
    struct yetty_yfsvm_program *prog;
    uint32_t current_func_start;
    uint16_t reg_alloc; /* bitmask of allocated registers */
    const char *error;
};

static void builder_init(struct yetty_yfsvm_builder *b, struct yetty_yfsvm_program *prog)
{
    memset(b, 0, sizeof(*b));
    b->prog = prog;
}

static uint8_t builder_alloc_reg(struct yetty_yfsvm_builder *b)
{
    for (uint8_t i = 1; i < YFSVM_MAX_REGISTERS; i++) {
        if (!(b->reg_alloc & (1u << i))) {
            b->reg_alloc |= (1u << i);
            return i;
        }
    }
    b->error = "out of registers";
    return 0;
}

static void builder_free_reg(struct yetty_yfsvm_builder *b, uint8_t reg)
{
    if (reg > 0) {
        b->reg_alloc &= ~(1u << reg);
    }
}

static void builder_emit(struct yetty_yfsvm_builder *b, uint32_t instr)
{
    if (b->prog->code_count >= YFSVM_MAX_INSTRUCTIONS) {
        b->error = "too many instructions";
        return;
    }
    b->prog->code[b->prog->code_count++] = instr;
}

static uint16_t builder_add_constant(struct yetty_yfsvm_builder *b, float value)
{
    /* Check for existing constant */
    for (uint32_t i = 0; i < b->prog->constant_count; i++) {
        if (b->prog->constants[i] == value) {
            return (uint16_t)i;
        }
    }

    if (b->prog->constant_count >= YFSVM_MAX_CONSTANTS) {
        b->error = "too many constants";
        return 0;
    }

    uint16_t idx = (uint16_t)b->prog->constant_count;
    b->prog->constants[b->prog->constant_count++] = value;
    return idx;
}

static uint8_t builder_load_const(struct yetty_yfsvm_builder *b, float value)
{
    uint16_t idx = builder_add_constant(b, value);
    uint8_t reg = builder_alloc_reg(b);
    builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_LOAD_C, reg, 0, 0, idx));
    return reg;
}

static void builder_begin_function(struct yetty_yfsvm_builder *b)
{
    b->current_func_start = b->prog->code_count;
    b->reg_alloc = 1; /* r0 reserved for return */
}

static void builder_end_function(struct yetty_yfsvm_builder *b)
{
    if (b->prog->function_count >= YFSVM_MAX_FUNCTIONS) {
        b->error = "too many functions";
        return;
    }
    struct yetty_yfsvm_function *fn = &b->prog->functions[b->prog->function_count++];
    fn->code_offset = b->current_func_start;
    fn->code_length = b->prog->code_count - b->current_func_start;
}

/*=============================================================================
 * AST compilation
 *===========================================================================*/

static uint8_t compile_node(struct yetty_yfsvm_builder *b, const struct yetty_yexpr_node *node);

static uint8_t compile_number(struct yetty_yfsvm_builder *b, const struct yetty_yexpr_node *node)
{
    return builder_load_const(b, (float)node->number);
}

static uint8_t compile_identifier(struct yetty_yfsvm_builder *b,
                                  const struct yetty_yexpr_node *node)
{
    const char *name = node->ident;

    if (strcmp(name, "x") == 0) {
        uint8_t reg = builder_alloc_reg(b);
        builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_LOAD_X, reg, 0, 0, 0));
        b->prog->uses_x = 1;
        return reg;
    }

    if (strcmp(name, "t") == 0 || strcmp(name, "time") == 0) {
        uint8_t reg = builder_alloc_reg(b);
        builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_LOAD_T, reg, 0, 0, 0));
        b->prog->uses_time = 1;
        return reg;
    }

    if (strcmp(name, "pi") == 0 || strcmp(name, "PI") == 0) {
        return builder_load_const(b, 3.14159265358979323846f);
    }
    if (strcmp(name, "e") == 0 || strcmp(name, "E") == 0) {
        return builder_load_const(b, 2.71828182845904523536f);
    }
    if (strcmp(name, "tau") == 0 || strcmp(name, "TAU") == 0) {
        return builder_load_const(b, 6.28318530717958647693f);
    }

    /* Sampler inputs: s0-s7 */
    if (name[0] == 's' && name[1] >= '0' && name[1] <= '7' && name[2] == '\0') {
        uint8_t idx = name[1] - '0';
        uint8_t reg = builder_alloc_reg(b);
        builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_LOAD_S, reg, 0, 0, idx));
        return reg;
    }

    b->error = "unknown identifier";
    return 0;
}

static uint8_t compile_buffer_ref(struct yetty_yfsvm_builder *b,
                                  const struct yetty_yexpr_node *node)
{
    int sampler_idx = node->buffer_ref.index - 1;
    if (sampler_idx < 0 || sampler_idx > 7) {
        b->error = "buffer index out of range";
        return 0;
    }
    uint8_t reg = builder_alloc_reg(b);
    builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_LOAD_S, reg, 0, 0, (uint16_t)sampler_idx));
    return reg;
}

static uint8_t compile_binary_op(struct yetty_yfsvm_builder *b, const struct yetty_yexpr_node *node)
{
    uint8_t left = compile_node(b, node->binary.left);
    if (b->error) {
        return 0;
    }
    uint8_t right = compile_node(b, node->binary.right);
    if (b->error) {
        builder_free_reg(b, left);
        return 0;
    }

    uint8_t dst = builder_alloc_reg(b);
    YfsvmOpcode op;

    switch (node->binary.op) {
    case YETTY_YEXPR_OP_ADD:
        op = YETTY_YFSVM_OP_ADD;
        break;
    case YETTY_YEXPR_OP_SUB:
        op = YETTY_YFSVM_OP_SUB;
        break;
    case YETTY_YEXPR_OP_MUL:
        op = YETTY_YFSVM_OP_MUL;
        break;
    case YETTY_YEXPR_OP_DIV:
        op = YETTY_YFSVM_OP_DIV;
        break;
    case YETTY_YEXPR_OP_POW:
        op = YETTY_YFSVM_OP_POW;
        break;
    }

    builder_emit(b, yfsvm_encode(op, dst, left, right, 0));
    builder_free_reg(b, left);
    builder_free_reg(b, right);
    return dst;
}

static uint8_t compile_unary_op(struct yetty_yfsvm_builder *b, const struct yetty_yexpr_node *node)
{
    uint8_t operand = compile_node(b, node->unary.operand);
    if (b->error) {
        return 0;
    }

    uint8_t dst = builder_alloc_reg(b);
    builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_NEG, dst, operand, 0, 0));
    builder_free_reg(b, operand);
    return dst;
}

struct yetty_yfsvm_func_map {
    const char *name;
    YfsvmOpcode op;
};

static const struct yetty_yfsvm_func_map funcs_1arg[] = {
    {"sin", YETTY_YFSVM_OP_SIN},
    {"cos", YETTY_YFSVM_OP_COS},
    {"tan", YETTY_YFSVM_OP_TAN},
    {"asin", YETTY_YFSVM_OP_ASIN},
    {"acos", YETTY_YFSVM_OP_ACOS},
    {"atan", YETTY_YFSVM_OP_ATAN},
    {"sinh", YETTY_YFSVM_OP_SINH},
    {"cosh", YETTY_YFSVM_OP_COSH},
    {"tanh", YETTY_YFSVM_OP_TANH},
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
    {"fract", YETTY_YFSVM_OP_FRACT},
    {"frac", YETTY_YFSVM_OP_FRACT},
    {"sign", YETTY_YFSVM_OP_SIGN},
    {"saturate", YETTY_YFSVM_OP_CLAMP01},
    {NULL, 0},
};

static const struct yetty_yfsvm_func_map funcs_2arg[] = {
    {"pow", YETTY_YFSVM_OP_POW},   {"atan2", YETTY_YFSVM_OP_ATAN2},
    {"min", YETTY_YFSVM_OP_MIN},   {"max", YETTY_YFSVM_OP_MAX},
    {"mod", YETTY_YFSVM_OP_MOD},   {"fmod", YETTY_YFSVM_OP_MOD},
    {"step", YETTY_YFSVM_OP_STEP}, {NULL, 0},
};

static uint8_t compile_call(struct yetty_yfsvm_builder *b, const struct yetty_yexpr_node *node)
{
    const char *name = node->call.name;
    uint32_t argc = node->call.arg_count;

    if (argc == 1) {
        uint8_t arg = compile_node(b, node->call.args[0]);
        if (b->error) {
            return 0;
        }
        uint8_t dst = builder_alloc_reg(b);

        for (const struct yetty_yfsvm_func_map *f = funcs_1arg; f->name; f++) {
            if (strcmp(name, f->name) == 0) {
                builder_emit(b, yfsvm_encode(f->op, dst, arg, 0, 0));
                builder_free_reg(b, arg);
                return dst;
            }
        }

        builder_free_reg(b, arg);
        builder_free_reg(b, dst);
        b->error = "unknown 1-arg function";
        return 0;
    }

    if (argc == 2) {
        uint8_t a0 = compile_node(b, node->call.args[0]);
        if (b->error) {
            return 0;
        }
        uint8_t a1 = compile_node(b, node->call.args[1]);
        if (b->error) {
            builder_free_reg(b, a0);
            return 0;
        }
        uint8_t dst = builder_alloc_reg(b);

        for (const struct yetty_yfsvm_func_map *f = funcs_2arg; f->name; f++) {
            if (strcmp(name, f->name) == 0) {
                builder_emit(b, yfsvm_encode(f->op, dst, a0, a1, 0));
                builder_free_reg(b, a0);
                builder_free_reg(b, a1);
                return dst;
            }
        }

        builder_free_reg(b, a0);
        builder_free_reg(b, a1);
        builder_free_reg(b, dst);
        b->error = "unknown 2-arg function";
        return 0;
    }

    if (argc == 3) {
        uint8_t a0 = compile_node(b, node->call.args[0]);
        if (b->error) {
            return 0;
        }
        uint8_t a1 = compile_node(b, node->call.args[1]);
        if (b->error) {
            builder_free_reg(b, a0);
            return 0;
        }
        uint8_t a2 = compile_node(b, node->call.args[2]);
        if (b->error) {
            builder_free_reg(b, a0);
            builder_free_reg(b, a1);
            return 0;
        }
        uint8_t dst = builder_alloc_reg(b);

        if (strcmp(name, "mix") == 0 || strcmp(name, "lerp") == 0) {
            builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_MIX, dst, a0, a1, a2 & 0xF));
        } else if (strcmp(name, "clamp") == 0) {
            uint8_t tmp = builder_alloc_reg(b);
            builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_MAX, tmp, a0, a1, 0));
            builder_emit(b, yfsvm_encode(YETTY_YFSVM_OP_MIN, dst, tmp, a2, 0));
            builder_free_reg(b, tmp);
        } else {
            builder_free_reg(b, a0);
            builder_free_reg(b, a1);
            builder_free_reg(b, a2);
            builder_free_reg(b, dst);
            b->error = "unknown 3-arg function";
            return 0;
        }

        builder_free_reg(b, a0);
        builder_free_reg(b, a1);
        builder_free_reg(b, a2);
        return dst;
    }

    b->error = "unsupported argument count";
    return 0;
}

static uint8_t compile_node(struct yetty_yfsvm_builder *b, const struct yetty_yexpr_node *node)
{
    if (!node) {
        b->error = "null node";
        return 0;
    }

    switch (node->type) {
    case YETTY_YEXPR_NUMBER:
        return compile_number(b, node);
    case YETTY_YEXPR_IDENTIFIER:
        return compile_identifier(b, node);
    case YETTY_YEXPR_BUFFER_REF:
        return compile_buffer_ref(b, node);
    case YETTY_YEXPR_BINARY_OP:
        return compile_binary_op(b, node);
    case YETTY_YEXPR_UNARY_OP:
        return compile_unary_op(b, node);
    case YETTY_YEXPR_CALL:
        return compile_call(b, node);
    }

    b->error = "unsupported node type";
    return 0;
}

static struct yetty_yfsvm_program_result compile_single(struct yetty_yfsvm_program *prog,
                                                        const struct yetty_yexpr_node *ast)
{
    struct yetty_yfsvm_builder b;
    builder_init(&b, prog);
    builder_begin_function(&b);

    uint8_t result_reg = compile_node(&b, ast);
    if (b.error) {
        return YETTY_ERR(yetty_yfsvm_program, b.error);
    }

    if (result_reg != 0) {
        builder_emit(&b, yfsvm_encode(YETTY_YFSVM_OP_MOV, 0, result_reg, 0, 0));
        builder_free_reg(&b, result_reg);
    }

    builder_emit(&b, yfsvm_encode(YETTY_YFSVM_OP_RET, 0, 0, 0, 0));
    builder_end_function(&b);

    if (b.error) {
        return YETTY_ERR(yetty_yfsvm_program, b.error);
    }

    return YETTY_OK(yetty_yfsvm_program, *prog);
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_yfsvm_program_result yetty_yfsvm_compile(const struct yetty_yexpr_node *ast)
{
    if (!ast) {
        return YETTY_ERR(yetty_yfsvm_program, "null AST");
    }

    struct yetty_yfsvm_program prog = {0};
    return compile_single(&prog, ast);
}

struct yetty_yfsvm_program_result yetty_yfsvm_compile_expr(const char *source, size_t len)
{
    struct yetty_yexpr_parse_result parse_res = yetty_yexpr_parse(source, len);
    if (YETTY_IS_ERR(parse_res)) {
        return YETTY_ERR(yetty_yfsvm_program, parse_res.error.msg);
    }

    return yetty_yfsvm_compile(parse_res.value.root);
}

struct yetty_yfsvm_program_result yetty_yfsvm_compile_multi(
    const struct yetty_yexpr_plot_expr *plot)
{
    if (!plot) {
        return YETTY_ERR(yetty_yfsvm_program, "null plot expression");
    }

    struct yetty_yfsvm_program prog = {0};

    for (uint32_t i = 0; i < plot->def_count; i++) {
        struct yetty_yfsvm_builder b;
        builder_init(&b, &prog);
        builder_begin_function(&b);

        uint8_t result_reg = compile_node(&b, plot->defs[i].expression);
        if (b.error) {
            return YETTY_ERR(yetty_yfsvm_program, b.error);
        }

        if (result_reg != 0) {
            builder_emit(&b, yfsvm_encode(YETTY_YFSVM_OP_MOV, 0, result_reg, 0, 0));
            builder_free_reg(&b, result_reg);
        }

        builder_emit(&b, yfsvm_encode(YETTY_YFSVM_OP_RET, 0, 0, 0, 0));
        builder_end_function(&b);

        if (b.error) {
            return YETTY_ERR(yetty_yfsvm_program, b.error);
        }
    }

    return YETTY_OK(yetty_yfsvm_program, prog);
}

struct yetty_yfsvm_program_result yetty_yfsvm_compile_multi_expr(const char *source, size_t len)
{
    struct yetty_yexpr_plot_parse_result parse_res = yetty_yexpr_parse_plot(source, len);
    if (YETTY_IS_ERR(parse_res)) {
        return YETTY_ERR(yetty_yfsvm_program, parse_res.error.msg);
    }

    return yetty_yfsvm_compile_multi(&parse_res.value.plot);
}

uint32_t yetty_yfsvm_program_serialize(const struct yetty_yfsvm_program *prog, uint32_t *buf,
                                       uint32_t buf_capacity)
{
    if (!prog || !buf) {
        return 0;
    }

    /* Layout: header(4) + func_table(MAX_FUNCTIONS) + constants + code */
    uint32_t total = 4 + YFSVM_MAX_FUNCTIONS + prog->constant_count + prog->code_count;
    if (total > buf_capacity) {
        return 0;
    }

    uint32_t pos = 0;

    /* Header */
    buf[pos++] = YFSVM_MAGIC;
    buf[pos++] = YFSVM_VERSION;
    buf[pos++] = prog->function_count;
    buf[pos++] = prog->constant_count;

    /* Function table - packed (length:16 | offset:16), padded to MAX_FUNCTIONS */
    for (uint32_t i = 0; i < YFSVM_MAX_FUNCTIONS; i++) {
        if (i < prog->function_count) {
            uint32_t packed = ((prog->functions[i].code_length & 0xFFFF) << 16) |
                              (prog->functions[i].code_offset & 0xFFFF);
            buf[pos++] = packed;
        } else {
            buf[pos++] = 0; /* Padding */
        }
    }

    /* Constants (bitcast float to u32) */
    for (uint32_t i = 0; i < prog->constant_count; i++) {
        uint32_t bits;
        memcpy(&bits, &prog->constants[i], sizeof(bits));
        buf[pos++] = bits;
    }

    /* Code */
    memcpy(&buf[pos], prog->code, prog->code_count * sizeof(uint32_t));
    pos += prog->code_count;

    return pos;
}

/* yetty_yfsvm_get_shader_resource_set moved to shader-rs.c (yetty_yfsvm). */
