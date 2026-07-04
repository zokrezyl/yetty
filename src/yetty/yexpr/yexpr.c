#include <yetty/yexpr/yexpr.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Lexer
 *===========================================================================*/

enum yetty_yexpr_token_type {
    YETTY_YEXPR_TOK_NUMBER,
    YETTY_YEXPR_TOK_IDENTIFIER,
    YETTY_YEXPR_TOK_HEX_COLOR,
    YETTY_YEXPR_TOK_STRING,
    YETTY_YEXPR_TOK_PLUS,
    YETTY_YEXPR_TOK_MINUS,
    YETTY_YEXPR_TOK_STAR,
    YETTY_YEXPR_TOK_SLASH,
    YETTY_YEXPR_TOK_CARET,
    YETTY_YEXPR_TOK_LPAREN,
    YETTY_YEXPR_TOK_RPAREN,
    YETTY_YEXPR_TOK_COMMA,
    YETTY_YEXPR_TOK_EQUALS,
    YETTY_YEXPR_TOK_AT,
    YETTY_YEXPR_TOK_DOT,
    YETTY_YEXPR_TOK_DOTDOT,
    YETTY_YEXPR_TOK_SEMICOLON,
    YETTY_YEXPR_TOK_EOF,
    YETTY_YEXPR_TOK_ERROR,
};

struct yetty_yexpr_token {
    enum yetty_yexpr_token_type type;
    const char *start;
    size_t len;
    double num_value;
};

struct yetty_yexpr_lexer {
    const char *source;
    size_t source_len;
    size_t pos;
};

static void lexer_init(struct yetty_yexpr_lexer *lex, const char *source, size_t len)
{
    lex->source = source;
    lex->source_len = len;
    lex->pos = 0;
}

static char lex_current(const struct yetty_yexpr_lexer *lex)
{
    if (lex->pos >= lex->source_len) {
        return '\0';
    }
    return lex->source[lex->pos];
}

static char lex_advance(struct yetty_yexpr_lexer *lex)
{
    char c = lex_current(lex);
    if (c != '\0') {
        lex->pos++;
    }
    return c;
}

static char lex_peek_next(const struct yetty_yexpr_lexer *lex)
{
    if (lex->pos + 1 >= lex->source_len) {
        return '\0';
    }
    return lex->source[lex->pos + 1];
}

static void lex_skip_whitespace(struct yetty_yexpr_lexer *lex)
{
    while (lex->pos < lex->source_len && isspace((unsigned char)lex_current(lex))) {
        lex->pos++;
    }
}

static struct yetty_yexpr_token lex_make(enum yetty_yexpr_token_type type, const char *start,
                                         size_t len)
{
    struct yetty_yexpr_token tok = {0};
    tok.type = type;
    tok.start = start;
    tok.len = len;
    return tok;
}

static struct yetty_yexpr_token lex_scan_number(struct yetty_yexpr_lexer *lex)
{
    const char *start = lex->source + lex->pos;

    while (isdigit((unsigned char)lex_current(lex))) {
        lex_advance(lex);
    }

    if (lex_current(lex) == '.' && isdigit((unsigned char)lex_peek_next(lex))) {
        lex_advance(lex);
        while (isdigit((unsigned char)lex_current(lex))) {
            lex_advance(lex);
        }
    }

    if (lex_current(lex) == 'e' || lex_current(lex) == 'E') {
        lex_advance(lex);
        if (lex_current(lex) == '+' || lex_current(lex) == '-') {
            lex_advance(lex);
        }
        while (isdigit((unsigned char)lex_current(lex))) {
            lex_advance(lex);
        }
    }

    size_t len = (lex->source + lex->pos) - start;
    struct yetty_yexpr_token tok = lex_make(YETTY_YEXPR_TOK_NUMBER, start, len);

    char buf[64];
    size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, start, copy_len);
    buf[copy_len] = '\0';
    tok.num_value = strtod(buf, NULL);

    return tok;
}

static struct yetty_yexpr_token lex_scan_identifier(struct yetty_yexpr_lexer *lex)
{
    const char *start = lex->source + lex->pos;

    while (isalnum((unsigned char)lex_current(lex)) || lex_current(lex) == '_') {
        lex_advance(lex);
    }

    return lex_make(YETTY_YEXPR_TOK_IDENTIFIER, start, (lex->source + lex->pos) - start);
}

static struct yetty_yexpr_token lex_scan_string(struct yetty_yexpr_lexer *lex)
{
    char quote = lex_advance(lex);
    const char *start = lex->source + lex->pos;

    while (lex_current(lex) != quote && lex_current(lex) != '\0') {
        if (lex_current(lex) == '\\' && lex_peek_next(lex) != '\0') {
            lex_advance(lex);
        }
        lex_advance(lex);
    }

    size_t len = (lex->source + lex->pos) - start;
    if (lex_current(lex) == quote) {
        lex_advance(lex);
    }

    return lex_make(YETTY_YEXPR_TOK_STRING, start, len);
}

static struct yetty_yexpr_token lex_next(struct yetty_yexpr_lexer *lex)
{
    lex_skip_whitespace(lex);

    if (lex->pos >= lex->source_len) {
        return lex_make(YETTY_YEXPR_TOK_EOF, lex->source + lex->pos, 0);
    }

    char c = lex_current(lex);
    const char *start = lex->source + lex->pos;

    switch (c) {
    case '+':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_PLUS, start, 1);
    case '-':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_MINUS, start, 1);
    case '*':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_STAR, start, 1);
    case '/':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_SLASH, start, 1);
    case '^':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_CARET, start, 1);
    case '(':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_LPAREN, start, 1);
    case ')':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_RPAREN, start, 1);
    case ',':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_COMMA, start, 1);
    case '=':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_EQUALS, start, 1);
    case '@':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_AT, start, 1);
    case '.':
        if (lex_peek_next(lex) == '.') {
            lex_advance(lex);
            lex_advance(lex);
            return lex_make(YETTY_YEXPR_TOK_DOTDOT, start, 2);
        }
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_DOT, start, 1);
    case ';':
        lex_advance(lex);
        return lex_make(YETTY_YEXPR_TOK_SEMICOLON, start, 1);
    case '"':
    case '\'':
        return lex_scan_string(lex);
    case '#':
        lex_advance(lex);
        while (isxdigit((unsigned char)lex_current(lex))) {
            lex_advance(lex);
        }
        return lex_make(YETTY_YEXPR_TOK_HEX_COLOR, start, (lex->source + lex->pos) - start);
    default:
        break;
    }

    if (isdigit((unsigned char)c)) {
        return lex_scan_number(lex);
    }

    if (isalpha((unsigned char)c) || c == '_') {
        return lex_scan_identifier(lex);
    }

    lex_advance(lex);
    return lex_make(YETTY_YEXPR_TOK_ERROR, start, 1);
}

/*=============================================================================
 * Parser
 *===========================================================================*/

struct yetty_yexpr_parser {
    struct yetty_yexpr_lexer lex;
    struct yetty_yexpr_token current;
    struct yetty_yexpr_arena *arena;
    const char *error;
};

static struct yetty_yexpr_node *alloc_node(struct yetty_yexpr_parser *p)
{
    if (p->arena->count >= YETTY_YEXPR_MAX_NODES) {
        p->error = "too many AST nodes";
        return NULL;
    }
    struct yetty_yexpr_node *n = &p->arena->nodes[p->arena->count++];
    memset(n, 0, sizeof(*n));
    return n;
}

static void parser_advance(struct yetty_yexpr_parser *p)
{
    p->current = lex_next(&p->lex);
}

static int parser_check(const struct yetty_yexpr_parser *p, enum yetty_yexpr_token_type type)
{
    return p->current.type == type;
}

static int parser_match(struct yetty_yexpr_parser *p, enum yetty_yexpr_token_type type)
{
    if (parser_check(p, type)) {
        parser_advance(p);
        return 1;
    }
    return 0;
}

static void token_to_str(const struct yetty_yexpr_token *tok, char *buf, size_t buf_size)
{
    size_t copy_len = tok->len < buf_size - 1 ? tok->len : buf_size - 1;
    memcpy(buf, tok->start, copy_len);
    buf[copy_len] = '\0';
}

/* Forward declarations */
static struct yetty_yexpr_node *parse_expr(struct yetty_yexpr_parser *p);
static struct yetty_yexpr_node *parse_term(struct yetty_yexpr_parser *p);
static struct yetty_yexpr_node *parse_factor(struct yetty_yexpr_parser *p);
static struct yetty_yexpr_node *parse_unary(struct yetty_yexpr_parser *p);
static struct yetty_yexpr_node *parse_primary(struct yetty_yexpr_parser *p);

static struct yetty_yexpr_node *parse_expr(struct yetty_yexpr_parser *p)
{
    struct yetty_yexpr_node *left = parse_term(p);
    if (!left) {
        return NULL;
    }

    while (parser_check(p, YETTY_YEXPR_TOK_PLUS) || parser_check(p, YETTY_YEXPR_TOK_MINUS)) {
        enum yetty_yexpr_binary_op op =
            parser_check(p, YETTY_YEXPR_TOK_PLUS) ? YETTY_YEXPR_OP_ADD : YETTY_YEXPR_OP_SUB;
        parser_advance(p);
        struct yetty_yexpr_node *right = parse_term(p);
        if (!right) {
            return NULL;
        }

        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_BINARY_OP;
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }

    return left;
}

static struct yetty_yexpr_node *parse_term(struct yetty_yexpr_parser *p)
{
    struct yetty_yexpr_node *left = parse_factor(p);
    if (!left) {
        return NULL;
    }

    while (parser_check(p, YETTY_YEXPR_TOK_STAR) || parser_check(p, YETTY_YEXPR_TOK_SLASH)) {
        enum yetty_yexpr_binary_op op =
            parser_check(p, YETTY_YEXPR_TOK_STAR) ? YETTY_YEXPR_OP_MUL : YETTY_YEXPR_OP_DIV;
        parser_advance(p);
        struct yetty_yexpr_node *right = parse_factor(p);
        if (!right) {
            return NULL;
        }

        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_BINARY_OP;
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }

    return left;
}

static struct yetty_yexpr_node *parse_factor(struct yetty_yexpr_parser *p)
{
    struct yetty_yexpr_node *left = parse_unary(p);
    if (!left) {
        return NULL;
    }

    if (parser_check(p, YETTY_YEXPR_TOK_CARET)) {
        parser_advance(p);
        struct yetty_yexpr_node *right = parse_unary(p);
        if (!right) {
            return NULL;
        }

        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_BINARY_OP;
        node->binary.op = YETTY_YEXPR_OP_POW;
        node->binary.left = left;
        node->binary.right = right;
        left = node;
    }

    return left;
}

static struct yetty_yexpr_node *parse_unary(struct yetty_yexpr_parser *p)
{
    if (parser_check(p, YETTY_YEXPR_TOK_MINUS)) {
        parser_advance(p);
        struct yetty_yexpr_node *operand = parse_unary(p);
        if (!operand) {
            return NULL;
        }

        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_UNARY_OP;
        node->unary.op = YETTY_YEXPR_OP_NEG;
        node->unary.operand = operand;
        return node;
    }

    return parse_primary(p);
}

static struct yetty_yexpr_node *parse_call(struct yetty_yexpr_parser *p, const char *name,
                                           size_t name_len)
{
    parser_advance(p); /* consume '(' */

    struct yetty_yexpr_node *node = alloc_node(p);
    if (!node) {
        return NULL;
    }
    node->type = YETTY_YEXPR_CALL;

    size_t copy_len =
        name_len < YETTY_YEXPR_MAX_NAME_LEN - 1 ? name_len : YETTY_YEXPR_MAX_NAME_LEN - 1;
    memcpy(node->call.name, name, copy_len);
    node->call.name[copy_len] = '\0';
    node->call.arg_count = 0;

    if (!parser_check(p, YETTY_YEXPR_TOK_RPAREN)) {
        struct yetty_yexpr_node *arg = parse_expr(p);
        if (!arg) {
            return NULL;
        }
        node->call.args[node->call.arg_count++] = arg;

        while (parser_match(p, YETTY_YEXPR_TOK_COMMA)) {
            if (node->call.arg_count >= YETTY_YEXPR_MAX_CALL_ARGS) {
                p->error = "too many function arguments";
                return NULL;
            }
            arg = parse_expr(p);
            if (!arg) {
                return NULL;
            }
            node->call.args[node->call.arg_count++] = arg;
        }
    }

    if (!parser_match(p, YETTY_YEXPR_TOK_RPAREN)) {
        p->error = "expected ')'";
        return NULL;
    }

    return node;
}

static struct yetty_yexpr_node *parse_primary(struct yetty_yexpr_parser *p)
{
    /* Number */
    if (parser_check(p, YETTY_YEXPR_TOK_NUMBER)) {
        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_NUMBER;
        node->number = p->current.num_value;
        parser_advance(p);
        return node;
    }

    /* Buffer reference: @buffer1 */
    if (parser_check(p, YETTY_YEXPR_TOK_AT)) {
        parser_advance(p);
        if (!parser_check(p, YETTY_YEXPR_TOK_IDENTIFIER)) {
            p->error = "expected identifier after '@'";
            return NULL;
        }

        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_BUFFER_REF;
        token_to_str(&p->current, node->buffer_ref.name, YETTY_YEXPR_MAX_NAME_LEN);

        /* Extract index from "buffer1" */
        node->buffer_ref.index = 0;
        if (p->current.len > 6 && memcmp(p->current.start, "buffer", 6) == 0) {
            char idx_buf[8];
            size_t idx_len = p->current.len - 6;
            if (idx_len < sizeof(idx_buf)) {
                memcpy(idx_buf, p->current.start + 6, idx_len);
                idx_buf[idx_len] = '\0';
                node->buffer_ref.index = atoi(idx_buf);
            }
        }

        parser_advance(p);
        return node;
    }

    /* Identifier or function call */
    if (parser_check(p, YETTY_YEXPR_TOK_IDENTIFIER)) {
        const char *name = p->current.start;
        size_t name_len = p->current.len;
        parser_advance(p);

        if (parser_check(p, YETTY_YEXPR_TOK_LPAREN)) {
            return parse_call(p, name, name_len);
        }

        struct yetty_yexpr_node *node = alloc_node(p);
        if (!node) {
            return NULL;
        }
        node->type = YETTY_YEXPR_IDENTIFIER;
        size_t copy_len =
            name_len < YETTY_YEXPR_MAX_NAME_LEN - 1 ? name_len : YETTY_YEXPR_MAX_NAME_LEN - 1;
        memcpy(node->ident, name, copy_len);
        node->ident[copy_len] = '\0';
        return node;
    }

    /* Parenthesized expression */
    if (parser_check(p, YETTY_YEXPR_TOK_LPAREN)) {
        parser_advance(p);
        struct yetty_yexpr_node *expr = parse_expr(p);
        if (!expr) {
            return NULL;
        }
        if (!parser_match(p, YETTY_YEXPR_TOK_RPAREN)) {
            p->error = "expected ')'";
            return NULL;
        }
        return expr;
    }

    p->error = "expected expression";
    return NULL;
}

/*=============================================================================
 * Plot expression parser
 *===========================================================================*/

static struct yetty_yexpr_node *parse_bare_expr_from_ident(struct yetty_yexpr_parser *p,
                                                           const char *name, size_t name_len)
{
    struct yetty_yexpr_node *expr;

    if (parser_check(p, YETTY_YEXPR_TOK_LPAREN)) {
        expr = parse_call(p, name, name_len);
    } else {
        expr = alloc_node(p);
        if (!expr) {
            return NULL;
        }
        expr->type = YETTY_YEXPR_IDENTIFIER;
        size_t copy_len =
            name_len < YETTY_YEXPR_MAX_NAME_LEN - 1 ? name_len : YETTY_YEXPR_MAX_NAME_LEN - 1;
        memcpy(expr->ident, name, copy_len);
        expr->ident[copy_len] = '\0';
    }
    if (!expr) {
        return NULL;
    }

    /* Handle trailing operators */
    while (parser_check(p, YETTY_YEXPR_TOK_PLUS) || parser_check(p, YETTY_YEXPR_TOK_MINUS) ||
           parser_check(p, YETTY_YEXPR_TOK_STAR) || parser_check(p, YETTY_YEXPR_TOK_SLASH) ||
           parser_check(p, YETTY_YEXPR_TOK_CARET)) {
        enum yetty_yexpr_binary_op op;
        if (parser_check(p, YETTY_YEXPR_TOK_PLUS)) {
            op = YETTY_YEXPR_OP_ADD;
        } else if (parser_check(p, YETTY_YEXPR_TOK_MINUS)) {
            op = YETTY_YEXPR_OP_SUB;
        } else if (parser_check(p, YETTY_YEXPR_TOK_STAR)) {
            op = YETTY_YEXPR_OP_MUL;
        } else if (parser_check(p, YETTY_YEXPR_TOK_SLASH)) {
            op = YETTY_YEXPR_OP_DIV;
        } else {
            op = YETTY_YEXPR_OP_POW;
        }
        parser_advance(p);

        struct yetty_yexpr_node *right = parse_term(p);
        if (!right) {
            return NULL;
        }

        struct yetty_yexpr_node *bin = alloc_node(p);
        if (!bin) {
            return NULL;
        }
        bin->type = YETTY_YEXPR_BINARY_OP;
        bin->binary.op = op;
        bin->binary.left = expr;
        bin->binary.right = right;
        expr = bin;
    }

    return expr;
}

/* Parse `N[kKmM]?` into uint32. Returns 0 on success, -1 on bad input. */
static int parse_size_with_suffix(const char *s, uint32_t *out)
{
    char *endp = NULL;
    unsigned long v = strtoul(s, &endp, 10);
    if (!endp || endp == s) {
        return -1;
    }
    if (*endp == 'k' || *endp == 'K') {
        v *= 1024u;
        endp++;
    } else if (*endp == 'm' || *endp == 'M') {
        v *= 1024u * 1024u;
        endp++;
    }
    if (*endp != '\0') {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

/* Locate (or create) a buffer decl by name. */
static struct yetty_yexpr_plot_buffer *plot_buffer_find_or_create(
    struct yetty_yexpr_parser *p, struct yetty_yexpr_plot_expr *plot, const char *name)
{
    for (uint32_t i = 0; i < plot->buffer_count; i++) {
        if (strcmp(plot->buffers[i].name, name) == 0) {
            return &plot->buffers[i];
        }
    }
    if (plot->buffer_count >= YETTY_YEXPR_MAX_PLOT_BUFFERS) {
        p->error = "too many buffer declarations";
        return NULL;
    }
    struct yetty_yexpr_plot_buffer *b = &plot->buffers[plot->buffer_count++];
    memset(b, 0, sizeof(*b));
    strncpy(b->name, name, YETTY_YEXPR_MAX_NAME_LEN - 1);
    b->name[YETTY_YEXPR_MAX_NAME_LEN - 1] = '\0';
    return b;
}

/* Parse `A..B` after the LHS of an `=`. Both sides may be numbers or
 * unary-minus / identifier (pi, -pi, etc.) — we resolve common constants
 * inline. Returns 0/1 to mean failure/success. */
static int parse_range_value(struct yetty_yexpr_parser *p, float *lo, float *hi)
{
    /* Reuse parse_unary to handle -pi / -1.5 / pi gracefully, then evaluate
     * the resulting tiny AST. */
    struct yetty_yexpr_node *lo_node = parse_unary(p);
    if (!lo_node) {
        return 0;
    }
    if (!parser_match(p, YETTY_YEXPR_TOK_DOTDOT)) {
        p->error = "expected '..' in range";
        return 0;
    }
    struct yetty_yexpr_node *hi_node = parse_unary(p);
    if (!hi_node) {
        return 0;
    }

    /* Evaluate constant range bounds. */
    struct yetty_yexpr_node *bounds[2] = {lo_node, hi_node};
    float out[2];
    for (int i = 0; i < 2; i++) {
        const struct yetty_yexpr_node *n = bounds[i];
        float sign = 1.0f;
        if (n->type == YETTY_YEXPR_UNARY_OP && n->unary.op == YETTY_YEXPR_OP_NEG) {
            sign = -1.0f;
            n = n->unary.operand;
        }
        if (n->type == YETTY_YEXPR_NUMBER) {
            out[i] = sign * (float)n->number;
        } else if (n->type == YETTY_YEXPR_IDENTIFIER) {
            const char *id = n->ident;
            if (strcmp(id, "pi") == 0 || strcmp(id, "PI") == 0) {
                out[i] = sign * 3.14159265358979323846f;
            } else if (strcmp(id, "tau") == 0 || strcmp(id, "TAU") == 0) {
                out[i] = sign * 6.28318530717958647693f;
            } else if (strcmp(id, "e") == 0 || strcmp(id, "E") == 0) {
                out[i] = sign * 2.71828182845904523536f;
            } else {
                p->error = "range bound must be a numeric constant";
                return 0;
            }
        } else {
            p->error = "range bound must be a numeric constant";
            return 0;
        }
    }
    *lo = out[0];
    *hi = out[1];
    return 1;
}

static int parse_view_attr(struct yetty_yexpr_parser *p, struct yetty_yexpr_plot_expr *plot)
{
    /* '@' already consumed; current token is "view". Expect: view = X1..X2 , Y1..Y2 */
    parser_advance(p); /* consume "view" */
    if (!parser_match(p, YETTY_YEXPR_TOK_EQUALS)) {
        p->error = "expected '=' after @view";
        return -1;
    }
    float xlo, xhi, ylo, yhi;
    if (!parse_range_value(p, &xlo, &xhi)) {
        return -1;
    }
    if (!parser_match(p, YETTY_YEXPR_TOK_COMMA)) {
        p->error = "expected ',' between x and y ranges in @view";
        return -1;
    }
    if (!parse_range_value(p, &ylo, &yhi)) {
        return -1;
    }
    plot->view_x_min = xlo;
    plot->view_x_max = xhi;
    plot->view_y_min = ylo;
    plot->view_y_max = yhi;
    plot->has_view = 1;
    return 0;
}

/* Parse comma-separated numeric values after `=`. Stops at ';' or EOF.
 * Returns 0 if any values were read, -1 on parse error. */
static int parse_inline_values(struct yetty_yexpr_parser *p, struct yetty_yexpr_plot_buffer *buf)
{
    for (;;) {
        /* Allow unary-minus prefix. */
        float sign = 1.0f;
        if (parser_match(p, YETTY_YEXPR_TOK_MINUS)) {
            sign = -1.0f;
        }
        if (!parser_check(p, YETTY_YEXPR_TOK_NUMBER)) {
            p->error = "expected numeric value in @<buf>.values list";
            return -1;
        }
        if (buf->inline_count < YETTY_YEXPR_MAX_INLINE_VALUES) {
            buf->inline_values[buf->inline_count++] = sign * (float)p->current.num_value;
        }
        parser_advance(p);
        if (!parser_match(p, YETTY_YEXPR_TOK_COMMA)) {
            break;
        }
    }
    return 0;
}

static int parse_plot_attr(struct yetty_yexpr_parser *p, struct yetty_yexpr_plot_expr *plot)
{
    parser_advance(p); /* consume '@' */

    if (!parser_check(p, YETTY_YEXPR_TOK_IDENTIFIER)) {
        p->error = "expected identifier after '@'";
        return -1;
    }

    /* Special case: @view = X1..X2 , Y1..Y2 — no dot, no nested name. */
    if (p->current.len == 4 && memcmp(p->current.start, "view", 4) == 0) {
        return parse_view_attr(p, plot);
    }

    char plot_name[YETTY_YEXPR_MAX_NAME_LEN];
    token_to_str(&p->current, plot_name, sizeof(plot_name));
    parser_advance(p);

    if (!parser_match(p, YETTY_YEXPR_TOK_DOT)) {
        p->error = "expected '.' after plot name";
        return -1;
    }

    if (!parser_check(p, YETTY_YEXPR_TOK_IDENTIFIER)) {
        p->error = "expected attribute name after '.'";
        return -1;
    }
    char attr_name[YETTY_YEXPR_MAX_NAME_LEN];
    token_to_str(&p->current, attr_name, sizeof(attr_name));
    parser_advance(p);

    if (!parser_match(p, YETTY_YEXPR_TOK_EQUALS)) {
        p->error = "expected '=' after attribute name";
        return -1;
    }

    /* Buffer-property attrs: route to the buffer table. */
    int is_size = strcmp(attr_name, "size") == 0;
    int is_values = strcmp(attr_name, "values") == 0;

    /* The attr targets a known buffer declaration? */
    struct yetty_yexpr_plot_buffer *target = NULL;
    if (is_size || is_values) {
        for (uint32_t i = 0; i < plot->buffer_count; i++) {
            if (strcmp(plot->buffers[i].name, plot_name) == 0) {
                target = &plot->buffers[i];
                break;
            }
        }
    }

    if (target && is_size) {
        if (!parser_check(p, YETTY_YEXPR_TOK_NUMBER) &&
            !parser_check(p, YETTY_YEXPR_TOK_IDENTIFIER)) {
            p->error = "expected size value";
            return -1;
        }
        char size_buf[32];
        size_t copy_len =
            p->current.len < sizeof(size_buf) - 1 ? p->current.len : sizeof(size_buf) - 1;
        memcpy(size_buf, p->current.start, copy_len);
        size_buf[copy_len] = '\0';
        if (parse_size_with_suffix(size_buf, &target->size) < 0) {
            p->error = "invalid size value";
            return -1;
        }
        target->has_size = 1;
        parser_advance(p);
        return 0;
    }

    if (target && is_values) {
        /* Three forms: bare '=' (payload), '="file"' (file), '=v1,v2,…' (inline). */
        if (parser_check(p, YETTY_YEXPR_TOK_STRING)) {
            size_t copy_len = p->current.len < sizeof(target->file_path) - 1
                                  ? p->current.len
                                  : sizeof(target->file_path) - 1;
            memcpy(target->file_path, p->current.start, copy_len);
            target->file_path[copy_len] = '\0';
            parser_advance(p);
            return 0;
        }
        if (parser_check(p, YETTY_YEXPR_TOK_NUMBER) || parser_check(p, YETTY_YEXPR_TOK_MINUS)) {
            return parse_inline_values(p, target);
        }
        /* No value → payload mode. */
        target->wants_payload = 1;
        return 0;
    }

    /* Generic attribute fallthrough (e.g., color). */
    char value[64] = {0};
    if (parser_check(p, YETTY_YEXPR_TOK_HEX_COLOR) || parser_check(p, YETTY_YEXPR_TOK_STRING) ||
        parser_check(p, YETTY_YEXPR_TOK_IDENTIFIER) || parser_check(p, YETTY_YEXPR_TOK_NUMBER)) {
        size_t copy_len = p->current.len < sizeof(value) - 1 ? p->current.len : sizeof(value) - 1;
        memcpy(value, p->current.start, copy_len);
        value[copy_len] = '\0';
        parser_advance(p);
    } else {
        p->error = "expected value after '='";
        return -1;
    }

    if (plot->attr_count >= YETTY_YEXPR_MAX_PLOT_ATTRS) {
        p->error = "too many plot attributes";
        return -1;
    }

    struct yetty_yexpr_plot_attr *attr = &plot->attrs[plot->attr_count++];
    memcpy(attr->plot_name, plot_name, sizeof(attr->plot_name));
    memcpy(attr->attr_name, attr_name, sizeof(attr->attr_name));
    memcpy(attr->value, value, sizeof(attr->value));

    return 0;
}

static int add_plot_def(struct yetty_yexpr_parser *p, struct yetty_yexpr_plot_expr *plot,
                        const char *name, struct yetty_yexpr_node *expr)
{
    if (plot->def_count >= YETTY_YEXPR_MAX_PLOT_DEFS) {
        p->error = "too many plot definitions";
        return -1;
    }

    struct yetty_yexpr_plot_def *def = &plot->defs[plot->def_count++];
    strncpy(def->name, name, YETTY_YEXPR_MAX_NAME_LEN - 1);
    def->name[YETTY_YEXPR_MAX_NAME_LEN - 1] = '\0';
    def->expression = expr;
    return 0;
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_yexpr_node_ptr_result yetty_yexpr_parse(const char *source, size_t len,
                                                     struct yetty_yexpr_arena *arena)
{
    if (!source) {
        return YETTY_ERR(yetty_yexpr_node_ptr, "source is NULL");
    }
    if (!arena) {
        return YETTY_ERR(yetty_yexpr_node_ptr, "arena is NULL");
    }

    arena->count = 0;

    struct yetty_yexpr_parser parser = {0};
    lexer_init(&parser.lex, source, len);
    parser.arena = arena;
    parser_advance(&parser);

    struct yetty_yexpr_node *root = parse_expr(&parser);
    if (!root || parser.error) {
        return YETTY_ERR(yetty_yexpr_node_ptr, parser.error ? parser.error : "parse failed");
    }

    return YETTY_OK(yetty_yexpr_node_ptr, root);
}

struct yetty_yexpr_plot_expr_result yetty_yexpr_parse_plot(const char *source, size_t len,
                                                           struct yetty_yexpr_arena *arena)
{
    if (!source) {
        return YETTY_ERR(yetty_yexpr_plot_expr, "source is NULL");
    }
    if (!arena) {
        return YETTY_ERR(yetty_yexpr_plot_expr, "arena is NULL");
    }

    arena->count = 0;

    struct yetty_yexpr_plot_expr plot = {0};
    struct yetty_yexpr_parser parser = {0};
    lexer_init(&parser.lex, source, len);
    parser.arena = arena;
    parser_advance(&parser);

    while (!parser_check(&parser, YETTY_YEXPR_TOK_EOF) && !parser.error) {
        /* Skip semicolons */
        while (parser_match(&parser, YETTY_YEXPR_TOK_SEMICOLON)) {
            ;
        }
        if (parser_check(&parser, YETTY_YEXPR_TOK_EOF)) {
            break;
        }

        if (parser_check(&parser, YETTY_YEXPR_TOK_AT)) {
            /* Plot attribute: @name.attr = value */
            if (parse_plot_attr(&parser, &plot) < 0) {
                break;
            }
        } else if (parser_check(&parser, YETTY_YEXPR_TOK_IDENTIFIER)) {
            /* Could be: name=buffer   (declaration)
             *           name=A..B     (x/y domain or generic range)
             *           name=expr     (function definition)
             *           bare expression */
            const char *name = parser.current.start;
            size_t name_len = parser.current.len;
            parser_advance(&parser);

            if (parser_match(&parser, YETTY_YEXPR_TOK_EQUALS)) {
                char name_buf[YETTY_YEXPR_MAX_NAME_LEN];
                size_t copy_len = name_len < sizeof(name_buf) - 1 ? name_len : sizeof(name_buf) - 1;
                memcpy(name_buf, name, copy_len);
                name_buf[copy_len] = '\0';

                /* `name = buffer` — buffer-input declaration. The bare
                 * identifier "buffer" is the discriminator. */
                if (parser_check(&parser, YETTY_YEXPR_TOK_IDENTIFIER) && parser.current.len == 6 &&
                    memcmp(parser.current.start, "buffer", 6) == 0) {
                    parser_advance(&parser);
                    if (!plot_buffer_find_or_create(&parser, &plot, name_buf)) {
                        break;
                    }
                    parser_match(&parser, YETTY_YEXPR_TOK_SEMICOLON);
                    continue;
                }

                /* `name = A..B` — range. Recognised only for x / y today. */
                int is_x = (strcmp(name_buf, "x") == 0);
                int is_y = (strcmp(name_buf, "y") == 0);
                if (is_x || is_y) {
                    /* Look ahead: only commit to range parsing if a `..`
                     * follows the first unary value. Otherwise this is a
                     * plain `x = expr` def (rare but legal). */
                    size_t saved_pos = parser.lex.pos;
                    struct yetty_yexpr_token saved_cur = parser.current;
                    uint32_t saved_arena = arena->count;

                    float lo, hi;
                    if (parse_range_value(&parser, &lo, &hi)) {
                        if (is_x) {
                            plot.x_min = lo;
                            plot.x_max = hi;
                            plot.has_x_range = 1;
                        } else {
                            plot.y_min = lo;
                            plot.y_max = hi;
                            plot.has_y_range = 1;
                        }
                        parser_match(&parser, YETTY_YEXPR_TOK_SEMICOLON);
                        continue;
                    }
                    /* Roll back to try as a regular expression. */
                    parser.lex.pos = saved_pos;
                    parser.current = saved_cur;
                    arena->count = saved_arena;
                    parser.error = NULL;
                }

                /* Named definition: name = expr */
                struct yetty_yexpr_node *expr = parse_expr(&parser);
                if (!expr) {
                    break;
                }
                if (add_plot_def(&parser, &plot, name_buf, expr) < 0) {
                    break;
                }
            } else {
                /* Bare expression */
                struct yetty_yexpr_node *expr = parse_bare_expr_from_ident(&parser, name, name_len);
                if (!expr) {
                    break;
                }
                char auto_name[YETTY_YEXPR_MAX_NAME_LEN];
                snprintf(auto_name, sizeof(auto_name), "plot%u", plot.def_count + 1);
                if (add_plot_def(&parser, &plot, auto_name, expr) < 0) {
                    break;
                }
            }
        } else if (parser_check(&parser, YETTY_YEXPR_TOK_NUMBER) ||
                   parser_check(&parser, YETTY_YEXPR_TOK_LPAREN) ||
                   parser_check(&parser, YETTY_YEXPR_TOK_MINUS)) {
            /* Bare expression starting with number/paren/unary */
            struct yetty_yexpr_node *expr = parse_expr(&parser);
            if (!expr) {
                break;
            }
            char auto_name[YETTY_YEXPR_MAX_NAME_LEN];
            snprintf(auto_name, sizeof(auto_name), "plot%u", plot.def_count + 1);
            if (add_plot_def(&parser, &plot, auto_name, expr) < 0) {
                break;
            }
        } else {
            parser.error = "expected plot definition or expression";
            break;
        }

        parser_match(&parser, YETTY_YEXPR_TOK_SEMICOLON);
    }

    if (parser.error) {
        return YETTY_ERR(yetty_yexpr_plot_expr, parser.error);
    }

    return YETTY_OK(yetty_yexpr_plot_expr, plot);
}
