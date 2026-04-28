#include <yetty/yexpr/yexpr.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Lexer
 *===========================================================================*/

enum token_type {
    TOK_NUMBER,
    TOK_IDENTIFIER,
    TOK_HEX_COLOR,
    TOK_STRING,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_CARET,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_EQUALS,
    TOK_AT,
    TOK_DOT,
    TOK_SEMICOLON,
    TOK_EOF,
    TOK_ERROR,
};

struct token {
    enum token_type type;
    const char *start;
    size_t len;
    double num_value;
};

struct lexer {
    const char *source;
    size_t source_len;
    size_t pos;
};

static void lexer_init(struct lexer *lex, const char *source, size_t len)
{
    lex->source = source;
    lex->source_len = len;
    lex->pos = 0;
}

static char lex_current(const struct lexer *lex)
{
    if (lex->pos >= lex->source_len) {
        return '\0';
    }
    return lex->source[lex->pos];
}

static char lex_advance(struct lexer *lex)
{
    char c = lex_current(lex);
    if (c != '\0') {
        lex->pos++;
    }
    return c;
}

static char lex_peek_next(const struct lexer *lex)
{
    if (lex->pos + 1 >= lex->source_len) {
        return '\0';
    }
    return lex->source[lex->pos + 1];
}

static void lex_skip_whitespace(struct lexer *lex)
{
    while (lex->pos < lex->source_len && isspace((unsigned char)lex_current(lex))) {
        lex->pos++;
    }
}

static struct token lex_make(enum token_type type, const char *start, size_t len)
{
    struct token tok = {0};
    tok.type = type;
    tok.start = start;
    tok.len = len;
    return tok;
}

static struct token lex_scan_number(struct lexer *lex)
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
    struct token tok = lex_make(TOK_NUMBER, start, len);

    char buf[64];
    size_t copy_len = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, start, copy_len);
    buf[copy_len] = '\0';
    tok.num_value = strtod(buf, NULL);

    return tok;
}

static struct token lex_scan_identifier(struct lexer *lex)
{
    const char *start = lex->source + lex->pos;

    while (isalnum((unsigned char)lex_current(lex)) || lex_current(lex) == '_') {
        lex_advance(lex);
    }

    return lex_make(TOK_IDENTIFIER, start, (lex->source + lex->pos) - start);
}

static struct token lex_scan_string(struct lexer *lex)
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

    return lex_make(TOK_STRING, start, len);
}

static struct token lex_next(struct lexer *lex)
{
    lex_skip_whitespace(lex);

    if (lex->pos >= lex->source_len) {
        return lex_make(TOK_EOF, lex->source + lex->pos, 0);
    }

    char c = lex_current(lex);
    const char *start = lex->source + lex->pos;

    switch (c) {
    case '+':
        lex_advance(lex);
        return lex_make(TOK_PLUS, start, 1);
    case '-':
        lex_advance(lex);
        return lex_make(TOK_MINUS, start, 1);
    case '*':
        lex_advance(lex);
        return lex_make(TOK_STAR, start, 1);
    case '/':
        lex_advance(lex);
        return lex_make(TOK_SLASH, start, 1);
    case '^':
        lex_advance(lex);
        return lex_make(TOK_CARET, start, 1);
    case '(':
        lex_advance(lex);
        return lex_make(TOK_LPAREN, start, 1);
    case ')':
        lex_advance(lex);
        return lex_make(TOK_RPAREN, start, 1);
    case ',':
        lex_advance(lex);
        return lex_make(TOK_COMMA, start, 1);
    case '=':
        lex_advance(lex);
        return lex_make(TOK_EQUALS, start, 1);
    case '@':
        lex_advance(lex);
        return lex_make(TOK_AT, start, 1);
    case '.':
        lex_advance(lex);
        return lex_make(TOK_DOT, start, 1);
    case ';':
        lex_advance(lex);
        return lex_make(TOK_SEMICOLON, start, 1);
    case '"':
    case '\'':
        return lex_scan_string(lex);
    case '#':
        lex_advance(lex);
        while (isxdigit((unsigned char)lex_current(lex))) {
            lex_advance(lex);
        }
        return lex_make(TOK_HEX_COLOR, start, (lex->source + lex->pos) - start);
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
    return lex_make(TOK_ERROR, start, 1);
}

/*=============================================================================
 * Parser
 *===========================================================================*/

struct parser {
    struct lexer lex;
    struct token current;
    struct yetty_yexpr_arena *arena;
    const char *error;
};

static struct yetty_yexpr_node *alloc_node(struct parser *p)
{
    if (p->arena->count >= YETTY_YEXPR_MAX_NODES) {
        p->error = "too many AST nodes";
        return NULL;
    }
    struct yetty_yexpr_node *n = &p->arena->nodes[p->arena->count++];
    memset(n, 0, sizeof(*n));
    return n;
}

static void parser_advance(struct parser *p)
{
    p->current = lex_next(&p->lex);
}

static int parser_check(const struct parser *p, enum token_type type)
{
    return p->current.type == type;
}

static int parser_match(struct parser *p, enum token_type type)
{
    if (parser_check(p, type)) {
        parser_advance(p);
        return 1;
    }
    return 0;
}

static void token_to_str(const struct token *tok, char *buf, size_t buf_size)
{
    size_t copy_len = tok->len < buf_size - 1 ? tok->len : buf_size - 1;
    memcpy(buf, tok->start, copy_len);
    buf[copy_len] = '\0';
}

/* Forward declarations */
static struct yetty_yexpr_node *parse_expr(struct parser *p);
static struct yetty_yexpr_node *parse_term(struct parser *p);
static struct yetty_yexpr_node *parse_factor(struct parser *p);
static struct yetty_yexpr_node *parse_unary(struct parser *p);
static struct yetty_yexpr_node *parse_primary(struct parser *p);

static struct yetty_yexpr_node *parse_expr(struct parser *p)
{
    struct yetty_yexpr_node *left = parse_term(p);
    if (!left) {
        return NULL;
    }

    while (parser_check(p, TOK_PLUS) || parser_check(p, TOK_MINUS)) {
        enum yetty_yexpr_binary_op op =
            parser_check(p, TOK_PLUS) ? YETTY_YEXPR_OP_ADD : YETTY_YEXPR_OP_SUB;
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

static struct yetty_yexpr_node *parse_term(struct parser *p)
{
    struct yetty_yexpr_node *left = parse_factor(p);
    if (!left) {
        return NULL;
    }

    while (parser_check(p, TOK_STAR) || parser_check(p, TOK_SLASH)) {
        enum yetty_yexpr_binary_op op =
            parser_check(p, TOK_STAR) ? YETTY_YEXPR_OP_MUL : YETTY_YEXPR_OP_DIV;
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

static struct yetty_yexpr_node *parse_factor(struct parser *p)
{
    struct yetty_yexpr_node *left = parse_unary(p);
    if (!left) {
        return NULL;
    }

    if (parser_check(p, TOK_CARET)) {
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

static struct yetty_yexpr_node *parse_unary(struct parser *p)
{
    if (parser_check(p, TOK_MINUS)) {
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

static struct yetty_yexpr_node *parse_call(struct parser *p, const char *name, size_t name_len)
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

    if (!parser_check(p, TOK_RPAREN)) {
        struct yetty_yexpr_node *arg = parse_expr(p);
        if (!arg) {
            return NULL;
        }
        node->call.args[node->call.arg_count++] = arg;

        while (parser_match(p, TOK_COMMA)) {
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

    if (!parser_match(p, TOK_RPAREN)) {
        p->error = "expected ')'";
        return NULL;
    }

    return node;
}

static struct yetty_yexpr_node *parse_primary(struct parser *p)
{
    /* Number */
    if (parser_check(p, TOK_NUMBER)) {
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
    if (parser_check(p, TOK_AT)) {
        parser_advance(p);
        if (!parser_check(p, TOK_IDENTIFIER)) {
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
    if (parser_check(p, TOK_IDENTIFIER)) {
        const char *name = p->current.start;
        size_t name_len = p->current.len;
        parser_advance(p);

        if (parser_check(p, TOK_LPAREN)) {
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
    if (parser_check(p, TOK_LPAREN)) {
        parser_advance(p);
        struct yetty_yexpr_node *expr = parse_expr(p);
        if (!expr) {
            return NULL;
        }
        if (!parser_match(p, TOK_RPAREN)) {
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

static struct yetty_yexpr_node *parse_bare_expr_from_ident(struct parser *p, const char *name,
                                                           size_t name_len)
{
    struct yetty_yexpr_node *expr;

    if (parser_check(p, TOK_LPAREN)) {
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
    while (parser_check(p, TOK_PLUS) || parser_check(p, TOK_MINUS) || parser_check(p, TOK_STAR) ||
           parser_check(p, TOK_SLASH) || parser_check(p, TOK_CARET)) {
        enum yetty_yexpr_binary_op op;
        if (parser_check(p, TOK_PLUS)) {
            op = YETTY_YEXPR_OP_ADD;
        } else if (parser_check(p, TOK_MINUS)) {
            op = YETTY_YEXPR_OP_SUB;
        } else if (parser_check(p, TOK_STAR)) {
            op = YETTY_YEXPR_OP_MUL;
        } else if (parser_check(p, TOK_SLASH)) {
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

static int parse_plot_attr(struct parser *p, struct yetty_yexpr_plot_expr *plot)
{
    parser_advance(p); /* consume '@' */

    if (!parser_check(p, TOK_IDENTIFIER)) {
        p->error = "expected identifier after '@'";
        return -1;
    }
    char plot_name[YETTY_YEXPR_MAX_NAME_LEN];
    token_to_str(&p->current, plot_name, sizeof(plot_name));
    parser_advance(p);

    if (!parser_match(p, TOK_DOT)) {
        p->error = "expected '.' after plot name";
        return -1;
    }

    if (!parser_check(p, TOK_IDENTIFIER)) {
        p->error = "expected attribute name after '.'";
        return -1;
    }
    char attr_name[YETTY_YEXPR_MAX_NAME_LEN];
    token_to_str(&p->current, attr_name, sizeof(attr_name));
    parser_advance(p);

    if (!parser_match(p, TOK_EQUALS)) {
        p->error = "expected '=' after attribute name";
        return -1;
    }

    char value[64] = {0};
    if (parser_check(p, TOK_HEX_COLOR) || parser_check(p, TOK_STRING) ||
        parser_check(p, TOK_IDENTIFIER)) {
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

static int add_plot_def(struct parser *p, struct yetty_yexpr_plot_expr *plot, const char *name,
                        struct yetty_yexpr_node *expr)
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

struct yetty_yexpr_parse_result yetty_yexpr_parse(const char *source, size_t len)
{
    if (!source) {
        return YETTY_ERR(yetty_yexpr_parse, "source is NULL");
    }

    struct yetty_yexpr_parse_output res = {0};
    struct parser p = {0};
    lexer_init(&p.lex, source, len);
    p.arena = &res.arena;
    parser_advance(&p);

    res.root = parse_expr(&p);
    if (!res.root || p.error) {
        return YETTY_ERR(yetty_yexpr_parse, p.error ? p.error : "parse failed");
    }

    return YETTY_OK(yetty_yexpr_parse, res);
}

struct yetty_yexpr_plot_parse_result yetty_yexpr_parse_plot(const char *source, size_t len)
{
    if (!source) {
        return YETTY_ERR(yetty_yexpr_plot_parse, "source is NULL");
    }

    struct yetty_yexpr_plot_parse_output res = {0};
    struct parser p = {0};
    lexer_init(&p.lex, source, len);
    p.arena = &res.arena;
    parser_advance(&p);

    while (!parser_check(&p, TOK_EOF) && !p.error) {
        /* Skip semicolons */
        while (parser_match(&p, TOK_SEMICOLON)) {
            ;
        }
        if (parser_check(&p, TOK_EOF)) {
            break;
        }

        if (parser_check(&p, TOK_AT)) {
            /* Plot attribute: @name.attr = value */
            if (parse_plot_attr(&p, &res.plot) < 0) {
                break;
            }
        } else if (parser_check(&p, TOK_IDENTIFIER)) {
            /* Could be: name = expr  OR  bare expression */
            const char *name = p.current.start;
            size_t name_len = p.current.len;
            parser_advance(&p);

            if (parser_match(&p, TOK_EQUALS)) {
                /* Named definition: name = expr */
                struct yetty_yexpr_node *expr = parse_expr(&p);
                if (!expr) {
                    break;
                }
                char name_buf[YETTY_YEXPR_MAX_NAME_LEN];
                size_t copy_len = name_len < sizeof(name_buf) - 1 ? name_len : sizeof(name_buf) - 1;
                memcpy(name_buf, name, copy_len);
                name_buf[copy_len] = '\0';
                if (add_plot_def(&p, &res.plot, name_buf, expr) < 0) {
                    break;
                }
            } else {
                /* Bare expression */
                struct yetty_yexpr_node *expr = parse_bare_expr_from_ident(&p, name, name_len);
                if (!expr) {
                    break;
                }
                char auto_name[YETTY_YEXPR_MAX_NAME_LEN];
                snprintf(auto_name, sizeof(auto_name), "plot%u", res.plot.def_count + 1);
                if (add_plot_def(&p, &res.plot, auto_name, expr) < 0) {
                    break;
                }
            }
        } else if (parser_check(&p, TOK_NUMBER) || parser_check(&p, TOK_LPAREN) ||
                   parser_check(&p, TOK_MINUS)) {
            /* Bare expression starting with number/paren/unary */
            struct yetty_yexpr_node *expr = parse_expr(&p);
            if (!expr) {
                break;
            }
            char auto_name[YETTY_YEXPR_MAX_NAME_LEN];
            snprintf(auto_name, sizeof(auto_name), "plot%u", res.plot.def_count + 1);
            if (add_plot_def(&p, &res.plot, auto_name, expr) < 0) {
                break;
            }
        } else {
            p.error = "expected plot definition or expression";
            break;
        }

        parser_match(&p, TOK_SEMICOLON);
    }

    if (p.error) {
        return YETTY_ERR(yetty_yexpr_plot_parse, p.error);
    }

    return YETTY_OK(yetty_yexpr_plot_parse, res);
}
