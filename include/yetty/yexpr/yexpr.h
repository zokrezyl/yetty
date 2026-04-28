#ifndef YETTY_YEXPR_H
#define YETTY_YEXPR_H

/*
 * yexpr - Expression parser for yetty
 *
 * Parses mathematical expressions into an AST.
 * Used by yplot (bytecode compiler) and other components.
 *
 * Grammar:
 *   expr    = term (('+' | '-') term)*
 *   term    = factor (('*' | '/') factor)*
 *   factor  = unary ('^' unary)?
 *   unary   = '-'? primary
 *   primary = NUMBER | IDENT | IDENT '(' args ')' | '(' expr ')' | '@' IDENT
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * AST node types
 *===========================================================================*/

enum yetty_yexpr_node_type {
    YETTY_YEXPR_NUMBER,
    YETTY_YEXPR_IDENTIFIER,
    YETTY_YEXPR_BUFFER_REF,
    YETTY_YEXPR_BINARY_OP,
    YETTY_YEXPR_UNARY_OP,
    YETTY_YEXPR_CALL,
};

enum yetty_yexpr_binary_op {
    YETTY_YEXPR_OP_ADD,
    YETTY_YEXPR_OP_SUB,
    YETTY_YEXPR_OP_MUL,
    YETTY_YEXPR_OP_DIV,
    YETTY_YEXPR_OP_POW,
};

enum yetty_yexpr_unary_op {
    YETTY_YEXPR_OP_NEG,
};

#define YETTY_YEXPR_MAX_CALL_ARGS 4
#define YETTY_YEXPR_MAX_NAME_LEN 32
#define YETTY_YEXPR_MAX_NODES 256

struct yetty_yexpr_node {
    enum yetty_yexpr_node_type type;
    union {
        /* YETTY_YEXPR_NUMBER */
        double number;

        /* YETTY_YEXPR_IDENTIFIER */
        char ident[YETTY_YEXPR_MAX_NAME_LEN];

        /* YETTY_YEXPR_BUFFER_REF */
        struct {
            char name[YETTY_YEXPR_MAX_NAME_LEN];
            int index;
        } buffer_ref;

        /* YETTY_YEXPR_BINARY_OP */
        struct {
            enum yetty_yexpr_binary_op op;
            struct yetty_yexpr_node *left;
            struct yetty_yexpr_node *right;
        } binary;

        /* YETTY_YEXPR_UNARY_OP */
        struct {
            enum yetty_yexpr_unary_op op;
            struct yetty_yexpr_node *operand;
        } unary;

        /* YETTY_YEXPR_CALL */
        struct {
            char name[YETTY_YEXPR_MAX_NAME_LEN];
            struct yetty_yexpr_node *args[YETTY_YEXPR_MAX_CALL_ARGS];
            uint32_t arg_count;
        } call;
    };
};

/*=============================================================================
 * Arena - owns all nodes from a parse
 *===========================================================================*/

struct yetty_yexpr_arena {
    struct yetty_yexpr_node nodes[YETTY_YEXPR_MAX_NODES];
    uint32_t count;
};

/*=============================================================================
 * Plot expression - multiple named function definitions
 *===========================================================================*/

#define YETTY_YEXPR_MAX_PLOT_DEFS 16
#define YETTY_YEXPR_MAX_PLOT_ATTRS 32

struct yetty_yexpr_plot_def {
    char name[YETTY_YEXPR_MAX_NAME_LEN];
    struct yetty_yexpr_node *expression;
};

struct yetty_yexpr_plot_attr {
    char plot_name[YETTY_YEXPR_MAX_NAME_LEN];
    char attr_name[YETTY_YEXPR_MAX_NAME_LEN];
    char value[64];
};

struct yetty_yexpr_plot_expr {
    struct yetty_yexpr_plot_def defs[YETTY_YEXPR_MAX_PLOT_DEFS];
    uint32_t def_count;
    struct yetty_yexpr_plot_attr attrs[YETTY_YEXPR_MAX_PLOT_ATTRS];
    uint32_t attr_count;
};

/*=============================================================================
 * Parse results
 *===========================================================================*/

struct yetty_yexpr_parse_output {
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_node *root;
};

YETTY_YRESULT_DECLARE(yetty_yexpr_parse, struct yetty_yexpr_parse_output);

struct yetty_yexpr_plot_parse_output {
    struct yetty_yexpr_arena arena;
    struct yetty_yexpr_plot_expr plot;
};

YETTY_YRESULT_DECLARE(yetty_yexpr_plot_parse, struct yetty_yexpr_plot_parse_output);

/*=============================================================================
 * API
 *===========================================================================*/

/* Parse a single expression: "sin(x) + cos(x)" */
struct yetty_yexpr_parse_result yetty_yexpr_parse(const char *source, size_t len);

/* Parse multi-plot expression: "f = sin(x); g = cos(x); @f.color = #FF0000" */
struct yetty_yexpr_plot_parse_result yetty_yexpr_parse_plot(const char *source, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YEXPR_H */
