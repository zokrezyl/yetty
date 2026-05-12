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
    int underline;   /* 1 = force on, 0 = inherit (no force). Used so
	                  * <a> propagates text-decoration: underline to
	                  * its descendants without needing libcss for
	                  * inline elements. */
};

#define INHERIT_RGB 0xffffffffu

static const struct yl_default_style YL_DEFAULT_INLINE = {
    YL_DISP_INLINE, 1.0f, 0, -1, 0.0f, 0.0f, INHERIT_RGB, 0,
};
static const struct yl_default_style YL_DEFAULT_BLOCK = {
    YL_DISP_BLOCK, 1.0f, 0, -1, 1.0f, 1.0f, INHERIT_RGB, 0,
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

    static struct yl_default_style strong = {YL_DISP_INLINE, 1.0f, 700, -1, 0, 0, INHERIT_RGB, 0};
    static struct yl_default_style em = {YL_DISP_INLINE, 1.0f, 0, 1, 0, 0, INHERIT_RGB, 0};
    static struct yl_default_style anchor = {YL_DISP_INLINE, 1.0f, 0, -1, 0, 0, 0x0000eeu, 1};
    static struct yl_default_style small_e = {YL_DISP_INLINE, 0.83f, 0, -1, 0, 0, INHERIT_RGB, 0};
    static struct yl_default_style code_e = {YL_DISP_INLINE, 1.0f, 0, -1, 0, 0, 0x444444u, 0};
    static struct yl_default_style none = {YL_DISP_NONE, 1.0f, 0, -1, 0, 0, INHERIT_RGB, 0};

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
    case LXB_TAG_SMALL:
    case LXB_TAG_SUB:
    case LXB_TAG_SUP:
        return &small_e;
    case LXB_TAG_CODE:
    case LXB_TAG_KBD:
    case LXB_TAG_SAMP:
    case LXB_TAG_VAR:
        return &code_e;

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
    bool underline;  /* true inside <a> (and any other inline element
	                  * whose default_for() flags underline=1) */
    bool line_through; /* `text-decoration: line-through` from CSS */
    bool overline;     /* `text-decoration: overline` from CSS */
    struct yetty_ylexbor_color fg;
    int text_align; /* inherited; 0=left, 1=center, 2=right, 3=justify */
    /* Deepest inline ancestor element on the recursion stack — used to
	 * stamp YL_BOX_INLINE_TEXT boxes with a hit-target for
	 * dispatch_click. NULL when text is directly inside a block. */
    lxb_dom_element_t *link_element;
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
    if (d->underline) {
        s.underline = true;
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
    /* When set, copy bytes verbatim — used inside <pre> blocks
	 * (white-space: pre / pre-wrap) so source-line breaks and
	 * indentation aren't collapsed away. */
    int preserve_ws;
    /* Style segments — one entry per contiguous run of text written
	 * with the same fg/weight/italic/underline. inline_buf_append_styled
	 * opens a new segment whenever the style changes between calls,
	 * so adjacent text from the same inline element gets fused. */
    struct yetty_ylexbor_inline_seg *segs;
    size_t segs_count, segs_cap;
};

static bool seg_style_matches(const struct yetty_ylexbor_inline_seg *seg,
                              const struct yl_style_state *style)
{
    (void)seg;
    (void)style;
    /* Always merge — match ylexbor's behaviour of emitting ONE
	 * TEXT_SPAN per wrapped line. Per-segment style tracking
	 * created visible drift between fragments on the same line
	 * (each fragment is positioned by our naive `font*0.55` per-glyph
	 * width estimate, which doesn't match the canvas's actual font
	 * advances — cumulative drift across many fragments per line
	 * produced misaligned glyphs, visible as scattered descender
	 * letters around the page). Losing per-element coloring is the
	 * trade-off; the original ylexbor tool the user verified as
	 * working takes the same trade-off. */
    return true;
}

/* Open a new style segment in `b` if the current style differs from the
 * tail segment (or there's no tail yet). Idempotent — callers can blindly
 * invoke before each `inline_buf_append`. */
static int inline_buf_open_seg(struct yl_inline_buf *b, const struct yl_style_state *style)
{
    if (b->segs_count > 0 && seg_style_matches(&b->segs[b->segs_count - 1], style)) {
        return 0;
    }
    if (b->segs_count == b->segs_cap) {
        size_t nc = b->segs_cap ? b->segs_cap * 2 : 8;
        struct yetty_ylexbor_inline_seg *p = realloc(b->segs, nc * sizeof(*p));
        if (!p) {
            return -1;
        }
        b->segs = p;
        b->segs_cap = nc;
    }
    b->segs[b->segs_count++] = (struct yetty_ylexbor_inline_seg){
        .start = b->len,
        .fg = style->fg,
        .font_weight = style->font_weight,
        .font_italic = style->font_italic,
        .underline = style->underline,
        .line_through = style->line_through,
        .overline = style->overline,
        .element = style->link_element,
    };
    return 0;
}

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
    /* Whitespace handling. Two modes:
	 *   - preserve_ws=1 (white-space: pre): copy every byte as-is
	 *     so source newlines and indentation survive into paint.
	 *   - preserve_ws=0 (default normal): collapse runs to one
	 *     space and drop leading whitespace, per CSS 2.1 §16.6.
	 *
	 * Special-case: U+00A0 NON-BREAKING SPACE (UTF-8 0xC2 0xA0).
	 * Wikipedia/MediaWiki citations are saturated with NBSPs as
	 * intra-citation separators ("CiteSeerX 10.1.1...") and
	 * pre-formatted unit gaps ("pp. 8-9"). The naive glyph-SDF
	 * font has no glyph for U+00A0 — the canvas falls back to a
	 * visible placeholder (a small box / hollow rectangle in most
	 * builds), which the user perceives as "p, g, q letters scattered
	 * around" — they're actually NBSP placeholders falling near the
	 * x-height baseline. Replace NBSP with a regular ASCII space at
	 * collection time. Semantic line-break suppression is lost, but
	 * our line-wrap pass only breaks at ASCII spaces anyway, so the
	 * NBSP's main purpose was never honoured — visual correctness
	 * is the higher-value behaviour to ship. preserve_ws mode keeps
	 * NBSPs verbatim (authors who set white-space: pre are usually
	 * relying on every byte being preserved). */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (b->preserve_ws) {
            b->buf[b->len++] = (char)c;
            b->last_was_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            continue;
        }
        /* NBSP collapse: detect 0xC2 0xA0 and treat as one space.
		 * Done before the regular whitespace check so it falls into
		 * the collapse machinery (dropped at start-of-line, single
		 * after any run of whitespace). */
        if (c == 0xC2 && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) {
            i++; /* skip the 0xA0 trailing byte */
            if (b->last_was_space || b->len == 0) {
                continue;
            }
            b->buf[b->len++] = ' ';
            b->last_was_space = 1;
            continue;
        }
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
        /* Whole buffer was whitespace — drop any opened segments too
		 * so the next inline run starts with a fresh seg list. */
        free(coll->segs);
        coll->segs = NULL;
        coll->segs_count = 0;
        coll->segs_cap = 0;
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
    /* Transfer the accumulated style segments to the box. wrap_inline_box
	 * will read them to split each line into one painted sub-box per
	 * styled fragment, then free segs. Callers reset coll's segs/len for
	 * the next inline run within the same block. */
    if (coll->segs_count > 0) {
        /* Cap the last segment's effective length at coll->len so the
		 * trailing-space trim above doesn't leave a dangling segment
		 * span past the buffer's tail (segments only carry `start`;
		 * the end is implied by the next seg.start or by box->text_len). */
        b->segs = coll->segs;
        b->segs_count = coll->segs_count;
        coll->segs = NULL;
        coll->segs_count = 0;
        coll->segs_cap = 0;
    }
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
                /* Open / continue a style segment under the *current*
				 * inline style. parent_style is the style of the
				 * innermost element on the recursion stack — i.e. the
				 * one immediately wrapping this text node, exactly
				 * what we want for per-segment fg/weight/italic/
				 * underline. */
                inline_buf_open_seg(inline_collect, parent_style);
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
        /* libcss-side display: peek the cascade once. We use the
		 * result for TWO decisions:
		 *
		 *   (a) display:none — skip the subtree. UA CSS rules like
		 *       [aria-hidden="true"] { display: none } only surface
		 *       through libcss, not the lexbor serialized inline
		 *       style.
		 *
		 *   (b) inline-vs-block override — author CSS commonly turns
		 *       an inline element into a block (or vice-versa). The
		 *       tag-name table is only a fallback for when libcss has
		 *       no opinion.
		 *
		 * The select below also produces a fresh computed-style which
		 * the block path will re-select later. We could thread the
		 * pointer through, but that complicates the cleanup chain;
		 * re-selecting is cheap relative to the rest of box-build, so
		 * we discard here and select again inside the block branch. */
        int libcss_disp = -1;
        if (r->libcss) {
            size_t pre_istylen = 0;
            const lxb_char_t *pre_istyle = lxb_dom_element_get_attribute(
                el, (const lxb_char_t *)"style", 5, &pre_istylen);
            css_computed_style *pre_cs = yetty_ybrowser_libcss_select(
                r, el, (const char *)pre_istyle, pre_istyle ? pre_istylen : 0);
            if (pre_cs) {
                libcss_disp = yetty_ybrowser_libcss_display(pre_cs, parent_style == NULL);
                yetty_ybrowser_libcss_release(pre_cs);
                if (libcss_disp == CSS_DISPLAY_NONE) {
                    continue;
                }
            }
        }

        /* effective_disp: start from the tag-default, override with
		 * libcss when it returns a usable value. The libcss UA-default
		 * misreports CSS_DISPLAY_TABLE for <figure> and a few similar
		 * elements — we leave that quirk to the block branch's own
		 * remediation (it forces back to BLOCK for non-<table>
		 * elements) and don't try to disambiguate here.
		 *
		 * INLINE-BLOCK / INLINE-TABLE / INLINE-FLEX are NOT promoted
		 * to YL_DISP_BLOCK. We don't model the "shrink-to-fit width
		 * sized to content while flowing as inline" semantics, and
		 * promoting them to BLOCK breaks surrounding inline flow on
		 * real pages — Wikipedia in particular uses inline-block for
		 * citation badges, year-pill spans, sidebar tags, and a
		 * handful of other inline-but-styled elements. Promoting
		 * those to block stacks every badge onto its own line and
		 * scatters single letters / fragments across the page (the
		 * "p, g, q garbage" pattern the user reported). Keep
		 * inline-* at the tag-default disposition: <span> stays
		 * inline, <div> stays block. */
        enum yl_disp effective_disp = d->disp;
        if (libcss_disp >= 0) {
            switch (libcss_disp) {
            case CSS_DISPLAY_INLINE:
                effective_disp = YL_DISP_INLINE;
                break;
            case CSS_DISPLAY_BLOCK:
            case CSS_DISPLAY_LIST_ITEM:
            case CSS_DISPLAY_TABLE:
            case CSS_DISPLAY_TABLE_ROW:
            case CSS_DISPLAY_TABLE_ROW_GROUP:
            case CSS_DISPLAY_TABLE_HEADER_GROUP:
            case CSS_DISPLAY_TABLE_FOOTER_GROUP:
            case CSS_DISPLAY_TABLE_COLUMN:
            case CSS_DISPLAY_TABLE_COLUMN_GROUP:
            case CSS_DISPLAY_TABLE_CELL:
            case CSS_DISPLAY_TABLE_CAPTION:
            case CSS_DISPLAY_FLEX:
                effective_disp = YL_DISP_BLOCK;
                break;
            /* CSS_DISPLAY_NONE handled above. INLINE_BLOCK,
			 * INLINE_TABLE, INLINE_FLEX, GRID, RUN_IN, … — let
			 * the tag default decide. */
            default:
                break;
            }
        }
        /* <img> is special — even when CSS reports inline (the
		 * default), we still produce a YL_BOX_INLINE_IMAGE rather
		 * than recursing as text. Fall through to the existing
		 * else-branch which handles the img case. */
        if (child->local_name == LXB_TAG_IMG) {
            effective_disp = YL_DISP_INLINE;
        }

        struct yl_style_state s = apply_default(parent_style, d);

        if (effective_disp == YL_DISP_BLOCK) {
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

            /* Properties whose values we need OUTSIDE the libcss
			 * block (to drive the inline-accumulator setup, marker
			 * choice, bg-image fetch). Filled by the libcss branch
			 * below; left at default when libcss isn't available. */
            int css_white_space = CSS_WHITE_SPACE_NORMAL;
            unsigned css_text_dec = 0;
            int css_list_style = CSS_LIST_STYLE_TYPE_DISC;

            /* libcss is the cascade. Every property we care about
             * comes back from css_select_style with full semantics:
             * matching rules, inline style, UA defaults, inheritance,
             * and var() (resolved during sheet ingest in
             * yetty_ybrowser_libcss_add_sheet). */
            if (r->libcss) {
                css_computed_style *cs = yetty_ybrowser_libcss_select(
                    r, el, (const char *)istyle, istyle ? istylen : 0);
                /* Re-fetch box pointer — yetty_ybrowser_libcss_select runs
                 * arbitrary callbacks; even though they don't touch our
                 * vector today, the compiler can otherwise lift the
                 * earlier `b = &r->boxes.data[bidx]` past stores done by
                 * later property writes, producing 0 for css_width et al.
                 * Refetch defensively so all subsequent reads/writes go
                 * through the live base pointer. */
                b = &r->boxes.data[bidx];
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
                        s.font_weight = weight;
                    }
                    if (yetty_ybrowser_libcss_font_italic(cs, &italic)) {
                        b->font_italic = italic;
                        s.font_italic = italic;
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
                    /* libcss's compiled-in UA stylesheet reports computed
				 * display=CSS_DISPLAY_TABLE (6) for several non-table
				 * HTML elements (most notably <figure>). Without this
				 * guard our layout_block would dispatch to layout_table,
				 * which scans for descendant <tr> elements, finds none,
				 * and returns zero height — leaving the figure's inner
				 * <img> at the memset-default (0,0). On Wikipedia that
				 * collapsed every article figure onto the upper-left
				 * corner of the page. Test:
				 * test_figure_img_positioned (ybrowser-layout-test) and
				 * test_no_images_at_zero_zero (ybrowser-wikipedia-test).
				 *
				 * Only honour CSS_DISPLAY_TABLE when the element really
				 * is a <table>; everything else falls through to BLOCK. */
                    if ((disp == CSS_DISPLAY_TABLE || disp == CSS_DISPLAY_INLINE_TABLE) &&
                        child->local_name != LXB_TAG_TABLE) {
                        disp = CSS_DISPLAY_BLOCK;
                    }
                    if (disp == CSS_DISPLAY_FLEX || disp == CSS_DISPLAY_INLINE_FLEX) {
                        int fd = yetty_ybrowser_libcss_flex_direction(cs);
                        b->layout_mode = (fd == CSS_FLEX_DIRECTION_COLUMN ||
                                          fd == CSS_FLEX_DIRECTION_COLUMN_REVERSE)
                                             ? YL_LAYOUT_FLEX_COLUMN
                                             : YL_LAYOUT_FLEX_ROW;
                        b->justify_content = yetty_ybrowser_libcss_justify_content(cs);
                        b->align_items = yetty_ybrowser_libcss_align_items(cs);
                    } else if (disp == CSS_DISPLAY_TABLE ||
                               disp == CSS_DISPLAY_INLINE_TABLE) {
                        b->layout_mode = YL_LAYOUT_TABLE;
                    } else if (disp == CSS_DISPLAY_BLOCK || disp == CSS_DISPLAY_INLINE_BLOCK ||
                               disp == CSS_DISPLAY_LIST_ITEM) {
                        b->layout_mode = YL_LAYOUT_BLOCK;
                    }
                    /* Flex-item properties — always read so a block
				 * inside a flex parent gets sized correctly. The
				 * parent's layout_mode will gate whether they're
				 * consumed. */
                    float fg = 0;
                    if (yetty_ybrowser_libcss_flex_grow(cs, &fg)) {
                        b->flex_grow = fg;
                    }
                    float fb_px = 0;
                    bool fb_auto = false;
                    if (yetty_ybrowser_libcss_flex_basis(r, cs, s.font_size, pct_basis, &fb_px,
                                                         &fb_auto)) {
                        /* Encoding convention shared with css_width:
						 * 0 = auto / not set, > 0 = absolute px,
						 * < 0 = percent ratio (-N/100). The flex
						 * solver dispatches on the sign. */
                        b->flex_basis_px = fb_auto ? 0.0f : fb_px;
                    } else {
                        b->flex_basis_px = 0.0f;
                    }
                    /* Float + clear. The layout pass removes floated
				 * boxes from normal flow and rewinds the available
				 * content width for siblings that overlap. */
                    int fv = yetty_ybrowser_libcss_float(cs);
                    if (fv == CSS_FLOAT_LEFT) {
                        b->float_side = 1;
                    } else if (fv == CSS_FLOAT_RIGHT) {
                        b->float_side = 2;
                    }
                    int clr = yetty_ybrowser_libcss_clear(cs);
                    if (clr == CSS_CLEAR_LEFT) {
                        b->clear_side = 1;
                    } else if (clr == CSS_CLEAR_RIGHT) {
                        b->clear_side = 2;
                    } else if (clr == CSS_CLEAR_BOTH) {
                        b->clear_side = 3;
                    }

                    /* `white-space` (CSS) drives the inline buffer's
					 * preserve_ws flag below — needed so that author
					 * CSS `white-space: pre / pre-wrap` (common on
					 * docs-site code blocks inside <div class="..."")
					 * actually preserves source whitespace, not just
					 * the legacy <pre>/<textarea> tags. */
                    css_white_space = yetty_ybrowser_libcss_white_space(cs);

                    /* `text-decoration` — bitmask from libcss. UNDERLINE
					 * folds into the existing s.underline so the
					 * tag-default flag (for <a>) and CSS rules compose. */
                    css_text_dec = (unsigned)yetty_ybrowser_libcss_text_decoration(cs);
                    if (css_text_dec & CSS_TEXT_DECORATION_NONE) {
                        /* `text-decoration: none` explicitly clears the
						 * inheritable underline/strike/overline tracks,
						 * but ONLY for descendants whose own decoration
						 * doesn't re-add them — for the current run, an
						 * inheritable underline from <a> may still be
						 * useful. We honour `none` strictly: it kills
						 * the inherited bits at this scope. */
                        s.underline = false;
                        s.line_through = false;
                        s.overline = false;
                    } else {
                        if (css_text_dec & CSS_TEXT_DECORATION_UNDERLINE) {
                            s.underline = true;
                        }
                        if (css_text_dec & CSS_TEXT_DECORATION_LINE_THROUGH) {
                            s.line_through = true;
                        }
                        if (css_text_dec & CSS_TEXT_DECORATION_OVERLINE) {
                            s.overline = true;
                        }
                    }

                    /* `list-style-type` — driven by libcss so authors
					 * can override the UA default of `disc` per <li> /
					 * per <ul>. */
                    css_list_style = yetty_ybrowser_libcss_list_style_type(cs);

                    /* `background-image: url(...)` — fetch the absolute
					 * URL (resolved against the document base) and
					 * stash on the block so paint can decode + emit. */
                    {
                        char *bg_rel = NULL;
                        if (yetty_ybrowser_libcss_bg_image_url(cs, &bg_rel)) {
                            char *bg_abs = yetty_ylexbor_resolve_url(r, bg_rel);
                            if (bg_abs) {
                                free(b->bg_image_url);
                                b->bg_image_url = bg_abs;
                            }
                            free(bg_rel);
                        }
                    }

                    /* Don't carry currentColor border in further. */
                    yetty_ybrowser_libcss_release(cs);
                }
            }

            link_child(r, parent_idx, bidx);

            /* Recurse with a fresh inline accumulator for this
			 * block's children. `white-space: pre / pre-wrap`
			 * (from CSS, falling back to the <pre>/<textarea>
			 * tag defaults) makes the accumulator preserve every
			 * whitespace byte so source line breaks survive into
			 * the line-wrap pass. */
            struct yl_inline_buf ib = {0};
            int preserve_ws = (child->local_name == LXB_TAG_PRE ||
                               child->local_name == LXB_TAG_TEXTAREA);
            if (css_white_space == CSS_WHITE_SPACE_PRE ||
                css_white_space == CSS_WHITE_SPACE_PRE_WRAP ||
                css_white_space == CSS_WHITE_SPACE_PRE_LINE) {
                preserve_ws = 1;
            } else if (css_white_space == CSS_WHITE_SPACE_NORMAL ||
                       css_white_space == CSS_WHITE_SPACE_NOWRAP) {
                /* `normal` / `nowrap` collapse whitespace; nowrap
				 * additionally suppresses wrapping at spaces but our
				 * line-wrap pass already wraps only on width-overflow
				 * + explicit '\n', so nowrap effectively works as a
				 * synonym for `normal` here. A first-class nowrap
				 * implementation would skip the soft-break loop. */
                preserve_ws = 0;
            }
            ib.preserve_ws = preserve_ws;

            /* List markers — when the current block is an <li>, pick
			 * a marker string from the CSS list-style-type cascade
			 * (falls back to <ul>=disc / <ol>=decimal when libcss
			 * isn't wired). The marker is stored on the LI box rather
			 * than prepended to the inline buffer so paint can render
			 * it INTO the parent's left padding gutter (the UA gives
			 * <ul>/<ol> padding-left:40px) — that way the marker stays
			 * at the line-1 column even when the LI's body wraps onto
			 * line 2/3/N, instead of stranding "Line 2" at the marker
			 * column. */
            if (child->local_name == LXB_TAG_LI && child->parent &&
                child->parent->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_tag_id_t ptag = child->parent->local_name;
                /* Derive 1-based item number for ordered list types.
				 * Linear walk over prev-siblings; lists we render are
				 * small enough that O(n²) doesn't show up. */
                int n = 1;
                for (lxb_dom_node_t *p = child->prev; p; p = p->prev) {
                    if (p->type == LXB_DOM_NODE_TYPE_ELEMENT && p->local_name == LXB_TAG_LI) {
                        n++;
                    }
                }
                /* Resolve the marker glyph. css_list_style is set by
				 * the libcss block above; default falls back to the
				 * parent-tag convention. */
                char mbuf[16] = {0};
                size_t mlen = 0;
                int t = css_list_style;
                /* Map INHERIT (libcss may not have a concrete value
				 * for li, since UA defaults set list-style on the
				 * parent ul/ol) to a per-parent-tag default. */
                if (t == CSS_LIST_STYLE_TYPE_INHERIT) {
                    t = (ptag == LXB_TAG_OL) ? CSS_LIST_STYLE_TYPE_DECIMAL
                                              : CSS_LIST_STYLE_TYPE_DISC;
                }
                /* Even when libcss reports DISC, an <ol> parent
				 * should overrule that — DISC is the universal CSS
				 * UA default which doesn't know about <ol>. */
                if (t == CSS_LIST_STYLE_TYPE_DISC && ptag == LXB_TAG_OL) {
                    t = CSS_LIST_STYLE_TYPE_DECIMAL;
                }
                if (t == CSS_LIST_STYLE_TYPE_NONE) {
                    /* Author or UA explicitly said "no marker". */
                    mlen = 0;
                } else {
                    switch (t) {
                    case CSS_LIST_STYLE_TYPE_DISC: {
                        const char *s_ = "\xe2\x80\xa2"; /* U+2022 • */
                        memcpy(mbuf, s_, 3);
                        mlen = 3;
                        break;
                    }
                    case CSS_LIST_STYLE_TYPE_CIRCLE: {
                        const char *s_ = "\xe2\x97\xa6"; /* U+25E6 ◦ */
                        memcpy(mbuf, s_, 3);
                        mlen = 3;
                        break;
                    }
                    case CSS_LIST_STYLE_TYPE_SQUARE: {
                        const char *s_ = "\xe2\x96\xa0"; /* U+25A0 ▪ */
                        memcpy(mbuf, s_, 3);
                        mlen = 3;
                        break;
                    }
                    case CSS_LIST_STYLE_TYPE_DECIMAL:
                    case CSS_LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO: {
                        int w = snprintf(mbuf, sizeof(mbuf), "%d.", n);
                        if (w > 0) mlen = (size_t)w;
                        break;
                    }
                    case CSS_LIST_STYLE_TYPE_LOWER_ALPHA:
                    case CSS_LIST_STYLE_TYPE_LOWER_LATIN: {
                        char ch = (char)('a' + ((n - 1) % 26));
                        mbuf[0] = ch;
                        mbuf[1] = '.';
                        mlen = 2;
                        break;
                    }
                    case CSS_LIST_STYLE_TYPE_UPPER_ALPHA:
                    case CSS_LIST_STYLE_TYPE_UPPER_LATIN: {
                        char ch = (char)('A' + ((n - 1) % 26));
                        mbuf[0] = ch;
                        mbuf[1] = '.';
                        mlen = 2;
                        break;
                    }
                    default: {
                        /* Roman, greek, hebrew, CJK etc — fall back
						 * to decimal so the list at least reads. */
                        int w = snprintf(mbuf, sizeof(mbuf), "%d.", n);
                        if (w > 0) mlen = (size_t)w;
                        break;
                    }
                    }
                }
                /* Re-fetch the box pointer — earlier libcss calls in
				 * this block may have made allocations that grew the
				 * box vector and invalidated `b`. */
                b = &r->boxes.data[bidx];
                if (mlen > 0) {
                    b->marker_text = yetty_ylexbor_arena_dup(r, mbuf, mlen);
                    b->marker_text_len = mlen;
                }
            }
            /* A new block creates a fresh inline scope — text
			 * directly inside this block has no inline ancestor, so
			 * any link_element inherited from the parent's recursion
			 * must be cleared. Without this, "<a>x</a> <p>y</p>"
			 * would tag "y" inside the <p> with the <a>'s element. */
            s.link_element = NULL;
            walk(r, child, &s, bidx, &ib, depth + 1);
            flush_inline(r, &s, bidx, &ib);
            free(ib.buf);
            free(ib.segs);
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
			 * parent block's inline buffer. Track the deepest
			 * CLICKABLE inline ancestor on s.link_element so seg
			 * boundaries don't fire on every <span>/<bdi>/<cite>
			 * transition — only on entering / leaving an <a> /
			 * <button> / <input> / <area> / <label> / <summary>.
			 *
			 * Why this matters: P1.2 originally stamped every
			 * inline element on the seg. With Wikipedia's ~1000
			 * spans per article that produces ~1000 extra TEXT_SPAN
			 * prims, each emitted at a position computed from our
			 * naive per_glyph estimate. The canvas's real font
			 * advances differ from that estimate by a few px per
			 * glyph; multiplied across hundreds of fragments the
			 * visible cumulative drift LOOKS like scattered letters
			 * floating around words ("p, g, q garbage" complaint).
			 *
			 * Click hit-test still works because dispatch_click
			 * walks UP from the box's element — and the box's
			 * element is now the nearest clickable ancestor, which
			 * is what the user's click would have hit anyway.
			 * Inline non-clickable spans don't have listeners to
			 * fire on click; losing per-span granularity costs us
			 * nothing. */
            switch (child->local_name) {
            case LXB_TAG_A:
            case LXB_TAG_AREA:
            case LXB_TAG_BUTTON:
            case LXB_TAG_INPUT:
            case LXB_TAG_LABEL:
            case LXB_TAG_SELECT:
            case LXB_TAG_SUMMARY:
                s.link_element = el;
                break;
            default:
                /* Inherit whatever the parent's clickable ancestor
				 * was — don't overwrite with this non-clickable
				 * inline element. */
                break;
            }
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

    /* Free per-box heap (segs + bg_image_url) from the previous build.
	 * wrap_inline_box clears segs on consumption, but a relayout path
	 * that skips painting (or aborts mid-build) leaves them owned by
	 * the box, and bg_image_url is allocated fresh on every build —
	 * resetting size to 0 without this loop leaks both. */
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        if (r->boxes.data[i].segs) {
            free(r->boxes.data[i].segs);
            r->boxes.data[i].segs = NULL;
            r->boxes.data[i].segs_count = 0;
        }
        free(r->boxes.data[i].bg_image_url);
        r->boxes.data[i].bg_image_url = NULL;
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
        .underline = false,
        .line_through = false,
        .overline = false,
        .fg = root->fg,
        .text_align = 0,
        .link_element = NULL,
    };

    struct yl_inline_buf ib = {0};
    walk(r, lxb_dom_interface_node(r->document), &initial, root_idx, &ib, 0);
    flush_inline(r, &initial, root_idx, &ib);
    free(ib.buf);
    free(ib.segs);

    return YETTY_OK_VOID();
}
