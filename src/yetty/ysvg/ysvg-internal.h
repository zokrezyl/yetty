#ifndef YETTY_YSVG_INTERNAL_H
#define YETTY_YSVG_INTERNAL_H

/*
 * Internal definitions shared across the ysvg translation units.
 *
 *   ysvg-parse.c   builds a struct yetty_ysvg_doc tree from XML source
 *   ysvg-attrs.c   parses SVG attribute mini-languages (numbers, lengths,
 *                  colors, transforms, path 'd', points list, viewBox)
 *   ysvg-style.c   resolves the cascaded style for an element
 *   ysvg-paint.c   walks the tree and emits ypaint primitives + text spans
 *   ysvg.c         glues the pipeline together; owns the public entry point
 *
 * Memory model: one root yetty_ysvg_doc owns every node, attribute string,
 * and path segment block. Destroying the doc frees everything.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/buffer.h>
#include <yetty/ysvg/ysvg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=============================================================================
 * SVG element kinds (Tiny 1.2 graphics subset)
 *===========================================================================*/

enum yetty_ysvg_elem {
    YETTY_YSVG_ELEM_UNKNOWN = 0,
    YETTY_YSVG_ELEM_SVG,
    YETTY_YSVG_ELEM_G,
    YETTY_YSVG_ELEM_DEFS,
    YETTY_YSVG_ELEM_USE,
    YETTY_YSVG_ELEM_TITLE,
    YETTY_YSVG_ELEM_DESC,
    YETTY_YSVG_ELEM_METADATA,
    YETTY_YSVG_ELEM_RECT,
    YETTY_YSVG_ELEM_CIRCLE,
    YETTY_YSVG_ELEM_ELLIPSE,
    YETTY_YSVG_ELEM_LINE,
    YETTY_YSVG_ELEM_POLYLINE,
    YETTY_YSVG_ELEM_POLYGON,
    YETTY_YSVG_ELEM_PATH,
    YETTY_YSVG_ELEM_TEXT,
    YETTY_YSVG_ELEM_TSPAN,
    YETTY_YSVG_ELEM_TEXTAREA, /* Tiny 1.2 */
    YETTY_YSVG_ELEM_IMAGE,
    YETTY_YSVG_ELEM_A,
    YETTY_YSVG_ELEM_SWITCH,
    YETTY_YSVG_ELEM_FONT, /* Tiny 1.2 in-document font (parsed, not rasterized) */
    YETTY_YSVG_ELEM_LINEARGRADIENT,
    YETTY_YSVG_ELEM_RADIALGRADIENT,
    YETTY_YSVG_ELEM_STOP,
    YETTY_YSVG_ELEM_STYLE,
};

/*=============================================================================
 * Attribute keys we recognise. UNKNOWN attributes are still stored as raw
 * name/value strings so xlink:href / id lookups work even when the key isn't
 * in this enum.
 *===========================================================================*/

enum yetty_ysvg_attr_key {
    YETTY_YSVG_ATTR_UNKNOWN = 0,
    YETTY_YSVG_ATTR_ID,
    YETTY_YSVG_ATTR_CLASS,
    YETTY_YSVG_ATTR_STYLE,
    YETTY_YSVG_ATTR_VIEWBOX,
    YETTY_YSVG_ATTR_WIDTH,
    YETTY_YSVG_ATTR_HEIGHT,
    YETTY_YSVG_ATTR_X,
    YETTY_YSVG_ATTR_Y,
    YETTY_YSVG_ATTR_X1,
    YETTY_YSVG_ATTR_Y1,
    YETTY_YSVG_ATTR_X2,
    YETTY_YSVG_ATTR_Y2,
    YETTY_YSVG_ATTR_CX,
    YETTY_YSVG_ATTR_CY,
    YETTY_YSVG_ATTR_R,
    YETTY_YSVG_ATTR_RX,
    YETTY_YSVG_ATTR_RY,
    YETTY_YSVG_ATTR_FX,
    YETTY_YSVG_ATTR_FY,
    YETTY_YSVG_ATTR_D,
    YETTY_YSVG_ATTR_POINTS,
    YETTY_YSVG_ATTR_TRANSFORM,
    YETTY_YSVG_ATTR_GRADIENTTRANSFORM,
    YETTY_YSVG_ATTR_FILL,
    YETTY_YSVG_ATTR_FILL_OPACITY,
    YETTY_YSVG_ATTR_FILL_RULE,
    YETTY_YSVG_ATTR_STROKE,
    YETTY_YSVG_ATTR_STROKE_OPACITY,
    YETTY_YSVG_ATTR_STROKE_WIDTH,
    YETTY_YSVG_ATTR_STROKE_LINECAP,
    YETTY_YSVG_ATTR_STROKE_LINEJOIN,
    YETTY_YSVG_ATTR_STROKE_DASHARRAY,
    YETTY_YSVG_ATTR_STROKE_MITERLIMIT,
    YETTY_YSVG_ATTR_OPACITY,
    YETTY_YSVG_ATTR_DISPLAY,
    YETTY_YSVG_ATTR_VISIBILITY,
    YETTY_YSVG_ATTR_FONT_FAMILY,
    YETTY_YSVG_ATTR_FONT_SIZE,
    YETTY_YSVG_ATTR_FONT_WEIGHT,
    YETTY_YSVG_ATTR_FONT_STYLE,
    YETTY_YSVG_ATTR_TEXT_ANCHOR,
    YETTY_YSVG_ATTR_HREF,
    YETTY_YSVG_ATTR_XLINK_HREF,
    YETTY_YSVG_ATTR_OFFSET,
    YETTY_YSVG_ATTR_STOP_COLOR,
    YETTY_YSVG_ATTR_STOP_OPACITY,
    YETTY_YSVG_ATTR_GRADIENTUNITS,
    YETTY_YSVG_ATTR_SPREADMETHOD,
    YETTY_YSVG_ATTR_PRESERVEASPECTRATIO,
};

struct yetty_ysvg_attr {
    enum yetty_ysvg_attr_key key;
    /* Owned NUL-terminated strings; yxml streams content in small chunks
     * so we accumulate into heap buffers rather than slicing the source. */
    char *name;
    size_t name_len;
    char *value;
    size_t value_len;
};

struct yetty_ysvg_node {
    enum yetty_ysvg_elem elem;
    /* Owned NUL-terminated tag name. */
    char *tag_name;
    size_t tag_name_len;

    struct yetty_ysvg_attr *attrs;
    size_t attr_count;
    size_t attr_cap;

    /* Text content (concatenated character data + entity-expanded). For
     * <text>/<tspan>: rendered as a span. Owned heap allocation. */
    char *text;
    size_t text_len;

    struct yetty_ysvg_node *first_child;
    struct yetty_ysvg_node *last_child;
    struct yetty_ysvg_node *next_sibling;
    struct yetty_ysvg_node *parent;
};

/* Forward declaration so the CSS function prototypes below don't fall
 * into C's "function prototype scope" trap (where `struct yetty_ysvg_style *`
 * inside a prototype would otherwise declare a fresh tag distinct from
 * the later file-scope definition). */
struct yetty_ysvg_style;

/*=============================================================================
 * Author CSS — parsed from every <style> element encountered.
 *
 * Why not libcss: NetSurf's libcss does not expose the SVG paint
 * properties (no CSS_PROP_FILL / CSS_PROP_STROKE accessors) — `fill:`
 * and `stroke:` declarations are silently dropped at parse time. So we
 * roll our own parser, sized for the SVG Tiny 1.2 styling chapter:
 *   - simple selectors:    *, type, .class, #id, [attr], [attr=val],
 *                          [attr~=val], [attr|=val], :pseudo-class
 *   - combinators:         descendant, child (>), adjacent (+), general
 *                          sibling (~)
 *   - compound + complex selectors with proper specificity (a, b, c)
 *   - !important
 *   - all SVG Tiny 1.2 CSS-stylable properties
 *   - @-rules (@media, @import) tokenised and skipped
 *
 * Pseudo-classes recognised but treated as "never match": :hover,
 * :focus, :active, :link, :visited, :enabled, :disabled, :checked,
 * :target. Structural pseudo-classes that depend only on the DOM:
 * :first-child, :last-child, :only-child, :root, :empty — actually
 * implemented.
 *===========================================================================*/

enum yetty_ysvg_css_combinator {
    YETTY_YSVG_CSS_DESCENDANT = 0, /* whitespace */
    YETTY_YSVG_CSS_CHILD,          /* > */
    YETTY_YSVG_CSS_ADJACENT,       /* + */
    YETTY_YSVG_CSS_SIBLING,        /* ~ */
};

enum yetty_ysvg_css_simple_kind {
    YETTY_YSVG_CSS_SIMPLE_UNIVERSAL = 0, /* * */
    YETTY_YSVG_CSS_SIMPLE_TYPE,          /* tag */
    YETTY_YSVG_CSS_SIMPLE_ID,            /* #id */
    YETTY_YSVG_CSS_SIMPLE_CLASS,         /* .class */
    YETTY_YSVG_CSS_SIMPLE_ATTR,          /* [attr], [attr OP value] */
    YETTY_YSVG_CSS_SIMPLE_PSEUDO,        /* :name */
};

enum yetty_ysvg_css_attr_op {
    YETTY_YSVG_CSS_ATTR_EXISTS = 0, /* [name] */
    YETTY_YSVG_CSS_ATTR_EQUAL,      /* [name="value"] */
    YETTY_YSVG_CSS_ATTR_INCLUDES,   /* [name~="value"] */
    YETTY_YSVG_CSS_ATTR_DASHMATCH,  /* [name|="value"] */
    YETTY_YSVG_CSS_ATTR_PREFIX,     /* [name^="value"] */
    YETTY_YSVG_CSS_ATTR_SUFFIX,     /* [name$="value"] */
    YETTY_YSVG_CSS_ATTR_SUBSTR,     /* [name*="value"] */
};

enum yetty_ysvg_css_pseudo_kind {
    YETTY_YSVG_CSS_PSEUDO_UNKNOWN = 0,
    /* structural — actually evaluated from the DOM */
    YETTY_YSVG_CSS_PSEUDO_FIRST_CHILD,
    YETTY_YSVG_CSS_PSEUDO_LAST_CHILD,
    YETTY_YSVG_CSS_PSEUDO_ONLY_CHILD,
    YETTY_YSVG_CSS_PSEUDO_ROOT,
    YETTY_YSVG_CSS_PSEUDO_EMPTY,
    YETTY_YSVG_CSS_PSEUDO_NOT, /* with arg */
    /* dynamic / link — recognised, always false */
    YETTY_YSVG_CSS_PSEUDO_HOVER,
    YETTY_YSVG_CSS_PSEUDO_FOCUS,
    YETTY_YSVG_CSS_PSEUDO_ACTIVE,
    YETTY_YSVG_CSS_PSEUDO_LINK,
    YETTY_YSVG_CSS_PSEUDO_VISITED,
    YETTY_YSVG_CSS_PSEUDO_ENABLED,
    YETTY_YSVG_CSS_PSEUDO_DISABLED,
    YETTY_YSVG_CSS_PSEUDO_CHECKED,
    YETTY_YSVG_CSS_PSEUDO_TARGET,
};

struct yetty_ysvg_css_simple {
    enum yetty_ysvg_css_simple_kind kind;
    /* Universal: all fields unused. Type: name=tag. Class/Id: name=value.
     * Attr: name=attr-name, value=attr-value (or NULL for EXISTS), op=...
     * Pseudo: name="hover", pseudo=PSEUDO_HOVER, value=arg-string for :not(). */
    char *name;
    char *value;
    enum yetty_ysvg_css_attr_op attr_op;
    enum yetty_ysvg_css_pseudo_kind pseudo;
    /* For :not(simple) we hold a child simple selector. */
    struct yetty_ysvg_css_simple *not_child;
};

struct yetty_ysvg_css_compound {
    struct yetty_ysvg_css_simple *simples;
    size_t simple_count;
};

struct yetty_ysvg_css_complex_step {
    enum yetty_ysvg_css_combinator combinator; /* combinator to LEFT (between this and previous) */
    struct yetty_ysvg_css_compound compound;
};

/* A complex selector is a chain of compounds connected by combinators.
 * Read left-to-right; the rightmost compound matches the candidate node. */
struct yetty_ysvg_css_complex {
    struct yetty_ysvg_css_complex_step *steps; /* >=1 */
    size_t step_count;
    /* CSS 2.1 specificity: a(ids) b(classes/attrs/pseudo-classes) c(types). */
    uint16_t spec_a, spec_b, spec_c;
};

struct yetty_ysvg_css_decl {
    char *property; /* lowercased, owned */
    char *value;    /* trimmed, owned */
    int important;
};

struct yetty_ysvg_css_rule {
    /* Comma-separated selectors → list of complex selectors. */
    struct yetty_ysvg_css_complex *selectors;
    size_t selector_count;
    struct yetty_ysvg_css_decl *decls;
    size_t decl_count;
    uint32_t order; /* source order, tiebreaker for equal specificity */
};

struct yetty_ysvg_doc {
    struct yetty_ysvg_node *root; /* the <svg> node, or NULL if parse failed */

    /* Bump-allocated nodes — one big arena of fixed-size chunks. We never
     * realloc them so child pointers stay valid. Per-node owned heap
     * (attrs array, name strings, text) is freed in doc_destroy. */
    struct yetty_ysvg_node_chunk *node_chunks;

    /* Author stylesheet — every <style> element's content, in document
     * order. Empty when the SVG carries no CSS. */
    struct yetty_ysvg_css_rule *rules;
    size_t rule_count;
    size_t rule_cap;
};

/* Parse `<style>` text content and append rules to `doc`. Returns 0 on
 * success, -1 on OOM (partial rules may have landed — destroy frees
 * them). Tolerant of malformed input: bad rule = skipped at the next
 * '}'. */
int yetty_ysvg_css_parse(struct yetty_ysvg_doc *doc, const char *css, size_t len);

/* Apply every CSS rule that matches `node`, in (specificity, source
 * order) ascending — !important declarations win over non-important. */
void yetty_ysvg_css_apply(struct yetty_ysvg_style *out, const struct yetty_ysvg_doc *doc,
                          const struct yetty_ysvg_node *node);

void yetty_ysvg_css_free_doc_rules(struct yetty_ysvg_doc *doc);

/* Shared with ysvg-style.c so CSS declarations and inline `style=`
 * declarations route through the same property→style applier. */
void yetty_ysvg_style_apply_property(struct yetty_ysvg_style *out, const char *prop, size_t plen,
                                     const char *val, size_t vlen);

void yetty_ysvg_doc_destroy(struct yetty_ysvg_doc *doc);

/* Parse SVG XML source into a doc tree. On error, returns a result with a
 * NULL doc and a populated error. */
YETTY_YRESULT_DECLARE(yetty_ysvg_doc_ptr, struct yetty_ysvg_doc *);
struct yetty_ysvg_doc_ptr_result yetty_ysvg_parse(const char *src, size_t len);

/*=============================================================================
 * Style cascade
 *===========================================================================*/

enum yetty_ysvg_paint_kind {
    YETTY_YSVG_PAINT_NONE = 0,
    YETTY_YSVG_PAINT_COLOR,
    YETTY_YSVG_PAINT_INHERIT,
    YETTY_YSVG_PAINT_CURRENTCOLOR,
    YETTY_YSVG_PAINT_URL, /* fill="url(#grad)" — resolved to color stop[0] in this build */
};

struct yetty_ysvg_paint {
    enum yetty_ysvg_paint_kind kind;
    uint32_t color; /* RGBA: 0xRRGGBBAA — packed to ABGR at emit time */
    /* slice into doc src for url(#id) — used to resolve gradient ref */
    const char *url_id;
    size_t url_id_len;
};

enum yetty_ysvg_linecap { YETTY_YSVG_LINECAP_BUTT = 0, YETTY_YSVG_LINECAP_ROUND, YETTY_YSVG_LINECAP_SQUARE };
enum yetty_ysvg_linejoin { YETTY_YSVG_LINEJOIN_MITER = 0, YETTY_YSVG_LINEJOIN_ROUND, YETTY_YSVG_LINEJOIN_BEVEL };
enum yetty_ysvg_text_anchor { YETTY_YSVG_ANCHOR_START = 0, YETTY_YSVG_ANCHOR_MIDDLE, YETTY_YSVG_ANCHOR_END };

struct yetty_ysvg_style {
    struct yetty_ysvg_paint fill;
    struct yetty_ysvg_paint stroke;
    float fill_opacity;
    float stroke_opacity;
    float opacity;
    float stroke_width;
    enum yetty_ysvg_linecap stroke_linecap;
    enum yetty_ysvg_linejoin stroke_linejoin;
    float font_size;
    enum yetty_ysvg_text_anchor text_anchor;
    bool display;    /* false = display:none */
    bool visibility; /* false = visibility:hidden */
};

void yetty_ysvg_style_init_root(struct yetty_ysvg_style *s, float default_font_size);

/* Resolve `style` for `node` inheriting from `parent_style`. Cascade
 * order matches SVG Tiny 1.2:
 *   inherited parent → presentation attrs → CSS rules (doc->rules)
 *     → inline `style="…"` → !important from CSS / inline.
 * `doc` is consulted for the author stylesheet; pass NULL to skip CSS. */
void yetty_ysvg_style_resolve(struct yetty_ysvg_style *out,
                              const struct yetty_ysvg_style *parent_style,
                              const struct yetty_ysvg_doc *doc,
                              const struct yetty_ysvg_node *node);

/*=============================================================================
 * 2D affine transform (column-major 2×3)
 *
 *   [ a c e ]   [ x ]
 *   [ b d f ] · [ y ]
 *   [ 0 0 1 ]   [ 1 ]
 *===========================================================================*/

struct yetty_ysvg_xform {
    float a, b, c, d, e, f;
};

void yetty_ysvg_xform_identity(struct yetty_ysvg_xform *m);
void yetty_ysvg_xform_multiply(struct yetty_ysvg_xform *out, const struct yetty_ysvg_xform *l,
                               const struct yetty_ysvg_xform *r);
void yetty_ysvg_xform_point(const struct yetty_ysvg_xform *m, float x, float y, float *ox,
                            float *oy);

/* Parse an SVG transform="..." list into `out`. Returns 1 on success. */
int yetty_ysvg_parse_transform(const char *s, size_t len, struct yetty_ysvg_xform *out);

/*=============================================================================
 * Attribute parsers (ysvg-attrs.c)
 *===========================================================================*/

/* Skim a CSS number (signed, optional fraction, optional exponent) starting
 * at `*pos`. Advances `*pos` past trailing whitespace/comma. Returns 1 on
 * success, 0 at EOF. */
int yetty_ysvg_parse_number(const char *s, size_t len, size_t *pos, float *out);

/* Parse a length: number + optional unit (px/pt/pc/mm/cm/in/em/ex/%). For
 * Tiny 1.2 we resolve everything to user units; for % units relative to
 * `pct_base`. */
float yetty_ysvg_parse_length(const char *s, size_t len, float pct_base, float fallback);

/* Parse a color string (#rgb / #rrggbb / #rrggbbaa / named / rgb(r,g,b)
 * / rgba(...)). Returns 1 on success. Sets *out_alpha to 255 unless the
 * input encodes alpha. */
int yetty_ysvg_parse_color(const char *s, size_t len, uint32_t *out_rgba);

/* Find attribute by key on a node. NULL if absent. */
const struct yetty_ysvg_attr *yetty_ysvg_attr_find(const struct yetty_ysvg_node *node,
                                                   enum yetty_ysvg_attr_key key);

/* Find attribute by literal name (for unknown-key fallback). */
const struct yetty_ysvg_attr *yetty_ysvg_attr_find_named(const struct yetty_ysvg_node *node,
                                                         const char *name);

/*=============================================================================
 * Path data
 *
 * Flattening: a path becomes a flat polyline (sequence of line segments)
 * after curves and arcs are subdivided.
 *===========================================================================*/

struct yetty_ysvg_point {
    float x, y;
};

/* A `subpath` is a contiguous run of points; `closed` says whether the last
 * point implicitly connects back to the first. */
struct yetty_ysvg_subpath {
    struct yetty_ysvg_point *points;
    size_t count;
    bool closed;
};

struct yetty_ysvg_path {
    struct yetty_ysvg_subpath *subs;
    size_t sub_count;
    size_t sub_cap;
};

void yetty_ysvg_path_destroy(struct yetty_ysvg_path *p);

/* Flatten an SVG path "d" string into polylines. `tolerance` is the max
 * chord-deviation in user units (smaller = more segments). On OOM returns
 * 0; on success returns 1. */
int yetty_ysvg_path_flatten(struct yetty_ysvg_path *out, const char *d, size_t len,
                            float tolerance);

/* Flatten a polyline/polygon `points` list. `closed` true => polygon. */
int yetty_ysvg_path_from_points(struct yetty_ysvg_path *out, const char *pts, size_t len,
                                bool closed);

/*=============================================================================
 * Color helpers
 *===========================================================================*/

/* Pack RGBA8888 (R in low byte) into the ABGR layout ypaint expects. */
uint32_t yetty_ysvg_rgba_to_abgr(uint32_t rgba);

/* Multiply only the alpha byte of an RGBA word. */
uint32_t yetty_ysvg_rgba_mul_alpha(uint32_t rgba, float k);

/*=============================================================================
 * Paint pass
 *===========================================================================*/

struct yetty_ysvg_paint_ctx {
    struct yetty_ydraw_core_buffer *buf;
    float default_font_size;
    float line_spacing;
    /* Scene bounds — points outside are still emitted but the buffer
     * carries these for the layer's scroll region. */
    float scene_min_x, scene_min_y, scene_max_x, scene_max_y;
    /* Root transform: maps SVG user units (viewBox space) to display
     * pixels. The yetty ypaint canvas treats primitive coords as
     * pixels, so we bake the viewBox→pixel scale here rather than
     * leaving it to the receiver. */
    struct yetty_ysvg_xform root_ctm;
    float user_to_pixel_scale; /* uniform scale built into root_ctm */
};

struct yetty_ycore_void_result yetty_ysvg_paint(const struct yetty_ysvg_doc *doc,
                                                struct yetty_ysvg_paint_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YSVG_INTERNAL_H */
