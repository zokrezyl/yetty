/*
 * yexpr expression-parser contract test — pure, headless.
 *
 * Two layers of coverage:
 *
 *   1. The grammar's ACCEPT/REJECT surface (#416): well-formed expressions
 *      parse, malformed ones are rejected with an error Result.
 *   2. AST structure (#428): the parser allocates every node out of a
 *      caller-provided arena, so the returned root (and all child links)
 *      stay valid after the call returns. These tests walk the tree —
 *      precedence, associativity, unary nodes, calls, buffer refs — and
 *      double as a use-after-return regression guard under ASAN.
 */

#include <yetty/yexpr/yexpr.h>

#include "ytest.h"

#include <stddef.h>
#include <string.h>

static int parses_ok(const char *source)
{
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_node_ptr_result parse_res =
        yetty_yexpr_parse(source, strlen(source), &arena);
    if (YETTY_IS_ERR(parse_res)) {
        yetty_ycore_error_destroy(parse_res.error);
        return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
 * Well-formed expressions across every grammar production parse successfully.
 *-------------------------------------------------------------------------*/
static void test_accepts_valid(struct ytest *test)
{
    YTEST_CHECK(test, parses_ok("42"));              /* number */
    YTEST_CHECK(test, parses_ok("3.14"));            /* decimal */
    YTEST_CHECK(test, parses_ok("x"));               /* identifier */
    YTEST_CHECK(test, parses_ok("1 + 2 * 3"));       /* precedence mix */
    YTEST_CHECK(test, parses_ok("2 ^ 3"));           /* power */
    YTEST_CHECK(test, parses_ok("-5"));              /* unary negation */
    YTEST_CHECK(test, parses_ok("-x * 2"));          /* unary in a term */
    YTEST_CHECK(test, parses_ok("(1 + 2) * 3"));     /* parentheses */
    YTEST_CHECK(test, parses_ok("((1 + 2))"));       /* nested parentheses */
    YTEST_CHECK(test, parses_ok("sin(x)"));          /* 1-arg call */
    YTEST_CHECK(test, parses_ok("max(1, 2)"));       /* 2-arg call */
    YTEST_CHECK(test, parses_ok("f(a, b, c, d)"));   /* 4 args (the limit) */
    YTEST_CHECK(test, parses_ok("sin(x) + cos(y)")); /* two calls */
    YTEST_CHECK(test, parses_ok("@buffer3"));        /* buffer reference */
}

/*---------------------------------------------------------------------------
 * Malformed input is rejected with an error Result (no crash).
 *-------------------------------------------------------------------------*/
static void test_rejects_malformed(struct ytest *test)
{
    YTEST_CHECK(test, !parses_ok(""));                 /* empty */
    YTEST_CHECK(test, !parses_ok("1 +"));              /* dangling operator */
    YTEST_CHECK(test, !parses_ok(")"));                /* unmatched close paren */
    YTEST_CHECK(test, !parses_ok("(1 + 2"));           /* unclosed paren */
    YTEST_CHECK(test, !parses_ok("* 5"));              /* leading binary op */
    YTEST_CHECK(test, !parses_ok("f(a, b, c, d, e)")); /* one past the 4-arg limit */
    YTEST_CHECK(test, !parses_ok("@"));                /* '@' with no identifier */
}

/*---------------------------------------------------------------------------
 * AST shape helpers. REQUIRE-based: a wrong node type would make the
 * follow-up dereferences meaningless, so abort the test path instead.
 *-------------------------------------------------------------------------*/
static const struct yetty_yexpr_node *require_binary(struct ytest *test,
                                                     const struct yetty_yexpr_node *node,
                                                     enum yetty_yexpr_binary_op op)
{
    YTEST_REQUIRE_NOT_NULL(test, node);
    YTEST_REQUIRE_EQ_INT(test, node->type, YETTY_YEXPR_BINARY_OP);
    YTEST_REQUIRE_EQ_INT(test, node->binary.op, op);
    YTEST_REQUIRE_NOT_NULL(test, node->binary.left);
    YTEST_REQUIRE_NOT_NULL(test, node->binary.right);
    return node;
}

static void require_number(struct ytest *test, const struct yetty_yexpr_node *node, double value)
{
    YTEST_REQUIRE_NOT_NULL(test, node);
    YTEST_REQUIRE_EQ_INT(test, node->type, YETTY_YEXPR_NUMBER);
    YTEST_CHECK_NEAR(test, node->number, value, 1e-9);
}

static void require_identifier(struct ytest *test, const struct yetty_yexpr_node *node,
                               const char *name)
{
    YTEST_REQUIRE_NOT_NULL(test, node);
    YTEST_REQUIRE_EQ_INT(test, node->type, YETTY_YEXPR_IDENTIFIER);
    YTEST_CHECK_STR_EQ(test, node->ident, name);
}

/*---------------------------------------------------------------------------
 * Leaf nodes survive the return: number value and identifier name are read
 * back through the returned root pointer.
 *-------------------------------------------------------------------------*/
static void test_ast_leaves(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    struct yetty_yexpr_node_ptr_result parse_res = yetty_yexpr_parse("42", 2, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    require_number(test, parse_res.value, 42.0);

    parse_res = yetty_yexpr_parse("foo_bar", 7, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    require_identifier(test, parse_res.value, "foo_bar");
}

/*---------------------------------------------------------------------------
 * Precedence: '*' binds tighter than '+', '^' tighter than '*', and
 * parentheses override.
 *-------------------------------------------------------------------------*/
static void test_ast_precedence(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    /* 1 + 2 * 3  ⇒  ADD(1, MUL(2, 3)) */
    struct yetty_yexpr_node_ptr_result parse_res = yetty_yexpr_parse("1 + 2 * 3", 9, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_node *root = require_binary(test, parse_res.value, YETTY_YEXPR_OP_ADD);
    require_number(test, root->binary.left, 1.0);
    const struct yetty_yexpr_node *mul =
        require_binary(test, root->binary.right, YETTY_YEXPR_OP_MUL);
    require_number(test, mul->binary.left, 2.0);
    require_number(test, mul->binary.right, 3.0);

    /* (1 + 2) * 3  ⇒  MUL(ADD(1, 2), 3) */
    parse_res = yetty_yexpr_parse("(1 + 2) * 3", 11, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    root = require_binary(test, parse_res.value, YETTY_YEXPR_OP_MUL);
    const struct yetty_yexpr_node *add =
        require_binary(test, root->binary.left, YETTY_YEXPR_OP_ADD);
    require_number(test, add->binary.left, 1.0);
    require_number(test, add->binary.right, 2.0);
    require_number(test, root->binary.right, 3.0);

    /* 2 * 3 ^ 2  ⇒  MUL(2, POW(3, 2)) */
    parse_res = yetty_yexpr_parse("2 * 3 ^ 2", 9, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    root = require_binary(test, parse_res.value, YETTY_YEXPR_OP_MUL);
    require_number(test, root->binary.left, 2.0);
    const struct yetty_yexpr_node *pow =
        require_binary(test, root->binary.right, YETTY_YEXPR_OP_POW);
    require_number(test, pow->binary.left, 3.0);
    require_number(test, pow->binary.right, 2.0);
}

/*---------------------------------------------------------------------------
 * Associativity: '-' and '/' chains fold left.
 *-------------------------------------------------------------------------*/
static void test_ast_associativity(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    /* 1 - 2 - 3  ⇒  SUB(SUB(1, 2), 3) */
    struct yetty_yexpr_node_ptr_result parse_res = yetty_yexpr_parse("1 - 2 - 3", 9, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_node *root = require_binary(test, parse_res.value, YETTY_YEXPR_OP_SUB);
    require_number(test, root->binary.right, 3.0);
    const struct yetty_yexpr_node *inner =
        require_binary(test, root->binary.left, YETTY_YEXPR_OP_SUB);
    require_number(test, inner->binary.left, 1.0);
    require_number(test, inner->binary.right, 2.0);

    /* 8 / 4 / 2  ⇒  DIV(DIV(8, 4), 2) */
    parse_res = yetty_yexpr_parse("8 / 4 / 2", 9, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    root = require_binary(test, parse_res.value, YETTY_YEXPR_OP_DIV);
    require_number(test, root->binary.right, 2.0);
    inner = require_binary(test, root->binary.left, YETTY_YEXPR_OP_DIV);
    require_number(test, inner->binary.left, 8.0);
    require_number(test, inner->binary.right, 4.0);
}

/*---------------------------------------------------------------------------
 * Unary nodes: negation wraps its operand, nests, and binds tighter than '*'.
 *-------------------------------------------------------------------------*/
static void test_ast_unary(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    /* -5  ⇒  NEG(5) */
    struct yetty_yexpr_node_ptr_result parse_res = yetty_yexpr_parse("-5", 2, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_node *root = parse_res.value;
    YTEST_REQUIRE_EQ_INT(test, root->type, YETTY_YEXPR_UNARY_OP);
    YTEST_REQUIRE_EQ_INT(test, root->unary.op, YETTY_YEXPR_OP_NEG);
    require_number(test, root->unary.operand, 5.0);

    /* --5  ⇒  NEG(NEG(5)) */
    parse_res = yetty_yexpr_parse("--5", 3, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    root = parse_res.value;
    YTEST_REQUIRE_EQ_INT(test, root->type, YETTY_YEXPR_UNARY_OP);
    YTEST_REQUIRE_NOT_NULL(test, root->unary.operand);
    YTEST_REQUIRE_EQ_INT(test, root->unary.operand->type, YETTY_YEXPR_UNARY_OP);
    require_number(test, root->unary.operand->unary.operand, 5.0);

    /* -x * 2  ⇒  MUL(NEG(x), 2) — negation stays inside the term */
    parse_res = yetty_yexpr_parse("-x * 2", 6, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_node *mul = require_binary(test, parse_res.value, YETTY_YEXPR_OP_MUL);
    YTEST_REQUIRE_EQ_INT(test, mul->binary.left->type, YETTY_YEXPR_UNARY_OP);
    require_identifier(test, mul->binary.left->unary.operand, "x");
    require_number(test, mul->binary.right, 2.0);
}

/*---------------------------------------------------------------------------
 * Function calls: name, arg count, arg nodes, and nesting.
 *-------------------------------------------------------------------------*/
static void test_ast_call(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    /* max(1, x) */
    struct yetty_yexpr_node_ptr_result parse_res = yetty_yexpr_parse("max(1, x)", 9, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_node *root = parse_res.value;
    YTEST_REQUIRE_EQ_INT(test, root->type, YETTY_YEXPR_CALL);
    YTEST_CHECK_STR_EQ(test, root->call.name, "max");
    YTEST_REQUIRE_EQ_INT(test, root->call.arg_count, 2);
    require_number(test, root->call.args[0], 1.0);
    require_identifier(test, root->call.args[1], "x");

    /* sin(cos(x)) — call nested as an argument */
    parse_res = yetty_yexpr_parse("sin(cos(x))", 11, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    root = parse_res.value;
    YTEST_REQUIRE_EQ_INT(test, root->type, YETTY_YEXPR_CALL);
    YTEST_CHECK_STR_EQ(test, root->call.name, "sin");
    YTEST_REQUIRE_EQ_INT(test, root->call.arg_count, 1);
    const struct yetty_yexpr_node *inner = root->call.args[0];
    YTEST_REQUIRE_NOT_NULL(test, inner);
    YTEST_REQUIRE_EQ_INT(test, inner->type, YETTY_YEXPR_CALL);
    YTEST_CHECK_STR_EQ(test, inner->call.name, "cos");
    YTEST_REQUIRE_EQ_INT(test, inner->call.arg_count, 1);
    require_identifier(test, inner->call.args[0], "x");
}

/*---------------------------------------------------------------------------
 * Buffer references: @bufferN extracts the index, other names default to 0.
 *-------------------------------------------------------------------------*/
static void test_ast_buffer_ref(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    struct yetty_yexpr_node_ptr_result parse_res = yetty_yexpr_parse("@buffer3", 8, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_node *root = parse_res.value;
    YTEST_REQUIRE_EQ_INT(test, root->type, YETTY_YEXPR_BUFFER_REF);
    YTEST_CHECK_STR_EQ(test, root->buffer_ref.name, "buffer3");
    YTEST_CHECK_EQ_INT(test, root->buffer_ref.index, 3);

    parse_res = yetty_yexpr_parse("@wave", 5, &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    root = parse_res.value;
    YTEST_REQUIRE_EQ_INT(test, root->type, YETTY_YEXPR_BUFFER_REF);
    YTEST_CHECK_STR_EQ(test, root->buffer_ref.name, "wave");
    YTEST_CHECK_EQ_INT(test, root->buffer_ref.index, 0);
}

/*---------------------------------------------------------------------------
 * Plot parse: defs carry live expression ASTs (same arena contract as the
 * single-expression API), attrs/ranges/buffer decls come through as values.
 *-------------------------------------------------------------------------*/
static void test_plot_ast(struct ytest *test)
{
    struct yetty_yexpr_arena arena;

    const char *source = "f = sin(x); g = x * 2; @f.color = #FF0000; x = 0..6.28; "
                         "d = buffer; @d.size = 4; @d.values = 1, 2, 3";
    struct yetty_yexpr_plot_expr_result parse_res =
        yetty_yexpr_parse_plot(source, strlen(source), &arena);
    YTEST_REQUIRE_OK(test, parse_res);
    const struct yetty_yexpr_plot_expr *plot = &parse_res.value;

    /* Definitions: names plus dereferenceable expression trees. */
    YTEST_REQUIRE_EQ_INT(test, plot->def_count, 2);
    YTEST_CHECK_STR_EQ(test, plot->defs[0].name, "f");
    const struct yetty_yexpr_node *f_expr = plot->defs[0].expression;
    YTEST_REQUIRE_NOT_NULL(test, f_expr);
    YTEST_REQUIRE_EQ_INT(test, f_expr->type, YETTY_YEXPR_CALL);
    YTEST_CHECK_STR_EQ(test, f_expr->call.name, "sin");
    YTEST_REQUIRE_EQ_INT(test, f_expr->call.arg_count, 1);
    require_identifier(test, f_expr->call.args[0], "x");

    YTEST_CHECK_STR_EQ(test, plot->defs[1].name, "g");
    const struct yetty_yexpr_node *g_expr =
        require_binary(test, plot->defs[1].expression, YETTY_YEXPR_OP_MUL);
    require_identifier(test, g_expr->binary.left, "x");
    require_number(test, g_expr->binary.right, 2.0);

    /* Attribute table. */
    YTEST_REQUIRE_EQ_INT(test, plot->attr_count, 1);
    YTEST_CHECK_STR_EQ(test, plot->attrs[0].plot_name, "f");
    YTEST_CHECK_STR_EQ(test, plot->attrs[0].attr_name, "color");
    YTEST_CHECK_STR_EQ(test, plot->attrs[0].value, "#FF0000");

    /* Inline x-domain. */
    YTEST_CHECK_EQ_INT(test, plot->has_x_range, 1);
    YTEST_CHECK_NEAR(test, plot->x_min, 0.0f, 1e-6);
    YTEST_CHECK_NEAR(test, plot->x_max, 6.28f, 1e-6);

    /* Buffer declaration with explicit size and inline values. */
    YTEST_REQUIRE_EQ_INT(test, plot->buffer_count, 1);
    YTEST_CHECK_STR_EQ(test, plot->buffers[0].name, "d");
    YTEST_CHECK_EQ_INT(test, plot->buffers[0].has_size, 1);
    YTEST_CHECK_EQ_INT(test, plot->buffers[0].size, 4);
    YTEST_REQUIRE_EQ_INT(test, plot->buffers[0].inline_count, 3);
    YTEST_CHECK_NEAR(test, plot->buffers[0].inline_values[0], 1.0f, 1e-6);
    YTEST_CHECK_NEAR(test, plot->buffers[0].inline_values[1], 2.0f, 1e-6);
    YTEST_CHECK_NEAR(test, plot->buffers[0].inline_values[2], 3.0f, 1e-6);
}

int main(void)
{
    struct ytest test = ytest_begin("yexpr_parse");
    YTEST_RUN(&test, test_accepts_valid);
    YTEST_RUN(&test, test_rejects_malformed);
    YTEST_RUN(&test, test_ast_leaves);
    YTEST_RUN(&test, test_ast_precedence);
    YTEST_RUN(&test, test_ast_associativity);
    YTEST_RUN(&test, test_ast_unary);
    YTEST_RUN(&test, test_ast_call);
    YTEST_RUN(&test, test_ast_buffer_ref);
    YTEST_RUN(&test, test_plot_ast);
    return ytest_end(&test);
}
