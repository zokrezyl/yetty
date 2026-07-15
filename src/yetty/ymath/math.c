/*
 * math.c — TeX-subset math layout: tokenizer → node tree → box measure →
 * TEXT/SDF prim emission. See include/yetty/ymath/ymath.h for the subset
 * contract.
 *
 * Metrics use the same 0.55 × font_size × glyph_count width heuristic the
 * other GPU-less producers use (no font metrics client-side); the MSDF
 * glyphs stay crisp at any zoom, only advance widths are approximate.
 */

#include <yetty/ymath/ymath.h>

#include <yetty/yface/yface.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define YMATH_SCRIPT_SCALE 0.65f
#define YMATH_LIMIT_SCALE 0.60f
#define YMATH_GLYPH_WIDTH 0.55f
#define YMATH_ASCENT 0.78f
#define YMATH_DESCENT 0.24f
#define YMATH_AXIS 0.30f /* math axis height above baseline, in em */

/*=============================================================================
 * Node tree
 *===========================================================================*/

enum ymath_node_kind {
    YMATH_NODE_ROW,      /* children in sequence */
    YMATH_NODE_TEXT,     /* literal glyph run (utf8) */
    YMATH_NODE_FRACTION, /* child[0] over child[1] */
    YMATH_NODE_RADICAL,  /* sqrt of child[0] */
    YMATH_NODE_SCRIPTS,  /* child[0] base, child[1] sup?, child[2] sub? */
    YMATH_NODE_BIGOP,    /* glyph + child[0] lower?, child[1] upper? */
};

struct ymath_node {
    enum ymath_node_kind kind;
    char text[64]; /* TEXT: utf8 run; BIGOP: the operator glyph */
    struct ymath_node **children;
    size_t child_count;
    size_t child_capacity;
    /* filled by measure() */
    float width;
    float ascent;
    float descent;
};

static void node_destroy(struct ymath_node *node)
{
    if (!node) {
        return;
    }
    for (size_t i = 0; i < node->child_count; i++) {
        node_destroy(node->children[i]);
    }
    free(node->children);
    free(node);
}

static struct ymath_node *node_create(enum ymath_node_kind kind)
{
    struct ymath_node *node = calloc(1, sizeof(struct ymath_node));
    if (node) {
        node->kind = kind;
    }
    return node;
}

static bool node_append(struct ymath_node *parent, struct ymath_node *child)
{
    if (!parent || !child) {
        return false;
    }
    if (parent->child_count == parent->child_capacity) {
        size_t grown_capacity = parent->child_capacity ? parent->child_capacity * 2 : 4;
        struct ymath_node **grown =
            realloc(parent->children, grown_capacity * sizeof(struct ymath_node *));
        if (!grown) {
            return false;
        }
        parent->children = grown;
        parent->child_capacity = grown_capacity;
    }
    parent->children[parent->child_count++] = child;
    return true;
}

/*=============================================================================
 * Command table
 *===========================================================================*/

struct ymath_command {
    const char *name;
    const char *glyph;   /* utf8 replacement (NULL for structural commands) */
    bool roman_function; /* \sin etc — keep the name as upright text */
    bool big_operator;
};

static const struct ymath_command *ymath_command_lookup(const char *name, size_t name_len)
{
    static const struct ymath_command commands[] = {
        {"alpha", "\xce\xb1", false, false},
        {"beta", "\xce\xb2", false, false},
        {"gamma", "\xce\xb3", false, false},
        {"delta", "\xce\xb4", false, false},
        {"epsilon", "\xce\xb5", false, false},
        {"zeta", "\xce\xb6", false, false},
        {"eta", "\xce\xb7", false, false},
        {"theta", "\xce\xb8", false, false},
        {"iota", "\xce\xb9", false, false},
        {"kappa", "\xce\xba", false, false},
        {"lambda", "\xce\xbb", false, false},
        {"mu", "\xce\xbc", false, false},
        {"nu", "\xce\xbd", false, false},
        {"xi", "\xce\xbe", false, false},
        {"pi", "\xcf\x80", false, false},
        {"rho", "\xcf\x81", false, false},
        {"sigma", "\xcf\x83", false, false},
        {"tau", "\xcf\x84", false, false},
        {"upsilon", "\xcf\x85", false, false},
        {"phi", "\xcf\x86", false, false},
        {"chi", "\xcf\x87", false, false},
        {"psi", "\xcf\x88", false, false},
        {"omega", "\xcf\x89", false, false},
        {"Gamma", "\xce\x93", false, false},
        {"Delta", "\xce\x94", false, false},
        {"Theta", "\xce\x98", false, false},
        {"Lambda", "\xce\x9b", false, false},
        {"Xi", "\xce\x9e", false, false},
        {"Pi", "\xce\xa0", false, false},
        {"Sigma", "\xce\xa3", false, false},
        {"Phi", "\xce\xa6", false, false},
        {"Psi", "\xce\xa8", false, false},
        {"Omega", "\xce\xa9", false, false},
        {"pm", "\xc2\xb1", false, false},
        {"mp", "\xe2\x88\x93", false, false},
        {"times", "\xc3\x97", false, false},
        {"cdot", "\xc2\xb7", false, false},
        {"le", "\xe2\x89\xa4", false, false},
        {"leq", "\xe2\x89\xa4", false, false},
        {"ge", "\xe2\x89\xa5", false, false},
        {"geq", "\xe2\x89\xa5", false, false},
        {"ne", "\xe2\x89\xa0", false, false},
        {"neq", "\xe2\x89\xa0", false, false},
        {"approx", "\xe2\x89\x88", false, false},
        {"infty", "\xe2\x88\x9e", false, false},
        {"partial", "\xe2\x88\x82", false, false},
        {"nabla", "\xe2\x88\x87", false, false},
        {"to", "\xe2\x86\x92", false, false},
        {"rightarrow", "\xe2\x86\x92", false, false},
        {"hbar", "\xc4\xa7", false, false},
        {"ell", "\xe2\x84\x93", false, false},
        {"dots", "\xe2\x80\xa6", false, false},
        {"cdots", "\xe2\x8b\xaf", false, false},
        {"prime", "\xe2\x80\xb2", false, false},
        {"sum", "\xe2\x88\x91", false, true},
        {"int", "\xe2\x88\xab", false, true},
        {"prod", "\xe2\x88\x8f", false, true},
        {"sin", NULL, true, false},
        {"cos", NULL, true, false},
        {"tan", NULL, true, false},
        {"log", NULL, true, false},
        {"ln", NULL, true, false},
        {"exp", NULL, true, false},
        {NULL, NULL, false, false},
    };
    for (size_t i = 0; commands[i].name; i++) {
        if (strlen(commands[i].name) == name_len &&
            strncmp(commands[i].name, name, name_len) == 0) {
            return &commands[i];
        }
    }
    return NULL;
}

/*=============================================================================
 * Parser
 *===========================================================================*/

struct ymath_parser {
    const char *cursor;
    const char *end;
    int depth;
};

static struct ymath_node *parse_row(struct ymath_parser *parser, char stop);

/* One "atom": a group, a command, or a literal glyph run. NULL at end. */
static struct ymath_node *parse_atom(struct ymath_parser *parser, char stop)
{
    while (parser->cursor < parser->end && (*parser->cursor == ' ' || *parser->cursor == '\t' ||
                                            *parser->cursor == '\n' || *parser->cursor == '\r')) {
        parser->cursor++;
    }
    if (parser->cursor >= parser->end || *parser->cursor == stop || *parser->cursor == '}') {
        return NULL;
    }
    if (parser->depth > 32) {
        return NULL; /* runaway nesting */
    }

    char head = *parser->cursor;
    if (head == '{') {
        parser->cursor++;
        parser->depth++;
        struct ymath_node *group = parse_row(parser, '}');
        parser->depth--;
        if (parser->cursor < parser->end && *parser->cursor == '}') {
            parser->cursor++;
        }
        return group;
    }

    if (head == '\\') {
        parser->cursor++;
        const char *name_start = parser->cursor;
        while (parser->cursor < parser->end &&
               ((*parser->cursor >= 'a' && *parser->cursor <= 'z') ||
                (*parser->cursor >= 'A' && *parser->cursor <= 'Z'))) {
            parser->cursor++;
        }
        size_t name_len = (size_t)(parser->cursor - name_start);
        if (name_len == 0) {
            /* escaped single char, e.g. \{ \} \\ — emit literally */
            if (parser->cursor < parser->end) {
                struct ymath_node *literal = node_create(YMATH_NODE_TEXT);
                if (literal) {
                    literal->text[0] = *parser->cursor;
                }
                parser->cursor++;
                return literal;
            }
            return NULL;
        }

        if (name_len == 4 && strncmp(name_start, "frac", 4) == 0) {
            struct ymath_node *fraction = node_create(YMATH_NODE_FRACTION);
            struct ymath_node *numerator = parse_atom(parser, stop);
            struct ymath_node *denominator = parse_atom(parser, stop);
            if (!fraction || !numerator || !denominator || !node_append(fraction, numerator) ||
                !node_append(fraction, denominator)) {
                node_destroy(fraction);
                if (fraction == NULL || fraction->child_count < 1) {
                    node_destroy(numerator);
                }
                node_destroy(denominator);
                return NULL;
            }
            return fraction;
        }
        if (name_len == 4 && strncmp(name_start, "sqrt", 4) == 0) {
            struct ymath_node *radical = node_create(YMATH_NODE_RADICAL);
            struct ymath_node *body = parse_atom(parser, stop);
            if (!radical || !body || !node_append(radical, body)) {
                node_destroy(radical);
                node_destroy(body);
                return NULL;
            }
            return radical;
        }
        if ((name_len == 4 && strncmp(name_start, "left", 4) == 0) ||
            (name_len == 5 && strncmp(name_start, "right", 5) == 0)) {
            /* growth not implemented: keep the delimiter char itself */
            if (parser->cursor < parser->end && *parser->cursor != stop) {
                struct ymath_node *delimiter = node_create(YMATH_NODE_TEXT);
                if (delimiter) {
                    if (*parser->cursor == '.') {
                        delimiter->text[0] = '\0'; /* \left. invisible */
                    } else {
                        delimiter->text[0] = *parser->cursor;
                    }
                }
                parser->cursor++;
                return delimiter;
            }
            return node_create(YMATH_NODE_TEXT);
        }

        const struct ymath_command *command = ymath_command_lookup(name_start, name_len);
        if (command && command->big_operator) {
            struct ymath_node *big = node_create(YMATH_NODE_BIGOP);
            if (big) {
                snprintf(big->text, sizeof big->text, "%s", command->glyph);
            }
            return big;
        }
        struct ymath_node *symbol = node_create(YMATH_NODE_TEXT);
        if (!symbol) {
            return NULL;
        }
        if (command && command->glyph) {
            snprintf(symbol->text, sizeof symbol->text, "%s", command->glyph);
        } else if (command && command->roman_function) {
            snprintf(symbol->text, sizeof symbol->text, "%.*s", (int)name_len, name_start);
        } else {
            /* unknown command: show its name so typos stay visible */
            snprintf(symbol->text, sizeof symbol->text, "%.*s", (int)name_len, name_start);
        }
        return symbol;
    }

    /* Literal run: consume until a structural character. */
    struct ymath_node *literal = node_create(YMATH_NODE_TEXT);
    if (!literal) {
        return NULL;
    }
    size_t out = 0;
    while (parser->cursor < parser->end && out < sizeof(literal->text) - 1) {
        char c = *parser->cursor;
        if (c == stop || c == '{' || c == '}' || c == '\\' || c == '^' || c == '_' || c == ' ' ||
            c == '\t' || c == '\n' || c == '\r') {
            break;
        }
        literal->text[out++] = c;
        parser->cursor++;
    }
    literal->text[out] = '\0';
    if (out == 0) {
        node_destroy(literal);
        return NULL;
    }
    return literal;
}

/* Row: atoms with optional ^/_ scripts (also feeding big-operator limits). */
static struct ymath_node *parse_row(struct ymath_parser *parser, char stop)
{
    struct ymath_node *row = node_create(YMATH_NODE_ROW);
    if (!row) {
        return NULL;
    }
    for (;;) {
        struct ymath_node *atom = parse_atom(parser, stop);
        if (!atom) {
            break;
        }
        /* Attach any ^ / _ scripts to this atom. */
        struct ymath_node *superscript = NULL;
        struct ymath_node *subscript = NULL;
        for (;;) {
            while (parser->cursor < parser->end && *parser->cursor == ' ') {
                parser->cursor++;
            }
            if (parser->cursor < parser->end && *parser->cursor == '^' && !superscript) {
                parser->cursor++;
                superscript = parse_atom(parser, stop);
            } else if (parser->cursor < parser->end && *parser->cursor == '_' && !subscript) {
                parser->cursor++;
                subscript = parse_atom(parser, stop);
            } else {
                break;
            }
        }
        if (superscript || subscript) {
            if (atom->kind == YMATH_NODE_BIGOP) {
                /* children: [0] lower limit (may be empty row), [1] upper */
                struct ymath_node *lower = subscript ? subscript : node_create(YMATH_NODE_ROW);
                struct ymath_node *upper = superscript ? superscript : node_create(YMATH_NODE_ROW);
                if (!node_append(atom, lower) || !node_append(atom, upper)) {
                    node_destroy(atom);
                    node_destroy(row);
                    return NULL;
                }
            } else {
                struct ymath_node *scripts = node_create(YMATH_NODE_SCRIPTS);
                struct ymath_node *sup_slot =
                    superscript ? superscript : node_create(YMATH_NODE_ROW);
                struct ymath_node *sub_slot = subscript ? subscript : node_create(YMATH_NODE_ROW);
                if (!scripts || !node_append(scripts, atom) || !node_append(scripts, sup_slot) ||
                    !node_append(scripts, sub_slot)) {
                    node_destroy(scripts);
                    node_destroy(row);
                    return NULL;
                }
                atom = scripts;
            }
        }
        if (!node_append(row, atom)) {
            node_destroy(atom);
            node_destroy(row);
            return NULL;
        }
    }
    return row;
}

/*=============================================================================
 * Measure + emit
 *===========================================================================*/

static size_t utf8_glyph_count(const char *text)
{
    size_t count = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor;) {
        cursor += (*cursor & 0x80) == 0        ? 1
                  : ((*cursor & 0xE0) == 0xC0) ? 2
                  : ((*cursor & 0xF0) == 0xE0) ? 3
                                               : 4;
        count++;
    }
    return count;
}

static void measure(struct ymath_node *node, float font_size)
{
    switch (node->kind) {
    case YMATH_NODE_TEXT: {
        node->width = YMATH_GLYPH_WIDTH * font_size * (float)utf8_glyph_count(node->text);
        node->ascent = YMATH_ASCENT * font_size;
        node->descent = YMATH_DESCENT * font_size;
        break;
    }
    case YMATH_NODE_ROW: {
        float pad = font_size * 0.08f;
        node->width = 0.0f;
        node->ascent = YMATH_ASCENT * font_size * 0.5f;
        node->descent = YMATH_DESCENT * font_size * 0.5f;
        for (size_t i = 0; i < node->child_count; i++) {
            measure(node->children[i], font_size);
            node->width += node->children[i]->width + (i > 0 ? pad : 0.0f);
            if (node->children[i]->ascent > node->ascent) {
                node->ascent = node->children[i]->ascent;
            }
            if (node->children[i]->descent > node->descent) {
                node->descent = node->children[i]->descent;
            }
        }
        break;
    }
    case YMATH_NODE_FRACTION: {
        measure(node->children[0], font_size);
        measure(node->children[1], font_size);
        float gap = font_size * 0.12f;
        float axis = YMATH_AXIS * font_size;
        float widest = node->children[0]->width > node->children[1]->width
                           ? node->children[0]->width
                           : node->children[1]->width;
        node->width = widest + font_size * 0.3f;
        node->ascent = axis + gap + node->children[0]->ascent + node->children[0]->descent;
        node->descent = -axis + gap + node->children[1]->ascent + node->children[1]->descent;
        break;
    }
    case YMATH_NODE_RADICAL: {
        measure(node->children[0], font_size);
        node->width = node->children[0]->width + font_size * 0.75f;
        node->ascent = node->children[0]->ascent + font_size * 0.22f;
        node->descent = node->children[0]->descent;
        break;
    }
    case YMATH_NODE_SCRIPTS: {
        struct ymath_node *base = node->children[0];
        struct ymath_node *superscript = node->children[1];
        struct ymath_node *subscript = node->children[2];
        measure(base, font_size);
        measure(superscript, font_size * YMATH_SCRIPT_SCALE);
        measure(subscript, font_size * YMATH_SCRIPT_SCALE);
        float script_width =
            superscript->width > subscript->width ? superscript->width : subscript->width;
        node->width = base->width + script_width + font_size * 0.05f;
        float sup_rise = font_size * 0.42f;
        float sub_drop = font_size * 0.18f;
        node->ascent = base->ascent;
        if (superscript->child_count || superscript->kind == YMATH_NODE_TEXT) {
            float sup_top = sup_rise + superscript->ascent;
            if (sup_top > node->ascent) {
                node->ascent = sup_top;
            }
        }
        node->descent = base->descent;
        float sub_bottom = sub_drop + subscript->descent;
        if ((subscript->child_count || subscript->kind == YMATH_NODE_TEXT) &&
            sub_bottom > node->descent) {
            node->descent = sub_bottom;
        }
        break;
    }
    case YMATH_NODE_BIGOP: {
        float op_size = font_size * 1.45f;
        node->width = YMATH_GLYPH_WIDTH * op_size;
        node->ascent = YMATH_ASCENT * op_size - font_size * 0.15f;
        node->descent = YMATH_DESCENT * op_size + font_size * 0.15f;
        if (node->child_count == 2) {
            measure(node->children[0], font_size * YMATH_LIMIT_SCALE);
            measure(node->children[1], font_size * YMATH_LIMIT_SCALE);
            float widest = node->width;
            if (node->children[0]->width > widest) {
                widest = node->children[0]->width;
            }
            if (node->children[1]->width > widest) {
                widest = node->children[1]->width;
            }
            node->width = widest;
            node->ascent +=
                node->children[1]->ascent + node->children[1]->descent + font_size * 0.1f;
            node->descent +=
                node->children[0]->ascent + node->children[0]->descent + font_size * 0.1f;
        }
        break;
    }
    }
}

struct ymath_emit_context {
    struct yetty_ydraw_drawable_list *list;
    uint32_t color;
    uint32_t next_id;
};

static struct yetty_ycore_void_result emit_text(struct ymath_emit_context *context, float x,
                                                float baseline, const char *text, float font_size)
{
    size_t length = strlen(text);
    if (length == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_buffer view = {
        .data = (uint8_t *)(uintptr_t)text,
        .capacity = length,
        .size = length,
    };
    return yetty_ydraw_drawable_list_add_text(context->list, x, baseline, &view, font_size,
                                              context->color, 0, -1, 0.0f);
}

static struct yetty_ycore_void_result emit_rule(struct ymath_emit_context *context, float x,
                                                float y, float width, float thickness)
{
    struct yetty_ysdf_box rule = {
        .center_x = x + width * 0.5f,
        .center_y = y,
        .half_width = width * 0.5f,
        .half_height = thickness * 0.5f,
        .corner_radius = 0.0f,
    };
    /* Text color arrives 0xAABBGGRR (BGR order); SDF boxes read R in the
     * low byte — swap R and B so rules match the glyph color. */
    uint32_t swapped = (context->color & 0xFF00FF00u) | ((context->color >> 16) & 0xFFu) |
                       ((context->color & 0xFFu) << 16);
    return yetty_ydraw_drawable_list_add_cmd_add_box(context->list, 0u, context->next_id++, swapped,
                                                     0u, 0.0f, &rule);
}

static struct yetty_ycore_void_result emit_node(struct ymath_emit_context *context,
                                                struct ymath_node *node, float x, float baseline,
                                                float font_size)
{
    switch (node->kind) {
    case YMATH_NODE_TEXT:
        return emit_text(context, x, baseline, node->text, font_size);
    case YMATH_NODE_ROW: {
        float pad = font_size * 0.08f;
        float cursor_x = x;
        for (size_t i = 0; i < node->child_count; i++) {
            struct yetty_ycore_void_result child_res =
                emit_node(context, node->children[i], cursor_x, baseline, font_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "ymath: row child");
            cursor_x += node->children[i]->width + pad;
        }
        return YETTY_OK_VOID();
    }
    case YMATH_NODE_FRACTION: {
        struct ymath_node *numerator = node->children[0];
        struct ymath_node *denominator = node->children[1];
        float gap = font_size * 0.12f;
        float axis = YMATH_AXIS * font_size;
        float bar_y = baseline - axis;
        struct yetty_ycore_void_result bar_res =
            emit_rule(context, x, bar_y, node->width, font_size * 0.055f + 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bar_res, "ymath: fraction bar");
        float numerator_baseline = bar_y - gap - numerator->descent;
        float denominator_baseline = bar_y + gap + denominator->ascent;
        struct yetty_ycore_void_result numerator_res =
            emit_node(context, numerator, x + (node->width - numerator->width) * 0.5f,
                      numerator_baseline, font_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, numerator_res, "ymath: numerator");
        return emit_node(context, denominator, x + (node->width - denominator->width) * 0.5f,
                         denominator_baseline, font_size);
    }
    case YMATH_NODE_RADICAL: {
        struct ymath_node *body = node->children[0];
        float glyph_size = (body->ascent + body->descent) * 1.05f;
        if (glyph_size < font_size) {
            glyph_size = font_size;
        }
        /* √ glyph sized to the body, then an overbar to its right. */
        struct yetty_ycore_void_result glyph_res =
            emit_text(context, x, baseline + body->descent, "\xe2\x88\x9a", glyph_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, glyph_res, "ymath: radical glyph");
        float bar_left = x + YMATH_GLYPH_WIDTH * glyph_size * 0.92f;
        float bar_y = baseline - body->ascent - font_size * 0.12f;
        struct yetty_ycore_void_result bar_res = emit_rule(
            context, bar_left, bar_y, x + node->width - bar_left, font_size * 0.055f + 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bar_res, "ymath: radical bar");
        return emit_node(context, body, x + font_size * 0.7f, baseline, font_size);
    }
    case YMATH_NODE_SCRIPTS: {
        struct ymath_node *base = node->children[0];
        struct ymath_node *superscript = node->children[1];
        struct ymath_node *subscript = node->children[2];
        struct yetty_ycore_void_result base_res = emit_node(context, base, x, baseline, font_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, base_res, "ymath: script base");
        float script_x = x + base->width + font_size * 0.05f;
        struct yetty_ycore_void_result sup_res =
            emit_node(context, superscript, script_x, baseline - font_size * 0.42f,
                      font_size * YMATH_SCRIPT_SCALE);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sup_res, "ymath: superscript");
        return emit_node(context, subscript, script_x,
                         baseline + font_size * 0.18f + subscript->ascent * 0.4f,
                         font_size * YMATH_SCRIPT_SCALE);
    }
    case YMATH_NODE_BIGOP: {
        float op_size = font_size * 1.45f;
        float op_width = YMATH_GLYPH_WIDTH * op_size;
        float op_x = x + (node->width - op_width) * 0.5f;
        struct yetty_ycore_void_result op_res =
            emit_text(context, op_x, baseline + font_size * 0.15f, node->text, op_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, op_res, "ymath: big operator");
        if (node->child_count == 2) {
            struct ymath_node *lower = node->children[0];
            struct ymath_node *upper = node->children[1];
            float op_ascent = YMATH_ASCENT * op_size - font_size * 0.15f;
            float op_descent = YMATH_DESCENT * op_size + font_size * 0.15f;
            struct yetty_ycore_void_result upper_res =
                emit_node(context, upper, x + (node->width - upper->width) * 0.5f,
                          baseline - op_ascent - upper->descent - font_size * 0.1f,
                          font_size * YMATH_LIMIT_SCALE);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, upper_res, "ymath: upper limit");
            struct yetty_ycore_void_result lower_res =
                emit_node(context, lower, x + (node->width - lower->width) * 0.5f,
                          baseline + op_descent + lower->ascent + font_size * 0.1f,
                          font_size * YMATH_LIMIT_SCALE);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, lower_res, "ymath: lower limit");
        }
        return YETTY_OK_VOID();
    }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ydraw_drawable_list_result yetty_ymath_render(
    const char *source, size_t source_len, const struct yetty_ymath_render_config *config)
{
    if (!source || source_len == 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymath: empty source");
    }

    float font_size = config && config->font_size > 0.0f ? config->font_size : 28.0f;
    uint32_t color = config && config->color ? config->color : 0xFFE4E5E0u;
    float origin_x = config ? config->origin_x : 0.0f;
    float origin_y = config ? config->origin_y : 0.0f;

    struct ymath_parser parser = {
        .cursor = source,
        .end = source + source_len,
    };
    struct ymath_node *root = parse_row(&parser, '\0');
    if (!root) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymath: parse failed");
    }
    measure(root, font_size);

    float margin = font_size * 0.4f;
    struct yetty_ydraw_drawable_list_config list_config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = origin_x + root->width + margin * 2.0f,
        .scene_max_y = origin_y + root->ascent + root->descent + margin * 2.0f,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_config);
    if (YETTY_IS_ERR(list_res)) {
        node_destroy(root);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymath: list create", list_res);
    }

    struct ymath_emit_context context = {
        .list = list_res.value,
        .color = color,
        .next_id = 3000,
    };
    struct yetty_ycore_void_result emit_res =
        emit_node(&context, root, origin_x + margin, origin_y + margin + root->ascent, font_size);
    node_destroy(root);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "ymath: emit", emit_res);
    }
    return list_res;
}

struct yetty_ycore_size_result yetty_ymath_osc_emit(const struct yetty_ydraw_drawable_list *list,
                                                    FILE *out)
{
    if (!list || !out) {
        return YETTY_ERR(yetty_ycore_size, "ymath_osc_emit: NULL list or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "ymath_osc_emit: empty serialize");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "ymath_osc_emit: yface_emit failed", emit_res);
    }
    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
