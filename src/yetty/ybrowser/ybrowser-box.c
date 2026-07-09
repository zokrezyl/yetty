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

/* Match lexbor tag IDs; values pulled from <lexbor/tag/const.h>. */
static const struct yl_default_style *default_for(lxb_tag_id_t tag)
{
    static const struct yl_default_style YL_DEFAULT_INLINE = {
        YL_DISP_INLINE, 1.0f, 0, -1, 0.0f, 0.0f, INHERIT_RGB, 0,
    };
    static const struct yl_default_style YL_DEFAULT_BLOCK = {
        YL_DISP_BLOCK, 1.0f, 0, -1, 1.0f, 1.0f, INHERIT_RGB, 0,
    };
    static const struct yl_default_style h1 = {YL_DISP_BLOCK, 2.00f, 700,        -1,
                                               0.67f,         0.67f, INHERIT_RGB};
    static const struct yl_default_style h2 = {YL_DISP_BLOCK, 1.50f, 700,        -1,
                                               0.83f,         0.83f, INHERIT_RGB};
    static const struct yl_default_style h3 = {YL_DISP_BLOCK, 1.17f, 700,        -1,
                                               1.00f,         1.00f, INHERIT_RGB};
    static const struct yl_default_style h4 = {YL_DISP_BLOCK, 1.00f, 700,        -1,
                                               1.33f,         1.33f, INHERIT_RGB};
    static const struct yl_default_style h5 = {YL_DISP_BLOCK, 0.83f, 700,        -1,
                                               1.67f,         1.67f, INHERIT_RGB};
    static const struct yl_default_style h6 = {YL_DISP_BLOCK, 0.67f, 700,        -1,
                                               2.33f,         2.33f, INHERIT_RGB};

    static const struct yl_default_style strong = {YL_DISP_INLINE, 1.0f, 700, -1, 0, 0,
                                                   INHERIT_RGB,    0};
    static const struct yl_default_style em = {YL_DISP_INLINE, 1.0f, 0, 1, 0, 0, INHERIT_RGB, 0};
    static const struct yl_default_style anchor = {YL_DISP_INLINE, 1.0f, 0, -1, 0, 0, 0x0000eeu, 1};
    static const struct yl_default_style small_e = {YL_DISP_INLINE, 0.83f, 0, -1, 0, 0,
                                                    INHERIT_RGB,    0};
    static const struct yl_default_style code_e = {YL_DISP_INLINE, 1.0f, 0, -1, 0, 0, 0x444444u, 0};
    static const struct yl_default_style none = {YL_DISP_NONE, 1.0f, 0, -1, 0, 0, INHERIT_RGB, 0};

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
     * its math markup). We hide the subtrees outright until we render
     * them properly. <svg> is NOT here: it gets a replaced box (sized
     * from CSS/attrs/viewBox, subtree never walked) so icon/logo
     * layout reserves the right space. */
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
    float glyph_advance; /* per-font advance override (1.0 for Ahem); 0 = global.
	                      * Inherited so flushed inline-text boxes pick it up. */
    bool underline;      /* true inside <a> (and any other inline element
	                  * whose default_for() flags underline=1) */
    bool line_through;   /* `text-decoration: line-through` from CSS */
    bool overline;       /* `text-decoration: overline` from CSS */
    bool nowrap;         /* `white-space: nowrap` (inherited) — text runs
	                  * under this style never soft-wrap */
    struct yetty_ylexbor_color fg;
    int text_align;  /* inherited; 0=left, 1=center, 2=right, 3=justify */
    bool vis_hidden; /* visibility:hidden|collapse — inherited, so the
	                  * anonymous inline-text boxes flush_inline emits under
	                  * a hidden subtree pick it up and paint nothing. A
	                  * visibility:visible descendant re-shows via the cascade. */
    float opacity;   /* effective opacity [0,1] = ancestors' × own. Threaded by
	                  * value so a child inherits the folded-down product; each
	                  * element multiplies its own on top. Copied to every box
	                  * (incl. anonymous inline text) so a near-zero subtree
	                  * paints nothing. */
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
    /* Set when any text contributing to the run was under
	 * `white-space: nowrap` — the flushed box then never soft-wraps
	 * (a whole-run approximation; per-segment nowrap isn't modelled). */
    int any_nowrap;
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

/* Resolve a grid-column span from an element's class list for responsive
 * design-system grids that encode the span IN the class name — Primer Brand
 * (github) is the dominant case: `Grid__column--medium-span-7`,
 * `Grid__column--large-span-12`, or a bare `...span-6...`. The breakpoint
 * prefix (xsmall/small/medium/large/xlarge) gates which span applies at a given
 * viewport width; the largest breakpoint whose min-width is <= `viewport_w`
 * wins (CSS source-order / cascade behaviour for these mobile-first systems).
 * Returns the effective span (1..maxn), or 0 if the class list encodes none. */
static int grid_span_from_classes(const char *cls, size_t cls_len, int viewport_w, int maxn)
{
    if (cls == NULL || cls_len == 0) {
        return 0;
    }
    /* Mobile-first breakpoint min-widths (Primer Brand). A token with no
	 * breakpoint prefix is the base (min-width 0). */
    static const struct {
        const char *name;
        int min_width;
    } breakpoints[] = {
        {"xsmall", 0}, {"small", 544}, {"medium", 768}, {"large", 1012}, {"xlarge", 1280},
    };
    int best_span = 0;
    int best_min = -1;
    /* Walk every `span-<N>` occurrence; classify its breakpoint by the token
	 * immediately preceding `span-` (`--<bp>-span-N`), default base. */
    for (size_t i = 0; i + 5 <= cls_len; i++) {
        if (strncmp(cls + i, "span-", 5) != 0) {
            continue;
        }
        int span = atoi(cls + i + 5);
        if (span < 1 || span > maxn) {
            continue;
        }
        int min_width = 0; /* base / no prefix */
        if (i >= 4 && strncmp(cls + i - 4, "col-", 4) == 0) {
            /* Tailwind `<bp>:col-span-N` — the responsive prefix sits before
			 * `col-`; an unprefixed `col-span-N` is the base. Tailwind's default
			 * breakpoint min-widths: sm 640, md 768, lg 1024, xl 1280, 2xl 1536. */
            if (i >= 7 && strncmp(cls + i - 7, "sm:col-", 7) == 0) {
                min_width = 640;
            } else if (i >= 7 && strncmp(cls + i - 7, "md:col-", 7) == 0) {
                min_width = 768;
            } else if (i >= 7 && strncmp(cls + i - 7, "lg:col-", 7) == 0) {
                min_width = 1024;
            } else if (i >= 7 && strncmp(cls + i - 7, "xl:col-", 7) == 0) {
                min_width = 1280;
            } else if (i >= 8 && strncmp(cls + i - 8, "2xl:col-", 8) == 0) {
                min_width = 1536;
            }
        } else {
            /* Primer Brand `--<bp>-span-N` (github). */
            for (size_t b = 0; b < sizeof(breakpoints) / sizeof(breakpoints[0]); b++) {
                size_t nlen = strlen(breakpoints[b].name);
                /* Match `<bp>-span-` ending exactly at `i` (i.e. `<bp>-` precedes). */
                if (i >= nlen + 1 && strncmp(cls + i - nlen - 1, breakpoints[b].name, nlen) == 0 &&
                    cls[i - 1] == '-') {
                    min_width = breakpoints[b].min_width;
                    break;
                }
            }
        }
        if (min_width <= viewport_w && min_width > best_min) {
            best_min = min_width;
            best_span = span;
        }
    }
    return best_span;
}

/* Force a hard line break (`<br>`) into the inline buffer. A plain
 * inline_buf_append("\n") would be collapsed to a single space under the
 * default white-space handling; the wrap pass only breaks a line on an
 * explicit '\n', so push the byte verbatim. Marks last_was_space so a
 * collapsible space immediately after the break is dropped (CSS collapses
 * whitespace around a forced break). */
static int inline_buf_force_break(struct yl_inline_buf *b)
{
    if (b->len + 2 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 256;
        while (nc < b->len + 2) {
            nc *= 2;
        }
        char *p = realloc(b->buf, nc);
        if (p == NULL) {
            return -1;
        }
        b->buf = p;
        b->cap = nc;
    }
    b->buf[b->len++] = '\n';
    b->last_was_space = 1;
    return 0;
}

/* ===========================================================================
 * DOM walker — recursive
 * ===========================================================================*/

/* Parse one signed length token out of [s, end) — digits, sign, decimals.
 * Sets *out to the numeric value and *is_pct to whether it is followed by
 * '%'. Returns the cursor past the number (and unit), or `s` if no number. */
static const char *parse_transform_number(const char *s, const char *end, float *out, bool *is_pct)
{
    while (s < end && (*s == ' ' || *s == '\t')) {
        s++;
    }
    char buf[32];
    int n = 0;
    while (s < end && n < 31 && (*s == '-' || *s == '+' || *s == '.' || (*s >= '0' && *s <= '9'))) {
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
    b->opacity = 1.0f; /* opaque by default; style_to_box/element cascade override */
    *out_idx = r->boxes.size++;
    return YETTY_OK_VOID();
}

/* Append `cidx` as the last child of `parent_idx`. Walks the existing
 * sibling chain to find the tail. O(n) per append in the parent's
 * direct children — fine for the tree sizes we deal with. */
static void link_child(struct yetty_ylexbor *r, uint32_t parent_idx, uint32_t cidx)
{
    struct yetty_ylexbor_box *p = &r->boxes.data[parent_idx];
    r->boxes.data[cidx].parent = parent_idx;
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

bool yetty_ylexbor_box_clipped_out(const struct yetty_ylexbor *r, uint32_t idx)
{
    if (r == NULL || idx >= r->boxes.size) {
        return false;
    }
    const struct yetty_ylexbor_box *b = &r->boxes.data[idx];
    /* Walk owning ancestors up to the root (index 0). A box is dropped when it
	 * lies wholly beyond the padding box of an overflow-clipping ancestor — how
	 * a carousel/tab-panel hides the slide it has translated off its viewport
	 * (Google News weather widget: the forecast is pushed +208px past a 165px
	 * overflow:hidden strip). We have no per-pixel scissor, so a box merely
	 * straddling the edge is kept, not partially clipped. Parent indices are
	 * strictly smaller (tree-order allocation), so the walk always terminates. */
    uint32_t cur = b->parent;
    while (true) {
        const struct yetty_ylexbor_box *anc = &r->boxes.data[cur];
        if (cur != idx && anc->clip_overflow && anc->w > 0.0f && anc->h > 0.0f) {
            const float slack = 0.5f;
            if (b->x >= anc->x + anc->w - slack || b->x + b->w <= anc->x + slack ||
                b->y >= anc->y + anc->h - slack || b->y + b->h <= anc->y + slack) {
                return true;
            }
        }
        if (cur == 0) {
            break;
        }
        cur = anc->parent;
    }
    return false;
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
    b->glyph_advance = s->glyph_advance;
    b->fg = s->fg;
    b->vis_hidden = s->vis_hidden;
    b->opacity = s->opacity;
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

/* Parse one CSS <length> token (px/em/rem, or a bare 0) into pixels.
 * `font_size` scales em/rem. Returns true on success. Percent and other
 * units are rejected (the flex-basis percent path stays on the libcss
 * value). */
static bool flex_parse_len_px(const char *tok, size_t len, float font_size, float *out_px)
{
    if (len == 0) {
        return false;
    }
    char *endp = NULL;
    char buf[32];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, tok, n);
    buf[n] = '\0';
    float value = strtof(buf, &endp);
    if (endp == buf) {
        return false;
    }
    while (*endp == ' ' || *endp == '\t') {
        endp++;
    }
    if (*endp == '\0' || strncasecmp(endp, "px", 2) == 0) {
        *out_px = value;
        return true;
    }
    if (strncasecmp(endp, "rem", 3) == 0 || strncasecmp(endp, "em", 2) == 0) {
        *out_px = value * font_size;
        return true;
    }
    return false;
}

/* Parse the inline `flex` shorthand (e.g. `flex:0 0 150px`, `flex:1`,
 * `flex:none`, `flex:auto`) from a style attribute. libcss in this tree does
 * not expand the shorthand into flex-grow/flex-basis longhands, so authored
 * inline `flex:` was silently ignored — items fell back to the auto-basis
 * even-split and ignored their requested size. On a hit, writes grow + basis
 * (basis encoding shared with css_width: >0 px, 0 = auto/0, the caller decides)
 * and returns true; `*out_basis_auto` distinguishes `flex:none/auto` (basis
 * keeps the item's own width) from a definite `0` basis. */
static bool parse_inline_flex(const lxb_char_t *style, size_t slen, float font_size,
                              float *out_grow, float *out_basis_px, bool *out_basis_auto)
{
    size_t vlen = 0;
    const char *value = find_inline_decl(style, slen, "flex", 4, &vlen);
    if (value == NULL) {
        return false;
    }
    /* Single-keyword forms. */
    if (vlen == 4 && strncasecmp(value, "none", 4) == 0) {
        *out_grow = 0.0f;
        *out_basis_px = 0.0f;
        *out_basis_auto = true; /* keep the item's own width/content size */
        return true;
    }
    if (vlen == 4 && strncasecmp(value, "auto", 4) == 0) {
        *out_grow = 1.0f;
        *out_basis_px = 0.0f;
        *out_basis_auto = true;
        return true;
    }
    /* Tokenize on whitespace. A unitless number is grow (1st) / shrink (2nd,
     * ignored — we don't model shrink ratios); a length/`auto` is the basis. */
    float grow = 0.0f;
    bool grow_seen = false;
    float basis_px = 0.0f;
    bool basis_auto = true; /* CSS default flex basis for a bare number is 0,
                             * but with no explicit length we let the item's
                             * width drive — overridden below on a length hit */
    bool basis_set = false;
    int unitless_seen = 0;
    size_t i = 0;
    while (i < vlen) {
        while (i < vlen && (value[i] == ' ' || value[i] == '\t')) {
            i++;
        }
        size_t t0 = i;
        while (i < vlen && value[i] != ' ' && value[i] != '\t') {
            i++;
        }
        size_t tlen = i - t0;
        if (tlen == 0) {
            break;
        }
        const char *tok = value + t0;
        if (tlen == 4 && strncasecmp(tok, "auto", 4) == 0) {
            basis_auto = true;
            basis_set = true;
            continue;
        }
        float px;
        bool is_len = flex_parse_len_px(tok, tlen, font_size, &px);
        /* A token with a unit (or that didn't fully consume as a bare number)
         * is the basis; a bare unitless number is grow/shrink. */
        bool has_unit = false;
        for (size_t k = 0; k < tlen; k++) {
            char ch = tok[k];
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '%') {
                has_unit = true;
                break;
            }
        }
        if (has_unit) {
            if (is_len) {
                basis_px = px;
                basis_auto = false;
                basis_set = true;
            }
        } else {
            char nbuf[32];
            size_t nn = tlen < sizeof(nbuf) - 1 ? tlen : sizeof(nbuf) - 1;
            memcpy(nbuf, tok, nn);
            nbuf[nn] = '\0';
            float num = strtof(nbuf, NULL);
            if (unitless_seen == 0) {
                grow = num;
                grow_seen = true;
            }
            /* second unitless = shrink (not modelled); ignore */
            unitless_seen++;
            /* A bare `flex:<number>` means basis 0% — definite 0, item grows
             * from zero. Only when NO explicit basis length follows. */
            if (!basis_set) {
                basis_px = 0.0f;
                basis_auto = false;
            }
        }
    }
    if (!grow_seen && !basis_set) {
        return false;
    }
    *out_grow = grow;
    *out_basis_px = basis_px;
    *out_basis_auto = basis_auto;
    return true;
}

/* `display: none` check — the cheap path for hiding entire subtrees.
 * Returns 1 if the element should be entirely skipped at box-build. */
static int is_display_none(lxb_dom_element_t *el)
{
    if (!el) {
        return 0;
    }
    /* NOTE: stylesheet-driven `display:none` is decided by the libcss pass in
	 * walk() (which evaluates media queries correctly). We deliberately do NOT
	 * consult lexbor's computed style here — its cascade applies `@media print`
	 * (and other non-screen) rules to the screen render, which hid CNN's whole
	 * <body> (the page ships `@media print{…}` + complex `body:not(...)`
	 * rules). Only the unconditional inline-style + `hidden` attribute are
	 * cheap, media-independent skips. */
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
        coll->any_nowrap = 0;
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
    b->nowrap = coll->any_nowrap != 0;
    link_child(r, parent_idx, cidx);

    coll->len = 0;
    coll->last_was_space = 0;
    coll->any_nowrap = 0;
    return YETTY_OK_VOID();
}

/* True when `parent_node` has at least one non-whitespace text child —
 * i.e. an inline-level element under it flows WITHIN a text run (a
 * Wikipedia citation badge inside a paragraph). False for widget rows
 * whose children are all elements (toolbars, card corner buttons). */
static bool has_inline_text_siblings(const lxb_dom_node_t *parent_node)
{
    for (const lxb_dom_node_t *sibling = parent_node->first_child; sibling != NULL;
         sibling = sibling->next) {
        if (sibling->type != LXB_DOM_NODE_TYPE_TEXT) {
            continue;
        }
        const lxb_dom_text_t *text_node = lxb_dom_interface_text(sibling);
        const char *bytes = (const char *)text_node->char_data.data.data;
        size_t text_len = text_node->char_data.data.length;
        for (size_t byte_index = 0; byte_index < text_len; byte_index++) {
            unsigned char ch = (unsigned char)bytes[byte_index];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                return true;
            }
        }
    }
    return false;
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
                if (parent_style->nowrap) {
                    inline_collect->any_nowrap = 1;
                }
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
        bool cascade_sized = false;
        if (r->libcss) {
            size_t pre_istylen = 0;
            const lxb_char_t *pre_istyle =
                lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &pre_istylen);
            css_computed_style *pre_cs = yetty_ybrowser_libcss_select(
                r, el, (const char *)pre_istyle, pre_istyle ? pre_istylen : 0);
            if (pre_cs) {
                libcss_disp = yetty_ybrowser_libcss_display(pre_cs, parent_style == NULL);
                /* Inline-block promotion (below) needs to know whether the
				 * CASCADE sizes this element — stylesheet-sized buttons
				 * (48x48 toolbar icons) must become real boxes even amid
				 * inline text. Only the yes/no matters here, so rough
				 * font-size / percent bases are fine. */
                if (libcss_disp == CSS_DISPLAY_INLINE_BLOCK ||
                    libcss_disp == CSS_DISPLAY_INLINE_FLEX) {
                    float probe_px = 0.0f;
                    cascade_sized =
                        yetty_ybrowser_libcss_width(r, pre_cs, 16.0f, 0.0f, &probe_px) ||
                        yetty_ybrowser_libcss_height(r, pre_cs, 16.0f, 0.0f, &probe_px);
                }
                yetty_ybrowser_libcss_release(pre_cs);
                if (libcss_disp == CSS_DISPLAY_NONE) {
                    continue;
                }
            }
        }
        /* A `display:none` rule libcss dropped for its L4 selector (`:is()`,
		 * `:has()`, `:where()`) — hide the subtree the page meant to hide. */
        if (yetty_ylexbor_display_none_lookup(r, el)) {
            continue;
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
        bool promote_shrink_to_fit = false;
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
            case CSS_DISPLAY_INLINE_BLOCK:
            case CSS_DISPLAY_INLINE_FLEX: {
                /* Promote an inline-level box (inline-block / inline-flex)
				 * to a shrink-to-fit block when it is either explicitly
				 * sized inline OR standalone — no text run flows around it
				 * (toolbars, card corner buttons, icon wrappers). An
				 * inline-block WITH surrounding text (Wikipedia citation
				 * badges / year-pills / sidebar tags) stays inline: block
				 * layout would seize a line per badge and scatter
				 * fragments. The promoted box is marked shrink_to_fit so
				 * it takes its content width, not the parent's. */
                size_t ibs_len = 0;
                const lxb_char_t *ibs =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &ibs_len);
                bool sized_inline =
                    ibs != NULL && (find_inline_decl(ibs, ibs_len, "width", 5, NULL) != NULL ||
                                    find_inline_decl(ibs, ibs_len, "height", 6, NULL) != NULL);
                /* cascade_sized: the stylesheet supplies width/height (the
				 * gnews 48x48 icon buttons) — same promotion as an inline
				 * size. Content-sized badges amid text (Wikipedia) have
				 * neither and stay inline. */
                if (sized_inline || cascade_sized || !has_inline_text_siblings(node)) {
                    effective_disp = YL_DISP_BLOCK;
                    promote_shrink_to_fit = true;
                }
                break;
            }
            /* CSS_DISPLAY_NONE handled above. INLINE_BLOCK,
			 * INLINE_TABLE, INLINE_FLEX, GRID, RUN_IN, … — let
			 * the tag default decide. */
            default:
                break;
            }
        }
        /* Custom elements (hyphenated names — <c-wiz>, <ytd-app>, …) are
		 * structural containers in practice; sites pair them with
		 * display:block rules that don't always survive into our cascade.
		 * A standalone one (no inline text flowing around it) renders as
		 * a block so it produces a box and its subtree joins block flow —
		 * matching how Chrome lays these containers out on real pages. A
		 * custom element amid text keeps the inline default. */
        if (effective_disp == YL_DISP_INLINE &&
            (libcss_disp < 0 || libcss_disp == CSS_DISPLAY_INLINE) &&
            !has_inline_text_siblings(node)) {
            size_t custom_name_len = 0;
            const lxb_char_t *custom_name = lxb_dom_element_local_name(el, &custom_name_len);
            if (custom_name && memchr(custom_name, '-', custom_name_len) != NULL) {
                effective_disp = YL_DISP_BLOCK;
            }
        }

        /* Grid/flex items are blockified: an inline-level element child
		 * of a grid/flex container computes to a block-level box. Without
		 * this an unstyled wrapper (gnews <c-wiz>, plain <a>/<span>
		 * items) stays inline, produces no box, and its whole subtree
		 * escapes the grid/flex placement. Text runs are untouched —
		 * they still flow as anonymous inline content. */
        {
            enum yetty_ylexbor_layout_mode parent_mode = r->boxes.data[parent_idx].layout_mode;
            if ((parent_mode == YL_LAYOUT_GRID || parent_mode == YL_LAYOUT_FLEX_ROW ||
                 parent_mode == YL_LAYOUT_FLEX_COLUMN) &&
                effective_disp == YL_DISP_INLINE) {
                effective_disp = YL_DISP_BLOCK;
            }
        }

        /* Replaced form controls render as real boxes even at the inline
		 * default — promote standalone ones (no surrounding text run)
		 * exactly like inline-block widgets; their intrinsic size comes
		 * from the cascade-fallback below. */
        if (effective_disp == YL_DISP_INLINE &&
            (child->local_name == LXB_TAG_INPUT || child->local_name == LXB_TAG_SELECT ||
             child->local_name == LXB_TAG_TEXTAREA) &&
            !has_inline_text_siblings(node)) {
            effective_disp = YL_DISP_BLOCK;
            promote_shrink_to_fit = true;
        }

        /* <img> and <svg> are replaced elements — even when CSS (or the
		 * blockification above) says otherwise, they route to the
		 * replaced-box branch below rather than recursing as content. */
        if (child->local_name == LXB_TAG_IMG || child->local_name == LXB_TAG_SVG) {
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
            b->shrink_to_fit = promote_shrink_to_fit;

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
                    /* `font-family: Ahem` (the WPT test font) — every glyph is
					 * exactly 1em wide, so use advance ratio 1.0 for exact text
					 * geometry. Inherited via the style state so the flushed
					 * inline-text boxes measure with it too. */
                    if (yetty_ybrowser_libcss_font_is_ahem(cs)) {
                        s.glyph_advance = 1.0f;
                    }
                    b->glyph_advance = s.glyph_advance;
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
                    /* Precise calc(<pct> ± <len>) width: libcss dropped it (or
                     * the coarse `calc→%` pre-pass approximated it). Record the
                     * terms; resolve_pct_metrics finalizes the exact px against
                     * the containing block so grid columns land on the Chrome
                     * pixel. */
                    {
                        float calc_pct = 0.0f;
                        float calc_off = 0.0f;
                        if (yetty_ylexbor_calc_length_lookup(r, el, 0, s.font_size, &calc_pct,
                                                             &calc_off)) {
                            b->calc_width_pct = calc_pct;
                            b->calc_width_offset = calc_off;
                            b->calc_width_set = 1;
                        }
                    }
                    if (yetty_ybrowser_libcss_height(r, cs, s.font_size, pct_basis, &px)) {
                        b->css_height = px;
                        b->css_height_set = 1;
                    }
                    b->width_keyword = (uint8_t)yetty_ylexbor_width_keyword_lookup(r, el);
                    b->line_clamp = (uint8_t)yetty_ylexbor_line_clamp_lookup(r, el);
                    /* aspect-ratio / `::after{padding-bottom:%}` frame height —
                     * libcss drops both; resolve height = width * ratio at
                     * layout so image frames don't inflate to intrinsic size. */
                    if (!b->css_height_set) {
                        b->aspect_ratio = yetty_ylexbor_aspect_ratio_lookup(r, el);
                    }
                    /* Stylesheet `height: var(...)` re-resolved against
				 * element-scoped custom properties — the sheet-level
				 * substitution only saw the global table. */
                    {
                        float var_height_px = 0.0f;
                        if (yetty_ylexbor_var_height_lookup(r, el, s.font_size, &var_height_px)) {
                            b->css_height = var_height_px;
                        }
                    }
                    /* Form controls are replaced-like: with no author
					 * width/height they still render at an intrinsic size
					 * (Chrome's text input is ~170x22 at the default font).
					 * Without this a promoted <input> box measures empty and
					 * a search bar collapses to 0. Author CSS wins above. */
                    if (child->local_name == LXB_TAG_INPUT || child->local_name == LXB_TAG_SELECT ||
                        child->local_name == LXB_TAG_TEXTAREA) {
                        if (b->css_width == 0.0f) {
                            b->css_width = child->local_name == LXB_TAG_TEXTAREA
                                               ? s.font_size * 30.0f
                                               : s.font_size * 10.6f;
                        }
                        if (b->css_height == 0.0f) {
                            b->css_height = child->local_name == LXB_TAG_TEXTAREA
                                                ? s.font_size * 4.5f
                                                : s.font_size * 1.4f;
                        }
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
                    /* `display: inherit` — styles are selected per element
					 * (no parent composition), so libcss can't resolve the
					 * keyword itself. Map it from the parent box's resolved
					 * layout (the gnews subgrid idiom:
					 * `.Oc0wGc{display:inherit;grid-template-columns:inherit}`
					 * nests grid wrappers several levels deep). */
                    if (disp == CSS_DISPLAY_INHERIT) {
                        switch (r->boxes.data[parent_idx].layout_mode) {
                        case YL_LAYOUT_GRID:
                            disp = CSS_DISPLAY_GRID;
                            break;
                        case YL_LAYOUT_FLEX_ROW:
                        case YL_LAYOUT_FLEX_COLUMN:
                            disp = CSS_DISPLAY_FLEX;
                            break;
                        default:
                            disp = CSS_DISPLAY_BLOCK;
                            break;
                        }
                    }
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
                        b->main_reverse = (fd == CSS_FLEX_DIRECTION_ROW_REVERSE ||
                                           fd == CSS_FLEX_DIRECTION_COLUMN_REVERSE);
                        b->justify_content = yetty_ybrowser_libcss_justify_content(cs);
                        b->align_items = yetty_ybrowser_libcss_align_items(cs);
                        /* flex-wrap + align-content from the CASCADE (stylesheet),
						 * not just inline — WPT tests and real sites set these via
						 * classes. */
                        int fw = yetty_ybrowser_libcss_flex_wrap(cs);
                        if (fw == CSS_FLEX_WRAP_WRAP || fw == CSS_FLEX_WRAP_WRAP_REVERSE) {
                            b->flex_wrap = 1;
                            b->wrap_reverse = (fw == CSS_FLEX_WRAP_WRAP_REVERSE);
                        }
                        b->align_content = yetty_ybrowser_libcss_align_content(cs);
                        /* Flex `gap` (stored in grid_col_gap — a box is flex OR
						 * grid, never both). libcss in this tree models only the
						 * legacy multicol column-gap, so read the modern `gap`/
						 * `column-gap` shorthand from the inline style, falling
						 * back to the author-stylesheet flex-gap table scanned by
						 * add_css (`.cards { display:flex; gap:16px }`). */
                        {
                            size_t gs_len = 0;
                            const lxb_char_t *gs = lxb_dom_element_get_attribute(
                                el, (const lxb_char_t *)"style", 5, &gs_len);
                            if (gs && gs_len > 0) {
                                float gap = yetty_ylexbor_css_inline_gap((const char *)gs, gs_len);
                                if (gap > 0.0f) {
                                    b->grid_col_gap = gap;
                                }
                            }
                            if (b->grid_col_gap <= 0.0f) {
                                size_t gap_cls_len = 0;
                                const lxb_char_t *gap_cls = lxb_dom_element_get_attribute(
                                    el, (const lxb_char_t *)"class", 5, &gap_cls_len);
                                size_t tag_len = 0;
                                const lxb_char_t *tag_name =
                                    lxb_dom_element_local_name(el, &tag_len);
                                float gap = yetty_ylexbor_flex_gap_lookup(
                                    r, el, (const char *)gap_cls, gap_cls_len,
                                    (const char *)tag_name, tag_len);
                                if (gap > 0.0f) {
                                    b->grid_col_gap = gap;
                                }
                            }
                        }
                    } else if (disp == CSS_DISPLAY_TABLE || disp == CSS_DISPLAY_INLINE_TABLE) {
                        b->layout_mode = YL_LAYOUT_TABLE;
                        /* `table-layout: fixed` — no libcss bridge; read it
						 * from the inline style. Drives equal column widths in
						 * layout_table. */
                        size_t tllen = 0;
                        const char *tl = find_inline_decl(istyle, istyle ? istylen : 0,
                                                          "table-layout", 12, &tllen);
                        if (tl != NULL && tllen >= 5 && strncasecmp(tl, "fixed", 5) == 0) {
                            b->table_fixed = 1;
                        }
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
                            const struct yl_grid_class *gt = yetty_ylexbor_grid_class_lookup(r, el);
                            if (gt && gt->repeat_auto && gt->ntracks == 1) {
                                /* repeat(auto-fit|auto-fill, <track>) — the
								 * column count follows the content: one track
								 * per element child (`grid-auto-flow: column`
								 * menu strips; a max-content track sizes each
								 * column to its item). */
                                int element_children = 0;
                                for (lxb_dom_node_t *scan = lxb_dom_interface_node(el)->first_child;
                                     scan != NULL; scan = scan->next) {
                                    if (scan->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                                        element_children++;
                                    }
                                }
                                if (element_children > YL_GRID_MAX_TRACKS) {
                                    element_children = YL_GRID_MAX_TRACKS;
                                }
                                if (element_children >= 1) {
                                    b->layout_mode = YL_LAYOUT_GRID;
                                    for (int track = 0; track < element_children; track++) {
                                        b->grid_tracks[track] = gt->tracks[0];
                                    }
                                    b->grid_ntracks = (uint8_t)element_children;
                                    b->grid_col_gap = gt->col_gap;
                                    b->grid_row_gap = gt->row_gap;
                                }
                            } else if (gt && gt->ntracks >= 1 &&
                                       gt->ntracks <= YL_GRID_MAX_TRACKS) {
                                b->layout_mode = YL_LAYOUT_GRID;
                                memcpy(b->grid_tracks, gt->tracks, sizeof(b->grid_tracks));
                                b->grid_ntracks = gt->ntracks;
                                b->grid_col_gap = gt->col_gap;
                                b->grid_row_gap = gt->row_gap;
                            } else if (gt && gt->inherit_template) {
                                /* `grid-template-columns: inherit` — copy the
								 * parent box's resolved tracks (CSS inherit =
								 * the parent's computed value; a non-grid
								 * parent contributes nothing). */
                                const struct yetty_ylexbor_box *parent_box =
                                    &r->boxes.data[parent_idx];
                                if (parent_box->grid_ntracks > 0) {
                                    b->layout_mode = YL_LAYOUT_GRID;
                                    memcpy(b->grid_tracks, parent_box->grid_tracks,
                                           sizeof(b->grid_tracks));
                                    b->grid_ntracks = parent_box->grid_ntracks;
                                    b->grid_col_gap = parent_box->grid_col_gap;
                                    b->grid_row_gap = parent_box->grid_row_gap;
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
                                if (ntracks >= 1 && ntracks <= YL_GRID_MAX_TRACKS) {
                                    b->layout_mode = YL_LAYOUT_GRID;
                                    memcpy(b->grid_tracks, tracks, sizeof(b->grid_tracks));
                                    b->grid_ntracks = (uint8_t)ntracks;
                                    b->grid_col_gap = col_gap;
                                    b->grid_row_gap = row_gap;
                                } else {
                                    /* Named-line placement path. The plain
									 * parse rejects `[name]` templates; retry
									 * allowing them, but only commit to a grid
									 * when the template really has named lines AND
									 * every element child carries an inline
									 * `grid-column:<name>` (otherwise auto-flow
									 * would mis-place unplaced items — keep block
									 * flow, which is what protects Wikipedia's
									 * Vector shell). */
                                    struct yl_grid_track ntk[YL_GRID_MAX_TRACKS] = {0};
                                    float ncg = 0.0f, nrg = 0.0f;
                                    const char *gval = NULL;
                                    size_t gval_len = 0;
                                    int nt = yetty_ylexbor_grid_parse_inline_named(
                                        (const char *)istyle, istyle_len, ntk, YL_GRID_MAX_TRACKS,
                                        &ncg, &nrg, &gval, &gval_len);
                                    if (nt >= 2 && nt <= YL_GRID_MAX_TRACKS && gval != NULL &&
                                        memchr(gval, '[', gval_len) != NULL) {
                                        bool all_named = true;
                                        int nkids = 0;
                                        for (lxb_dom_node_t *kn =
                                                 lxb_dom_interface_node(el)->first_child;
                                             kn != NULL; kn = kn->next) {
                                            if (kn->type != LXB_DOM_NODE_TYPE_ELEMENT) {
                                                continue;
                                            }
                                            nkids++;
                                            size_t kslen = 0;
                                            const lxb_char_t *ks = lxb_dom_element_get_attribute(
                                                lxb_dom_interface_element(kn),
                                                (const lxb_char_t *)"style", 5, &kslen);
                                            if (ks == NULL ||
                                                find_inline_decl(ks, kslen, "grid-column", 11,
                                                                 NULL) == NULL) {
                                                all_named = false;
                                                break;
                                            }
                                        }
                                        if (all_named && nkids > 0) {
                                            b->layout_mode = YL_LAYOUT_GRID;
                                            memcpy(b->grid_tracks, ntk, sizeof(b->grid_tracks));
                                            b->grid_ntracks = (uint8_t)nt;
                                            b->grid_col_gap = ncg;
                                            b->grid_row_gap = nrg;
                                            char *tmp = malloc(gval_len + 1);
                                            if (tmp != NULL) {
                                                memcpy(tmp, gval, gval_len);
                                                tmp[gval_len] = '\0';
                                                b->grid_line_spec =
                                                    yetty_ylexbor_arena_dup(r, tmp, gval_len + 1);
                                                free(tmp);
                                            }
                                        }
                                    }
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
                    (void)yetty_ybrowser_libcss_order(cs, &b->flex_order);
                    int self_align = yetty_ybrowser_libcss_align_self(cs);
                    b->align_self =
                        (self_align == CSS_ALIGN_SELF_AUTO || self_align == CSS_ALIGN_SELF_INHERIT)
                            ? 0
                            : (int8_t)self_align;
                    /* flex-shrink: -1 sentinel = unset (solver uses the CSS
					 * initial 1.0); a definite value (incl. 0 = `shrink-0`) is
					 * stored as-is so fixed sidebars don't collapse. */
                    b->flex_shrink = -1.0f;
                    float fsh = 0;
                    if (yetty_ybrowser_libcss_flex_shrink(cs, &fsh)) {
                        b->flex_shrink = fsh;
                    }
                    float fb_px = 0;
                    bool fb_auto = false;
                    if (yetty_ybrowser_libcss_flex_basis(r, cs, s.font_size, pct_basis, &fb_px,
                                                         &fb_auto)) {
                        /* Encoding convention shared with css_width:
						 * 0 = auto / not set, > 0 = absolute px,
						 * < 0 = percent ratio (-N/100). The flex
						 * solver dispatches on the sign. An EXPLICIT
						 * zero basis (`flex:1` = `1 1 0%`) must stay
						 * distinguishable from auto — encode it as an
						 * invisible epsilon so the solver's auto
						 * branches (content sizing) never claim it. */
                        b->flex_basis_px = fb_auto ? 0.0f : (fb_px == 0.0f ? 0.0001f : fb_px);
                    } else {
                        b->flex_basis_px = 0.0f;
                    }
                    /* Inline `flex` shorthand — libcss here doesn't expand it
					 * into longhands, so parse it ourselves and let it win over
					 * the longhand reads above. */
                    {
                        float sh_grow = 0.0f;
                        float sh_basis = 0.0f;
                        bool sh_basis_auto = false;
                        if (parse_inline_flex(istyle, istyle ? istylen : 0, s.font_size, &sh_grow,
                                              &sh_basis, &sh_basis_auto)) {
                            b->flex_grow = sh_grow;
                            b->flex_basis_px =
                                sh_basis_auto ? 0.0f : (sh_basis == 0.0f ? 0.0001f : sh_basis);
                        }
                    }
                    /* Inline `flex-wrap` (no libcss bridge). `wrap` /
					 * `wrap-reverse` both enable wrapping; `nowrap` (default)
					 * leaves it single-line. */
                    {
                        size_t fwlen = 0;
                        const char *fw =
                            find_inline_decl(istyle, istyle ? istylen : 0, "flex-wrap", 9, &fwlen);
                        if (fw != NULL && fwlen >= 4 && strncasecmp(fw, "wrap", 4) == 0) {
                            b->flex_wrap = 1;
                        }
                    }
                    /* `grid-column: <line-name>` — the START line name for named
					 * grid placement. Only the leading token (before any `/`) is
					 * captured, and only when it's a name (not a numeric line
					 * index, which we don't model). Consumed by layout_grid when
					 * the parent is a named grid. */
                    {
                        size_t gc_len = 0;
                        const char *gc = find_inline_decl(istyle, istyle ? istylen : 0,
                                                          "grid-column", 11, &gc_len);
                        if (gc != NULL && gc_len > 0) {
                            /* `span N` (in `span N` or `auto / span N`) — the item
							 * occupies N columns when auto-flowed. github's whole
							 * 12-col layout places cards this way. */
                            int gc_start = 0;
                            int gc_span = 0;
                            if (yetty_ylexbor_grid_parse_placement(gc, 0, gc_len, &gc_start,
                                                                   &gc_span) &&
                                gc_span <= YL_GRID_MAX_TRACKS && gc_start <= 255) {
                                b->grid_col_start = (uint8_t)gc_start;
                                b->grid_col_span = (uint8_t)gc_span;
                            }
                            /* A leading line NAME (not a number / `span` / `auto`)
							 * — for named-line placement. */
                            size_t nm = 0;
                            while (nm < gc_len && gc[nm] != '/' && gc[nm] != ' ' &&
                                   gc[nm] != '\t') {
                                nm++;
                            }
                            char first = gc[0];
                            bool numeric = (first >= '0' && first <= '9') || first == '-';
                            bool is_span = (nm == 4 && strncasecmp(gc, "span", 4) == 0);
                            bool is_auto = (nm == 4 && strncasecmp(gc, "auto", 4) == 0);
                            if (nm > 0 && !numeric && !is_span && !is_auto) {
                                char *tmp = malloc(nm + 1);
                                if (tmp != NULL) {
                                    memcpy(tmp, gc, nm);
                                    tmp[nm] = '\0';
                                    b->grid_col_name = yetty_ylexbor_arena_dup(r, tmp, nm + 1);
                                    free(tmp);
                                }
                            }
                        }
                        /* Inline `grid-row` — numeric start/span for explicit
						 * row placement (`1/span 5`, `2/4`). */
                        size_t gr_len = 0;
                        const char *gr =
                            find_inline_decl(istyle, istyle ? istylen : 0, "grid-row", 8, &gr_len);
                        if (gr != NULL && gr_len > 0) {
                            int gr_start = 0;
                            int gr_span = 0;
                            if (yetty_ylexbor_grid_parse_placement(gr, 0, gr_len, &gr_start,
                                                                   &gr_span) &&
                                gr_start <= 255 && gr_span <= 255) {
                                b->grid_row_start = (uint8_t)gr_start;
                                b->grid_row_span = (uint8_t)gr_span;
                            }
                        }
                    }
                    /* No inline placement? Try the author-stylesheet placement
					 * table first (`.tile.feature { grid-column: span 2 }`,
					 * `.card.wide .thumb { grid-column: 3/3 }` — libcss has no
					 * grid support, so add_css text-scans these), then the
					 * responsive design systems (github's Primer Brand) that
					 * encode the span in the CLASS name
					 * (`Grid__column--medium-span-7`), resolved for the current
					 * viewport width. Without these every card in a 12-col grid
					 * defaults to one column and the sections stack. */
                    if (b->grid_col_span == 0 || b->grid_row_span == 0) {
                        struct yl_grid_placement table_placement =
                            yetty_ylexbor_grid_span_class_lookup(r, el);
                        if (b->grid_col_span == 0 && table_placement.col_span > 0) {
                            b->grid_col_start = table_placement.col_start;
                            b->grid_col_span = table_placement.col_span;
                        }
                        if (b->grid_row_span == 0 && table_placement.row_span > 0) {
                            b->grid_row_start = table_placement.row_start;
                            b->grid_row_span = table_placement.row_span;
                        }
                    }
                    if (b->grid_col_span == 0) {
                        size_t cls_len2 = 0;
                        const lxb_char_t *cls2 = lxb_dom_element_get_attribute(
                            el, (const lxb_char_t *)"class", 5, &cls_len2);
                        if (cls2 != NULL && cls_len2 > 0) {
                            int span = grid_span_from_classes((const char *)cls2, cls_len2,
                                                              r->viewport_w, YL_GRID_MAX_TRACKS);
                            if (span >= 1) {
                                b->grid_col_span = (uint8_t)span;
                            }
                        }
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
                    {
                        int visibility = yetty_ybrowser_libcss_visibility(cs);
                        s.vis_hidden = visibility == CSS_VISIBILITY_HIDDEN ||
                                       visibility == CSS_VISIBILITY_COLLAPSE;
                        b->vis_hidden = s.vis_hidden;
                    }
                    /* Opacity folds down the subtree (group effect, not
					 * inherited): effective = ancestors' × own. */
                    s.opacity = s.opacity * yetty_ybrowser_libcss_opacity(cs);
                    b->opacity = s.opacity;
                    b->clip_overflow = yetty_ybrowser_libcss_clips_overflow(cs);
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
                            int got = yetty_ybrowser_libcss_inset(r, cs, side, s.font_size,
                                                                  pct_basis, &inset);
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
                    /* Class-based `transform: translate(...)` — libcss drops the
					 * property, so a JS carousel that slides its inactive panel
					 * off an overflow-clipped viewport via a stylesheet rule
					 * (Google News weather widget) would otherwise render every
					 * panel stacked. The inline offset above wins when present. */
                    if (!b->has_transform) {
                        float txv = 0.0f, tyv = 0.0f;
                        bool txp = false, typ = false;
                        if (yetty_ylexbor_transform_lookup(r, el, &txv, &tyv, &txp, &typ)) {
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
					 * the legacy <pre>/<textarea> tags. The computed
					 * value already folds in inheritance, so `nowrap`
					 * is re-derived (set or cleared) at every element
					 * libcss styled. */
                    css_white_space = yetty_ybrowser_libcss_white_space(cs);
                    s.nowrap = (css_white_space == CSS_WHITE_SPACE_NOWRAP);

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
            if (child->local_name == LXB_TAG_BR) {
                /* <br>: a forced line break. Inject a hard newline into the
				 * parent block's inline buffer (the wrap pass breaks on '\n')
				 * and skip recursion — <br> has no inline content of its own. */
                inline_buf_force_break(inline_collect);
                continue;
            }
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
                /* Inherited style flows into the replaced element — the
				 * SVG path resolves `currentColor` against fg and seeds
				 * its default font-size from font_size. */
                ib->fg = s.fg;
                ib->font_size = s.font_size;

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
                        if (yetty_ybrowser_libcss_height(r, img_cs, s.font_size, basis, &px)) {
                            if (px > 0.0f) {
                                css_img_h = px;
                            } else if (px < 0.0f) {
                                /* Percentage height (e.g. 100%) — no containing
								 * block yet; record the fraction and bind it at
								 * layout against the nearest definite-height
								 * ancestor (aspect frame). */
                                ib->img_pct_h = -px;
                            }
                        }
                        /* float on a replaced inline element pulls it out of
					 * the inline flow into the block float path — the
					 * classic article-thumbnail-with-text-wrap pattern.
					 * Margins matter there (they widen the band the text
					 * wraps around), so capture them alongside. */
                        int img_float = yetty_ybrowser_libcss_float(img_cs);
                        if (img_float == CSS_FLOAT_LEFT) {
                            ib->float_side = 1;
                        } else if (img_float == CSS_FLOAT_RIGHT) {
                            ib->float_side = 2;
                        }
                        if (ib->float_side != 0) {
                            float *img_margin_dst[4] = {&ib->margin_top, &ib->margin_right,
                                                        &ib->margin_bottom, &ib->margin_left};
                            for (int side = 0; side < 4; side++) {
                                bool margin_auto = false;
                                bool margin_pct = false;
                                float margin_px = 0.0f;
                                if (yetty_ybrowser_libcss_margin(r, img_cs, side, s.font_size,
                                                                 basis, &margin_px, &margin_auto,
                                                                 &margin_pct) &&
                                    !margin_auto && !margin_pct) {
                                    *img_margin_dst[side] = margin_px;
                                }
                            }
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
            if (child->local_name == LXB_TAG_SVG) {
                /* Inline <svg> is a replaced element for layout: a box
                 * sized from CSS, its width/height attributes, or the
                 * viewBox — and its vector subtree is NOT walked
                 * (<path>/<title> are not layout content; the svg <title>
                 * otherwise leaks into the text run). Painting the vector
                 * is a separate concern — reserving the right space fixes
                 * toolbar/logo/icon layout. */
                struct yetty_ycore_void_result flush_res =
                    flush_inline(r, parent_style, parent_idx, inline_collect);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "walk: flush before svg");
                uint32_t svg_idx;
                struct yetty_ycore_void_result alloc_res = box_alloc(r, &svg_idx);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, alloc_res, "walk: box_alloc svg");
                struct yetty_ylexbor_box *svg_box = &r->boxes.data[svg_idx];
                svg_box->kind = YL_BOX_INLINE_IMAGE;
                svg_box->element = el;
                svg_box->fg = s.fg;
                svg_box->font_size = s.font_size;

                float attr_w = 0.0f, attr_h = 0.0f;
                size_t alen = 0;
                const lxb_char_t *aw =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"width", 5, &alen);
                if (aw && alen > 0) {
                    attr_w = (float)atof((const char *)aw);
                }
                const lxb_char_t *ah =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"height", 6, &alen);
                if (ah && alen > 0) {
                    attr_h = (float)atof((const char *)ah);
                }
                float view_w = 0.0f, view_h = 0.0f;
                const lxb_char_t *vb =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"viewBox", 7, &alen);
                if (vb && alen > 0) {
                    float view_min_x = 0.0f, view_min_y = 0.0f;
                    if (sscanf((const char *)vb, "%f %f %f %f", &view_min_x, &view_min_y, &view_w,
                               &view_h) != 4) {
                        view_w = 0.0f;
                        view_h = 0.0f;
                    }
                }

                float css_w = 0.0f, css_h = 0.0f;
                {
                    size_t svg_istylen = 0;
                    const lxb_char_t *svg_istyle = lxb_dom_element_get_attribute(
                        el, (const lxb_char_t *)"style", 5, &svg_istylen);
                    css_computed_style *svg_cs = yetty_ybrowser_libcss_select(
                        r, el, (const char *)svg_istyle, svg_istyle ? svg_istylen : 0);
                    /* box vector may have moved during select. */
                    svg_box = &r->boxes.data[svg_idx];
                    if (svg_cs) {
                        float px = 0.0f;
                        float pct_basis_svg = s.font_size * 16.0f;
                        if (yetty_ybrowser_libcss_width(r, svg_cs, s.font_size, pct_basis_svg,
                                                        &px) &&
                            px > 0.0f) {
                            css_w = px;
                        }
                        if (yetty_ybrowser_libcss_height(r, svg_cs, s.font_size, pct_basis_svg,
                                                         &px) &&
                            px > 0.0f) {
                            css_h = px;
                        }
                        /* The element's computed `color` (cascade-resolved,
					 * so plain inheritance already flowed through it)
					 * overrides the walker snapshot for currentColor. */
                        struct yetty_ylexbor_color svg_color;
                        if (yetty_ybrowser_libcss_color(svg_cs, &svg_color)) {
                            svg_box->fg = svg_color;
                        }
                        /* Same replaced-element float handling as <img>. */
                        int svg_float = yetty_ybrowser_libcss_float(svg_cs);
                        if (svg_float == CSS_FLOAT_LEFT) {
                            svg_box->float_side = 1;
                        } else if (svg_float == CSS_FLOAT_RIGHT) {
                            svg_box->float_side = 2;
                        }
                        if (svg_box->float_side != 0) {
                            float *svg_margin_dst[4] = {
                                &svg_box->margin_top, &svg_box->margin_right,
                                &svg_box->margin_bottom, &svg_box->margin_left};
                            for (int side = 0; side < 4; side++) {
                                bool margin_auto = false;
                                bool margin_pct = false;
                                float margin_px = 0.0f;
                                if (yetty_ybrowser_libcss_margin(r, svg_cs, side, s.font_size,
                                                                 pct_basis_svg, &margin_px,
                                                                 &margin_auto, &margin_pct) &&
                                    !margin_auto && !margin_pct) {
                                    *svg_margin_dst[side] = margin_px;
                                }
                            }
                        }
                        yetty_ybrowser_libcss_release(svg_cs);
                        svg_box = &r->boxes.data[svg_idx];
                    }
                }

                /* Per-axis priority: CSS, then attribute; a missing axis
                 * derives from the viewBox aspect; nothing at all falls
                 * back to the viewBox size, else the CSS replaced default
                 * (300x150). */
                float box_w = css_w > 0.0f ? css_w : attr_w;
                float box_h = css_h > 0.0f ? css_h : attr_h;
                if (box_w > 0.0f && box_h <= 0.0f) {
                    box_h = (view_w > 0.0f && view_h > 0.0f) ? box_w * view_h / view_w : box_w;
                } else if (box_h > 0.0f && box_w <= 0.0f) {
                    box_w = (view_w > 0.0f && view_h > 0.0f) ? box_h * view_w / view_h : box_h;
                } else if (box_w <= 0.0f && box_h <= 0.0f) {
                    /* No CSS, no attrs: viewBox size, else assume a small
					 * icon. (Chrome's replaced default is 300x150, but we
					 * don't paint the vector — a giant invisible spacer
					 * hurts more than an undersized one.) */
                    box_w = view_w > 0.0f ? view_w : 24.0f;
                    box_h = view_h > 0.0f ? view_h : 24.0f;
                }
                svg_box->w = box_w;
                svg_box->h = box_h;
                link_child(r, parent_idx, svg_idx);
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
            /* Restricted to CLICKABLE inline elements (the seg-boundary set
				 * below). Reading per-element style on every <span> would
				 * re-split a run into many segments — a Wikipedia `[N]` citation
				 * `<a><span>[</span>12<span>]</span></a>` fragmented 3 ways.
				 * Non-clickable inlines inherit the run style. */
            int inl_clickable =
                (child->local_name == LXB_TAG_A || child->local_name == LXB_TAG_AREA ||
                 child->local_name == LXB_TAG_BUTTON || child->local_name == LXB_TAG_INPUT ||
                 child->local_name == LXB_TAG_LABEL || child->local_name == LXB_TAG_SELECT ||
                 child->local_name == LXB_TAG_SUMMARY);
            if (r->libcss && inl_clickable) {
                size_t inl_istylen = 0;
                const lxb_char_t *inl_istyle =
                    lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &inl_istylen);
                css_computed_style *inl_cs = yetty_ybrowser_libcss_select(
                    r, el, (const char *)inl_istyle, inl_istyle ? inl_istylen : 0);
                if (inl_cs) {
                    struct yetty_ylexbor_color inl_color;
                    /* Only colour + text-decoration are read for inline
					 * elements. Reading per-element font-size/weight here split
					 * runs into extra style segments (a Wikipedia `[1]`
					 * citation fragmented 3 ways); bold/italic already come from
					 * the tag-default table, and font-size from the block path. */
                    if (yetty_ybrowser_libcss_color(inl_cs, &inl_color)) {
                        s.fg = inl_color;
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
                    /* `white-space: nowrap` on an inline link/button label
					 * (gnews nav chips, Material button text) — the run it
					 * contributes to must not soft-wrap. Only SET here:
					 * clearing is the block path's job. */
                    if (yetty_ybrowser_libcss_white_space(inl_cs) == CSS_WHITE_SPACE_NOWRAP) {
                        s.nowrap = true;
                    }
                    {
                        int inl_vis = yetty_ybrowser_libcss_visibility(inl_cs);
                        s.vis_hidden =
                            inl_vis == CSS_VISIBILITY_HIDDEN || inl_vis == CSS_VISIBILITY_COLLAPSE;
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

    /* Free per-box heap (segs) from the previous build. wrap_inline_box
	 * clears segs on consumption, but a relayout path that skips painting
	 * (or aborts mid-build) leaves them owned by the box — resetting size
	 * to 0 without this loop leaks them. */
    for (uint32_t i = 0; i < r->boxes.size; i++) {
        if (r->boxes.data[i].segs) {
            free(r->boxes.data[i].segs);
            r->boxes.data[i].segs = NULL;
            r->boxes.data[i].segs_count = 0;
        }
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
        .opacity = 1.0f,
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
