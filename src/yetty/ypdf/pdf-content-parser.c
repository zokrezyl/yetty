/*
 * PDF content stream parser (C port of yetty-poc pdf-content-parser.cpp).
 *
 * Stateful interpreter for the PDF operator stream. Tokenises via
 * pdfioStreamGetToken, keeps a growable operand stack, and dispatches operators
 * to inline handlers that mutate graphics / text / path state.
 *
 * Storage choices:
 *   - operand stack: realloc-doubling growable array of (char *, size_t)
 *     slots. Most operators take ≤6 operands, but a single TJ can stack
 *     hundreds of array elements (alternating string / kerning offset),
 *     and a real PDF page issues many such; an unbounded stack matches
 *     the original C++ implementation's std::vector and avoids dropping
 *     text mid-array. Each slot holds a pointer to its own malloc'd
 *     buffer sized to the largest token that ever passed through, so
 *     large strings don't force a 4KB minimum per slot.
 *   - current path: realloc-doubling growable array of pdf_path_point.
 *   - graphics state stack: realloc-doubling growable array. q/Q pairs
 *     typically nest shallowly (<10) but the spec allows more.
 *   - UTF-8 decode scratch: realloc-doubling byte buffer reused across spans.
 */

#include <yetty/ypdf/pdf-content-parser.h>
#include <yetty/ytrace.h>

#include <pdfio.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define OPERAND_STACK_INIT 16
#define MAX_OPERAND_LEN 4096
#define STATE_STACK_INIT 8
#define PATH_INIT 32
#define DECODED_INIT 256

/*=============================================================================
 * Internal types
 *===========================================================================*/

struct pdf_matrix {
    float a, b, c, d, e, f;
};

static struct pdf_matrix mat_identity(void)
{
    struct pdf_matrix m = {1, 0, 0, 1, 0, 0};
    return m;
}

static struct pdf_matrix mat_mul(struct pdf_matrix x, struct pdf_matrix y)
{
    struct pdf_matrix r;
    r.a = x.a * y.a + x.b * y.c;
    r.b = x.a * y.b + x.b * y.d;
    r.c = x.c * y.a + x.d * y.c;
    r.d = x.c * y.b + x.d * y.d;
    r.e = x.e * y.a + x.f * y.c + y.e;
    r.f = x.e * y.b + x.f * y.d + y.f;
    return r;
}

static void mat_transform(struct pdf_matrix m, float ix, float iy, float *ox, float *oy)
{
    *ox = m.a * ix + m.c * iy + m.e;
    *oy = m.b * ix + m.d * iy + m.f;
}

enum pdf_path_op {
    PDF_PATH_MOVE_TO,
    PDF_PATH_LINE_TO,
    PDF_PATH_CURVE_TO,
    PDF_PATH_CLOSE,
};

struct pdf_path_point {
    enum pdf_path_op op;
    float x, y;
    float x1, y1, x2, y2; /* control points for curves */
};

struct pdf_gstate {
    struct pdf_matrix ctm;
    /* Text state embedded here for q/Q. */
    float char_spacing;
    float word_spacing;
    float horizontal_scaling;
    float leading;
    float font_size;
    float rise;
    char font_name[64];
    /* Colours / line width. */
    float stroke_r, stroke_g, stroke_b;
    float fill_r, fill_g, fill_b;
    float line_width;
};

struct yetty_ypdf_content_parser {
    struct yetty_ypdf_content_parser_callbacks cb;

    /* Operand stack — growable. operands[i] is a pointer to a malloc'd
     * buffer (NUL-terminated). operand_lens[i] is the unterminated length.
     * Slots are kept allocated across ops_clear() so we reuse them on the
     * next operator without reallocating; only operand_count drops to 0. */
    char **operands;
    size_t *operand_lens;
    size_t *operand_caps; /* per-slot buffer capacity */
    int operand_count;
    int operand_capacity; /* number of slots allocated */

    /* Graphics state (current + saved stack). */
    struct pdf_gstate gstate;
    struct pdf_gstate *state_stack;
    size_t state_stack_count;
    size_t state_stack_capacity;

    /* Text state / matrices. */
    struct pdf_matrix text_matrix;
    struct pdf_matrix text_line_matrix;
    bool in_text_object;

    /* Path. */
    struct pdf_path_point *path;
    size_t path_count;
    size_t path_capacity;
    float current_x, current_y;
    float subpath_start_x, subpath_start_y;

    /* Decode scratch. */
    char *decode_buf;
    size_t decode_capacity;

    float page_height;
};

/*=============================================================================
 * Operand stack helpers
 *===========================================================================*/

static void ops_clear(struct yetty_ypdf_content_parser *p)
{
    /* Keep slot buffers alive — only reset the count. */
    p->operand_count = 0;
}

/* Grow the slot table to at least `want` slots. Existing slot buffers are
 * left intact; new slots start with NULL/0 capacity and get a buffer on
 * first push. Returns 0 on success, -1 on OOM. */
static int ops_reserve(struct yetty_ypdf_content_parser *p, int want)
{
    if (want <= p->operand_capacity) {
        return 0;
    }
    int cap = p->operand_capacity ? p->operand_capacity * 2 : OPERAND_STACK_INIT;
    while (cap < want) {
        cap *= 2;
    }
    char **no = realloc(p->operands, (size_t)cap * sizeof(*no));
    if (!no) {
        return -1;
    }
    p->operands = no;
    size_t *nl = realloc(p->operand_lens, (size_t)cap * sizeof(*nl));
    if (!nl) {
        return -1;
    }
    p->operand_lens = nl;
    size_t *nc = realloc(p->operand_caps, (size_t)cap * sizeof(*nc));
    if (!nc) {
        return -1;
    }
    p->operand_caps = nc;
    /* Zero-init the newly added slots so first push allocates fresh. */
    for (int i = p->operand_capacity; i < cap; i++) {
        p->operands[i] = NULL;
        p->operand_lens[i] = 0;
        p->operand_caps[i] = 0;
    }
    p->operand_capacity = cap;
    return 0;
}

static void ops_push(struct yetty_ypdf_content_parser *p, const char *s, size_t len)
{
    if (ops_reserve(p, p->operand_count + 1) < 0) {
        ywarn("ypdf: operand stack realloc failed, dropping token");
        return;
    }
    int i = p->operand_count;
    size_t need = len + 1;
    if (p->operand_caps[i] < need) {
        size_t cap = p->operand_caps[i] ? p->operand_caps[i] * 2 : 64;
        while (cap < need) {
            cap *= 2;
        }
        char *nb = realloc(p->operands[i], cap);
        if (!nb) {
            ywarn("ypdf: operand slot realloc failed, dropping token");
            return;
        }
        p->operands[i] = nb;
        p->operand_caps[i] = cap;
    }
    memcpy(p->operands[i], s, len);
    p->operands[i][len] = '\0';
    p->operand_lens[i] = len;
    p->operand_count++;
}

static const char *ops_at(const struct yetty_ypdf_content_parser *p, int idx)
{
    return p->operands[idx];
}

static float ops_float(const struct yetty_ypdf_content_parser *p, int idx)
{
    return (float)strtod(p->operands[idx], NULL);
}

/*=============================================================================
 * Growable helpers
 *===========================================================================*/

static int path_reserve(struct yetty_ypdf_content_parser *p, size_t want)
{
    if (want <= p->path_capacity) {
        return 0;
    }
    size_t cap = p->path_capacity ? p->path_capacity * 2 : PATH_INIT;
    while (cap < want) {
        cap *= 2;
    }
    struct pdf_path_point *np = realloc(p->path, cap * sizeof(*np));
    if (!np) {
        return -1;
    }
    p->path = np;
    p->path_capacity = cap;
    return 0;
}

static void path_push(struct yetty_ypdf_content_parser *p, struct pdf_path_point pt)
{
    if (path_reserve(p, p->path_count + 1) < 0) {
        return;
    }
    p->path[p->path_count++] = pt;
}

static int state_stack_reserve(struct yetty_ypdf_content_parser *p, size_t want)
{
    if (want <= p->state_stack_capacity) {
        return 0;
    }
    size_t cap = p->state_stack_capacity ? p->state_stack_capacity * 2 : STATE_STACK_INIT;
    while (cap < want) {
        cap *= 2;
    }
    struct pdf_gstate *ns = realloc(p->state_stack, cap * sizeof(*ns));
    if (!ns) {
        return -1;
    }
    p->state_stack = ns;
    p->state_stack_capacity = cap;
    return 0;
}

static int decode_reserve(struct yetty_ypdf_content_parser *p, size_t want)
{
    if (want <= p->decode_capacity) {
        return 0;
    }
    size_t cap = p->decode_capacity ? p->decode_capacity * 2 : DECODED_INIT;
    while (cap < want) {
        cap *= 2;
    }
    char *nb = realloc(p->decode_buf, cap);
    if (!nb) {
        return -1;
    }
    p->decode_buf = nb;
    p->decode_capacity = cap;
    return 0;
}

/*=============================================================================
 * String decoding (WinAnsi → UTF-8)
 *===========================================================================*/

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Append codepoint as UTF-8 at offset *pos in p->decode_buf. Grows buffer. */
static void append_utf8(struct yetty_ypdf_content_parser *p, size_t *pos, uint32_t cp)
{
    if (decode_reserve(p, *pos + 4) < 0) {
        return;
    }
    char *b = p->decode_buf + *pos;
    if (cp < 0x80) {
        b[0] = (char)cp;
        *pos += 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        *pos += 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        *pos += 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        *pos += 4;
    }
}

static void append_winansi(struct yetty_ypdf_content_parser *p, size_t *pos, uint8_t ch)
{
    /* WinAnsi (Windows-1252) 0x80-0x9F mapping to Unicode. */
    static const uint16_t winansi_high[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
    };
    uint32_t cp;
    if (ch < 0x80) {
        cp = ch;
    } else if (ch <= 0x9F) {
        cp = winansi_high[ch - 0x80];
    } else {
        cp = ch;
    }
    append_utf8(p, pos, cp);
}

/* Decode a pdfio string token into p->decode_buf. Returns byte length. */
static size_t decode_pdf_string(struct yetty_ypdf_content_parser *p, const char *token,
                                size_t token_len)
{
    size_t pos = 0;
    if (token_len == 0) {
        return 0;
    }

    if (token[0] == '(') {
        for (size_t i = 1; i < token_len; i++) {
            append_winansi(p, &pos, (uint8_t)token[i]);
        }
    } else if (token[0] == '<') {
        for (size_t i = 1; i + 1 < token_len; i += 2) {
            int hi = hex_val(token[i]);
            int lo = hex_val(token[i + 1]);
            if (hi >= 0 && lo >= 0) {
                append_winansi(p, &pos, (uint8_t)((hi << 4) | lo));
            }
        }
        if ((token_len - 1) % 2 == 1) {
            int hi = hex_val(token[token_len - 1]);
            if (hi >= 0) {
                append_winansi(p, &pos, (uint8_t)(hi << 4));
            }
        }
    } else {
        if (decode_reserve(p, token_len) < 0) {
            return 0;
        }
        memcpy(p->decode_buf, token, token_len);
        pos = token_len;
    }
    return pos;
}

/*=============================================================================
 * Handlers
 *===========================================================================*/

static void cmyk_to_rgb(float c, float m, float y, float k, float *r, float *g, float *b)
{
    *r = (1.0f - c) * (1.0f - k);
    *g = (1.0f - m) * (1.0f - k);
    *b = (1.0f - y) * (1.0f - k);
}

static void emit_text(struct yetty_ypdf_content_parser *p, const char *decoded, size_t decoded_len);

static void handle_BT(struct yetty_ypdf_content_parser *p)
{
    p->in_text_object = true;
    p->text_matrix = mat_identity();
    p->text_line_matrix = mat_identity();
}

static void handle_ET(struct yetty_ypdf_content_parser *p)
{
    p->in_text_object = false;
}

static void handle_Tf(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 2) {
        return;
    }
    int n = p->operand_count;
    p->gstate.font_size = ops_float(p, n - 1);
    const char *name = ops_at(p, n - 2);
    strncpy(p->gstate.font_name, name, sizeof(p->gstate.font_name) - 1);
    p->gstate.font_name[sizeof(p->gstate.font_name) - 1] = '\0';
}

static void handle_Tc(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count > 0) {
        p->gstate.char_spacing = ops_float(p, p->operand_count - 1);
    }
}
static void handle_Tw(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count > 0) {
        p->gstate.word_spacing = ops_float(p, p->operand_count - 1);
    }
}
static void handle_TL(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count > 0) {
        p->gstate.leading = ops_float(p, p->operand_count - 1);
    }
}
static void handle_Ts(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count > 0) {
        p->gstate.rise = ops_float(p, p->operand_count - 1);
    }
}

static void apply_Td(struct yetty_ypdf_content_parser *p, float tx, float ty)
{
    struct pdf_matrix translate = {1, 0, 0, 1, tx, ty};
    p->text_line_matrix = mat_mul(translate, p->text_line_matrix);
    p->text_matrix = p->text_line_matrix;
}

static void handle_Td(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 2) {
        return;
    }
    int n = p->operand_count;
    float ty = ops_float(p, n - 1);
    float tx = ops_float(p, n - 2);
    apply_Td(p, tx, ty);
}

static void handle_TD(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 2) {
        return;
    }
    int n = p->operand_count;
    float ty = ops_float(p, n - 1);
    p->gstate.leading = -ty;
    handle_Td(p);
}

static void handle_Tm(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 6) {
        return;
    }
    int base = p->operand_count - 6;
    struct pdf_matrix m = {
        ops_float(p, base + 0), ops_float(p, base + 1), ops_float(p, base + 2),
        ops_float(p, base + 3), ops_float(p, base + 4), ops_float(p, base + 5),
    };
    p->text_matrix = m;
    p->text_line_matrix = m;
}

static void handle_Tstar(struct yetty_ypdf_content_parser *p)
{
    apply_Td(p, 0.0f, -p->gstate.leading);
}

static struct yetty_ypdf_text_state snapshot_text_state(const struct yetty_ypdf_content_parser *p)
{
    struct yetty_ypdf_text_state ts;
    ts.char_spacing = p->gstate.char_spacing;
    ts.word_spacing = p->gstate.word_spacing;
    ts.horizontal_scaling = p->gstate.horizontal_scaling;
    ts.leading = p->gstate.leading;
    ts.font_size = p->gstate.font_size;
    ts.rise = p->gstate.rise;
    ts.font_name = p->gstate.font_name;
    ts.fill_r = p->gstate.fill_r;
    ts.fill_g = p->gstate.fill_g;
    ts.fill_b = p->gstate.fill_b;
    return ts;
}

static void handle_Tj(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count == 0) {
        return;
    }
    int n = p->operand_count - 1;
    size_t len = decode_pdf_string(p, p->operands[n], p->operand_lens[n]);
    emit_text(p, p->decode_buf, len);
}

static void handle_TJ(struct yetty_ypdf_content_parser *p)
{
    bool in_array = false;
    for (int i = 0; i < p->operand_count; i++) {
        const char *op = p->operands[i];
        size_t op_len = p->operand_lens[i];
        if (op_len == 1 && op[0] == '[') {
            in_array = true;
            continue;
        }
        if (op_len == 1 && op[0] == ']') {
            in_array = false;
            continue;
        }
        if (!in_array) {
            continue;
        }

        if (op_len > 0 && (op[0] == '(' || op[0] == '<')) {
            size_t len = decode_pdf_string(p, op, op_len);
            emit_text(p, p->decode_buf, len);
        } else {
            /* Numeric adjustment in thousandths of text space unit. */
            float adj = (float)strtod(op, NULL);
            float disp = -adj / 1000.0f * p->gstate.font_size;
            float hscale = p->gstate.horizontal_scaling / 100.0f;
            disp *= hscale;
            p->text_matrix.e += disp * p->text_matrix.a;
            p->text_matrix.f += disp * p->text_matrix.b;
        }
    }
}

static void handle_quote(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count == 0) {
        return;
    }
    int n = p->operand_count - 1;
    /* Save the string, do T*, then Tj the saved string. */
    char saved[MAX_OPERAND_LEN];
    size_t saved_len = p->operand_lens[n];
    memcpy(saved, p->operands[n], saved_len);
    saved[saved_len] = '\0';
    handle_Tstar(p);
    size_t len = decode_pdf_string(p, saved, saved_len);
    emit_text(p, p->decode_buf, len);
}

static void handle_dbl_quote(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 3) {
        return;
    }
    int n = p->operand_count;
    p->gstate.word_spacing = ops_float(p, n - 3);
    p->gstate.char_spacing = ops_float(p, n - 2);
    char saved[MAX_OPERAND_LEN];
    size_t saved_len = p->operand_lens[n - 1];
    memcpy(saved, p->operands[n - 1], saved_len);
    saved[saved_len] = '\0';
    handle_Tstar(p);
    size_t len = decode_pdf_string(p, saved, saved_len);
    emit_text(p, p->decode_buf, len);
}

static void handle_cm(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 6) {
        return;
    }
    int base = p->operand_count - 6;
    struct pdf_matrix m = {
        ops_float(p, base + 0), ops_float(p, base + 1), ops_float(p, base + 2),
        ops_float(p, base + 3), ops_float(p, base + 4), ops_float(p, base + 5),
    };
    p->gstate.ctm = mat_mul(m, p->gstate.ctm);
}

static void handle_q(struct yetty_ypdf_content_parser *p)
{
    if (state_stack_reserve(p, p->state_stack_count + 1) < 0) {
        return;
    }
    p->state_stack[p->state_stack_count++] = p->gstate;
}

static void handle_Q(struct yetty_ypdf_content_parser *p)
{
    if (p->state_stack_count == 0) {
        return;
    }
    p->gstate = p->state_stack[--p->state_stack_count];
}

static void handle_w(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count > 0) {
        p->gstate.line_width = ops_float(p, p->operand_count - 1);
    }
}

static void handle_RG(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 3) {
        return;
    }
    int base = p->operand_count - 3;
    p->gstate.stroke_r = ops_float(p, base + 0);
    p->gstate.stroke_g = ops_float(p, base + 1);
    p->gstate.stroke_b = ops_float(p, base + 2);
}
static void handle_rg(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 3) {
        return;
    }
    int base = p->operand_count - 3;
    p->gstate.fill_r = ops_float(p, base + 0);
    p->gstate.fill_g = ops_float(p, base + 1);
    p->gstate.fill_b = ops_float(p, base + 2);
}
static void handle_G(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count == 0) {
        return;
    }
    float g = ops_float(p, p->operand_count - 1);
    p->gstate.stroke_r = p->gstate.stroke_g = p->gstate.stroke_b = g;
}
static void handle_g(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count == 0) {
        return;
    }
    float g = ops_float(p, p->operand_count - 1);
    p->gstate.fill_r = p->gstate.fill_g = p->gstate.fill_b = g;
}
static void handle_K(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 4) {
        return;
    }
    int base = p->operand_count - 4;
    cmyk_to_rgb(ops_float(p, base), ops_float(p, base + 1), ops_float(p, base + 2),
                ops_float(p, base + 3), &p->gstate.stroke_r, &p->gstate.stroke_g,
                &p->gstate.stroke_b);
}
static void handle_k(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 4) {
        return;
    }
    int base = p->operand_count - 4;
    cmyk_to_rgb(ops_float(p, base), ops_float(p, base + 1), ops_float(p, base + 2),
                ops_float(p, base + 3), &p->gstate.fill_r, &p->gstate.fill_g, &p->gstate.fill_b);
}

static void handle_moveto(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 2) {
        return;
    }
    int n = p->operand_count;
    float y = ops_float(p, n - 1);
    float x = ops_float(p, n - 2);
    struct pdf_path_point pt = {PDF_PATH_MOVE_TO, x, y, 0, 0, 0, 0};
    path_push(p, pt);
    p->current_x = x;
    p->current_y = y;
    p->subpath_start_x = x;
    p->subpath_start_y = y;
}

static void handle_lineto(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 2) {
        return;
    }
    int n = p->operand_count;
    float y = ops_float(p, n - 1);
    float x = ops_float(p, n - 2);
    struct pdf_path_point pt = {PDF_PATH_LINE_TO, x, y, 0, 0, 0, 0};
    path_push(p, pt);
    p->current_x = x;
    p->current_y = y;
}

static void handle_curveto(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 6) {
        return;
    }
    int base = p->operand_count - 6;
    float x1 = ops_float(p, base + 0), y1 = ops_float(p, base + 1);
    float x2 = ops_float(p, base + 2), y2 = ops_float(p, base + 3);
    float x3 = ops_float(p, base + 4), y3 = ops_float(p, base + 5);
    struct pdf_path_point pt = {PDF_PATH_CURVE_TO, x3, y3, x1, y1, x2, y2};
    path_push(p, pt);
    p->current_x = x3;
    p->current_y = y3;
}

static void handle_curveto_v(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 4) {
        return;
    }
    int base = p->operand_count - 4;
    float x2 = ops_float(p, base + 0), y2 = ops_float(p, base + 1);
    float x3 = ops_float(p, base + 2), y3 = ops_float(p, base + 3);
    struct pdf_path_point pt = {PDF_PATH_CURVE_TO, x3, y3, p->current_x, p->current_y, x2, y2};
    path_push(p, pt);
    p->current_x = x3;
    p->current_y = y3;
}

static void handle_curveto_y(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 4) {
        return;
    }
    int base = p->operand_count - 4;
    float x1 = ops_float(p, base + 0), y1 = ops_float(p, base + 1);
    float x3 = ops_float(p, base + 2), y3 = ops_float(p, base + 3);
    struct pdf_path_point pt = {PDF_PATH_CURVE_TO, x3, y3, x1, y1, x3, y3};
    path_push(p, pt);
    p->current_x = x3;
    p->current_y = y3;
}

static void handle_rect(struct yetty_ypdf_content_parser *p)
{
    if (p->operand_count < 4) {
        return;
    }
    int base = p->operand_count - 4;
    float x = ops_float(p, base + 0);
    float y = ops_float(p, base + 1);
    float w = ops_float(p, base + 2);
    float h = ops_float(p, base + 3);

    struct pdf_path_point a = {PDF_PATH_MOVE_TO, x, y, 0, 0, 0, 0};
    struct pdf_path_point b = {PDF_PATH_LINE_TO, x + w, y, 0, 0, 0, 0};
    struct pdf_path_point c = {PDF_PATH_LINE_TO, x + w, y + h, 0, 0, 0, 0};
    struct pdf_path_point d = {PDF_PATH_LINE_TO, x, y + h, 0, 0, 0, 0};
    struct pdf_path_point e = {PDF_PATH_CLOSE, x, y, 0, 0, 0, 0};
    path_push(p, a);
    path_push(p, b);
    path_push(p, c);
    path_push(p, d);
    path_push(p, e);

    p->current_x = x;
    p->current_y = y;
    p->subpath_start_x = x;
    p->subpath_start_y = y;
}

static void handle_closepath(struct yetty_ypdf_content_parser *p)
{
    struct pdf_path_point pt = {PDF_PATH_CLOSE, p->subpath_start_x, p->subpath_start_y, 0, 0, 0, 0};
    path_push(p, pt);
    p->current_x = p->subpath_start_x;
    p->current_y = p->subpath_start_y;
}

static void paint_path(struct yetty_ypdf_content_parser *p, enum yetty_ypdf_paint_mode mode);

static void handle_stroke(struct yetty_ypdf_content_parser *p)
{
    paint_path(p, YETTY_YPDF_PAINT_STROKE);
    p->path_count = 0;
}
static void handle_close_stroke(struct yetty_ypdf_content_parser *p)
{
    handle_closepath(p);
    paint_path(p, YETTY_YPDF_PAINT_STROKE);
    p->path_count = 0;
}
static void handle_fill(struct yetty_ypdf_content_parser *p)
{
    paint_path(p, YETTY_YPDF_PAINT_FILL);
    p->path_count = 0;
}
static void handle_fill_and_stroke(struct yetty_ypdf_content_parser *p)
{
    paint_path(p, YETTY_YPDF_PAINT_FILL_AND_STROKE);
    p->path_count = 0;
}
static void handle_close_fill_and_stroke(struct yetty_ypdf_content_parser *p)
{
    handle_closepath(p);
    paint_path(p, YETTY_YPDF_PAINT_FILL_AND_STROKE);
    p->path_count = 0;
}
static void handle_end_path(struct yetty_ypdf_content_parser *p)
{
    p->path_count = 0;
}

/*=============================================================================
 * paint_path — detect axis-aligned rectangle or emit line segments
 *===========================================================================*/

static void paint_path(struct yetty_ypdf_content_parser *p, enum yetty_ypdf_paint_mode mode)
{
    if (p->path_count == 0) {
        return;
    }

    /* Axis-aligned rectangle path from re + S/f/B: M L L L h */
    if (p->path_count == 5 && p->path[0].op == PDF_PATH_MOVE_TO &&
        p->path[1].op == PDF_PATH_LINE_TO && p->path[2].op == PDF_PATH_LINE_TO &&
        p->path[3].op == PDF_PATH_LINE_TO && p->path[4].op == PDF_PATH_CLOSE) {

        float x0 = p->path[0].x, y0 = p->path[0].y;
        float x1 = p->path[1].x, y1 = p->path[1].y;
        float x2 = p->path[2].x, y2 = p->path[2].y;
        float x3 = p->path[3].x, y3 = p->path[3].y;

        bool axis_aligned = (y0 == y1 && x1 == x2 && y2 == y3 && x3 == x0) ||
                            (x0 == x1 && y1 == y2 && x2 == x3 && y3 == y0);

        if (axis_aligned) {
            float min_x = x0, min_y = y0, max_x = x0, max_y = y0;
            float xs[4] = {x0, x1, x2, x3};
            float ys[4] = {y0, y1, y2, y3};
            for (int i = 1; i < 4; i++) {
                if (xs[i] < min_x) {
                    min_x = xs[i];
                }
                if (xs[i] > max_x) {
                    max_x = xs[i];
                }
                if (ys[i] < min_y) {
                    min_y = ys[i];
                }
                if (ys[i] > max_y) {
                    max_y = ys[i];
                }
            }
            float tmin_x, tmin_y, tmax_x, tmax_y;
            mat_transform(p->gstate.ctm, min_x, min_y, &tmin_x, &tmin_y);
            mat_transform(p->gstate.ctm, max_x, max_y, &tmax_x, &tmax_y);
            if (tmin_x > tmax_x) {
                float t = tmin_x;
                tmin_x = tmax_x;
                tmax_x = t;
            }
            if (tmin_y > tmax_y) {
                float t = tmin_y;
                tmin_y = tmax_y;
                tmax_y = t;
            }

            if (p->cb.rect_paint) {
                p->cb.rect_paint(p->cb.user_data, tmin_x, tmin_y, tmax_x - tmin_x, tmax_y - tmin_y,
                                 mode, p->gstate.stroke_r, p->gstate.stroke_g, p->gstate.stroke_b,
                                 p->gstate.fill_r, p->gstate.fill_g, p->gstate.fill_b,
                                 p->gstate.line_width);
            }
            return;
        }
    }

    if (mode == YETTY_YPDF_PAINT_FILL || !p->cb.line_paint) {
        return;
    }

    float prev_x = 0, prev_y = 0;
    float move_x = 0, move_y = 0;
    for (size_t i = 0; i < p->path_count; i++) {
        const struct pdf_path_point *pt = &p->path[i];
        float tx, ty;
        switch (pt->op) {
        case PDF_PATH_MOVE_TO:
            mat_transform(p->gstate.ctm, pt->x, pt->y, &tx, &ty);
            prev_x = tx;
            prev_y = ty;
            move_x = tx;
            move_y = ty;
            break;
        case PDF_PATH_LINE_TO:
        case PDF_PATH_CURVE_TO: /* approximate curve as line to endpoint */
            mat_transform(p->gstate.ctm, pt->x, pt->y, &tx, &ty);
            p->cb.line_paint(p->cb.user_data, prev_x, prev_y, tx, ty, p->gstate.stroke_r,
                             p->gstate.stroke_g, p->gstate.stroke_b, p->gstate.line_width);
            prev_x = tx;
            prev_y = ty;
            break;
        case PDF_PATH_CLOSE:
            if (prev_x != move_x || prev_y != move_y) {
                p->cb.line_paint(p->cb.user_data, prev_x, prev_y, move_x, move_y,
                                 p->gstate.stroke_r, p->gstate.stroke_g, p->gstate.stroke_b,
                                 p->gstate.line_width);
            }
            prev_x = move_x;
            prev_y = move_y;
            break;
        }
    }
}

/*=============================================================================
 * emit_text
 *===========================================================================*/

static void emit_text(struct yetty_ypdf_content_parser *p, const char *decoded, size_t decoded_len)
{
    if (!decoded || decoded_len == 0 || !p->in_text_object) {
        return;
    }
    if (!p->cb.text_emit) {
        return;
    }

    struct pdf_matrix font_matrix = {
        p->gstate.font_size, 0, 0, p->gstate.font_size, 0, p->gstate.rise,
    };
    struct pdf_matrix trm = mat_mul(mat_mul(font_matrix, p->text_matrix), p->gstate.ctm);

    float pos_x, pos_y;
    mat_transform(trm, 0.0f, 0.0f, &pos_x, &pos_y);

    float effective = sqrtf(trm.b * trm.b + trm.d * trm.d);
    if (effective < 0.5f) {
        effective = p->gstate.font_size;
    }

    float rotation = atan2f(trm.b, trm.a);
    if (fabsf(rotation) < 0.001f) {
        rotation = 0.0f;
    }

    struct yetty_ypdf_text_state ts = snapshot_text_state(p);
    struct float_result adv_res = p->cb.text_emit(p->cb.user_data, decoded, decoded_len, pos_x,
                                                  pos_y, effective, rotation, &ts);

    if (YETTY_IS_OK(adv_res)) {
        float total_advance = adv_res.value;
        p->text_matrix.e += total_advance * p->text_matrix.a;
        p->text_matrix.f += total_advance * p->text_matrix.b;
    }
}

/*=============================================================================
 * Dispatch
 *===========================================================================*/

static bool streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static void dispatch(struct yetty_ypdf_content_parser *p, const char *op)
{
    if (streq(op, "BT")) {
        handle_BT(p);
        return;
    }
    if (streq(op, "ET")) {
        handle_ET(p);
        return;
    }

    if (streq(op, "Tf")) {
        handle_Tf(p);
        return;
    }
    if (streq(op, "Tc")) {
        handle_Tc(p);
        return;
    }
    if (streq(op, "Tw")) {
        handle_Tw(p);
        return;
    }
    if (streq(op, "TL")) {
        handle_TL(p);
        return;
    }
    if (streq(op, "Ts")) {
        handle_Ts(p);
        return;
    }

    if (streq(op, "Td")) {
        handle_Td(p);
        return;
    }
    if (streq(op, "TD")) {
        handle_TD(p);
        return;
    }
    if (streq(op, "Tm")) {
        handle_Tm(p);
        return;
    }
    if (streq(op, "T*")) {
        handle_Tstar(p);
        return;
    }

    if (streq(op, "Tj")) {
        handle_Tj(p);
        return;
    }
    if (streq(op, "TJ")) {
        handle_TJ(p);
        return;
    }
    if (streq(op, "'")) {
        handle_quote(p);
        return;
    }
    if (streq(op, "\"")) {
        handle_dbl_quote(p);
        return;
    }

    if (streq(op, "cm")) {
        handle_cm(p);
        return;
    }
    if (streq(op, "q")) {
        handle_q(p);
        return;
    }
    if (streq(op, "Q")) {
        handle_Q(p);
        return;
    }
    if (streq(op, "w")) {
        handle_w(p);
        return;
    }

    if (streq(op, "RG")) {
        handle_RG(p);
        return;
    }
    if (streq(op, "rg")) {
        handle_rg(p);
        return;
    }
    if (streq(op, "G")) {
        handle_G(p);
        return;
    }
    if (streq(op, "g")) {
        handle_g(p);
        return;
    }
    if (streq(op, "K")) {
        handle_K(p);
        return;
    }
    if (streq(op, "k")) {
        handle_k(p);
        return;
    }

    if (streq(op, "m")) {
        handle_moveto(p);
        return;
    }
    if (streq(op, "l")) {
        handle_lineto(p);
        return;
    }
    if (streq(op, "c")) {
        handle_curveto(p);
        return;
    }
    if (streq(op, "v")) {
        handle_curveto_v(p);
        return;
    }
    if (streq(op, "y")) {
        handle_curveto_y(p);
        return;
    }
    if (streq(op, "re")) {
        handle_rect(p);
        return;
    }
    if (streq(op, "h")) {
        handle_closepath(p);
        return;
    }

    if (streq(op, "S")) {
        handle_stroke(p);
        return;
    }
    if (streq(op, "s")) {
        handle_close_stroke(p);
        return;
    }
    if (streq(op, "f") || streq(op, "F") || streq(op, "f*")) {
        handle_fill(p);
        return;
    }
    if (streq(op, "B") || streq(op, "B*")) {
        handle_fill_and_stroke(p);
        return;
    }
    if (streq(op, "b") || streq(op, "b*")) {
        handle_close_fill_and_stroke(p);
        return;
    }
    if (streq(op, "n")) {
        handle_end_path(p);
        return;
    }
}

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ypdf_content_parser_ptr_result yetty_ypdf_content_parser_create(
    const struct yetty_ypdf_content_parser_callbacks *cb)
{
    if (!cb) {
        return YETTY_ERR(yetty_ypdf_content_parser_ptr, "callbacks required");
    }

    struct yetty_ypdf_content_parser *p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_ypdf_content_parser_ptr, "alloc failed");
    }

    p->cb = *cb;
    p->gstate.ctm = mat_identity();
    p->gstate.horizontal_scaling = 100.0f;
    p->gstate.font_size = 12.0f;
    p->gstate.line_width = 1.0f;
    p->text_matrix = mat_identity();
    p->text_line_matrix = mat_identity();
    p->page_height = 792.0f;

    return YETTY_OK(yetty_ypdf_content_parser_ptr, p);
}

void yetty_ypdf_content_parser_destroy(struct yetty_ypdf_content_parser *p)
{
    if (!p) {
        return;
    }
    if (p->operands) {
        for (int i = 0; i < p->operand_capacity; i++) {
            free(p->operands[i]);
        }
        free(p->operands);
    }
    free(p->operand_lens);
    free(p->operand_caps);
    free(p->path);
    free(p->state_stack);
    free(p->decode_buf);
    free(p);
}

void yetty_ypdf_content_parser_set_page_height(struct yetty_ypdf_content_parser *p, float h)
{
    if (p) {
        p->page_height = h;
    }
}

struct yetty_ycore_void_result yetty_ypdf_content_parser_parse_stream(
    struct yetty_ypdf_content_parser *p, pdfio_stream_t *stream)
{
    if (!p) {
        return YETTY_ERR(yetty_ycore_void, "parser is NULL");
    }
    if (!stream) {
        return YETTY_ERR(yetty_ycore_void, "stream is NULL");
    }

    char token[MAX_OPERAND_LEN];
    while (pdfioStreamGetToken(stream, token, sizeof(token))) {
        char first = token[0];
        size_t tlen = strlen(token);

        if (first == '(' || first == '<' || first == '/') {
            ops_push(p, token, tlen);
        } else if (first == '[') {
            ops_push(p, "[", 1);
        } else if (first == ']') {
            ops_push(p, "]", 1);
        } else if ((first >= '0' && first <= '9') || first == '-' || first == '+' || first == '.') {
            ops_push(p, token, tlen);
        } else if ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') ||
                   first == '\'' || first == '"') {
            dispatch(p, token);
            ops_clear(p);
        }
    }

    return YETTY_OK_VOID();
}
