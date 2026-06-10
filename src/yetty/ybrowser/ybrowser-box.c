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
    /* Inherited line-height. A unitless `line-height` inherits as the
     * factor (resolved against each element's own font-size); a length
     * inherits as a used px value. 0/0 = `normal` → the inline-wrap pass
     * falls back to font_size * 1.25. Carried in the style state — not
     * just on the source element's block box — so the anonymous inline-
     * text boxes that flush_inline emits under a block inherit it, which
     * is where line-height actually drives layout. */
    float line_height_px;
    float line_height_factor;
    int font_weight;
    bool font_italic;
    bool underline;    /* true inside <a> (and any other inline element
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
    /* A run keeps merging into the current segment only while every
	 * visible style attribute is unchanged. When any differs (entering an
	 * <a>/<strong>/<em>, a color change, …) a new segment opens so the
	 * fragment carries that element's own fg/weight/italic/underline +
	 * hit-target element. The host renders monospace and sets a matching
	 * glyph_advance_ratio, so fragment x-positions line up — which lets us
	 * keep per-element styling (link color/underline, bold/italic runs)
	 * without the drift that forced the earlier always-merge stub. */
    return seg->fg.r == style->fg.r && seg->fg.g == style->fg.g && seg->fg.b == style->fg.b &&
           seg->fg.a == style->fg.a && seg->font_weight == style->font_weight &&
           seg->font_italic == style->font_italic && seg->underline == style->underline &&
           seg->line_through == style->line_through && seg->overline == style->overline &&
           seg->element == style->link_element;
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

/* Parse one signed length token out of [s, end) — digits, sign, decimals.
 * Sets *out to the numeric value and *is_pct to whether it is followed by
 * '%'. Returns the cursor past the number (and unit), or `s` if no number. */
static const char *parse_transform_number(const char *s, const char *end, float *out,
                                          bool *is_pct)
{
    while (s < end && (*s == ' ' || *s == '\t')) {
        s++;
    }
    char buf[32];
    int n = 0;
    while (s < end && n < 31 &&
           (*s == '-' || *s == '+' || *s == '.' || (*s >= '0' && *s <= '9'))) {
        buf[n++] = *s++;
    }
    if (n == 0) {
        return s;
    }
    buf[n] = '\0';
    *out = (float)atof(buf);
    while (s < end && *s == ' ') {
        s++;
    }
    *is_pct = (s < end && *s == '%');
    return s;
}

/* Extract the `translate` component of a `transform` value from an inline
 * style string (NOT NUL-terminated; length in style_len). Handles
 * translate(x[,y]) / translateX(x) / translateY(y); scale/rotate/matrix are
 * ignored. Percent values are kept as the raw number (e.g. -50 for -50%) with
 * the matching *_pct flag, resolved against the box's own size at layout.
 * Returns true if a translate was found. */
static bool parse_transform_translate(const char *style, size_t style_len, float *out_tx,
                                      float *out_ty, bool *out_tx_pct, bool *out_ty_pct)
{
    *out_tx = 0.0f;
    *out_ty = 0.0f;
    *out_tx_pct = false;
    *out_ty_pct = false;
    if (!style || style_len < 10) {
        return false;
    }
    static const char needle[] = "translate";
    const size_t needle_len = sizeof(needle) - 1;
    for (size_t i = 0; i + needle_len < style_len; i++) {
        bool match = true;
        for (size_t k = 0; k < needle_len; k++) {
            if (tolower((unsigned char)style[i + k]) != needle[k]) {
                match = false;
                break;
            }
        }
        if (!match) {
            continue;
        }
        size_t j = i + needle_len;
        int axis = 0; /* 0 = both, 1 = X only, 2 = Y only */
        if (j < style_len && (style[j] == 'x' || style[j] == 'X')) {
            axis = 1;
            j++;
        } else if (j < style_len && (style[j] == 'y' || style[j] == 'Y')) {
            axis = 2;
            j++;
        } else if (j < style_len && (style[j] == 'z' || style[j] == 'Z' || style[j] == '3')) {
            continue; /* translateZ / translate3d — skip (no 2D layout effect we model) */
        }
        if (j >= style_len || style[j] != '(') {
            continue;
        }
        j++;
        const char *p = style + j;
        const char *end = style + style_len;
        const char *close = memchr(p, ')', (size_t)(end - p));
        if (!close) {
            return false;
        }
        const char *comma = memchr(p, ',', (size_t)(close - p));
        const char *first_end = comma ? comma : close;
        float v1 = 0.0f, v2 = 0.0f;
        bool p1 = false, p2 = false;
        if (parse_transform_number(p, first_end, &v1, &p1) == p) {
            return false; /* no number present */
        }
        if (comma) {
            (void)parse_transform_number(comma + 1, close, &v2, &p2);
        }
        if (axis == 1) {
            *out_tx = v1;
            *out_tx_pct = p1;
        } else if (axis == 2) {
            *out_ty = v1;
            *out_ty_pct = p1;
        } else {
            *out_tx = v1;
            *out_tx_pct = p1;
            *out_ty = v2;
            *out_ty_pct = p2;
        }
        return true;
    }
    return false;
}

static struct yetty_ycore_void_result box_alloc(struct yetty_ylexbor *r, uint32_t *out_idx)
{
    struct yetty_ycore_void_result rr =
        _yetty_ylexbor_box_vec_reserve(&r->boxes, r->boxes.size + 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "box_alloc: reserve");
    struct yetty_ylexbor_box *b = &r->boxes.data[r->boxes.size];
    memset(b, 0, sizeof(*b));
    *out_idx = r->boxes.size++;
    return YETTY_OK_VOID();
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
    /* Resolve the inherited line-height against THIS element's font-size:
     * a length used value wins outright, else a unitless factor scales by
     * the element's own font, else 0 (normal) leaves the wrap default. */
    if (s->line_height_px > 0.0f) {
        b->line_height = s->line_height_px;
    } else if (s->line_height_factor > 0.0f) {
        b->line_height = s->line_height_factor * s->font_size;
    } else {
        b->line_height = 0.0f;
    }
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

static struct yetty_ycore_void_result walk(struct yetty_ylexbor *r, lxb_dom_node_t *node,
                                           const struct yl_style_state *parent_style,
                                           uint32_t parent_idx,
                                           struct yl_inline_buf *inline_collect, int depth);

/* Flush an accumulated inline-text run as one or more YL_BOX_INLINE_TEXT
 * children of `parent_idx`. Layout will wrap it into lines; box-build
 * stores the un-wrapped string. */
static struct yetty_ycore_void_result flush_inline(struct yetty_ylexbor *r,
                                                   const struct yl_style_state *style,
                                                   uint32_t parent_idx, struct yl_inline_buf *coll)
{
    if (coll->buf == NULL || coll->len == 0) {
        return YETTY_OK_VOID();
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
        return YETTY_OK_VOID();
    }

    uint32_t cidx;
    struct yetty_ycore_void_result alloc_res = box_alloc(r, &cidx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, alloc_res, "flush_inline: box_alloc");
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
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result walk(struct yetty_ylexbor *r, lxb_dom_node_t *node,
                                           const struct yl_style_state *parent_style,
                                           uint32_t parent_idx,
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
		 *   (a) display:none — skip the subtree. UA/author display:none
		 *       (e.g. `[hidden]`) only surfaces through libcss, not the
		 *       lexbor serialized inline style.
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
            const lxb_char_t *pre_istyle =
                lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &pre_istylen);
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
            struct yetty_ycore_void_result flush_res =
                flush_inline(r, parent_style, parent_idx, inline_collect);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "walk: flush before block");

            uint32_t bidx;
            struct yetty_ycore_void_result alloc_res = box_alloc(r, &bidx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, alloc_res, "walk: box_alloc block");
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
                css_computed_style *cs =
                    yetty_ybrowser_libcss_select(r, el, (const char *)istyle, istyle ? istylen : 0);
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
                    bool pct;

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
                    b->border_box = yetty_ybrowser_libcss_box_sizing(cs) != 0;
                    float lh_px = 0.0f;
                    float lh_factor = 0.0f;
                    if (yetty_ybrowser_libcss_line_height(r, cs, s.font_size, &lh_px, &lh_factor)) {
                        /* Update the inherited line-height; a concrete
                         * value replaces whichever form was inherited.
                         * Propagating into `s` lets the inline-text boxes
                         * flushed under this block inherit it. */
                        s.line_height_px = lh_px;
                        s.line_height_factor = lh_factor;
                        b->line_height = lh_px > 0.0f ? lh_px : lh_factor * s.font_size;
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

                    float *margin_dst[4] = {&b->margin_top, &b->margin_right, &b->margin_bottom,
                                            &b->margin_left};
                    float *padding_dst[4] = {&b->padding_top, &b->padding_right, &b->padding_bottom,
                                             &b->padding_left};
                    float *border_dst[4] = {&b->border_top, &b->border_right, &b->border_bottom,
                                            &b->border_left};
                    for (int side = 0; side < 4; side++) {
                        ma = false;
                        pct = false;
                        if (yetty_ybrowser_libcss_margin(r, cs, side, s.font_size, pct_basis, &px,
                                                         &ma, &pct)) {
                            if (ma) {
                                if (side == 1) {
                                    b->margin_right_auto = true;
                                } else if (side == 3) {
                                    b->margin_left_auto = true;
                                }
                            } else {
                                /* On a percent value `px` holds the ratio;
                                 * the layout pass resolves it against the
                                 * parent content width and clears the bit. */
                                *margin_dst[side] = px;
                                if (pct) {
                                    b->pct_mask |= (uint8_t)(YL_PCT_MARGIN_TOP << side);
                                }
                            }
                        }
                        pct = false;
                        if (yetty_ybrowser_libcss_padding(r, cs, side, s.font_size, pct_basis, &px,
                                                          &pct)) {
                            *padding_dst[side] = px;
                            if (pct) {
                                b->pct_mask |= (uint8_t)(YL_PCT_PADDING_TOP << side);
                            }
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
                        /* Flex `gap` (stored in grid_col_gap — a box is flex OR
						 * grid, never both). libcss in this tree models only the
						 * legacy multicol column-gap, so read the modern `gap`/
						 * `column-gap` shorthand from the inline style. */
                        {
                            size_t gs_len = 0;
                            const lxb_char_t *gs = lxb_dom_element_get_attribute(
                                el, (const lxb_char_t *)"style", 5, &gs_len);
                            if (gs && gs_len > 0) {
                                float gap =
                                    yetty_ylexbor_css_inline_gap((const char *)gs, gs_len);
                                if (gap > 0.0f) {
                                    b->grid_col_gap = gap;
                                }
                            }
                        }
                    } else if (disp == CSS_DISPLAY_TABLE || disp == CSS_DISPLAY_INLINE_TABLE) {
                        b->layout_mode = YL_LAYOUT_TABLE;
                    } else if (disp == CSS_DISPLAY_GRID || disp == CSS_DISPLAY_INLINE_GRID) {
                        /* No real grid track layout. The dominant visual
						 * effect of the modern content-column grid idiom
						 * (grid-template-columns: ... minmax(0, Nrem) ...)
						 * is that the content column is capped to N. We
						 * scanned that cap out of the author CSS; apply it
						 * as a max-width so content lays out at a readable
						 * width instead of filling the whole container.
						 * Stays block flow otherwise; only set when the
						 * element doesn't already carry an explicit
						 * max-width (css_max_width == 0 means unset; a
						 * negative value is a percent the cascade gave). */
                        b->layout_mode = YL_LAYOUT_BLOCK;
                        if (r->grid_content_max_px > 0.0f && b->css_max_width == 0.0f) {
                            b->css_max_width = r->grid_content_max_px;
                        }
                        /* If we parsed an explicit small-track grid template
						 * for this element's class, lay it out as a real grid.
						 * Restricted to 2–4 tracks: that covers the dominant
						 * story-card idiom (text + thumbnail) without needing
						 * grid-column spans, which the many-column page grids
						 * (repeat(12,…)) rely on — those safely stay block. */
                        {
                            size_t cls_len = 0;
                            const lxb_char_t *cls_attr = lxb_dom_element_get_attribute(
                                el, (const lxb_char_t *)"class", 5, &cls_len);
                            if (cls_attr && cls_len > 0) {
                                const struct yl_grid_class *gt = yetty_ylexbor_grid_class_lookup(
                                    r, (const char *)cls_attr, cls_len);
                                if (gt && gt->ntracks >= 2 && gt->ntracks <= 4) {
                                    b->layout_mode = YL_LAYOUT_GRID;
                                    memcpy(b->grid_tracks, gt->tracks, sizeof(b->grid_tracks));
                                    b->grid_ntracks = gt->ntracks;
                                    b->grid_col_gap = gt->col_gap;
                                    b->grid_row_gap = gt->row_gap;
                                }
                            }
                        }
                        /* Grid declared directly on the element's inline style
						 * (the class scanner only sees stylesheet rules). */
                        if (b->layout_mode != YL_LAYOUT_GRID) {
                            size_t istyle_len = 0;
                            const lxb_char_t *istyle = lxb_dom_element_get_attribute(
                                el, (const lxb_char_t *)"style", 5, &istyle_len);
                            if (istyle && istyle_len > 0) {
                                struct yl_grid_track tracks[YL_GRID_MAX_TRACKS] = {0};
                                float col_gap = 0.0f, row_gap = 0.0f;
                                int ntracks = yetty_ylexbor_grid_parse_inline(
                                    (const char *)istyle, istyle_len, tracks, YL_GRID_MAX_TRACKS,
                                    &col_gap, &row_gap);
                                if (ntracks >= 2 && ntracks <= 4) {
                                    b->layout_mode = YL_LAYOUT_GRID;
                                    memcpy(b->grid_tracks, tracks, sizeof(b->grid_tracks));
                                    b->grid_ntracks = (uint8_t)ntracks;
                                    b->grid_col_gap = col_gap;
                                    b->grid_row_gap = row_gap;
                                }
                            }
                        }
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
                    /* Float side from the cascade. The layout pass pulls
				 * floated blocks out of normal flow and narrows the
				 * in-flow content rectangle (avail_x / avail_w) at each
				 * y, and the inline-wrap pass wraps text into that
				 * narrowed rectangle — so floated figures sit beside the
				 * body text instead of overlapping it. This is what puts
				 * Wikipedia's infobox and thumbnails on the right. */
                    int flt = yetty_ybrowser_libcss_float(cs);
                    if (flt == CSS_FLOAT_LEFT) {
                        b->float_side = 1;
                    } else if (flt == CSS_FLOAT_RIGHT) {
                        b->float_side = 2;
                    } else {
                        b->float_side = 0;
                    }
                    int clr = yetty_ybrowser_libcss_clear(cs);
                    if (clr == CSS_CLEAR_LEFT) {
                        b->clear_side = 1;
                    } else if (clr == CSS_CLEAR_RIGHT) {
                        b->clear_side = 2;
                    } else if (clr == CSS_CLEAR_BOTH) {
                        b->clear_side = 3;
                    }

                    /* `position` + insets. RELATIVE shifts the box (and its
					 * subtree) by the insets after normal-flow placement;
					 * ABSOLUTE/FIXED pull it out of flow and place it against
					 * a containing block. STICKY collapses to RELATIVE. */
                    int pos = yetty_ybrowser_libcss_position(cs);
                    if (pos == CSS_POSITION_RELATIVE || pos == CSS_POSITION_STICKY) {
                        b->position = YL_POS_RELATIVE;
                    } else if (pos == CSS_POSITION_ABSOLUTE) {
                        b->position = YL_POS_ABSOLUTE;
                    } else if (pos == CSS_POSITION_FIXED) {
                        b->position = YL_POS_FIXED;
                    } else {
                        b->position = YL_POS_STATIC;
                    }
                    if (b->position != YL_POS_STATIC) {
                        float inset = 0.0f;
                        float *inset_field[4] = {&b->pos_top, &b->pos_right, &b->pos_bottom,
                                                 &b->pos_left};
                        for (int side = 0; side < 4; side++) {
                            int got = yetty_ybrowser_libcss_inset(r, cs, side, s.font_size, pct_basis,
                                                                  &inset);
                            if (got) {
                                *inset_field[side] = inset;
                                b->pos_set_mask |= (uint8_t)(1u << side);
                                if (got == 2) {
                                    b->pos_pct_mask |= (uint8_t)(1u << side);
                                }
                            }
                        }
                    }

                    /* `transform: translate(...)` from the inline style.
					 * libcss doesn't expose `transform`, and JS-driven sites
					 * (e.g. Google News) position cards by writing inline
					 * translate offsets — so parse it off the raw style here. */
                    if (istyle && istylen > 0) {
                        float txv = 0.0f, tyv = 0.0f;
                        bool txp = false, typ = false;
                        if (parse_transform_translate((const char *)istyle, istylen, &txv, &tyv,
                                                      &txp, &typ)) {
                            b->has_transform = true;
                            b->tf_tx = txv;
                            b->tf_ty = tyv;
                            b->tf_tx_pct = txp;
                            b->tf_ty_pct = typ;
                        }
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
            int preserve_ws =
                (child->local_name == LXB_TAG_PRE || child->local_name == LXB_TAG_TEXTAREA);
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
                        if (w > 0) {
                            mlen = (size_t)w;
                        }
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
                        if (w > 0) {
                            mlen = (size_t)w;
                        }
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
            struct yetty_ycore_void_result walk_res = walk(r, child, &s, bidx, &ib, depth + 1);
            if (YETTY_IS_ERR(walk_res)) {
                free(ib.buf);
                free(ib.segs);
                return YETTY_ERR(yetty_ycore_void, "walk: recurse block child", walk_res);
            }
            struct yetty_ycore_void_result flush_child_res = flush_inline(r, &s, bidx, &ib);
            if (YETTY_IS_ERR(flush_child_res)) {
                free(ib.buf);
                free(ib.segs);
                return YETTY_ERR(yetty_ycore_void, "walk: flush block child", flush_child_res);
            }
            free(ib.buf);
            free(ib.segs);
        } else {
            /* <img>: pre-decode (or hit the cache) so the box's
			 * geometry reflects natural pixel dimensions, with
			 * `width` / `height` HTML attributes overriding when
			 * present. The placeholder fallback (grey box) kicks
			 * in if the fetch or decode failed. */
            if (child->local_name == LXB_TAG_IMG) {
                struct yetty_ycore_void_result flush_res =
                    flush_inline(r, parent_style, parent_idx, inline_collect);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "walk: flush before img");
                uint32_t iidx;
                struct yetty_ycore_void_result alloc_res = box_alloc(r, &iidx);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, alloc_res, "walk: box_alloc img");
                struct yetty_ylexbor_box *ib = &r->boxes.data[iidx];
                ib->kind = YL_BOX_INLINE_IMAGE;
                ib->element = el;

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

                /* Resolve src/srcset/data-* and pre-decode for natural
				 * dimensions ONLY when at least one of the HTML
				 * size attrs is missing. With both attrs present
				 * (the Wikipedia/MediaWiki common case), layout
				 * already has everything it needs — defer the
				 * fetch to ybrowser-paint's parallel prefetch
				 * pass, which fires all images in one curl_multi
				 * batch instead of 30+ blocking easy-handle
				 * calls in document order. */
                struct yetty_ylexbor_img_cache_entry *cached = NULL;
                if (attr_w <= 0 || attr_h <= 0) {
                    char *abs = yetty_ylexbor_img_pick_url(r, el);
                    if (abs) {
                        cached = yetty_ylexbor_img_cache_get_or_load(r, abs);
                        free(abs);
                    }
                }

                int nat_w = (cached && !cached->failed) ? cached->w : 0;
                int nat_h = (cached && !cached->failed) ? cached->h : 0;

                /* CSS `width`/`height` on the image. These WIN over the HTML
				 * presentation-hint attrs and the natural size — the common
				 * real-world case (e.g. Google News favicons: no attrs,
				 * `width:14px` in CSS, but a 96x96 natural image). Without
				 * this the favicon rendered at 96x96 and buried the card.
				 * Only definite lengths are honoured here; the bridge returns
				 * a negative ratio for percentages, which we leave to the
				 * attr/natural fallback (percent needs the containing block). */
                float css_img_w = 0.0f, css_img_h = 0.0f;
                {
                    size_t img_istylen = 0;
                    const lxb_char_t *img_istyle = lxb_dom_element_get_attribute(
                        el, (const lxb_char_t *)"style", 5, &img_istylen);
                    css_computed_style *img_cs = yetty_ybrowser_libcss_select(
                        r, el, (const char *)img_istyle, img_istyle ? img_istylen : 0);
                    /* box vector may have moved during select. */
                    ib = &r->boxes.data[iidx];
                    if (img_cs) {
                        float px = 0.0f;
                        float basis = s.font_size * 16.0f;
                        if (yetty_ybrowser_libcss_width(r, img_cs, s.font_size, basis, &px) &&
                            px > 0.0f) {
                            css_img_w = px;
                        }
                        if (yetty_ybrowser_libcss_height(r, img_cs, s.font_size, basis, &px) &&
                            px > 0.0f) {
                            css_img_h = px;
                        }
                        yetty_ybrowser_libcss_release(img_cs);
                        ib = &r->boxes.data[iidx];
                    }
                }

                /* Resolve the box's pixel size. Priority per axis: CSS length,
				 * then the HTML presentation-hint attr, then the decoded
				 * natural size. Crucially, when exactly ONE axis is given
				 * explicitly the other is `auto` and must be derived from the
				 * natural ASPECT RATIO — not left at the natural pixel size.
				 * (Google News sizes its favicons with height:14 only; the
				 * natural image is 96x96, so the width must scale to 14, not
				 * stay at 96 and stretch the logo ~7x wide.) */
                float exp_w = css_img_w > 0.0f ? css_img_w : (attr_w > 0 ? (float)attr_w : 0.0f);
                float exp_h = css_img_h > 0.0f ? css_img_h : (attr_h > 0 ? (float)attr_h : 0.0f);
                if (exp_w > 0.0f && exp_h > 0.0f) {
                    ib->w = exp_w;
                    ib->h = exp_h;
                } else if (exp_w > 0.0f) {
                    ib->w = exp_w;
                    ib->h = (nat_w > 0 && nat_h > 0) ? exp_w * (float)nat_h / (float)nat_w
                            : (nat_h > 0)            ? (float)nat_h
                                                     : exp_w;
                } else if (exp_h > 0.0f) {
                    ib->h = exp_h;
                    ib->w = (nat_w > 0 && nat_h > 0) ? exp_h * (float)nat_w / (float)nat_h
                            : (nat_w > 0)            ? (float)nat_w
                                                     : exp_h;
                } else {
                    if (nat_w > 0) {
                        ib->w = (float)nat_w;
                    }
                    if (nat_h > 0) {
                        ib->h = (float)nat_h;
                    }
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
			 * spans per article that produces ~1000 extra TEXT_DRAWABLE_LIST
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
            /* Author CSS on an inline element must override the tag-default
			 * text style — otherwise every <a> paints the UA blue + underline
			 * even when the site sets e.g. a dark, un-underlined headline link
			 * (`.gPFEn{color:#1f1f1f}`, `text-decoration:none`). The block
			 * branch already does this; mirror it here so inline runs inherit
			 * the right color/weight/decoration before we recurse. */
            if (r->libcss) {
                size_t inl_istylen = 0;
                const lxb_char_t *inl_istyle =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &inl_istylen);
                css_computed_style *inl_cs = yetty_ybrowser_libcss_select(
                    r, el, (const char *)inl_istyle, inl_istyle ? inl_istylen : 0);
                if (inl_cs) {
                    struct yetty_ylexbor_color inl_color;
                    int inl_weight;
                    bool inl_italic;
                    float inl_px;
                    if (yetty_ybrowser_libcss_color(inl_cs, &inl_color)) {
                        s.fg = inl_color;
                    }
                    if (yetty_ybrowser_libcss_font_weight(inl_cs, &inl_weight)) {
                        s.font_weight = inl_weight;
                    }
                    if (yetty_ybrowser_libcss_font_italic(inl_cs, &inl_italic)) {
                        s.font_italic = inl_italic;
                    }
                    if (yetty_ybrowser_libcss_font_size(
                            r, inl_cs,
                            parent_style ? parent_style->font_size : r->default_font_size,
                            &inl_px)) {
                        s.font_size = inl_px;
                    }
                    unsigned inl_dec = (unsigned)yetty_ybrowser_libcss_text_decoration(inl_cs);
                    if (inl_dec & CSS_TEXT_DECORATION_NONE) {
                        s.underline = false;
                        s.line_through = false;
                        s.overline = false;
                    } else {
                        if (inl_dec & CSS_TEXT_DECORATION_UNDERLINE) {
                            s.underline = true;
                        }
                        if (inl_dec & CSS_TEXT_DECORATION_LINE_THROUGH) {
                            s.line_through = true;
                        }
                        if (inl_dec & CSS_TEXT_DECORATION_OVERLINE) {
                            s.overline = true;
                        }
                    }
                    yetty_ybrowser_libcss_release(inl_cs);
                }
            }

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
            struct yetty_ycore_void_result walk_res =
                walk(r, child, &s, parent_idx, inline_collect, depth);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, walk_res, "walk: recurse inline child");
        }
    }
    return YETTY_OK_VOID();
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
    struct yetty_ycore_void_result root_alloc_res = box_alloc(r, &root_idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, root_alloc_res, "ylexbor_box_build: alloc root");
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
    struct yetty_ycore_void_result walk_res =
        walk(r, lxb_dom_interface_node(r->document), &initial, root_idx, &ib, 0);
    if (YETTY_IS_ERR(walk_res)) {
        free(ib.buf);
        free(ib.segs);
        return YETTY_ERR(yetty_ycore_void, "ylexbor_box_build: walk", walk_res);
    }
    struct yetty_ycore_void_result flush_res = flush_inline(r, &initial, root_idx, &ib);
    if (YETTY_IS_ERR(flush_res)) {
        free(ib.buf);
        free(ib.segs);
        return YETTY_ERR(yetty_ycore_void, "ylexbor_box_build: flush", flush_res);
    }
    free(ib.buf);
    free(ib.segs);

    return YETTY_OK_VOID();
}
