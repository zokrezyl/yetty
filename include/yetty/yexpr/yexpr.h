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
#define YETTY_YEXPR_MAX_PLOT_BUFFERS 8
#define YETTY_YEXPR_MAX_INLINE_VALUES 64

struct yetty_yexpr_plot_def {
    char name[YETTY_YEXPR_MAX_NAME_LEN];
    struct yetty_yexpr_node *expression;
};

struct yetty_yexpr_plot_attr {
    char plot_name[YETTY_YEXPR_MAX_NAME_LEN];
    char attr_name[YETTY_YEXPR_MAX_NAME_LEN];
    char value[64];
};

/* One named buffer declaration: `f=buffer; @f.size=N; @f.values=...`.
 * The compiler treats f(x) as a sampler-load against this buffer's slot. */
struct yetty_yexpr_plot_buffer {
    char name[YETTY_YEXPR_MAX_NAME_LEN];
    uint32_t size;    /* explicit @f.size — 0 ⇒ unset */
    uint8_t has_size; /* size came from @f.size, not values */

    /* @f.values=… — three variants distinguished by these flags:
     *   inline_count > 0           : inline_values[0..inline_count] are the data
     *   wants_payload (count==0)   : @f.values= (bare) → expect from payload
     *   file_path[0] != 0          : @f.values="file" → load from file */
    float inline_values[YETTY_YEXPR_MAX_INLINE_VALUES];
    uint32_t inline_count;
    uint8_t wants_payload;
    char file_path[64];
};

struct yetty_yexpr_plot_expr {
    struct yetty_yexpr_plot_def defs[YETTY_YEXPR_MAX_PLOT_DEFS];
    uint32_t def_count;
    struct yetty_yexpr_plot_attr attrs[YETTY_YEXPR_MAX_PLOT_ATTRS];
    uint32_t attr_count;
    struct yetty_yexpr_plot_buffer buffers[YETTY_YEXPR_MAX_PLOT_BUFFERS];
    uint32_t buffer_count;

    /* x=A..B / y=A..B — domain (evaluation range). */
    float x_min, x_max;
    float y_min, y_max;
    uint8_t has_x_range;
    uint8_t has_y_range;

    /* @view=X1..X2,Y1..Y2 — initial viewport. */
    float view_x_min, view_x_max;
    float view_y_min, view_y_max;
    uint8_t has_view;
};

/*=============================================================================
 * Parse results
 *===========================================================================*/

YETTY_YRESULT_DECLARE(yetty_yexpr_node_ptr, struct yetty_yexpr_node *);
YETTY_YRESULT_DECLARE(yetty_yexpr_plot_expr, struct yetty_yexpr_plot_expr);

/*=============================================================================
 * API
 *
 * Both parsers allocate every AST node out of a caller-provided arena: the
 * returned root pointer and all child links point into *arena, so the AST
 * stays valid exactly as long as the arena does — no destroy call is needed.
 * The arena is reset on entry and does not need to be initialised. It is
 * plain value storage: keep it (stack or embedded) wherever the AST must
 * remain dereferenceable, and never copy structs holding node pointers past
 * the arena's lifetime.
 *===========================================================================*/

/* Parse a single expression: "sin(x) + cos(x)". Returns the AST root,
 * owned by *arena. */
struct yetty_yexpr_node_ptr_result yetty_yexpr_parse(const char *source, size_t len,
                                                     struct yetty_yexpr_arena *arena);

/* Parse multi-plot expression: "f = sin(x); g = cos(x); @f.color = #FF0000".
 * The returned plot struct is value data except defs[].expression, which
 * point into *arena. */
struct yetty_yexpr_plot_expr_result yetty_yexpr_parse_plot(const char *source, size_t len,
                                                           struct yetty_yexpr_arena *arena);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YEXPR_H */
