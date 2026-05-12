/*
 * ylexbor-box — DOM tree → flat box vector.
 *
 * MVP scope: walk the lexbor DOM, classify each element as block /
 * inline / skip purely from its tag name, and emit a flat list of
 * boxes with style derived from a tiny built-in user-agent stylesheet
 * (HTML default rendering — same shape every browser ships out of the
 * box). lexbor's full CSS cascade (lxb_style_value) is wired in
 * later — for now we get correct font weights / sizes for the common
 * HTML5 sectioning elements without touching CSS at all, which is
 * already enough to make documentation pages legible.
 */

#include "ybrowser-internal.h"
#include "ybrowser-libcss.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <lexbor/dom/dom.h>
#include <lexbor/html/html.h>
#include <lexbor/tag/const.h>
#include <lexbor/style/style.h>
#include <lexbor/core/str.h>

#include <yetty/ytrace/ytrace.h>

/* ===========================================================================
 * Built-in user-agent style table.
 *
 * Indexed by lxb_tag_id_t so lookup is O(1). For tags we don't list
 * (custom elements, the unknown-tag default) we fall back to inline
 * with default font + black color.
 * ===========================================================================*/

enum yl_disp { YL_DISP_INLINE, YL_DISP_BLOCK, YL_DISP_NONE };

struct yl_default_style {
    enum yl_disp disp;
    float font_size_em; /* multiplied by parent font size */
    int font_weight;    /* 0 = inherit, else 100..900 */
    int font_italic;    /* 0/1; -1 = inherit */
    float margin_top_em;
    float margin_bottom_em;
    uint32_t fg_rgb; /* 0xRRGGBB; 0xffffffff = inherit */
};

#define INHERIT_RGB 0xffffffffu

static const struct yl_default_style YL_DEFAULT_INLINE = {
    YL_DISP_INLINE, 1.0f, 0, -1, 0.0f, 0.0f, INHERIT_RGB,
};
static const struct yl_default_style YL_DEFAULT_BLOCK = {
    YL_DISP_BLOCK, 1.0f, 0, -1, 1.0f, 1.0f, INHERIT_RGB,
};

/* Match lexbor tag IDs; values pulled from <lexbor/tag/const.h>. */
static const struct yl_default_style *default_for(lxb_tag_id_t tag)
{
    static struct yl_default_style h1 = {YL_DISP_BLOCK, 2.00f, 700, -1, 0.67f, 0.67f, INHERIT_RGB};
    static struct yl_default_style h2 = {YL_DISP_BLOCK, 1.50f, 700, -1, 0.83f, 0.83f, INHERIT_RGB};
    static struct yl_default_style h3 = {YL_DISP_BLOCK, 1.17f, 700, -1, 1.00f, 1.00f, INHERIT_RGB};
    static struct yl_default_style h4 = {YL_DISP_BLOCK, 1.00f, 700, -1, 1.33f, 1.33f, INHERIT_RGB};
    static struct yl_default_style h5 = {YL_DISP_BLOCK, 0.83f, 700, -1, 1.67f, 1.67f, INHERIT_RGB};
    static struct yl_default_style h6 = {YL_DISP_BLOCK, 0.67f, 700, -1, 2.33f, 2.33f, INHERIT_RGB};

    static struct yl_default_style strong = {YL_DISP_INLINE, 1.0f, 700, -1, 0, 0, INHERIT_RGB};
    static struct yl_default_style em = {YL_DISP_INLINE, 1.0f, 0, 1, 0, 0, INHERIT_RGB};
    static struct yl_default_style anchor = {YL_DISP_INLINE, 1.0f, 0, -1, 0, 0, 0x0000eeu};
    static struct yl_default_style none = {YL_DISP_NONE, 1.0f, 0, -1, 0, 0, INHERIT_RGB};

    switch (tag) {
    case LXB_TAG_H1:
        return &h1;
    case LXB_TAG_H2:
        return &h2;
    case LXB_TAG_H3:
        return &h3;
    case LXB_TAG_H4:
        return &h4;
    case LXB_TAG_H5:
        return &h5;
    case LXB_TAG_H6:
        return &h6;

    case LXB_TAG_STRONG:
    case LXB_TAG_B:
        return &strong;
    case LXB_TAG_EM:
    case LXB_TAG_I:
    case LXB_TAG_CITE:
        return &em;
    case LXB_TAG_A:
        return &anchor;

    case LXB_TAG_HEAD:
    case LXB_TAG_TITLE:
    case LXB_TAG_META:
    case LXB_TAG_LINK:
    case LXB_TAG_SCRIPT:
    case LXB_TAG_STYLE:
    case LXB_TAG_TEMPLATE:
    /* Embedded content the renderer doesn't handle: walking into
     * these accumulates their text-node descendants as visible
     * inline text (the "garbage characters" Wikipedia produces from
     * its inline SVG icons / math markup). We hide the subtrees
     * outright until we render them properly. */
    case LXB_TAG_SVG:
    case LXB_TAG_MATH:
    case LXB_TAG_AUDIO:
    case LXB_TAG_VIDEO:
    case LXB_TAG_OBJECT:
    case LXB_TAG_EMBED:
    case LXB_TAG_IFRAME:
    case LXB_TAG_CANVAS:
        return &none;

    case LXB_TAG_HTML:
    case LXB_TAG_BODY:
    case LXB_TAG_DIV:
    case LXB_TAG_P:
    case LXB_TAG_SECTION:
    case LXB_TAG_ARTICLE:
    case LXB_TAG_HEADER:
    case LXB_TAG_FOOTER:
    case LXB_TAG_NAV:
    case LXB_TAG_ASIDE:
    case LXB_TAG_MAIN:
    case LXB_TAG_FIGURE:
    case LXB_TAG_FIGCAPTION:
    case LXB_TAG_UL:
    case LXB_TAG_OL:
    case LXB_TAG_LI:
    case LXB_TAG_DL:
    case LXB_TAG_DT:
    case LXB_TAG_DD:
    case LXB_TAG_BLOCKQUOTE:
    case LXB_TAG_PRE:
    case LXB_TAG_HR:
    case LXB_TAG_TABLE:
    case LXB_TAG_THEAD:
    case LXB_TAG_TBODY:
    case LXB_TAG_TR:
    case LXB_TAG_TD:
    case LXB_TAG_TH:
    case LXB_TAG_FORM:
        return &YL_DEFAULT_BLOCK;

    default:
        return &YL_DEFAULT_INLINE;
    }
}

/* ===========================================================================
 * Style stack — propagates inheritable properties down the tree.
 * ===========================================================================*/

struct yl_style_state {
    float font_size;
    int font_weight;
    bool font_italic;
    struct yetty_ylexbor_color fg;
    int text_align; /* inherited; 0=left, 1=center, 2=right, 3=justify */
};

static struct yetty_ylexbor_color rgb_to_color(uint32_t rgb, uint8_t a)
{
    struct yetty_ylexbor_color c = {
        .r = (uint8_t)(rgb >> 16),
        .g = (uint8_t)(rgb >> 8),
        .b = (uint8_t)(rgb),
        .a = a,
    };
    return c;
}

static struct yl_style_state apply_default(const struct yl_style_state *parent,
                                           const struct yl_default_style *d)
{
    struct yl_style_state s = *parent;
    s.font_size = parent->font_size * d->font_size_em;
    if (d->font_weight) {
        s.font_weight = d->font_weight;
    }
    if (d->font_italic >= 0) {
        s.font_italic = d->font_italic ? true : false;
    }
    if (d->fg_rgb != INHERIT_RGB) {
        s.fg = rgb_to_color(d->fg_rgb, 0xff);
    }
    return s;
}

/* ===========================================================================
 * Inline text accumulator. We collect a block's inline contents as a
 * single concatenated UTF-8 run + a list of "style segments" that the
 * layout pass uses for line-by-line wrapping. For the MVP we collapse
 * all inline children into a single text run with the most recently
 * propagated style — i.e. <p>plain <b>bold</b> plain</p> renders as one
 * line of plain text without distinguishing the bold span. Visible
 * weight differences come back when we add per-run style segments
 * (next iteration).
 * ===========================================================================*/

struct yl_inline_buf {
    char *buf;
    size_t len, cap;
    int last_was_space;
};

static int inline_buf_append(struct yl_inline_buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        while (nc < b->len + n + 1) {
            nc *= 2;
        }
        char *p = realloc(b->buf, nc);
        if (p == NULL) {
            return -1;
        }
        b->buf = p;
        b->cap = nc;
    }
    /* Whitespace collapsing per CSS normal — runs of WS become a single
	 * space; leading WS is dropped. Same default handful of HTML defaults
	 * use. Doesn't honor white-space:pre yet. */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        int is_ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (is_ws) {
            if (b->last_was_space || b->len == 0) {
                continue;
            }
            b->buf[b->len++] = ' ';
            b->last_was_space = 1;
        } else {
            b->buf[b->len++] = (char)c;
            b->last_was_space = 0;
        }
    }
    return 0;
}

/* ===========================================================================
 * DOM walker — recursive
 * ===========================================================================*/

static int box_alloc(struct yetty_ylexbor *r, uint32_t *out_idx)
{
    struct yetty_ycore_void_result rr =
        _yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
    if (YETTY_IS_ERR(rr)) {
        return -1;
    }
    struct yetty_ylexbor_box *b = &r->boxes.data[r->boxes.size];
    memset(b, 0, sizeof(*b));
    *out_idx = r->boxes.size++;
    return 0;
}

/* Append `cidx` as the last child of `parent_idx`. Walks the existing
 * sibling chain to find the tail. O(n) per append in the parent's
 * direct children — fine for the tree sizes we deal with. */
static void link_child(struct yetty_ylexbor *r, uint32_t parent_idx, uint32_t cidx)
{
    struct yetty_ylexbor_box *p = &r->boxes.data[parent_idx];
    if (p->child_count == 0) {
        p->first_child = cidx;
    } else {
        uint32_t t = p->first_child;
        while (r->boxes.data[t].next_sibling != 0) {
            t = r->boxes.data[t].next_sibling;
        }
        r->boxes.data[t].next_sibling = cidx;
    }
    p->child_count++;
}

static void style_to_box(struct yetty_ylexbor_box *b, const struct yl_style_state *s)
{
    b->font_size = s->font_size;
    b->font_weight = s->font_weight;
    b->font_italic = s->font_italic;
    b->fg = s->fg;
}

/* Crude search for a `key: value` declaration inside a `style="..."`
 * attribute. Returns a pointer into the attribute's bytes (NOT NUL-
 * terminated) plus *out_len, or NULL if not found.
 *
 * lexbor's full CSS cascade can do this properly via lxb_style_value,
 * but we don't yet wire that through; for the boot-time decisions
 * (display:flex on a top-level shell div) the inline attribute is
 * usually where the answer is anyway. */
static const char *find_inline_decl(const lxb_char_t *style, size_t len, const char *key,
                                    size_t klen, size_t *out_len)
{
    if (!style || len == 0) {
        return NULL;
    }
    for (size_t i = 0; i + klen + 1 < len; i++) {
        /* Match key at a `;` or string-start boundary, allowing
		 * leading whitespace before the key. */
        size_t start = i;
        while (start < len && (style[start] == ' ' || style[start] == '\t' ||
                               style[start] == '\n' || style[start] == ';')) {
            start++;
        }
        if (start + klen >= len) {
            break;
        }
        if (strncasecmp((const char *)style + start, key, klen) != 0) {
            i = start;
            continue;
        }
        size_t j = start + klen;
        while (j < len && (style[j] == ' ' || style[j] == '\t')) {
            j++;
        }
        if (j >= len || style[j] != ':') {
            i = start;
            continue;
        }
        j++;
        while (j < len && (style[j] == ' ' || style[j] == '\t')) {
            j++;
        }
        size_t v0 = j;
        while (j < len && style[j] != ';' && style[j] != '\n') {
            j++;
        }
        while (j > v0 && (style[j - 1] == ' ' || style[j - 1] == '\t' || style[j - 1] == '\r')) {
            j--;
        }
        if (out_len) {
            *out_len = j - v0;
        }
        return (const char *)style + v0;
    }
    return NULL;
}

/* Hex digit lookup → 0..15, -1 if not a hex digit. */
static int hex_nibble(char c)
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

/* Skinny CSS color parser. Handles:
 *   #rgb / #rrggbb / #rrggbbaa
 *   rgb(r, g, b) / rgba(r, g, b, a)  (integer 0..255 components, a 0..1)
 *   transparent / a small handful of common named colors
 *
 * Returns 1 + writes *out on success, 0 on parse failure. Bytes pointed
 * to by `s` need not be NUL-terminated; *len* bounds the read. */
static int parse_css_color(const char *s, size_t len, struct yetty_ylexbor_color *out)
{
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) {
        len--;
    }
    if (len == 0) {
        return 0;
    }

    if (s[0] == '#') {
        /* Hex. */
        int n = (int)len - 1;
        if (n != 3 && n != 4 && n != 6 && n != 8) {
            return 0;
        }
        int v[8] = {0};
        for (int i = 0; i < n; i++) {
            int h = hex_nibble(s[1 + i]);
            if (h < 0) {
                return 0;
            }
            v[i] = h;
        }
        if (n == 3 || n == 4) {
            out->r = (uint8_t)((v[0] << 4) | v[0]);
            out->g = (uint8_t)((v[1] << 4) | v[1]);
            out->b = (uint8_t)((v[2] << 4) | v[2]);
            out->a = (uint8_t)(n == 4 ? ((v[3] << 4) | v[3]) : 0xff);
        } else {
            out->r = (uint8_t)((v[0] << 4) | v[1]);
            out->g = (uint8_t)((v[2] << 4) | v[3]);
            out->b = (uint8_t)((v[4] << 4) | v[5]);
            out->a = (uint8_t)(n == 8 ? ((v[6] << 4) | v[7]) : 0xff);
        }
        return 1;
    }

    if (len >= 4 &&
        (strncasecmp(s, "rgb(", 4) == 0 || (len >= 5 && strncasecmp(s, "rgba(", 5) == 0))) {
        int has_a = (s[3] == 'a' || s[3] == 'A');
        const char *p = s + (has_a ? 5 : 4);
        const char *end = s + len;
        int comp[4] = {0, 0, 0, 255};
        int n = 0;
        while (p < end && n < (has_a ? 4 : 3)) {
            while (p < end && (*p == ' ' || *p == ',' || *p == '\t')) {
                p++;
            }
            char *e = NULL;
            double v = strtod(p, &e);
            if (e == p) {
                return 0;
            }
            if (n == 3) {
                comp[3] = (int)(v * 255.0 + 0.5);
            } else {
                comp[n] = (int)v;
            }
            p = e;
            n++;
        }
        out->r = (uint8_t)(comp[0] < 0 ? 0 : comp[0] > 255 ? 255 : comp[0]);
        out->g = (uint8_t)(comp[1] < 0 ? 0 : comp[1] > 255 ? 255 : comp[1]);
        out->b = (uint8_t)(comp[2] < 0 ? 0 : comp[2] > 255 ? 255 : comp[2]);
        out->a = (uint8_t)(comp[3] < 0 ? 0 : comp[3] > 255 ? 255 : comp[3]);
        return 1;
    }

    struct {
        const char *name;
        size_t nlen;
        uint32_t rgb;
        uint8_t a;
    } named[] = {
        {"transparent", 11, 0x000000, 0x00}, {"black", 5, 0x000000, 0xff},
        {"white", 5, 0xffffff, 0xff},        {"red", 3, 0xff0000, 0xff},
        {"green", 5, 0x008000, 0xff},        {"blue", 4, 0x0000ff, 0xff},
        {"yellow", 6, 0xffff00, 0xff},       {"orange", 6, 0xffa500, 0xff},
        {"purple", 6, 0x800080, 0xff},       {"grey", 4, 0x808080, 0xff},
        {"gray", 4, 0x808080, 0xff},         {"lightgrey", 9, 0xd3d3d3, 0xff},
        {"lightgray", 9, 0xd3d3d3, 0xff},    {"darkgrey", 8, 0xa9a9a9, 0xff},
        {"silver", 6, 0xc0c0c0, 0xff},       {"navy", 4, 0x000080, 0xff},
        {"teal", 4, 0x008080, 0xff},         {"cyan", 4, 0x00ffff, 0xff},
        {"magenta", 7, 0xff00ff, 0xff},      {"maroon", 6, 0x800000, 0xff},
        {"olive", 5, 0x808000, 0xff},        {"lime", 4, 0x00ff00, 0xff},
        {"aqua", 4, 0x00ffff, 0xff},         {"fuchsia", 7, 0xff00ff, 0xff},
    };
    for (size_t i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
        if (len == named[i].nlen && strncasecmp(s, named[i].name, named[i].nlen) == 0) {
            out->r = (uint8_t)(named[i].rgb >> 16);
            out->g = (uint8_t)(named[i].rgb >> 8);
            out->b = (uint8_t)(named[i].rgb);
            out->a = named[i].a;
            return 1;
        }
    }
    return 0;
}

/* Parse a CSS length into pixels. Handles:
 *   - <number>px (most common)
 *   - <number>em / rem  (relative to font_size)
 *   - bare <number>  (treat as px — leniency for typo'd values)
 *   - <number>% relative to `pct_basis` (only used for padding/margin
 *     where percentages resolve against the *containing-block width*)
 * Returns 1 on success, 0 otherwise. */
static int parse_css_length(const char *s, size_t len, float font_size, float pct_basis,
                            float *out_px)
{
    while (len > 0 && (*s == ' ' || *s == '\t')) {
        s++;
        len--;
    }
    if (len == 0) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    const char *u = end;
    size_t ulen = len - (size_t)(end - s);
    while (ulen > 0 && (*u == ' ' || *u == '\t')) {
        u++;
        ulen--;
    }
    float scale = 1.0f;
    if (ulen >= 2 && (u[0] == 'p' || u[0] == 'P') && (u[1] == 'x' || u[1] == 'X')) {
        scale = 1.0f;
    } else if (ulen >= 2 && (u[0] == 'e' || u[0] == 'E') && (u[1] == 'm' || u[1] == 'M')) {
        scale = font_size;
    } else if (ulen >= 3 && (u[0] == 'r' || u[0] == 'R') && (u[1] == 'e' || u[1] == 'E') &&
               (u[2] == 'm' || u[2] == 'M')) {
        scale = font_size;
    } else if (ulen >= 1 && u[0] == '%') {
        scale = pct_basis * 0.01f;
    } else if (ulen == 0) {
        scale = 1.0f;
    } else {
        return 0;
    }
    *out_px = (float)(v * scale);
    return 1;
}

/* Read padding-<side> / margin-<side> with shorthand fallback. The CSS
 * shorthand `padding: A` / `padding: A B` / `padding: A B C` /
 * `padding: A B C D` defines all four sides — see CSS 2.1 §8.4. We
 * implement the shorthand parse here so e.g. github's `padding: 16px 24px`
 * propagates to all four sides correctly.
 *
 * Returns 1 if the requested side was set on the inline-style block. */
static int read_inline_box_lengths(const lxb_char_t *style, size_t slen, const char *prop,
                                   size_t prop_len, float font_size, float pct_basis,
                                   float *out_top, float *out_right, float *out_bottom,
                                   float *out_left)
{
    int any = 0;
    /* Shorthand: padding / margin with 1..4 values. */
    size_t vlen = 0;
    const char *v = find_inline_decl(style, slen, prop, prop_len, &vlen);
    if (v && vlen) {
        float vals[4] = {0};
        int n = 0;
        size_t i = 0;
        while (i < vlen && n < 4) {
            while (i < vlen && (v[i] == ' ' || v[i] == '\t')) {
                i++;
            }
            if (i >= vlen) {
                break;
            }
            size_t j = i;
            while (j < vlen && v[j] != ' ' && v[j] != '\t') {
                j++;
            }
            float px = 0;
            if (parse_css_length(v + i, j - i, font_size, pct_basis, &px)) {
                vals[n++] = px;
            }
            i = j;
        }
        if (n > 0) {
            float t = vals[0];
            float right = (n >= 2) ? vals[1] : t;
            float bottom = (n >= 3) ? vals[2] : t;
            float left = (n >= 4) ? vals[3] : right;
            if (out_top) {
                *out_top = t;
            }
            if (out_right) {
                *out_right = right;
            }
            if (out_bottom) {
                *out_bottom = bottom;
            }
            if (out_left) {
                *out_left = left;
            }
            any = 1;
        }
    }
    /* Per-side overrides (padding-top / margin-left / …) take
	 * precedence over the shorthand — same as the cascade. */
    const char *sides[4] = {"-top", "-right", "-bottom", "-left"};
    float *targets[4] = {out_top, out_right, out_bottom, out_left};
    for (int s = 0; s < 4; s++) {
        char buf[32];
        size_t prefix = prop_len < sizeof(buf) - 8 ? prop_len : sizeof(buf) - 8;
        memcpy(buf, prop, prefix);
        size_t side_len = strlen(sides[s]);
        memcpy(buf + prefix, sides[s], side_len);
        size_t key_len = prefix + side_len;
        size_t plen = 0;
        const char *p = find_inline_decl(style, slen, buf, key_len, &plen);
        if (!p) {
            continue;
        }
        float px = 0;
        if (parse_css_length(p, plen, font_size, pct_basis, &px)) {
            if (targets[s]) {
                *targets[s] = px;
            }
            any = 1;
        }
    }
    return any;
}

/* Read a single CSS length value (no shorthand). Returns 1 on success. */
static int read_inline_length(const lxb_char_t *style, size_t slen, const char *key, size_t klen,
                              float font_size, float pct_basis, float *out_px)
{
    size_t vlen = 0;
    const char *v = find_inline_decl(style, slen, key, klen, &vlen);
    if (!v || vlen == 0) {
        return 0;
    }
    return parse_css_length(v, vlen, font_size, pct_basis, out_px);
}

/* Read an enum-like keyword property and match against `*choices`
 * (NULL-terminated). Returns the matched index (0-based), or -1. */
static int read_inline_keyword(const lxb_char_t *style, size_t slen, const char *key, size_t klen,
                               const char *const *choices)
{
    size_t vlen = 0;
    const char *v = find_inline_decl(style, slen, key, klen, &vlen);
    if (!v || vlen == 0) {
        return -1;
    }
    while (vlen > 0 && (*v == ' ' || *v == '\t')) {
        v++;
        vlen--;
    }
    for (int i = 0; choices[i]; i++) {
        size_t cl = strlen(choices[i]);
        if (vlen >= cl && strncasecmp(v, choices[i], cl) == 0) {
            return i;
        }
    }
    return -1;
}

/* Forward decl — read_computed_style is defined further down. */
static int read_computed_style(lxb_dom_element_t *el, char **out_data, size_t *out_len);

/* `display: none` check — the cheap path for hiding entire subtrees.
 * Returns 1 if the element should be entirely skipped at box-build. */
static int is_display_none(lxb_dom_element_t *el)
{
    if (!el) {
        return 0;
    }
    /* Computed style first — covers stylesheet rules. */
    char *cstyle = NULL;
    size_t cstyle_len = 0;
    if (read_computed_style(el, &cstyle, &cstyle_len)) {
        size_t vlen = 0;
        const char *d =
            find_inline_decl((const lxb_char_t *)cstyle, cstyle_len, "display", 7, &vlen);
        int hidden = d && vlen >= 4 && strncasecmp(d, "none", 4) == 0;
        free(cstyle);
        if (hidden) {
            return 1;
        }
    }
    /* Inline style. */
    size_t slen = 0;
    const lxb_char_t *style =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &slen);
    if (style && slen > 0) {
        size_t vlen = 0;
        const char *d = find_inline_decl(style, slen, "display", 7, &vlen);
        if (d && vlen >= 4 && strncasecmp(d, "none", 4) == 0) {
            return 1;
        }
    }
    /* `hidden` HTML attribute also hides the element per spec. */
    size_t hl = 0;
    const lxb_char_t *h = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"hidden", 6, &hl);
    if (h) {
        return 1;
    }
    return 0;
}

/* Look up `key` in an inline-style attribute, parse it as a CSS color,
 * write to *out on success. Returns 1 iff found+parsed.
 *
 * `r` provides the custom-property table for var() substitution; pass
 * NULL to skip resolution (e.g. when re-reading the inline `style=`
 * attribute as a fallback). */
static int read_inline_color(struct yetty_ylexbor *r, const lxb_char_t *style, size_t slen,
                             const char *key, size_t klen, struct yetty_ylexbor_color *out)
{
    size_t vlen = 0;
    const char *v = find_inline_decl(style, slen, key, klen, &vlen);
    if (!v) {
        return 0;
    }
    if (r != NULL) {
        char *resolved = yetty_ylexbor_css_vars_resolve(r, v, vlen);
        if (resolved) {
            int ok = parse_css_color(resolved, strlen(resolved), out);
            free(resolved);
            return ok;
        }
    }
    return parse_css_color(v, vlen, out);
}

/* Pull the *computed* CSS for `el` (cascade result of all matching
 * stylesheet rules + inline style) as a serialized declaration list.
 * Caller frees `*out_data` via free(). Returns 1 on success. */
static int read_computed_style(lxb_dom_element_t *el, char **out_data, size_t *out_len)
{
    lexbor_str_t str = {0};
    lxb_status_t s = lxb_dom_element_style_serialize_str(el, &str, LXB_DOM_ELEMENT_STYLE_OPT_UNDEF);
    if (s != LXB_STATUS_OK || str.data == NULL || str.length == 0) {
        return 0;
    }
    /* str.data is allocated in the doc's text mraw — copy it out
	 * so we can free it on the boundary. The buffer lives until
	 * document destroy otherwise, which is fine for our walker but
	 * fragile. Cheap copy keeps things obvious. */
    char *copy = malloc(str.length + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, str.data, str.length);
    copy[str.length] = '\0';
    *out_data = copy;
    *out_len = str.length;
    return 1;
}

static enum yetty_ylexbor_layout_mode layout_mode_for(lxb_dom_element_t *el)
{
    size_t slen = 0;
    const lxb_char_t *style =
        lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &slen);
    if (!style || slen == 0) {
        return YL_LAYOUT_BLOCK;
    }

    size_t vlen = 0;
    const char *disp = find_inline_decl(style, slen, "display", 7, &vlen);
    if (!disp) {
        return YL_LAYOUT_BLOCK;
    }
    if (!(vlen >= 4 && strncasecmp(disp, "flex", 4) == 0)) {
        return YL_LAYOUT_BLOCK;
    }

    size_t dirlen = 0;
    const char *dir = find_inline_decl(style, slen, "flex-direction", 14, &dirlen);
    if (dir && dirlen >= 6 && strncasecmp(dir, "column", 6) == 0) {
        return YL_LAYOUT_FLEX_COLUMN;
    }
    return YL_LAYOUT_FLEX_ROW;
}

static void walk(struct yetty_ylexbor *r, lxb_dom_node_t *node,
                 const struct yl_style_state *parent_style, uint32_t parent_idx,
                 struct yl_inline_buf *inline_collect, int depth);

/* Flush an accumulated inline-text run as one or more YL_BOX_INLINE_TEXT
 * children of `parent_idx`. Layout will wrap it into lines; box-build
 * stores the un-wrapped string. */
static void flush_inline(struct yetty_ylexbor *r, const struct yl_style_state *style,
                         uint32_t parent_idx, struct yl_inline_buf *coll)
{
    if (coll->buf == NULL || coll->len == 0) {
        return;
    }

    /* Trim trailing single space introduced by whitespace collapsing. */
    while (coll->len > 0 && coll->buf[coll->len - 1] == ' ') {
        coll->len--;
    }
    if (coll->len == 0) {
        coll->last_was_space = 0;
        return;
    }

    uint32_t cidx;
    if (box_alloc(r, &cidx) != 0) {
        return;
    }
    struct yetty_ylexbor_box *b = &r->boxes.data[cidx];
    b->kind = YL_BOX_INLINE_TEXT;
    style_to_box(b, style);
    b->text = yetty_ylexbor_arena_dup(r, coll->buf, coll->len);
    b->text_len = coll->len;
    link_child(r, parent_idx, cidx);

    coll->len = 0;
    coll->last_was_space = 0;
}

static void walk(struct yetty_ylexbor *r, lxb_dom_node_t *node,
                 const struct yl_style_state *parent_style, uint32_t parent_idx,
                 struct yl_inline_buf *inline_collect, int depth)
{
    for (lxb_dom_node_t *child = node->first_child; child != NULL; child = child->next) {
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_text_t *t = lxb_dom_interface_text(child);
            size_t len = t->char_data.data.length;
            const char *bytes = (const char *)t->char_data.data.data;
            if (inline_collect != NULL && len > 0) {
                inline_buf_append(inline_collect, bytes, len);
            }
            continue;
        }
        if (child->type != LXB_DOM_NODE_TYPE_ELEMENT) {
            continue;
        }

        lxb_dom_element_t *el = lxb_dom_interface_element(child);
        const struct yl_default_style *d = default_for(child->local_name);
        if (d->disp == YL_DISP_NONE) {
            continue;
        }

        /* Honor `display: none` and the `hidden` HTML attribute —
		 * cuts out off-screen menus, dropdowns, and hidden footers
		 * that would otherwise overlap real content. */
        if (is_display_none(el)) {
            continue;
        }

        struct yl_style_state s = apply_default(parent_style, d);

        if (d->disp == YL_DISP_BLOCK) {
            /* Flush any inline text accumulated for the parent
			 * block before opening a new child block. */
            flush_inline(r, parent_style, parent_idx, inline_collect);

            uint32_t bidx;
            if (box_alloc(r, &bidx) != 0) {
                return;
            }
            struct yetty_ylexbor_box *b = &r->boxes.data[bidx];
            b->kind = YL_BOX_BLOCK;
            b->element = el;

            /* Single cascade path: libcss. The inline `style=` is
             * read here only so we can hand it to css_select_style
             * with the correct precedence — everything else
             * (color/bg/padding/margin/border/text-align/flex)
             * comes back through the libcss override block below. */
            size_t istylen = 0;
            const lxb_char_t *istyle =
                lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &istylen);

            style_to_box(b, &s);

            /* Tag-default margins from default_for(d) — libcss
             * overrides with UA + author values when it has them. */
            b->margin_top = d->margin_top_em * s.font_size;
            b->margin_bottom = d->margin_bottom_em * s.font_size;

            /* Inherit text-align from parent. libcss overrides
             * below when the cascade has a definite value. */
            if (parent_style != NULL) {
                b->text_align = parent_style->text_align;
            }
            s.text_align = b->text_align;

            float pct_basis = s.font_size * 16.0f;

            b->layout_mode = YL_LAYOUT_BLOCK;

            /* libcss is the cascade. Every property we care about
             * comes back from css_select_style with full semantics:
             * matching rules, inline style, UA defaults, inheritance,
             * and var() (resolved during sheet ingest in
             * yetty_ybrowser_libcss_add_sheet). */
            if (r->libcss) {
                css_computed_style *cs = yetty_ybrowser_libcss_select(
                    r, el, (const char *)istyle, istyle ? istylen : 0);
                if (cs) {
                    struct yetty_ylexbor_color cc;
                    int weight;
                    bool italic;
                    float px;
                    bool ma;

                    if (yetty_ybrowser_libcss_color(cs, &cc)) {
                        b->fg = cc;
                        s.fg = cc;
                    }
                    if (yetty_ybrowser_libcss_bg_color(cs, &cc)) {
                        b->bg = cc;
                    }
                    if (yetty_ybrowser_libcss_font_size(
                            r, cs, parent_style ? parent_style->font_size : r->default_font_size,
                            &px)) {
                        b->font_size = px;
                        s.font_size = px;
                    }
                    if (yetty_ybrowser_libcss_font_weight(cs, &weight)) {
                        b->font_weight = weight;
                    }
                    if (yetty_ybrowser_libcss_font_italic(cs, &italic)) {
                        b->font_italic = italic;
                    }
                    if (yetty_ybrowser_libcss_width(r, cs, s.font_size, pct_basis, &px)) {
                        b->css_width = px;
                    }
                    if (yetty_ybrowser_libcss_height(r, cs, s.font_size, pct_basis, &px)) {
                        b->css_height = px;
                    }
                    if (yetty_ybrowser_libcss_max_width(r, cs, s.font_size, pct_basis, &px)) {
                        b->css_max_width = px;
                    }
                    if (yetty_ybrowser_libcss_min_width(r, cs, s.font_size, pct_basis, &px)) {
                        b->css_min_width = px;
                    }

                    float *margin_dst[4] = {&b->margin_top, &b->margin_right,
                                            &b->margin_bottom, &b->margin_left};
                    float *padding_dst[4] = {&b->padding_top, &b->padding_right,
                                             &b->padding_bottom, &b->padding_left};
                    float *border_dst[4] = {&b->border_top, &b->border_right,
                                            &b->border_bottom, &b->border_left};
                    for (int side = 0; side < 4; side++) {
                        ma = false;
                        if (yetty_ybrowser_libcss_margin(r, cs, side, s.font_size, pct_basis, &px,
                                                         &ma)) {
                            if (ma) {
                                if (side == 1) {
                                    b->margin_right_auto = true;
                                } else if (side == 3) {
                                    b->margin_left_auto = true;
                                }
                            } else {
                                *margin_dst[side] = px;
                            }
                        }
                        if (yetty_ybrowser_libcss_padding(r, cs, side, s.font_size, pct_basis,
                                                          &px)) {
                            *padding_dst[side] = px;
                        }
                        if (yetty_ybrowser_libcss_border_width(r, cs, side, s.font_size, &px)) {
                            *border_dst[side] = px;
                        }
                        struct yetty_ylexbor_color bc;
                        if (yetty_ybrowser_libcss_border_color(cs, side, &b->fg, &bc)) {
                            if (bc.a > 0 && b->border_color.a == 0) {
                                b->border_color = bc;
                            }
                        }
                    }
                    int ta = yetty_ybrowser_libcss_text_align(cs);
                    if (ta == CSS_TEXT_ALIGN_LEFT) {
                        b->text_align = 0;
                        s.text_align = 0;
                    } else if (ta == CSS_TEXT_ALIGN_CENTER) {
                        b->text_align = 1;
                        s.text_align = 1;
                    } else if (ta == CSS_TEXT_ALIGN_RIGHT) {
                        b->text_align = 2;
                        s.text_align = 2;
                    } else if (ta == CSS_TEXT_ALIGN_JUSTIFY) {
                        b->text_align = 3;
                        s.text_align = 3;
                    }

                    int disp = yetty_ybrowser_libcss_display(cs, parent_style == NULL);
                    if (disp == CSS_DISPLAY_FLEX || disp == CSS_DISPLAY_INLINE_FLEX) {
                        int fd = yetty_ybrowser_libcss_flex_direction(cs);
                        b->layout_mode = (fd == CSS_FLEX_DIRECTION_COLUMN ||
                                          fd == CSS_FLEX_DIRECTION_COLUMN_REVERSE)
                                             ? YL_LAYOUT_FLEX_COLUMN
                                             : YL_LAYOUT_FLEX_ROW;
                    } else if (disp == CSS_DISPLAY_BLOCK || disp == CSS_DISPLAY_INLINE_BLOCK ||
                               disp == CSS_DISPLAY_LIST_ITEM) {
                        b->layout_mode = YL_LAYOUT_BLOCK;
                    }
                    /* Don't carry currentColor border in further. */
                    yetty_ybrowser_libcss_release(cs);
                }
            }

            link_child(r, parent_idx, bidx);

            /* Recurse with a fresh inline accumulator for this
			 * block's children. */
            struct yl_inline_buf ib = {0};
            walk(r, child, &s, bidx, &ib, depth + 1);
            flush_inline(r, &s, bidx, &ib);
            free(ib.buf);
        } else {
            /* <img>: pre-decode (or hit the cache) so the box's
			 * geometry reflects natural pixel dimensions, with
			 * `width` / `height` HTML attributes overriding when
			 * present. The placeholder fallback (grey box) kicks
			 * in if the fetch or decode failed. */
            if (child->local_name == LXB_TAG_IMG) {
                flush_inline(r, parent_style, parent_idx, inline_collect);
                uint32_t iidx;
                if (box_alloc(r, &iidx) != 0) {
                    return;
                }
                struct yetty_ylexbor_box *ib = &r->boxes.data[iidx];
                ib->kind = YL_BOX_INLINE_IMAGE;
                ib->element = el;

                /* Resolve src/srcset/data-* + decode into the
				 * cache so layout has natural dimensions to
				 * fall back on. yetty_ylexbor_img_pick_url
				 * handles lazy-loading patterns where the
				 * real URL isn't in `src`. */
                struct yetty_ylexbor_img_cache_entry *cached = NULL;
                char *abs = yetty_ylexbor_img_pick_url(r, el);
                if (abs) {
                    cached = yetty_ylexbor_img_cache_get_or_load(r, abs);
                    free(abs);
                }

                /* HTML width/height attrs (in px) take priority
				 * — the spec calls these the "presentation
				 * hints"; sites use them to reserve space
				 * without waiting for decode. */
                int attr_w = -1, attr_h = -1;
                size_t alen = 0;
                const lxb_char_t *aw =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"width", 5, &alen);
                if (aw && alen > 0) {
                    attr_w = atoi((const char *)aw);
                }
                const lxb_char_t *ah =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"height", 6, &alen);
                if (ah && alen > 0) {
                    attr_h = atoi((const char *)ah);
                }

                int nat_w = (cached && !cached->failed) ? cached->w : 0;
                int nat_h = (cached && !cached->failed) ? cached->h : 0;
                if (attr_w > 0) {
                    ib->w = (float)attr_w;
                } else if (nat_w > 0) {
                    ib->w = (float)nat_w;
                }
                if (attr_h > 0) {
                    ib->h = (float)attr_h;
                } else if (nat_h > 0) {
                    ib->h = (float)nat_h;
                }
                /* If only one dimension is known, preserve aspect ratio. */
                if (ib->w > 0 && ib->h <= 0 && nat_w > 0 && nat_h > 0) {
                    ib->h = ib->w * (float)nat_h / (float)nat_w;
                }
                if (ib->h > 0 && ib->w <= 0 && nat_w > 0 && nat_h > 0) {
                    ib->w = ib->h * (float)nat_w / (float)nat_h;
                }

                link_child(r, parent_idx, iidx);
                continue;
            }
            /* Inline element: recurse, accumulating into the
			 * parent block's inline buffer. We pass parent_style
			 * for inheritance, not s — because only the *style*
			 * of the inline run as a whole is captured by the
			 * surrounding text, not per-segment styling (MVP). */
            walk(r, child, &s, parent_idx, inline_collect, depth);
        }
    }
}

/* ===========================================================================
 * Entry point.
 * ===========================================================================*/

struct yetty_ycore_void_result yetty_ylexbor_box_build(struct yetty_ylexbor *r)
{
    if (r == NULL || r->document == NULL) {
        return YETTY_ERR(yetty_ycore_void, "ylexbor_box_build: null");
    }

    r->boxes.size = 0;

    /* Root box wraps the whole viewport. */
    uint32_t root_idx;
    if (box_alloc(r, &root_idx) != 0) {
        return YETTY_ERR(yetty_ycore_void, "alloc root");
    }
    struct yetty_ylexbor_box *root = &r->boxes.data[root_idx];
    root->kind = YL_BOX_BLOCK;
    root->font_size = r->default_font_size;
    root->font_weight = 400;
    root->fg.r = root->fg.g = root->fg.b = 0;
    root->fg.a = 0xff;
    root->bg.r = root->bg.g = root->bg.b = 0xff;
    root->bg.a = 0xff;

    struct yl_style_state initial = {
        .font_size = r->default_font_size,
        .font_weight = 400,
        .font_italic = false,
        .fg = root->fg,
    };

    struct yl_inline_buf ib = {0};
    walk(r, lxb_dom_interface_node(r->document), &initial, root_idx, &ib, 0);
    flush_inline(r, &initial, root_idx, &ib);
    free(ib.buf);

    return YETTY_OK_VOID();
}
