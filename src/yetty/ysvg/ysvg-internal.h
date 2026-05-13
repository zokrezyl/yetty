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
#include <yetty/ypaint-core/buffer.h>
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

struct yetty_ysvg_doc {
    struct yetty_ysvg_node *root; /* the <svg> node, or NULL if parse failed */

    /* Bump-allocated nodes — one big arena of fixed-size chunks. We never
     * realloc them so child pointers stay valid. Per-node owned heap
     * (attrs array, name strings, text) is freed in doc_destroy. */
    struct yetty_ysvg_node_chunk *node_chunks;
};

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

/* Resolve `style` for `node` inheriting from `parent_style`. Reads attrs
 * (presentation + inline `style="..."`). */
void yetty_ysvg_style_resolve(struct yetty_ysvg_style *out,
                              const struct yetty_ysvg_style *parent_style,
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

/* HSL lightness invert: L' = 1 - L, keep hue + saturation, keep alpha.
 * Maps black ↔ white, light shades to dark shades of the same hue.
 * Used to remap SVG content (authored assuming a white page background)
 * to a dark-terminal-friendly palette. */
uint32_t yetty_ysvg_rgba_invert_lightness(uint32_t rgba);

/*=============================================================================
 * Paint pass
 *===========================================================================*/

struct yetty_ysvg_paint_ctx {
    struct yetty_ypaint_core_buffer *buf;
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
