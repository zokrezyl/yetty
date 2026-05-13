/*
 * ysvg-parse.c — XML → struct yetty_ysvg_doc using yxml (Yorhel/yxml).
 *
 * yxml is a SAX-style state machine: we feed it bytes and it returns
 * events. We accumulate element/attribute names and content into heap
 * buffers and build our DOM as we go.
 *
 * yxml handles for us: well-formedness, entities, CDATA, comments, PIs,
 * namespace prefixes (passed through verbatim in elem/attr names).
 */

#include "ysvg-internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yxml.h>

#define YSVG_NODE_CHUNK_SIZE 64
#define YSVG_YXML_BUFSIZE 4096
#define YSVG_DEPTH_MAX 256

struct yetty_ysvg_node_chunk {
    struct yetty_ysvg_node_chunk *next;
    size_t used;
    struct yetty_ysvg_node nodes[YSVG_NODE_CHUNK_SIZE];
};

/*=============================================================================
 * Element / attribute name lookup
 *===========================================================================*/

struct ysvg_keyword {
    const char *name;
    int code;
};

static const struct ysvg_keyword k_elems[] = {
    {"svg", YETTY_YSVG_ELEM_SVG},
    {"g", YETTY_YSVG_ELEM_G},
    {"defs", YETTY_YSVG_ELEM_DEFS},
    {"use", YETTY_YSVG_ELEM_USE},
    {"title", YETTY_YSVG_ELEM_TITLE},
    {"desc", YETTY_YSVG_ELEM_DESC},
    {"metadata", YETTY_YSVG_ELEM_METADATA},
    {"rect", YETTY_YSVG_ELEM_RECT},
    {"circle", YETTY_YSVG_ELEM_CIRCLE},
    {"ellipse", YETTY_YSVG_ELEM_ELLIPSE},
    {"line", YETTY_YSVG_ELEM_LINE},
    {"polyline", YETTY_YSVG_ELEM_POLYLINE},
    {"polygon", YETTY_YSVG_ELEM_POLYGON},
    {"path", YETTY_YSVG_ELEM_PATH},
    {"text", YETTY_YSVG_ELEM_TEXT},
    {"tspan", YETTY_YSVG_ELEM_TSPAN},
    {"textArea", YETTY_YSVG_ELEM_TEXTAREA},
    {"image", YETTY_YSVG_ELEM_IMAGE},
    {"a", YETTY_YSVG_ELEM_A},
    {"switch", YETTY_YSVG_ELEM_SWITCH},
    {"font", YETTY_YSVG_ELEM_FONT},
    {"linearGradient", YETTY_YSVG_ELEM_LINEARGRADIENT},
    {"radialGradient", YETTY_YSVG_ELEM_RADIALGRADIENT},
    {"stop", YETTY_YSVG_ELEM_STOP},
};

static const struct ysvg_keyword k_attrs[] = {
    {"id", YETTY_YSVG_ATTR_ID},
    {"class", YETTY_YSVG_ATTR_CLASS},
    {"style", YETTY_YSVG_ATTR_STYLE},
    {"viewBox", YETTY_YSVG_ATTR_VIEWBOX},
    {"width", YETTY_YSVG_ATTR_WIDTH},
    {"height", YETTY_YSVG_ATTR_HEIGHT},
    {"x", YETTY_YSVG_ATTR_X},
    {"y", YETTY_YSVG_ATTR_Y},
    {"x1", YETTY_YSVG_ATTR_X1},
    {"y1", YETTY_YSVG_ATTR_Y1},
    {"x2", YETTY_YSVG_ATTR_X2},
    {"y2", YETTY_YSVG_ATTR_Y2},
    {"cx", YETTY_YSVG_ATTR_CX},
    {"cy", YETTY_YSVG_ATTR_CY},
    {"r", YETTY_YSVG_ATTR_R},
    {"rx", YETTY_YSVG_ATTR_RX},
    {"ry", YETTY_YSVG_ATTR_RY},
    {"fx", YETTY_YSVG_ATTR_FX},
    {"fy", YETTY_YSVG_ATTR_FY},
    {"d", YETTY_YSVG_ATTR_D},
    {"points", YETTY_YSVG_ATTR_POINTS},
    {"transform", YETTY_YSVG_ATTR_TRANSFORM},
    {"gradientTransform", YETTY_YSVG_ATTR_GRADIENTTRANSFORM},
    {"fill", YETTY_YSVG_ATTR_FILL},
    {"fill-opacity", YETTY_YSVG_ATTR_FILL_OPACITY},
    {"fill-rule", YETTY_YSVG_ATTR_FILL_RULE},
    {"stroke", YETTY_YSVG_ATTR_STROKE},
    {"stroke-opacity", YETTY_YSVG_ATTR_STROKE_OPACITY},
    {"stroke-width", YETTY_YSVG_ATTR_STROKE_WIDTH},
    {"stroke-linecap", YETTY_YSVG_ATTR_STROKE_LINECAP},
    {"stroke-linejoin", YETTY_YSVG_ATTR_STROKE_LINEJOIN},
    {"stroke-dasharray", YETTY_YSVG_ATTR_STROKE_DASHARRAY},
    {"stroke-miterlimit", YETTY_YSVG_ATTR_STROKE_MITERLIMIT},
    {"opacity", YETTY_YSVG_ATTR_OPACITY},
    {"display", YETTY_YSVG_ATTR_DISPLAY},
    {"visibility", YETTY_YSVG_ATTR_VISIBILITY},
    {"font-family", YETTY_YSVG_ATTR_FONT_FAMILY},
    {"font-size", YETTY_YSVG_ATTR_FONT_SIZE},
    {"font-weight", YETTY_YSVG_ATTR_FONT_WEIGHT},
    {"font-style", YETTY_YSVG_ATTR_FONT_STYLE},
    {"text-anchor", YETTY_YSVG_ATTR_TEXT_ANCHOR},
    {"href", YETTY_YSVG_ATTR_HREF},
    {"xlink:href", YETTY_YSVG_ATTR_XLINK_HREF},
    {"offset", YETTY_YSVG_ATTR_OFFSET},
    {"stop-color", YETTY_YSVG_ATTR_STOP_COLOR},
    {"stop-opacity", YETTY_YSVG_ATTR_STOP_OPACITY},
    {"gradientUnits", YETTY_YSVG_ATTR_GRADIENTUNITS},
    {"spreadMethod", YETTY_YSVG_ATTR_SPREADMETHOD},
    {"preserveAspectRatio", YETTY_YSVG_ATTR_PRESERVEASPECTRATIO},
};

static enum yetty_ysvg_elem elem_lookup(const char *name)
{
    const char *local = name;
    const char *colon = strchr(name, ':');
    if (colon) local = colon + 1;
    for (size_t i = 0; i < sizeof(k_elems) / sizeof(k_elems[0]); i++) {
        if (strcmp(local, k_elems[i].name) == 0) {
            return (enum yetty_ysvg_elem)k_elems[i].code;
        }
    }
    return YETTY_YSVG_ELEM_UNKNOWN;
}

static enum yetty_ysvg_attr_key attr_lookup(const char *name)
{
    for (size_t i = 0; i < sizeof(k_attrs) / sizeof(k_attrs[0]); i++) {
        if (strcmp(name, k_attrs[i].name) == 0) {
            return (enum yetty_ysvg_attr_key)k_attrs[i].code;
        }
    }
    return YETTY_YSVG_ATTR_UNKNOWN;
}

/*=============================================================================
 * Allocation helpers
 *===========================================================================*/

static struct yetty_ysvg_node *doc_alloc_node(struct yetty_ysvg_doc *doc)
{
    struct yetty_ysvg_node_chunk *c = doc->node_chunks;
    if (!c || c->used == YSVG_NODE_CHUNK_SIZE) {
        struct yetty_ysvg_node_chunk *nc = calloc(1, sizeof(struct yetty_ysvg_node_chunk));
        if (!nc) return NULL;
        nc->next = doc->node_chunks;
        doc->node_chunks = nc;
        c = nc;
    }
    struct yetty_ysvg_node *n = &c->nodes[c->used++];
    memset(n, 0, sizeof(*n));
    return n;
}

static void node_append_child(struct yetty_ysvg_node *parent, struct yetty_ysvg_node *child)
{
    child->parent = parent;
    child->next_sibling = NULL;
    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
}

/* Append a byte to a growable heap buffer. */
static int buf_append(char **buf, size_t *len, size_t *cap, const char *src, size_t n)
{
    if (n == 0) return 0;
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 16;
        while (nc < *len + n + 1) nc *= 2;
        char *nb = realloc(*buf, nc);
        if (!nb) return -1;
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

/*=============================================================================
 * Parser driver
 *===========================================================================*/

struct ysvg_parse_ctx {
    struct yetty_ysvg_doc *doc;
    /* Element stack — node pointers from the chunk arena. */
    struct yetty_ysvg_node *stack[YSVG_DEPTH_MAX];
    int depth;
    /* Current element's accumulated text. */
    char *text_buf;
    size_t text_len, text_cap;
    /* Current attribute being assembled. */
    char *attr_name; /* strdup'd at ATTRSTART */
    char *attr_val;
    size_t attr_val_len, attr_val_cap;
    const char *err;
};

static void ctx_reset_text(struct ysvg_parse_ctx *c)
{
    free(c->text_buf);
    c->text_buf = NULL;
    c->text_len = c->text_cap = 0;
}

static void ctx_reset_attr(struct ysvg_parse_ctx *c)
{
    free(c->attr_name);
    free(c->attr_val);
    c->attr_name = NULL;
    c->attr_val = NULL;
    c->attr_val_len = c->attr_val_cap = 0;
}

static int handle_elemstart(struct ysvg_parse_ctx *ctx, const char *name)
{
    if (ctx->depth >= YSVG_DEPTH_MAX) {
        ctx->err = "ysvg-parse: nesting too deep";
        return -1;
    }
    struct yetty_ysvg_node *n = doc_alloc_node(ctx->doc);
    if (!n) {
        ctx->err = "ysvg-parse: out of memory (node)";
        return -1;
    }
    size_t nl = strlen(name);
    n->tag_name = malloc(nl + 1);
    if (!n->tag_name) {
        ctx->err = "ysvg-parse: out of memory (tag name)";
        return -1;
    }
    memcpy(n->tag_name, name, nl + 1);
    n->tag_name_len = nl;
    n->elem = elem_lookup(name);

    if (ctx->depth == 0) {
        ctx->doc->root = n;
    } else {
        node_append_child(ctx->stack[ctx->depth - 1], n);
    }
    ctx->stack[ctx->depth++] = n;
    ctx_reset_text(ctx);
    return 0;
}

static int handle_elemend(struct ysvg_parse_ctx *ctx)
{
    if (ctx->depth <= 0) {
        ctx->err = "ysvg-parse: stack underflow";
        return -1;
    }
    struct yetty_ysvg_node *n = ctx->stack[ctx->depth - 1];
    /* Trim leading/trailing whitespace from text — SVG indentation
     * shouldn't leak into rendered <text>/<tspan> content. */
    if (ctx->text_buf) {
        size_t s = 0, e = ctx->text_len;
        while (s < e && (ctx->text_buf[s] == ' ' || ctx->text_buf[s] == '\t' ||
                         ctx->text_buf[s] == '\n' || ctx->text_buf[s] == '\r'))
            s++;
        while (e > s && (ctx->text_buf[e - 1] == ' ' || ctx->text_buf[e - 1] == '\t' ||
                         ctx->text_buf[e - 1] == '\n' || ctx->text_buf[e - 1] == '\r'))
            e--;
        if (e > s) {
            size_t tl = e - s;
            if (s > 0) memmove(ctx->text_buf, ctx->text_buf + s, tl);
            ctx->text_buf[tl] = '\0';
            n->text = ctx->text_buf;
            n->text_len = tl;
        } else {
            free(ctx->text_buf);
        }
        ctx->text_buf = NULL;
        ctx->text_len = ctx->text_cap = 0;
    }
    ctx->depth--;
    return 0;
}

static int handle_attrstart(struct ysvg_parse_ctx *ctx, const char *name)
{
    ctx_reset_attr(ctx);
    ctx->attr_name = strdup(name);
    if (!ctx->attr_name) {
        ctx->err = "ysvg-parse: out of memory (attr name)";
        return -1;
    }
    return 0;
}

static int handle_attrend(struct ysvg_parse_ctx *ctx)
{
    if (ctx->depth <= 0 || !ctx->attr_name) {
        ctx->err = "ysvg-parse: orphan attribute";
        return -1;
    }
    struct yetty_ysvg_node *n = ctx->stack[ctx->depth - 1];
    if (n->attr_count == n->attr_cap) {
        size_t nc = n->attr_cap ? n->attr_cap * 2 : 4;
        struct yetty_ysvg_attr *na = realloc(n->attrs, nc * sizeof(*na));
        if (!na) {
            ctx->err = "ysvg-parse: out of memory (attr array)";
            return -1;
        }
        n->attrs = na;
        n->attr_cap = nc;
    }
    struct yetty_ysvg_attr a = {0};
    a.name = ctx->attr_name;
    a.name_len = strlen(ctx->attr_name);
    a.key = attr_lookup(ctx->attr_name);
    if (ctx->attr_val) {
        a.value = ctx->attr_val;
        a.value_len = ctx->attr_val_len;
    } else {
        a.value = strdup("");
        if (!a.value) {
            ctx->err = "ysvg-parse: out of memory (attr value)";
            free(ctx->attr_name);
            ctx->attr_name = NULL;
            return -1;
        }
        a.value_len = 0;
    }
    n->attrs[n->attr_count++] = a;
    /* Ownership transferred into the attr — clear pointers in ctx. */
    ctx->attr_name = NULL;
    ctx->attr_val = NULL;
    ctx->attr_val_len = ctx->attr_val_cap = 0;
    return 0;
}

static int handle_content(struct ysvg_parse_ctx *ctx, const char *data)
{
    /* yxml.data is a NUL-terminated short chunk (max 8 bytes). */
    size_t n = strlen(data);
    if (buf_append(&ctx->text_buf, &ctx->text_len, &ctx->text_cap, data, n) < 0) {
        ctx->err = "ysvg-parse: out of memory (text)";
        return -1;
    }
    return 0;
}

static int handle_attrval(struct ysvg_parse_ctx *ctx, const char *data)
{
    size_t n = strlen(data);
    if (buf_append(&ctx->attr_val, &ctx->attr_val_len, &ctx->attr_val_cap, data, n) < 0) {
        ctx->err = "ysvg-parse: out of memory (attr value)";
        return -1;
    }
    return 0;
}

/*=============================================================================
 * Public entry: parse + destroy
 *===========================================================================*/

struct yetty_ysvg_doc_ptr_result yetty_ysvg_parse(const char *src, size_t len)
{
    if (!src && len > 0) {
        return YETTY_ERR(yetty_ysvg_doc_ptr, "ysvg-parse: src is NULL but len > 0");
    }
    struct yetty_ysvg_doc *doc = calloc(1, sizeof(struct yetty_ysvg_doc));
    if (!doc) {
        return YETTY_ERR(yetty_ysvg_doc_ptr, "ysvg-parse: out of memory (doc)");
    }

    /* yxml workspace lives on the heap — names/attrs above a few bytes
     * spill into this buffer. */
    void *yxbuf = malloc(YSVG_YXML_BUFSIZE);
    if (!yxbuf) {
        free(doc);
        return YETTY_ERR(yetty_ysvg_doc_ptr, "ysvg-parse: out of memory (yxml buf)");
    }
    yxml_t x;
    yxml_init(&x, yxbuf, YSVG_YXML_BUFSIZE);

    struct ysvg_parse_ctx ctx = {0};
    ctx.doc = doc;

    int rc = 0;
    for (size_t i = 0; i < len; i++) {
        yxml_ret_t r = yxml_parse(&x, (unsigned char)src[i]);
        if (r < 0) {
            ctx.err = "ysvg-parse: yxml syntax error";
            rc = -1;
            break;
        }
        switch (r) {
        case YXML_OK:
        case YXML_PISTART:
        case YXML_PICONTENT:
        case YXML_PIEND:
            break;
        case YXML_ELEMSTART:
            if (handle_elemstart(&ctx, x.elem) < 0) rc = -1;
            break;
        case YXML_ELEMEND:
            if (handle_elemend(&ctx) < 0) rc = -1;
            break;
        case YXML_ATTRSTART:
            if (handle_attrstart(&ctx, x.attr) < 0) rc = -1;
            break;
        case YXML_ATTRVAL:
            if (handle_attrval(&ctx, x.data) < 0) rc = -1;
            break;
        case YXML_ATTREND:
            if (handle_attrend(&ctx) < 0) rc = -1;
            break;
        case YXML_CONTENT:
            if (handle_content(&ctx, x.data) < 0) rc = -1;
            break;
        default:
            break;
        }
        if (rc < 0) break;
    }
    if (rc == 0) {
        yxml_ret_t r = yxml_eof(&x);
        if (r < 0) {
            ctx.err = "ysvg-parse: yxml unexpected EOF";
            rc = -1;
        }
    }

    /* Free transient ctx buffers. */
    free(ctx.text_buf);
    free(ctx.attr_name);
    free(ctx.attr_val);
    free(yxbuf);

    if (rc < 0 || !doc->root) {
        const char *msg = ctx.err ? ctx.err : "ysvg-parse: no root element";
        yetty_ysvg_doc_destroy(doc);
        return YETTY_ERR(yetty_ysvg_doc_ptr, msg);
    }
    return YETTY_OK(yetty_ysvg_doc_ptr, doc);
}

void yetty_ysvg_doc_destroy(struct yetty_ysvg_doc *doc)
{
    if (!doc) return;

    for (struct yetty_ysvg_node_chunk *c = doc->node_chunks; c; c = c->next) {
        for (size_t i = 0; i < c->used; i++) {
            struct yetty_ysvg_node *n = &c->nodes[i];
            for (size_t a = 0; a < n->attr_count; a++) {
                free(n->attrs[a].name);
                free(n->attrs[a].value);
            }
            free(n->attrs);
            free(n->tag_name);
            free(n->text);
        }
    }
    struct yetty_ysvg_node_chunk *c = doc->node_chunks;
    while (c) {
        struct yetty_ysvg_node_chunk *n = c->next;
        free(c);
        c = n;
    }
    free(doc);
}

/*=============================================================================
 * Attribute lookup
 *===========================================================================*/

const struct yetty_ysvg_attr *yetty_ysvg_attr_find(const struct yetty_ysvg_node *node,
                                                   enum yetty_ysvg_attr_key key)
{
    if (!node || key == YETTY_YSVG_ATTR_UNKNOWN) return NULL;
    for (size_t i = 0; i < node->attr_count; i++) {
        if (node->attrs[i].key == key) return &node->attrs[i];
    }
    return NULL;
}

const struct yetty_ysvg_attr *yetty_ysvg_attr_find_named(const struct yetty_ysvg_node *node,
                                                         const char *name)
{
    if (!node || !name) return NULL;
    size_t nl = strlen(name);
    for (size_t i = 0; i < node->attr_count; i++) {
        if (node->attrs[i].name_len == nl && memcmp(node->attrs[i].name, name, nl) == 0) {
            return &node->attrs[i];
        }
    }
    return NULL;
}
