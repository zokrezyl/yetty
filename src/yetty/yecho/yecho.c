/*
 * yecho.c - text/glyph/block parser + ypaint-buffer renderer.
 *
 * Parser: walks input char-by-char. `@name` becomes a glyph span,
 * `{attrs: content}` a block span, everything else a text span. Escapes
 * (\@, \{, \}, \\) are resolved into the text. Blocks aren't recursive —
 * their content is plain text.
 *
 * Renderer: lays out spans left-to-right at a fixed (2.0, 2.0) origin,
 * advancing x by ~0.6*font_size per UTF-8 codepoint (proportional approx,
 * same as ymarkdown). Newlines bump y by font_size*line_spacing. Glyphs
 * become 4-byte-or-shorter UTF-8 runs (private-use BMP codepoints handed
 * out by yfont/shader-glyph). Blocks honour color=, bg= (SDF box behind
 * the run); style= attrs are recorded on the span but not yet rendered
 * (no font-style mapping on the wire — TODO).
 */

#include <yetty/yecho/yecho.h>

#include <yetty/yfont/shader-glyph.h>
#include <yetty/ycore/types.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yexpr/yexpr.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yplot/yplot-gen.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Document — opaque, owns spans + errors + all string allocations.
 *===========================================================================*/

struct yetty_yecho_doc {
    struct yetty_yecho_span *spans;
    size_t span_count;
    size_t span_cap;

    char **errors;
    size_t error_count;
    size_t error_cap;
};

static void doc_free(struct yetty_yecho_doc *doc)
{
    if (!doc) {
        return;
    }
    for (size_t i = 0; i < doc->span_count; i++) {
        struct yetty_yecho_span *s = &doc->spans[i];
        free(s->text);
        for (size_t j = 0; j < s->attr_count; j++) {
            free(s->attrs[j].key);
            free(s->attrs[j].value);
        }
        free(s->attrs);
    }
    free(doc->spans);
    for (size_t i = 0; i < doc->error_count; i++) {
        free(doc->errors[i]);
    }
    free(doc->errors);
    free(doc);
}

void yetty_yecho_doc_destroy(struct yetty_yecho_doc *doc)
{
    doc_free(doc);
}

size_t yetty_yecho_doc_span_count(const struct yetty_yecho_doc *doc)
{
    return doc ? doc->span_count : 0;
}

const struct yetty_yecho_span *
yetty_yecho_doc_span(const struct yetty_yecho_doc *doc, size_t idx)
{
    if (!doc || idx >= doc->span_count) {
        return NULL;
    }
    return &doc->spans[idx];
}

size_t yetty_yecho_doc_error_count(const struct yetty_yecho_doc *doc)
{
    return doc ? doc->error_count : 0;
}

const char *yetty_yecho_doc_error(const struct yetty_yecho_doc *doc, size_t idx)
{
    if (!doc || idx >= doc->error_count) {
        return NULL;
    }
    return doc->errors[idx];
}

/*=============================================================================
 * Doc builders (used by the parser)
 *===========================================================================*/

static struct yetty_yecho_span *doc_push_span(struct yetty_yecho_doc *doc)
{
    if (doc->span_count == doc->span_cap) {
        size_t nc = doc->span_cap ? doc->span_cap * 2 : 8;
        struct yetty_yecho_span *ns = realloc(doc->spans, nc * sizeof(*ns));
        if (!ns) {
            return NULL;
        }
        doc->spans = ns;
        doc->span_cap = nc;
    }
    struct yetty_yecho_span *s = &doc->spans[doc->span_count++];
    memset(s, 0, sizeof(*s));
    return s;
}

static int doc_push_error(struct yetty_yecho_doc *doc, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (doc->error_count == doc->error_cap) {
        size_t nc = doc->error_cap ? doc->error_cap * 2 : 4;
        char **ne = realloc(doc->errors, nc * sizeof(*ne));
        if (!ne) {
            return -1;
        }
        doc->errors = ne;
        doc->error_cap = nc;
    }
    char *copy = strdup(buf);
    if (!copy) {
        return -1;
    }
    doc->errors[doc->error_count++] = copy;
    return 0;
}

/*=============================================================================
 * Growable byte buffer used while parsing
 *===========================================================================*/

struct yetty_yecho_strbuf {
    char *data;
    size_t len;
    size_t cap;
};

static int sb_push(struct yetty_yecho_strbuf *sb, char c)
{
    if (sb->len + 1 >= sb->cap) {
        size_t nc = sb->cap ? sb->cap * 2 : 32;
        char *nd = realloc(sb->data, nc);
        if (!nd) {
            return -1;
        }
        sb->data = nd;
        sb->cap = nc;
    }
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 0;
}

static char *sb_take(struct yetty_yecho_strbuf *sb)
{
    /* Caller takes ownership; reset sb to empty. */
    if (!sb->data) {
        return strdup("");
    }
    char *out = sb->data;
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static void sb_free(struct yetty_yecho_strbuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

/*=============================================================================
 * Parser
 *===========================================================================*/

static int is_glyph_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_';
}

/* Parse a glyph after the leading '@' has been seen at *pos.
 *   ok=1, value=1: glyph consumed (span pushed, *pos advanced)
 *   ok=1, value=0: not a glyph here (caller treats '@' as literal text)
 *   ok=0:          hard error (alloc failure) */
static struct yetty_ycore_int_result
parse_glyph(struct yetty_yecho_doc *doc, const char *input, size_t len, size_t *pos)
{
    size_t start = *pos + 1; /* skip '@' */
    size_t end = start;
    while (end < len && is_glyph_name_char(input[end])) {
        end++;
    }
    if (end == start) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    char *name = malloc(end - start + 1);
    if (!name) {
        return YETTY_ERR(yetty_ycore_int, "parse_glyph: alloc failed");
    }
    memcpy(name, input + start, end - start);
    name[end - start] = '\0';

    /* Diagnostic: stringify "unknown glyph" into the doc's warning list. The
     * upstream cause chain is flattened away here; absorb to avoid leaking. */
    struct uint32_result cp_r = yetty_yfont_shader_glyph_codepoint(name);
    if (!cp_r.ok) {
        doc_push_error(doc, "unknown glyph: @%s", name);
        yetty_ycore_error_destroy(cp_r.error);
    }

    struct yetty_yecho_span *span = doc_push_span(doc);
    if (!span) {
        free(name);
        return YETTY_ERR(yetty_ycore_int, "parse_glyph: span alloc failed");
    }
    span->type = YETTY_YECHO_SPAN_GLYPH;
    span->text = name;
    *pos = end;
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Resolve simple escapes (\@ \{ \} \\) into a strbuf, until end-marker
 * or close. Used for the content side of a block. Stops at *pos == end. */
static int copy_escaped(const char *src, size_t start, size_t end, struct yetty_yecho_strbuf *out)
{
    for (size_t i = start; i < end; i++) {
        char c = src[i];
        if (c == '\\' && i + 1 < end) {
            char n = src[i + 1];
            if (n == '{' || n == '}' || n == '@' || n == '\\') {
                if (sb_push(out, n) < 0) {
                    return -1;
                }
                i++;
                continue;
            }
        }
        if (sb_push(out, c) < 0) {
            return -1;
        }
    }
    return 0;
}

/* Parse semicolon-separated `key=value` pairs from [start, end).
 * Quotes are honoured (no '; inside "..." or '...'). Empty / trim-only
 * statements are skipped. Returns 0 on success. */
static int parse_attrs(const char *src, size_t start, size_t end,
                       struct yetty_yecho_attr **out, size_t *out_count)
{
    struct yetty_yecho_attr *attrs = NULL;
    size_t count = 0, cap = 0;

    struct yetty_yecho_strbuf cur = {0};
    int in_quotes = 0;
    char quote = 0;

    /* Helper: flush current strbuf as one statement. Returns 0/-1. */
#define FLUSH_STMT()                                                                               \
    do {                                                                                           \
        const char *s = cur.data ? cur.data : "";                                                  \
        size_t slen = cur.len;                                                                     \
        size_t a = 0;                                                                              \
        size_t b = slen;                                                                           \
        while (a < b && (s[a] == ' ' || s[a] == '\t')) {                                           \
            a++;                                                                                   \
        }                                                                                          \
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) {                                   \
            b--;                                                                                   \
        }                                                                                          \
        if (b > a) {                                                                               \
            size_t eq = a;                                                                         \
            while (eq < b && s[eq] != '=') {                                                       \
                eq++;                                                                              \
            }                                                                                      \
            if (count == cap) {                                                                    \
                size_t nc = cap ? cap * 2 : 4;                                                     \
                struct yetty_yecho_attr *na = realloc(attrs, nc * sizeof(*na));                    \
                if (!na) {                                                                         \
                    goto oom;                                                                      \
                }                                                                                  \
                attrs = na;                                                                        \
                cap = nc;                                                                          \
            }                                                                                      \
            char *key = malloc(eq - a + 1);                                                        \
            if (!key) {                                                                            \
                goto oom;                                                                          \
            }                                                                                      \
            memcpy(key, s + a, eq - a);                                                            \
            key[eq - a] = '\0';                                                                    \
            char *val = NULL;                                                                      \
            if (eq < b) {                                                                          \
                size_t vlen = b - (eq + 1);                                                        \
                val = malloc(vlen + 1);                                                            \
                if (!val) {                                                                        \
                    free(key);                                                                     \
                    goto oom;                                                                      \
                }                                                                                  \
                memcpy(val, s + eq + 1, vlen);                                                     \
                val[vlen] = '\0';                                                                  \
            } else {                                                                               \
                val = strdup("");                                                                  \
                if (!val) {                                                                        \
                    free(key);                                                                     \
                    goto oom;                                                                      \
                }                                                                                  \
            }                                                                                      \
            attrs[count].key = key;                                                                \
            attrs[count].value = val;                                                              \
            count++;                                                                               \
        }                                                                                          \
        sb_free(&cur);                                                                             \
    } while (0)

    for (size_t i = start; i < end; i++) {
        char c = src[i];
        if (in_quotes) {
            if (sb_push(&cur, c) < 0) {
                goto oom;
            }
            if (c == quote) {
                in_quotes = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = 1;
            quote = c;
            if (sb_push(&cur, c) < 0) {
                goto oom;
            }
            continue;
        }
        if (c == ';') {
            FLUSH_STMT();
            continue;
        }
        if (sb_push(&cur, c) < 0) {
            goto oom;
        }
    }
    FLUSH_STMT();

    *out = attrs;
    *out_count = count;
    return 0;

oom:
    sb_free(&cur);
    for (size_t k = 0; k < count; k++) {
        free(attrs[k].key);
        free(attrs[k].value);
    }
    free(attrs);
    return -1;
#undef FLUSH_STMT
}

/* Parse a block starting at input[*pos] == '{'.
 * Returns 1 if consumed, 0 if the '{' should be treated as literal text. */
static int parse_block(struct yetty_yecho_doc *doc, const char *input, size_t len, size_t *pos)
{
    size_t start = *pos + 1;

    /* Find ':' at depth 0, honouring quotes. */
    int brace_depth = 0;
    int in_quotes = 0;
    char quote = 0;
    size_t colon = SIZE_MAX;
    for (size_t i = start; i < len; i++) {
        char c = input[i];
        if (in_quotes) {
            if (c == quote) {
                in_quotes = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = 1;
            quote = c;
        } else if (c == '{') {
            brace_depth++;
        } else if (c == '}') {
            if (brace_depth == 0) {
                doc_push_error(doc, "missing ':' in block");
                return 0;
            }
            brace_depth--;
        } else if (c == ':' && brace_depth == 0) {
            colon = i;
            break;
        }
    }
    if (colon == SIZE_MAX) {
        doc_push_error(doc, "missing ':' in block");
        return 0;
    }

    /* Find closing '}', honouring quotes + nested braces. Skip one space
     * after the colon to match the C++ poc. */
    size_t content_start = colon + 1;
    if (content_start < len && input[content_start] == ' ') {
        content_start++;
    }
    brace_depth = 0;
    in_quotes = 0;
    size_t close = SIZE_MAX;
    for (size_t i = content_start; i < len; i++) {
        char c = input[i];
        if (in_quotes) {
            if (c == quote) {
                in_quotes = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = 1;
            quote = c;
        } else if (c == '{') {
            brace_depth++;
        } else if (c == '}') {
            if (brace_depth == 0) {
                close = i;
                break;
            }
            brace_depth--;
        }
    }
    if (close == SIZE_MAX) {
        doc_push_error(doc, "unclosed block");
        return 0;
    }

    /* Build the span. */
    struct yetty_yecho_span *span = doc_push_span(doc);
    if (!span) {
        return 0;
    }
    span->type = YETTY_YECHO_SPAN_BLOCK;

    if (parse_attrs(input, start, colon, &span->attrs, &span->attr_count) < 0) {
        /* roll back the span we pushed */
        doc->span_count--;
        return 0;
    }

    /* Content with escapes resolved. */
    struct yetty_yecho_strbuf content = {0};
    if (copy_escaped(input, content_start, close, &content) < 0) {
        sb_free(&content);
        for (size_t j = 0; j < span->attr_count; j++) {
            free(span->attrs[j].key);
            free(span->attrs[j].value);
        }
        free(span->attrs);
        doc->span_count--;
        return 0;
    }
    span->text = sb_take(&content);

    *pos = close + 1;
    return 1;
}

/*=============================================================================
 * Public parse entry point
 *===========================================================================*/

struct yetty_yecho_doc_ptr_result
yetty_yecho_parse(const char *input, size_t len)
{
    if (!input && len > 0) {
        return YETTY_ERR(yetty_yecho_doc_ptr, "input is NULL but len > 0");
    }

    struct yetty_yecho_doc *doc = calloc(1, sizeof(*doc));
    if (!doc) {
        return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
    }

    struct yetty_yecho_strbuf text = {0};
    size_t pos = 0;
    while (pos < len) {
        char c = input[pos];

        /* Escape: \@ \{ \} \\ — resolved into accumulated text. */
        if (c == '\\' && pos + 1 < len) {
            char n = input[pos + 1];
            if (n == '{' || n == '}' || n == '@' || n == '\\') {
                if (sb_push(&text, n) < 0) {
                    sb_free(&text);
                    doc_free(doc);
                    return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
                }
                pos += 2;
                continue;
            }
        }

        /* Glyph: @name. Flush pending text first. */
        if (c == '@') {
            size_t saved_text = text.len;
            struct yetty_ycore_int_result gr = parse_glyph(doc, input, len, &pos);
            if (YETTY_IS_ERR(gr)) {
                sb_free(&text);
                doc_free(doc);
                return YETTY_ERR(yetty_yecho_doc_ptr, "parse_glyph failed", gr);
            }
            if (gr.value) {
                /* Insert the accumulated text before the just-pushed glyph. */
                if (saved_text > 0) {
                    struct yetty_yecho_span *glyph = &doc->spans[doc->span_count - 1];
                    char *txt = sb_take(&text);
                    /* Rotate: pop the glyph, push the text, push the glyph again.
                     * Pointers into doc->spans are invalidated by realloc inside
                     * doc_push_span — we copy by value first. */
                    struct yetty_yecho_span saved_glyph = *glyph;
                    doc->span_count--;
                    struct yetty_yecho_span *t = doc_push_span(doc);
                    if (!t) {
                        free(txt);
                        sb_free(&text);
                        doc_free(doc);
                        return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
                    }
                    t->type = YETTY_YECHO_SPAN_TEXT;
                    t->text = txt;
                    struct yetty_yecho_span *g2 = doc_push_span(doc);
                    if (!g2) {
                        free(saved_glyph.text);
                        sb_free(&text);
                        doc_free(doc);
                        return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
                    }
                    *g2 = saved_glyph;
                }
                continue;
            }
            /* Not a glyph — treat '@' as literal. */
            if (sb_push(&text, '@') < 0) {
                sb_free(&text);
                doc_free(doc);
                return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
            }
            pos++;
            continue;
        }

        /* Block: {attrs: content}. Flush pending text first. */
        if (c == '{') {
            size_t saved_text = text.len;
            if (parse_block(doc, input, len, &pos)) {
                if (saved_text > 0) {
                    struct yetty_yecho_span *block = &doc->spans[doc->span_count - 1];
                    char *txt = sb_take(&text);
                    struct yetty_yecho_span saved_block = *block;
                    doc->span_count--;
                    struct yetty_yecho_span *t = doc_push_span(doc);
                    if (!t) {
                        free(txt);
                        sb_free(&text);
                        doc_free(doc);
                        return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
                    }
                    t->type = YETTY_YECHO_SPAN_TEXT;
                    t->text = txt;
                    struct yetty_yecho_span *b2 = doc_push_span(doc);
                    if (!b2) {
                        sb_free(&text);
                        doc_free(doc);
                        return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
                    }
                    *b2 = saved_block;
                }
                continue;
            }
            if (sb_push(&text, '{') < 0) {
                sb_free(&text);
                doc_free(doc);
                return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
            }
            pos++;
            continue;
        }

        if (sb_push(&text, c) < 0) {
            sb_free(&text);
            doc_free(doc);
            return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
        }
        pos++;
    }

    /* Final text flush. */
    if (text.len > 0) {
        struct yetty_yecho_span *t = doc_push_span(doc);
        if (!t) {
            sb_free(&text);
            doc_free(doc);
            return YETTY_ERR(yetty_yecho_doc_ptr, "alloc failed");
        }
        t->type = YETTY_YECHO_SPAN_TEXT;
        t->text = sb_take(&text);
    }
    sb_free(&text);

    return YETTY_OK(yetty_yecho_doc_ptr, doc);
}

/*=============================================================================
 * Renderer — ypaint-core buffer.
 *
 * Layout: cursor walks left-to-right, x advances by approx width per
 * codepoint. '\n' inside a span resets x and bumps y. Glyphs occupy
 * one cell. Blocks are rendered as a single text run (all on one logical
 * line — newlines inside a block content advance y inside the block).
 *===========================================================================*/

#define YECHO_DEFAULT_FG 0xFFE6E6E6u
#define YECHO_DEFAULT_LINE_SPACING 1.2f
#define YECHO_X_ORIGIN 2.0f
#define YECHO_Y_ORIGIN 2.0f
#define YECHO_GLYPH_ADVANCE 0.6f /* cell advance per char (proportional approx) */

struct yetty_yecho_render_state {
    struct yetty_ypaint_core_buffer *buf;
    float cursor_x;
    float cursor_y;
    float font_size;
    float line_height;
    uint32_t default_fg;
    float scene_max_x;
    float scene_max_y;
};

/* Decode one UTF-8 codepoint starting at `s`, len = remaining bytes.
 * Returns codepoint (or 0xFFFD on bad bytes), advances *consumed. */
static uint32_t decode_utf8(const char *s, size_t len, size_t *consumed)
{
    if (len == 0) {
        *consumed = 0;
        return 0;
    }
    unsigned char b0 = (unsigned char)s[0];
    if (b0 < 0x80) {
        *consumed = 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && len >= 2) {
        *consumed = 2;
        return ((b0 & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu);
    }
    if ((b0 & 0xF0) == 0xE0 && len >= 3) {
        *consumed = 3;
        return ((b0 & 0x0Fu) << 12) | (((unsigned char)s[1] & 0x3Fu) << 6) |
               ((unsigned char)s[2] & 0x3Fu);
    }
    if ((b0 & 0xF8) == 0xF0 && len >= 4) {
        *consumed = 4;
        return ((b0 & 0x07u) << 18) | (((unsigned char)s[1] & 0x3Fu) << 12) |
               (((unsigned char)s[2] & 0x3Fu) << 6) | ((unsigned char)s[3] & 0x3Fu);
    }
    *consumed = 1;
    return 0xFFFDu;
}

/* Number of UTF-8 codepoints in [s, s+len). Used to estimate run width. */
static size_t utf8_codepoint_count(const char *s, size_t len)
{
    size_t n = 0;
    size_t i = 0;
    while (i < len) {
        size_t k = 0;
        decode_utf8(s + i, len - i, &k);
        if (k == 0) {
            break;
        }
        i += k;
        n++;
    }
    return n;
}

static struct yetty_ycore_void_result
render_text_run(struct yetty_yecho_render_state *rs, const char *s, size_t len, uint32_t color)
{
    /* Walk by line — any '\n' starts a new line at x_origin. */
    size_t line_start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == '\n') {
            size_t llen = i - line_start;
            if (llen > 0) {
                struct yetty_ycore_buffer text = {
                    .data = (uint8_t *)(uintptr_t)(s + line_start),
                    .size = llen,
                    .capacity = llen,
                };
                struct yetty_ycore_void_result tr = yetty_ypaint_core_buffer_add_text(
                    rs->buf, rs->cursor_x, rs->cursor_y, &text, rs->font_size, color, 0, -1, 0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "buffer_add_text failed");
                size_t cp_count = utf8_codepoint_count(s + line_start, llen);
                rs->cursor_x += (float)cp_count * rs->font_size * YECHO_GLYPH_ADVANCE;
                if (rs->cursor_x > rs->scene_max_x) {
                    rs->scene_max_x = rs->cursor_x;
                }
            }
            if (i < len && s[i] == '\n') {
                rs->cursor_x = YECHO_X_ORIGIN;
                rs->cursor_y += rs->line_height;
                if (rs->cursor_y > rs->scene_max_y) {
                    rs->scene_max_y = rs->cursor_y;
                }
                line_start = i + 1;
            }
        }
    }
    return YETTY_OK_VOID();
}

static int append_utf8(struct yetty_yecho_strbuf *sb, uint32_t cp)
{
    if (cp < 0x80) {
        return sb_push(sb, (char)cp);
    }
    if (cp < 0x800) {
        if (sb_push(sb, (char)(0xC0 | (cp >> 6))) < 0) {
            return -1;
        }
        return sb_push(sb, (char)(0x80 | (cp & 0x3F)));
    }
    if (cp < 0x10000) {
        if (sb_push(sb, (char)(0xE0 | (cp >> 12))) < 0) {
            return -1;
        }
        if (sb_push(sb, (char)(0x80 | ((cp >> 6) & 0x3F))) < 0) {
            return -1;
        }
        return sb_push(sb, (char)(0x80 | (cp & 0x3F)));
    }
    if (sb_push(sb, (char)(0xF0 | (cp >> 18))) < 0) {
        return -1;
    }
    if (sb_push(sb, (char)(0x80 | ((cp >> 12) & 0x3F))) < 0) {
        return -1;
    }
    if (sb_push(sb, (char)(0x80 | ((cp >> 6) & 0x3F))) < 0) {
        return -1;
    }
    return sb_push(sb, (char)(0x80 | (cp & 0x3F)));
}

/* Parse #RRGGBB or #RGB into 0xFFRRGGBB (alpha = 0xFF, R in low byte of the
 * RGB nibble — but ypaint text wants R in low byte of the WHOLE u32).
 *
 * The wire convention (text-span-prim.h): "color: u32 (RGBA, R in low byte)".
 * Returns 1 on success and writes *out; 0 on parse failure. */
static int parse_hex_color(const char *s, uint32_t *out)
{
    if (!s || s[0] != '#') {
        return 0;
    }
    const char *h = s + 1;
    size_t hl = strlen(h);
    char buf[7];
    if (hl == 3) {
        buf[0] = h[0];
        buf[1] = h[0];
        buf[2] = h[1];
        buf[3] = h[1];
        buf[4] = h[2];
        buf[5] = h[2];
        buf[6] = '\0';
    } else if (hl == 6) {
        memcpy(buf, h, 6);
        buf[6] = '\0';
    } else {
        return 0;
    }
    char *endp = NULL;
    unsigned long v = strtoul(buf, &endp, 16);
    if (!endp || *endp != '\0') {
        return 0;
    }
    uint32_t r = (uint32_t)((v >> 16) & 0xFF);
    uint32_t g = (uint32_t)((v >> 8) & 0xFF);
    uint32_t b = (uint32_t)(v & 0xFF);
    *out = 0xFF000000u | (b << 16) | (g << 8) | r;
    return 1;
}

/* Parse "lo..hi" into two floats. Returns 1 on success. */
static int parse_range(const char *s, float *lo, float *hi)
{
    if (!s) {
        return 0;
    }
    const char *dots = strstr(s, "..");
    if (!dots) {
        return 0;
    }
    char buf[64];
    size_t prefix_len = (size_t)(dots - s);
    if (prefix_len >= sizeof(buf)) {
        return 0;
    }
    memcpy(buf, s, prefix_len);
    buf[prefix_len] = '\0';
    char *endp = NULL;
    float low = strtof(buf, &endp);
    if (!endp || *endp != '\0') {
        return 0;
    }
    const char *tail = dots + 2;
    float high = strtof(tail, &endp);
    if (!endp || *endp != '\0') {
        return 0;
    }
    *lo = low;
    *hi = high;
    return 1;
}

static int span_has_attr(const struct yetty_yecho_span *span, const char *key)
{
    for (size_t i = 0; i < span->attr_count; i++) {
        if (strcmp(span->attrs[i].key, key) == 0) {
            return 1;
        }
    }
    return 0;
}

/* yplot block: emit a yplot complex primitive into the ypaint buffer.
 *
 * Block syntax (left side = attrs, right side = function definitions):
 *   {plot; w=400; h=200; xrange=-3.14..3.14; yrange=-1.5..1.5:
 *      f=sin(x); g=cos(x); @f.color=#FF6B6B}
 *
 * Function definitions and per-plot color attrs are parsed by
 * yetty_yexpr_parse_plot; expressions are compiled by yfsvm; the result
 * is serialized via yetty_yplot_uniforms_serialize and added as a primitive. */
static struct yetty_ycore_void_result
render_yplot_block(struct yetty_yecho_render_state *rs, const struct yetty_yecho_span *span)
{
    struct yetty_yplot_uniforms u = {
        .bounds_x = rs->cursor_x,
        .bounds_y = rs->cursor_y,
        .bounds_w = 400.0f,
        .bounds_h = 200.0f,
        .x_min = -3.14159f,
        .x_max = 3.14159f,
        .y_min = -1.5f,
        .y_max = 1.5f,
        .flags = 7, /* grid + axes + labels */
        .function_count = 0,
    };

    /* Apply yecho attrs. */
    for (size_t i = 0; i < span->attr_count; i++) {
        const char *k = span->attrs[i].key;
        const char *v = span->attrs[i].value;
        if (strcmp(k, "w") == 0 && v) {
            u.bounds_w = strtof(v, NULL);
        } else if (strcmp(k, "h") == 0 && v) {
            u.bounds_h = strtof(v, NULL);
        } else if (strcmp(k, "xrange") == 0) {
            float lo, hi;
            if (parse_range(v, &lo, &hi)) {
                u.x_min = lo;
                u.x_max = hi;
            }
        } else if (strcmp(k, "yrange") == 0) {
            float lo, hi;
            if (parse_range(v, &lo, &hi)) {
                u.y_min = lo;
                u.y_max = hi;
            }
        } else if (strcmp(k, "nogrid") == 0) {
            u.flags &= ~1u;
        } else if (strcmp(k, "noaxes") == 0) {
            u.flags &= ~2u;
        } else if (strcmp(k, "nolabels") == 0) {
            u.flags &= ~4u;
        }
    }

    /* Default color palette — overridden by @<name>.color attrs below. */
    static const uint32_t palette[8] = {
        0xFFFF6B6B, 0xFF4ECDC4, 0xFFFFE66D, 0xFF95E1D3,
        0xFFF38181, 0xFFAA96DA, 0xFF72D6C9, 0xFFFCBF49,
    };
    for (int i = 0; i < 8; i++) {
        u.colors[i] = palette[i];
    }

    /* Parse the content with yexpr's plot syntax (handles f=expr; @f.color=...). */
    const char *content = span->text ? span->text : "";
    size_t content_len = strlen(content);
    struct yetty_yexpr_plot_parse_result pr =
        yetty_yexpr_parse_plot(content, content_len);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ycore_void, "plot parse failed", pr);
    }

    u.function_count = pr.value.plot.def_count;
    if (u.function_count > 8) {
        u.function_count = 8;
    }

    /* Apply per-plot color attrs. plot_attr names like "color" reference
     * the plot definition's index by matching plot_name against def name. */
    for (uint32_t i = 0; i < pr.value.plot.attr_count; i++) {
        const struct yetty_yexpr_plot_attr *attr = &pr.value.plot.attrs[i];
        if (strcmp(attr->attr_name, "color") != 0) {
            continue;
        }
        for (uint32_t j = 0; j < u.function_count; j++) {
            if (strcmp(pr.value.plot.defs[j].name, attr->plot_name) == 0) {
                uint32_t c;
                if (parse_hex_color(attr->value, &c)) {
                    u.colors[j] = c;
                }
                break;
            }
        }
    }

    /* Compile to bytecode. */
    struct yetty_yfsvm_program_result prog = yetty_yfsvm_compile_multi(&pr.value.plot);
    if (YETTY_IS_ERR(prog)) {
        return YETTY_ERR(yetty_ycore_void, "yfsvm_compile_multi failed", prog);
    }

    uint32_t bc_buf[1024];
    uint32_t bc_len = yetty_yfsvm_program_serialize(&prog.value, bc_buf, 1024);
    if (bc_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "yfsvm_program_serialize failed");
    }

    struct yetty_yplot_buffers bufs = {
        .bytecode = bc_buf,
        .bytecode_len = bc_len,
    };

    size_t required = yetty_yplot_uniforms_serialized_size(&u, &bufs);
    uint8_t *prim_buf = malloc(required);
    if (!prim_buf) {
        return YETTY_ERR(yetty_ycore_void, "yplot prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yplot_uniforms_serialize(&u, &bufs, prim_buf, required);
    if (YETTY_IS_ERR(ser)) {
        free(prim_buf);
        return YETTY_ERR(yetty_ycore_void, "yplot_serialize failed", ser);
    }

    struct yetty_ypaint_core_id_result idr =
        yetty_ypaint_core_buffer_add_prim(rs->buf, prim_buf, required);
    free(prim_buf);
    if (idr.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "yplot add_prim failed");
    }

    /* Advance the cursor past the plot. The plot is a block element; the
     * next text run starts on a fresh line below it. */
    if (u.bounds_x + u.bounds_w > rs->scene_max_x) {
        rs->scene_max_x = u.bounds_x + u.bounds_w;
    }
    rs->cursor_x = YECHO_X_ORIGIN;
    rs->cursor_y += u.bounds_h + rs->line_height;
    if (rs->cursor_y > rs->scene_max_y) {
        rs->scene_max_y = rs->cursor_y;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result
render_block(struct yetty_yecho_render_state *rs, const struct yetty_yecho_span *span)
{
    if (span_has_attr(span, "plot")) {
        return render_yplot_block(rs, span);
    }

    uint32_t color = rs->default_fg;
    uint32_t bg = 0;
    int has_bg = 0;
    for (size_t i = 0; i < span->attr_count; i++) {
        const char *k = span->attrs[i].key;
        const char *v = span->attrs[i].value;
        if (strcmp(k, "color") == 0) {
            uint32_t c;
            if (parse_hex_color(v, &c)) {
                color = c;
            }
        } else if (strcmp(k, "bg") == 0) {
            uint32_t c;
            if (parse_hex_color(v, &c)) {
                bg = c;
                has_bg = 1;
            }
        }
        /* style= recorded in the doc but not applied here (no font-style
         * mapping on the wire yet). */
    }

    /* Background: a single SDF box behind the text run, sized from the
     * codepoint count. Multi-line block content gets one box per line.  */
    if (has_bg) {
        const char *s = span->text ? span->text : "";
        size_t len = strlen(s);
        size_t line_start = 0;
        float save_x = rs->cursor_x;
        float save_y = rs->cursor_y;
        for (size_t i = 0; i <= len; i++) {
            if (i == len || s[i] == '\n') {
                size_t llen = i - line_start;
                if (llen > 0) {
                    size_t cp_count = utf8_codepoint_count(s + line_start, llen);
                    float text_w = (float)cp_count * rs->font_size * YECHO_GLYPH_ADVANCE;
                    struct yetty_ysdf_box geom = {
                        .center_x = save_x + text_w * 0.5f,
                        .center_y = save_y + rs->font_size * 0.4f,
                        .half_width = text_w * 0.5f + 1.0f,
                        .half_height = rs->font_size * 0.5f + 0.5f,
                        .corner_radius = 0.0f,
                    };
                    struct yetty_ypaint_core_id_result br =
                        yetty_ysdf_add_box(rs->buf, 0, bg, 0, 0.0f, &geom);
                    if (br.error != YPAINT_OK) {
                        return YETTY_ERR(yetty_ycore_void, "ysdf_add_box failed");
                    }
                }
                if (i < len && s[i] == '\n') {
                    save_x = YECHO_X_ORIGIN;
                    save_y += rs->line_height;
                    line_start = i + 1;
                }
            }
        }
    }

    return render_text_run(rs, span->text ? span->text : "", span->text ? strlen(span->text) : 0,
                           color);
}

struct yetty_ypaint_core_buffer_result
yetty_yecho_doc_render(const struct yetty_yecho_doc *doc,
                   const struct yetty_yecho_render_config *config)
{
    if (!doc) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "doc is NULL");
    }

    /* Resolve config defaults. */
    uint32_t cell_w = (config && config->cell_width) ? config->cell_width : 8;
    uint32_t cell_h = (config && config->cell_height) ? config->cell_height : 16;
    uint32_t width_cells = (config && config->width_cells) ? config->width_cells : 80;
    float font_size = (config && config->font_size > 0.0f) ? config->font_size : (float)cell_h;
    float line_spacing =
        (config && config->line_spacing > 0.0f) ? config->line_spacing : YECHO_DEFAULT_LINE_SPACING;
    uint32_t default_fg =
        (config && config->default_fg) ? config->default_fg : (uint32_t)YECHO_DEFAULT_FG;

    struct yetty_ypaint_core_buffer_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = (float)(width_cells * cell_w),
        .scene_max_y = (float)cell_h * 2.0f, /* updated as content grows */
    };
    struct yetty_ypaint_core_buffer_result br = yetty_ypaint_core_buffer_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "ypaint buffer create failed", br);
    }

    struct yetty_yecho_render_state rs = {
        .buf = br.value,
        .cursor_x = YECHO_X_ORIGIN,
        .cursor_y = YECHO_Y_ORIGIN + font_size, /* baseline */
        .font_size = font_size,
        .line_height = font_size * line_spacing,
        .default_fg = default_fg,
        .scene_max_x = bcfg.scene_max_x,
        .scene_max_y = bcfg.scene_max_y,
    };

    for (size_t i = 0; i < doc->span_count; i++) {
        const struct yetty_yecho_span *s = &doc->spans[i];
        switch (s->type) {
        case YETTY_YECHO_SPAN_TEXT: {
            struct yetty_ycore_void_result tr = render_text_run(
                &rs, s->text ? s->text : "", s->text ? strlen(s->text) : 0, rs.default_fg);
            if (YETTY_IS_ERR(tr)) {
                yetty_ypaint_core_buffer_destroy(rs.buf);
                return YETTY_ERR(yetty_ypaint_core_buffer, "text span emission failed", tr);
            }
            break;
        }
        case YETTY_YECHO_SPAN_GLYPH: {
            uint32_t cp = 0;
            struct uint32_result r = yetty_yfont_shader_glyph_codepoint(s->text);
            if (r.ok) {
                cp = r.value;
            } else {
                /* Diagnostic chain already in the doc — drop the cause here. */
                yetty_ycore_error_destroy(r.error);
            }
            if (cp == 0) {
                /* Unknown glyph -> "[?name]" placeholder. */
                struct yetty_yecho_strbuf fb = {0};
                if (sb_push(&fb, '[') < 0 || sb_push(&fb, '?') < 0) {
                    sb_free(&fb);
                    yetty_ypaint_core_buffer_destroy(rs.buf);
                    return YETTY_ERR(yetty_ypaint_core_buffer, "glyph fallback alloc failed");
                }
                for (const char *p = s->text; p && *p; p++) {
                    if (sb_push(&fb, *p) < 0) {
                        sb_free(&fb);
                        yetty_ypaint_core_buffer_destroy(rs.buf);
                        return YETTY_ERR(yetty_ypaint_core_buffer, "glyph fallback alloc failed");
                    }
                }
                if (sb_push(&fb, ']') < 0) {
                    sb_free(&fb);
                    yetty_ypaint_core_buffer_destroy(rs.buf);
                    return YETTY_ERR(yetty_ypaint_core_buffer, "glyph fallback alloc failed");
                }
                struct yetty_ycore_void_result tr =
                    render_text_run(&rs, fb.data, fb.len, rs.default_fg);
                sb_free(&fb);
                if (YETTY_IS_ERR(tr)) {
                    yetty_ypaint_core_buffer_destroy(rs.buf);
                    return YETTY_ERR(yetty_ypaint_core_buffer,
                                     "glyph fallback emission failed", tr);
                }
            } else {
                struct yetty_yecho_strbuf u = {0};
                if (append_utf8(&u, cp) < 0) {
                    sb_free(&u);
                    yetty_ypaint_core_buffer_destroy(rs.buf);
                    return YETTY_ERR(yetty_ypaint_core_buffer, "glyph encode alloc failed");
                }
                struct yetty_ycore_void_result tr =
                    render_text_run(&rs, u.data, u.len, rs.default_fg);
                sb_free(&u);
                if (YETTY_IS_ERR(tr)) {
                    yetty_ypaint_core_buffer_destroy(rs.buf);
                    return YETTY_ERR(yetty_ypaint_core_buffer, "glyph emission failed", tr);
                }
            }
            break;
        }
        case YETTY_YECHO_SPAN_BLOCK: {
            struct yetty_ycore_void_result tr = render_block(&rs, s);
            if (YETTY_IS_ERR(tr)) {
                yetty_ypaint_core_buffer_destroy(rs.buf);
                return YETTY_ERR(yetty_ypaint_core_buffer, "block emission failed", tr);
            }
            break;
        }
        }
    }

    /* Update the scene bounds to what we actually painted. */
    yetty_ypaint_core_buffer_set_scene_bounds(rs.buf, 0.0f, 0.0f, rs.scene_max_x,
                                              rs.cursor_y + rs.font_size);

    return YETTY_OK(yetty_ypaint_core_buffer, rs.buf);
}

struct yetty_ypaint_core_buffer_result
yetty_yecho_render_string(const char *input, size_t len,
                          const struct yetty_yecho_render_config *config)
{
    struct yetty_yecho_doc_ptr_result pr = yetty_yecho_parse(input, len);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "parse failed", pr);
    }
    struct yetty_ypaint_core_buffer_result rr = yetty_yecho_doc_render(pr.value, config);
    yetty_yecho_doc_destroy(pr.value);
    return rr;
}

/*=============================================================================
 * OSC emission — wraps the buffer in a YPAINT_BIN envelope (mirror of
 * yetty_ycat_osc_bin_emit; we replicate it here so the yecho lib doesn't
 * pull the whole ycat target into thin clients).
 *===========================================================================*/

#include <yetty/yface/yface.h>
#include <yetty/yterm/osc-codes.h> /* YETTY_OSC_YPAINT_BIN */

struct yetty_ycore_size_result
yetty_yecho_osc_bin_emit(const struct yetty_ypaint_core_buffer *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yetty_yecho_osc_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ypaint_core_buffer_serialize((struct yetty_ypaint_core_buffer *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "yetty_yecho_osc_bin_emit: empty serialize");
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
    struct yetty_ycore_void_result r = yetty_yface_emit(YETTY_OSC_YPAINT_BIN, /*compressed=*/1,
                                                        &meta, sizeof(meta), raw, raw_size,
                                                        &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yetty_yecho_osc_bin_emit: yface_emit failed", r);
    }
    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
