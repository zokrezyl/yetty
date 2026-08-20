/*
 * ysvg-paint.c — walk the SVG DOM and emit ydraw primitives.
 *
 * For each element we:
 *   1. Resolve the cascaded style (parent ⊕ presentation attrs ⊕ inline).
 *   2. Compose the current-transform-matrix with this element's
 *      `transform="..."`.
 *   3. Emit ydraw primitives. Geometry is transformed at emit time
 *      since SDF primitives carry shape parameters, not a transform.
 *
 * Mapping summary:
 *   rect        → SDF box (axis-aligned) or SDF rounded_box (rx/ry > 0),
 *                 falls back to 4 segments when the transform contains
 *                 rotation/skew.
 *   circle      → SDF circle (or ellipse under non-uniform scale).
 *   ellipse     → SDF ellipse.
 *   line        → SDF segment.
 *   polyline    → sequence of SDF segments.
 *   polygon     → sequence of SDF segments + closing edge.
 *   path        → flattened to polyline segments per the path module.
 *   text/tspan  → TEXT_DRAWABLE_LIST drawable-list entry via add_text.
 *   g/a/svg/    → recurse.
 */

#include "ysvg-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yimage/yimage-gen.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YSVG_PATH_TOLERANCE 0.5f

/* Bound on <use> chasing <use> (or a group of them) so a self-referential or
 * mutually-referential document cannot recurse without end. */
#define YSVG_USE_DEPTH_MAX 12

struct ysvg_paint_state {
    struct yetty_ysvg_paint_ctx *ctx;
    const struct yetty_ysvg_doc *doc; /* needed for CSS + id/href lookup */
    struct yetty_ysvg_xform ctm;
    struct yetty_ysvg_style style;
    /* y cursor for stacking text lines when no explicit y attribute. */
    float text_y;
    /* Depth of <use> expansion — cycle guard. */
    int use_depth;
    /* Index of the innermost enclosing <a>'s link region in
	 * ctx->links, or -1 outside any anchor. Inherited by child walk
	 * states, so a whole subtree extends its anchor's region. */
    int active_link;
};

/*=============================================================================
 * Color resolution
 *
 * Compose fill_opacity * opacity into the alpha byte and produce an ABGR
 * packed word ready for ydraw.
 *===========================================================================*/

static uint32_t resolve_color(const struct yetty_ysvg_paint *p, float opacity, float prop_opacity)
{
    if (p->kind == YETTY_YSVG_PAINT_NONE) {
        return 0;
    }
    uint32_t rgba = p->color;
    float k = opacity * prop_opacity;
    if (k < 0.0f) {
        k = 0.0f;
    }
    if (k > 1.0f) {
        k = 1.0f;
    }
    if (k < 1.0f) {
        uint32_t a = rgba & 0xFFu;
        uint32_t na = (uint32_t)((float)a * k + 0.5f);
        if (na > 255) {
            na = 255;
        }
        rgba = (rgba & 0xFFFFFF00u) | na;
    }
    return yetty_ysvg_rgba_to_abgr(rgba);
}

/* Would this paint contribute any visible pixels? Gates both fill and stroke
 * emission so a `none` paint or a fully transparent one is skipped cleanly
 * (the packed word alone can't be tested — a transparent non-black colour
 * still packs to a non-zero word). */
static int paint_visible(const struct yetty_ysvg_paint *p, float opacity, float prop_opacity)
{
    if (p->kind == YETTY_YSVG_PAINT_NONE) {
        return 0;
    }
    float alpha = (float)(p->color & 0xFFu) / 255.0f * opacity * prop_opacity;
    return alpha > 0.001f;
}

/*=============================================================================
 * Reference resolution — id lookup, <use> targets, gradient paint servers
 *
 * Every node is reachable from doc->root via the child/sibling links, so a
 * recursive tree search resolves `#id` references without touching the node
 * arena. Documents here are small; a linear search per reference is fine.
 *===========================================================================*/

static const struct yetty_ysvg_node *find_node_by_id(const struct yetty_ysvg_node *node,
                                                     const char *id, size_t id_len)
{
    if (!node) {
        return NULL;
    }
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(node, YETTY_YSVG_ATTR_ID);
    if (a && a->value_len == id_len && memcmp(a->value, id, id_len) == 0) {
        return node;
    }
    for (struct yetty_ysvg_node *c = node->first_child; c; c = c->next_sibling) {
        const struct yetty_ysvg_node *found = find_node_by_id(c, id, id_len);
        if (found) {
            return found;
        }
    }
    return NULL;
}

/* Pull the value of `prop` out of an inline `style="..."` string, if present.
 * Used for stops that carry `style="stop-color:#..."` instead of the
 * presentation attribute. */
static void find_style_value(const char *style, size_t len, const char *prop, const char **out_val,
                             size_t *out_len)
{
    *out_val = NULL;
    *out_len = 0;
    if (!style) {
        return;
    }
    size_t plen = strlen(prop);
    for (size_t i = 0; i + plen <= len; i++) {
        if (memcmp(style + i, prop, plen) != 0) {
            continue;
        }
        if (i > 0) {
            char before = style[i - 1];
            if (before != ';' && before != ' ' && before != '\t' && before != '\n' &&
                before != '\r' && before != '"' && before != '\'') {
                continue;
            }
        }
        size_t j = i + plen;
        while (j < len && (style[j] == ' ' || style[j] == '\t')) {
            j++;
        }
        if (j >= len || style[j] != ':') {
            continue;
        }
        j++;
        while (j < len && (style[j] == ' ' || style[j] == '\t')) {
            j++;
        }
        size_t start = j;
        while (j < len && style[j] != ';') {
            j++;
        }
        size_t end = j;
        while (end > start && (style[end - 1] == ' ' || style[end - 1] == '\t')) {
            end--;
        }
        *out_val = style + start;
        *out_len = end - start;
        return;
    }
}

/* Resolve one <stop>'s colour, folding stop-opacity into the alpha byte.
 * Returns RGBA (0xRRGGBBAA). Reads presentation attrs first, then lets an
 * inline `style=` override them. */
static uint32_t read_stop_rgba(const struct yetty_ysvg_node *stop)
{
    uint32_t rgb = 0x000000FFu; /* default stop-color is black, opaque */
    float stop_opacity = 1.0f;
    const struct yetty_ysvg_attr *a;

    if ((a = yetty_ysvg_attr_find(stop, YETTY_YSVG_ATTR_STOP_COLOR))) {
        uint32_t parsed;
        if (yetty_ysvg_parse_color(a->value, a->value_len, &parsed)) {
            rgb = parsed;
        }
    }
    if ((a = yetty_ysvg_attr_find(stop, YETTY_YSVG_ATTR_STOP_OPACITY))) {
        stop_opacity = yetty_ysvg_parse_length(a->value, a->value_len, 1.0f, 1.0f);
    }
    if ((a = yetty_ysvg_attr_find(stop, YETTY_YSVG_ATTR_STYLE))) {
        const char *val;
        size_t vlen;
        find_style_value(a->value, a->value_len, "stop-color", &val, &vlen);
        if (val) {
            uint32_t parsed;
            if (yetty_ysvg_parse_color(val, vlen, &parsed)) {
                rgb = parsed;
            }
        }
        find_style_value(a->value, a->value_len, "stop-opacity", &val, &vlen);
        if (val) {
            stop_opacity = yetty_ysvg_parse_length(val, vlen, 1.0f, 1.0f);
        }
    }

    float alpha = (float)(rgb & 0xFFu) * stop_opacity;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 255.0f) {
        alpha = 255.0f;
    }
    return (rgb & 0xFFFFFF00u) | (uint32_t)(alpha + 0.5f);
}

/* A gradient may carry its <stop>s directly, or inherit them from another
 * gradient via xlink:href (Inkscape splits geometry and stops this way).
 * Follow the href chain to the gradient that actually holds the stops. */
static const struct yetty_ysvg_node *gradient_stops_node(const struct yetty_ysvg_doc *doc,
                                                         const struct yetty_ysvg_node *grad,
                                                         int depth)
{
    if (!grad || depth > 8) {
        return NULL;
    }
    for (struct yetty_ysvg_node *c = grad->first_child; c; c = c->next_sibling) {
        if (c->elem == YETTY_YSVG_ELEM_STOP) {
            return grad;
        }
    }
    const struct yetty_ysvg_attr *h = yetty_ysvg_attr_find(grad, YETTY_YSVG_ATTR_XLINK_HREF);
    if (!h) {
        h = yetty_ysvg_attr_find(grad, YETTY_YSVG_ATTR_HREF);
    }
    if (h && h->value_len > 1 && h->value[0] == '#') {
        const struct yetty_ysvg_node *ref =
            find_node_by_id(doc->root, h->value + 1, h->value_len - 1);
        return gradient_stops_node(doc, ref, depth + 1);
    }
    return NULL;
}

/* Approximate a gradient paint server by a single solid colour: the mean of
 * the stop ramp over offset [0,1] (piecewise-linear between stops, flat before
 * the first / after the last). The SDF primitives can't paint a real gradient
 * across an arbitrary shape, so this keeps gradient-filled artwork readable
 * instead of blank. Returns 1 and writes *out_rgba on success. */
static int resolve_gradient_color(const struct yetty_ysvg_doc *doc, const char *id, size_t id_len,
                                  uint32_t *out_rgba)
{
    const struct yetty_ysvg_node *grad = find_node_by_id(doc->root, id, id_len);
    if (!grad || (grad->elem != YETTY_YSVG_ELEM_LINEARGRADIENT &&
                  grad->elem != YETTY_YSVG_ELEM_RADIALGRADIENT)) {
        return 0;
    }
    const struct yetty_ysvg_node *stops_node = gradient_stops_node(doc, grad, 0);
    if (!stops_node) {
        return 0;
    }

    enum { YSVG_GRAD_MAX_STOPS = 64 };
    float offset[YSVG_GRAD_MAX_STOPS];
    float red[YSVG_GRAD_MAX_STOPS], green[YSVG_GRAD_MAX_STOPS];
    float blue[YSVG_GRAD_MAX_STOPS], stop_alpha[YSVG_GRAD_MAX_STOPS];
    size_t count = 0;
    float last_offset = 0.0f;
    for (struct yetty_ysvg_node *c = stops_node->first_child; c && count < YSVG_GRAD_MAX_STOPS;
         c = c->next_sibling) {
        if (c->elem != YETTY_YSVG_ELEM_STOP) {
            continue;
        }
        float off = 0.0f;
        const struct yetty_ysvg_attr *oa = yetty_ysvg_attr_find(c, YETTY_YSVG_ATTR_OFFSET);
        if (oa) {
            off = yetty_ysvg_parse_length(oa->value, oa->value_len, 1.0f, 0.0f);
        }
        if (off < 0.0f) {
            off = 0.0f;
        }
        if (off > 1.0f) {
            off = 1.0f;
        }
        if (off < last_offset) {
            off = last_offset; /* enforce monotonic offsets */
        }
        last_offset = off;
        uint32_t rgba = read_stop_rgba(c);
        offset[count] = off;
        red[count] = (float)((rgba >> 24) & 0xFFu);
        green[count] = (float)((rgba >> 16) & 0xFFu);
        blue[count] = (float)((rgba >> 8) & 0xFFu);
        stop_alpha[count] = (float)(rgba & 0xFFu);
        count++;
    }
    if (count == 0) {
        return 0;
    }
    double mean_r = 0.0, mean_g = 0.0, mean_b = 0.0, mean_a = 0.0;
    if (count == 1) {
        mean_r = red[0];
        mean_g = green[0];
        mean_b = blue[0];
        mean_a = stop_alpha[0];
    } else {
        /* Leading flat segment [0, offset[0]]. */
        mean_r = red[0] * offset[0];
        mean_g = green[0] * offset[0];
        mean_b = blue[0] * offset[0];
        mean_a = stop_alpha[0] * offset[0];
        for (size_t i = 1; i < count; i++) {
            float width = offset[i] - offset[i - 1];
            mean_r += 0.5 * (red[i - 1] + red[i]) * width;
            mean_g += 0.5 * (green[i - 1] + green[i]) * width;
            mean_b += 0.5 * (blue[i - 1] + blue[i]) * width;
            mean_a += 0.5 * (stop_alpha[i - 1] + stop_alpha[i]) * width;
        }
        /* Trailing flat segment [offset[last], 1]. */
        float tail = 1.0f - offset[count - 1];
        mean_r += red[count - 1] * tail;
        mean_g += green[count - 1] * tail;
        mean_b += blue[count - 1] * tail;
        mean_a += stop_alpha[count - 1] * tail;
    }
    uint32_t out_r = (uint32_t)(mean_r + 0.5);
    uint32_t out_g = (uint32_t)(mean_g + 0.5);
    uint32_t out_b = (uint32_t)(mean_b + 0.5);
    uint32_t out_a = (uint32_t)(mean_a + 0.5);
    if (out_r > 255) {
        out_r = 255;
    }
    if (out_g > 255) {
        out_g = 255;
    }
    if (out_b > 255) {
        out_b = 255;
    }
    if (out_a > 255) {
        out_a = 255;
    }
    *out_rgba = (out_r << 24) | (out_g << 16) | (out_b << 8) | out_a;
    return 1;
}

/* If `paint` is a url(#gradient) reference, bake it down to its representative
 * solid colour in place. Non-url paints are left untouched. */
static void resolve_url_paint(const struct yetty_ysvg_doc *doc, struct yetty_ysvg_paint *paint)
{
    if (paint->kind != YETTY_YSVG_PAINT_URL || !paint->url_id || paint->url_id_len == 0) {
        return;
    }
    uint32_t color;
    if (resolve_gradient_color(doc, paint->url_id, paint->url_id_len, &color)) {
        paint->color = color;
        ydebug("ysvg: gradient url(#%.*s) approximated as solid 0x%08x", (int)paint->url_id_len,
               paint->url_id, color);
    } else {
        ywarn("ysvg: unresolved paint url(#%.*s) — using fallback colour", (int)paint->url_id_len,
              paint->url_id);
    }
}

/*=============================================================================
 * Transform helpers
 *===========================================================================*/

static int xform_is_axis_aligned(const struct yetty_ysvg_xform *m)
{
    /* Pure translate + scale (no rotation / skew). */
    return fabsf(m->b) < 1e-6f && fabsf(m->c) < 1e-6f;
}

static float xform_avg_scale(const struct yetty_ysvg_xform *m)
{
    float sx = sqrtf(m->a * m->a + m->b * m->b);
    float sy = sqrtf(m->c * m->c + m->d * m->d);
    return (sx + sy) * 0.5f;
}

/*=============================================================================
 * Attribute readers
 *===========================================================================*/

static float attr_float(const struct yetty_ysvg_node *n, enum yetty_ysvg_attr_key k, float fallback)
{
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(n, k);
    if (!a) {
        return fallback;
    }
    return yetty_ysvg_parse_length(a->value, a->value_len, 100.0f, fallback);
}

static void compose_node_transform(struct yetty_ysvg_xform *out,
                                   const struct yetty_ysvg_xform *parent,
                                   const struct yetty_ysvg_node *node)
{
    struct yetty_ysvg_xform local;
    yetty_ysvg_xform_identity(&local);
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(node, YETTY_YSVG_ATTR_TRANSFORM);
    if (a) {
        yetty_ysvg_parse_transform(a->value, a->value_len, &local);
    }
    yetty_ysvg_xform_multiply(out, parent, &local);
}

/*=============================================================================
 * Segment emit helper — used by shape strokes and path flattening
 *===========================================================================*/

static struct yetty_ycore_void_result emit_segment(struct yetty_ysvg_paint_ctx *ctx,
                                                   const struct yetty_ysvg_xform *m, float x0,
                                                   float y0, float x1, float y1, uint32_t color,
                                                   float width)
{
    float ax, ay, bx, by;
    yetty_ysvg_xform_point(m, x0, y0, &ax, &ay);
    yetty_ysvg_xform_point(m, x1, y1, &bx, &by);
    struct yetty_ysdf_segment seg = {.start_x = ax, .start_y = ay, .end_x = bx, .end_y = by};
    /* SDF segment is a stroked line — the shader reads stroke_color, not
     * fill. Pass `color` as the stroke argument (fill=0). Mirrors how
     * pdf-renderer.c / yzoo.c invoke add_segment. */
    struct yetty_ycore_void_result r =
        yetty_ydraw_drawable_list_add_cmd_add_segment(ctx->buf, 0, 0, 0, color, width, &seg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ysvg: segment emit failed");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Polygon fill — triangulate each subpath and emit filled SDF triangles.
 *===========================================================================*/

static struct yetty_ycore_void_result emit_filled_subpaths(struct ysvg_paint_state *ps,
                                                           const struct yetty_ysvg_path *path,
                                                           uint32_t fill)
{
    /* Abutting triangles each anti-alias their shared edge to partial
     * coverage, leaving a thin seam where the background bleeds through. For
     * an opaque fill we hide those seams by stroking every triangle in its own
     * fill colour: neighbouring strokes overlap the shared edge and repaint it
     * solid. A translucent fill can't use this trick — overlapping strokes
     * would double-blend into a darker seam — so it is drawn flat. The seam
     * cover is in device pixels (vertices are already transformed). */
    uint32_t seam_stroke = 0;
    float seam_width = 0.0f;
    if (((fill >> 24) & 0xFFu) >= 250u) {
        seam_stroke = fill;
        seam_width = 1.0f;
    }

    for (size_t i = 0; i < path->sub_count; i++) {
        const struct yetty_ysvg_subpath *sp = &path->subs[i];
        if (sp->count < 3) {
            continue;
        }
        uint32_t *tris = NULL;
        size_t tri_count = yetty_ysvg_triangulate(sp->points, sp->count, &tris);
        if (tri_count == 0) {
            ydebug("ysvg: fill triangulation skipped for subpath (%zu verts)", sp->count);
            continue;
        }
        for (size_t t = 0; t < tri_count; t++) {
            const struct yetty_ysvg_point *a = &sp->points[tris[t * 3 + 0]];
            const struct yetty_ysvg_point *b = &sp->points[tris[t * 3 + 1]];
            const struct yetty_ysvg_point *c = &sp->points[tris[t * 3 + 2]];
            float ax, ay, bx, by, cx, cy;
            yetty_ysvg_xform_point(&ps->ctm, a->x, a->y, &ax, &ay);
            yetty_ysvg_xform_point(&ps->ctm, b->x, b->y, &bx, &by);
            yetty_ysvg_xform_point(&ps->ctm, c->x, c->y, &cx, &cy);
            struct yetty_ysdf_triangle geom = {.vertex_a_x = ax,
                                               .vertex_a_y = ay,
                                               .vertex_b_x = bx,
                                               .vertex_b_y = by,
                                               .vertex_c_x = cx,
                                               .vertex_c_y = cy};
            struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_triangle(
                ps->ctx->buf, 0, 0, fill, seam_stroke, seam_width, &geom);
            if (YETTY_IS_ERR(r)) {
                free(tris);
                return r;
            }
        }
        free(tris);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Shape emitters
 *===========================================================================*/

static struct yetty_ycore_void_result emit_rect(struct ysvg_paint_state *ps,
                                                const struct yetty_ysvg_node *n)
{
    float x = attr_float(n, YETTY_YSVG_ATTR_X, 0.0f);
    float y = attr_float(n, YETTY_YSVG_ATTR_Y, 0.0f);
    float w = attr_float(n, YETTY_YSVG_ATTR_WIDTH, 0.0f);
    float h = attr_float(n, YETTY_YSVG_ATTR_HEIGHT, 0.0f);
    float rx = attr_float(n, YETTY_YSVG_ATTR_RX, 0.0f);
    float ry = attr_float(n, YETTY_YSVG_ATTR_RY, 0.0f);
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    if (rx > 0.0f && ry == 0.0f) {
        ry = rx;
    }
    if (ry > 0.0f && rx == 0.0f) {
        rx = ry;
    }
    if (rx > w * 0.5f) {
        rx = w * 0.5f;
    }
    if (ry > h * 0.5f) {
        ry = h * 0.5f;
    }

    uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
    uint32_t stroke = resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
    float scale = xform_avg_scale(&ps->ctm);
    float sw =
        (ps->style.stroke.kind == YETTY_YSVG_PAINT_NONE) ? 0.0f : ps->style.stroke_width * scale;

    if (xform_is_axis_aligned(&ps->ctm)) {
        float cx, cy;
        yetty_ysvg_xform_point(&ps->ctm, x + w * 0.5f, y + h * 0.5f, &cx, &cy);
        float sx = ps->ctm.a; /* may be negative */
        float sy = ps->ctm.d;
        float hw = fabsf(sx) * w * 0.5f;
        float hh = fabsf(sy) * h * 0.5f;
        if (rx > 0.0f || ry > 0.0f) {
            float rs = fabsf(sx);
            struct yetty_ysdf_rounded_box geom = {
                .center_x = cx,
                .center_y = cy,
                .half_width = hw,
                .half_height = hh,
                .radius_top_right = rx * rs,
                .radius_bottom_right = rx * rs,
                .radius_top_left = rx * rs,
                .radius_bottom_left = rx * rs,
            };
            struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_rounded_box(
                ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ysvg: rect emit failed");
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_box geom = {.center_x = cx,
                                      .center_y = cy,
                                      .half_width = hw,
                                      .half_height = hh,
                                      .corner_radius = 0.0f};
        struct yetty_ycore_void_result r =
            yetty_ydraw_drawable_list_add_cmd_add_box(ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ysvg: rect emit failed");
        return YETTY_OK_VOID();
    }
    /* Rotated/skewed: fall back to 4 stroked segments. Fill is dropped
     * since SDF box can't represent a rotated quad. */
    if (stroke != 0 && sw > 0.0f) {
        struct yetty_ycore_void_result r;
        r = emit_segment(ps->ctx, &ps->ctm, x, y, x + w, y, stroke, sw);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        r = emit_segment(ps->ctx, &ps->ctm, x + w, y, x + w, y + h, stroke, sw);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        r = emit_segment(ps->ctx, &ps->ctm, x + w, y + h, x, y + h, stroke, sw);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        r = emit_segment(ps->ctx, &ps->ctm, x, y + h, x, y, stroke, sw);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_circle(struct ysvg_paint_state *ps,
                                                  const struct yetty_ysvg_node *n)
{
    float cx0 = attr_float(n, YETTY_YSVG_ATTR_CX, 0.0f);
    float cy0 = attr_float(n, YETTY_YSVG_ATTR_CY, 0.0f);
    float r = attr_float(n, YETTY_YSVG_ATTR_R, 0.0f);
    if (r <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float cx, cy;
    yetty_ysvg_xform_point(&ps->ctm, cx0, cy0, &cx, &cy);
    float sx = sqrtf(ps->ctm.a * ps->ctm.a + ps->ctm.b * ps->ctm.b);
    float sy = sqrtf(ps->ctm.c * ps->ctm.c + ps->ctm.d * ps->ctm.d);

    uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
    uint32_t stroke = resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
    float scale_avg = (sx + sy) * 0.5f;
    float sw = (ps->style.stroke.kind == YETTY_YSVG_PAINT_NONE)
                   ? 0.0f
                   : ps->style.stroke_width * scale_avg;

    if (fabsf(sx - sy) < 1e-4f) {
        struct yetty_ysdf_circle geom = {.center_x = cx, .center_y = cy, .radius = r * sx};
        struct yetty_ycore_void_result q = yetty_ydraw_drawable_list_add_cmd_add_circle(
            ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, q, "ysvg: circle emit failed");
    } else {
        struct yetty_ysdf_ellipse geom = {
            .center_x = cx, .center_y = cy, .radius_x = r * sx, .radius_y = r * sy};
        struct yetty_ycore_void_result q = yetty_ydraw_drawable_list_add_cmd_add_ellipse(
            ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, q, "ysvg: circle-as-ellipse emit failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_ellipse(struct ysvg_paint_state *ps,
                                                   const struct yetty_ysvg_node *n)
{
    float cx0 = attr_float(n, YETTY_YSVG_ATTR_CX, 0.0f);
    float cy0 = attr_float(n, YETTY_YSVG_ATTR_CY, 0.0f);
    float rx = attr_float(n, YETTY_YSVG_ATTR_RX, 0.0f);
    float ry = attr_float(n, YETTY_YSVG_ATTR_RY, 0.0f);
    if (rx <= 0.0f || ry <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float cx, cy;
    yetty_ysvg_xform_point(&ps->ctm, cx0, cy0, &cx, &cy);
    float sx = sqrtf(ps->ctm.a * ps->ctm.a + ps->ctm.b * ps->ctm.b);
    float sy = sqrtf(ps->ctm.c * ps->ctm.c + ps->ctm.d * ps->ctm.d);

    uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
    uint32_t stroke = resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
    float scale_avg = (sx + sy) * 0.5f;
    float sw = (ps->style.stroke.kind == YETTY_YSVG_PAINT_NONE)
                   ? 0.0f
                   : ps->style.stroke_width * scale_avg;
    struct yetty_ysdf_ellipse geom = {
        .center_x = cx, .center_y = cy, .radius_x = rx * sx, .radius_y = ry * sy};
    struct yetty_ycore_void_result q =
        yetty_ydraw_drawable_list_add_cmd_add_ellipse(ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, q, "ysvg: ellipse emit failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_line(struct ysvg_paint_state *ps,
                                                const struct yetty_ysvg_node *n)
{
    float x1 = attr_float(n, YETTY_YSVG_ATTR_X1, 0.0f);
    float y1 = attr_float(n, YETTY_YSVG_ATTR_Y1, 0.0f);
    float x2 = attr_float(n, YETTY_YSVG_ATTR_X2, 0.0f);
    float y2 = attr_float(n, YETTY_YSVG_ATTR_Y2, 0.0f);
    uint32_t stroke = resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
    if (stroke == 0) {
        return YETTY_OK_VOID();
    }
    float sw = ps->style.stroke_width * xform_avg_scale(&ps->ctm);
    return emit_segment(ps->ctx, &ps->ctm, x1, y1, x2, y2, stroke, sw);
}

static struct yetty_ycore_void_result emit_subpath_segments(struct ysvg_paint_state *ps,
                                                            const struct yetty_ysvg_subpath *sp,
                                                            uint32_t color, float width)
{
    for (size_t j = 1; j < sp->count; j++) {
        struct yetty_ycore_void_result r =
            emit_segment(ps->ctx, &ps->ctm, sp->points[j - 1].x, sp->points[j - 1].y,
                         sp->points[j].x, sp->points[j].y, color, width);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
    }
    if (sp->closed && sp->count >= 2) {
        struct yetty_ycore_void_result r = emit_segment(
            ps->ctx, &ps->ctm, sp->points[sp->count - 1].x, sp->points[sp->count - 1].y,
            sp->points[0].x, sp->points[0].y, color, width);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_points_shape(struct ysvg_paint_state *ps,
                                                        const struct yetty_ysvg_node *n, int closed)
{
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_POINTS);
    if (!a) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysvg_path path;
    if (!yetty_ysvg_path_from_points(&path, a->value, a->value_len, closed != 0)) {
        return YETTY_ERR(yetty_ycore_void, "ysvg: polyline/polygon parse failed");
    }

    /* Paint order: fill first, stroke on top (SVG semantics). A polyline is
     * implicitly closed for the purpose of filling. */
    if (paint_visible(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity)) {
        uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
        struct yetty_ycore_void_result r = emit_filled_subpaths(ps, &path, fill);
        if (YETTY_IS_ERR(r)) {
            yetty_ysvg_path_destroy(&path);
            return r;
        }
    }
    if (paint_visible(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity)) {
        uint32_t stroke =
            resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
        float sw = ps->style.stroke_width * xform_avg_scale(&ps->ctm);
        for (size_t i = 0; i < path.sub_count; i++) {
            struct yetty_ycore_void_result r = emit_subpath_segments(ps, &path.subs[i], stroke, sw);
            if (YETTY_IS_ERR(r)) {
                yetty_ysvg_path_destroy(&path);
                return r;
            }
        }
    }
    yetty_ysvg_path_destroy(&path);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_path(struct ysvg_paint_state *ps,
                                                const struct yetty_ysvg_node *n)
{
    const struct yetty_ysvg_attr *a = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_D);
    if (!a) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysvg_path path;
    float tol = YSVG_PATH_TOLERANCE / xform_avg_scale(&ps->ctm);
    if (tol < 0.05f) {
        tol = 0.05f;
    }
    if (!yetty_ysvg_path_flatten(&path, a->value, a->value_len, tol)) {
        return YETTY_ERR(yetty_ycore_void, "ysvg: path parse/flatten failed");
    }

    /* Paint order: fill (triangulated) first, then stroke on top. */
    if (paint_visible(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity)) {
        uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
        struct yetty_ycore_void_result r = emit_filled_subpaths(ps, &path, fill);
        if (YETTY_IS_ERR(r)) {
            yetty_ysvg_path_destroy(&path);
            return r;
        }
    }
    if (paint_visible(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity)) {
        uint32_t stroke =
            resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
        float sw = ps->style.stroke_width * xform_avg_scale(&ps->ctm);
        for (size_t i = 0; i < path.sub_count; i++) {
            struct yetty_ycore_void_result r = emit_subpath_segments(ps, &path.subs[i], stroke, sw);
            if (YETTY_IS_ERR(r)) {
                yetty_ysvg_path_destroy(&path);
                return r;
            }
        }
    }
    yetty_ysvg_path_destroy(&path);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Text
 *
 * We flatten <text> + <tspan> children into a single TEXT_DRAWABLE_LIST per node
 * for now — that's sufficient for the common SVG-tiny case where tspans
 * are used for inline styling. A future pass can split into multiple
 * spans when colours / anchors differ between tspans.
 *===========================================================================*/

static int collect_text(const struct yetty_ysvg_node *n, char *buf, size_t cap, size_t *len)
{
    if (n->text && n->text_len > 0) {
        if (*len + n->text_len + 1 > cap) {
            return -1;
        }
        memcpy(buf + *len, n->text, n->text_len);
        *len += n->text_len;
        buf[*len] = '\0';
    }
    for (struct yetty_ysvg_node *c = n->first_child; c; c = c->next_sibling) {
        if (c->elem == YETTY_YSVG_ELEM_TSPAN || c->elem == YETTY_YSVG_ELEM_TEXT) {
            if (collect_text(c, buf, cap, len) < 0) {
                return -1;
            }
        }
    }
    return 0;
}

static struct yetty_ycore_void_result emit_text(struct ysvg_paint_state *ps,
                                                const struct yetty_ysvg_node *n)
{
    char buf[1024];
    size_t blen = 0;
    if (collect_text(n, buf, sizeof(buf), &blen) < 0 || blen == 0) {
        return YETTY_OK_VOID();
    }
    float x = attr_float(n, YETTY_YSVG_ATTR_X, 0.0f);
    float y = attr_float(n, YETTY_YSVG_ATTR_Y, ps->text_y);
    float tx, ty;
    yetty_ysvg_xform_point(&ps->ctm, x, y, &tx, &ty);

    uint32_t color = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
    if (color == 0) {
        color = 0xFF000000u; /* black fallback */
    }
    float scale = xform_avg_scale(&ps->ctm);
    float font_size = ps->style.font_size * scale;

    /* text-anchor: shift x by an approximate text width (we don't have
     * glyph metrics here — use 0.55 * font_size per char as in ymarkdown). */
    if (ps->style.text_anchor != YETTY_YSVG_ANCHOR_START) {
        float approx_w = (float)blen * font_size * 0.55f;
        if (ps->style.text_anchor == YETTY_YSVG_ANCHOR_MIDDLE) {
            tx -= approx_w * 0.5f;
        } else {
            tx -= approx_w;
        }
    }

    struct yetty_ycore_buffer text = {
        .data = (uint8_t *)buf,
        .size = blen,
        .capacity = blen,
    };
    struct yetty_ycore_void_result tr = yetty_ydraw_drawable_list_add_text(
        ps->ctx->buf, tx, ty, &text, font_size, color, 0, -1, 0.0f);
    if (YETTY_IS_ERR(tr)) {
        return tr;
    }
    ps->text_y = y + ps->style.font_size * ps->ctx->line_spacing;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Recursive walk
 *===========================================================================*/

static struct yetty_ycore_void_result walk(struct ysvg_paint_state *parent,
                                           const struct yetty_ysvg_node *n);

/* Extend the active anchor's click region by a pixel-space rectangle. */
static void link_extend(struct ysvg_paint_state *ps, float min_x, float min_y, float max_x,
                        float max_y)
{
    if (ps->active_link < 0 || (size_t)ps->active_link >= ps->ctx->link_count) {
        return;
    }
    struct yetty_ysvg_link_region *region = &ps->ctx->links[ps->active_link];
    if (min_x < region->min_x) {
        region->min_x = min_x;
    }
    if (min_y < region->min_y) {
        region->min_y = min_y;
    }
    if (max_x > region->max_x) {
        region->max_x = max_x;
    }
    if (max_y > region->max_y) {
        region->max_y = max_y;
    }
}

/* Approximate a geometry element's bounds in USER space from its
 * attributes. Returns 0 for non-geometry nodes. For points/path data the
 * scan takes every number as a coordinate — relative path commands make
 * this an over-approximation, which is fine for a click region. */
static int node_user_bounds(const struct yetty_ysvg_node *n, float *out_x0, float *out_y0,
                            float *out_x1, float *out_y1)
{
    switch (n->elem) {
    case YETTY_YSVG_ELEM_RECT:
    case YETTY_YSVG_ELEM_IMAGE: {
        float x = attr_float(n, YETTY_YSVG_ATTR_X, 0.0f);
        float y = attr_float(n, YETTY_YSVG_ATTR_Y, 0.0f);
        float w = attr_float(n, YETTY_YSVG_ATTR_WIDTH, 0.0f);
        float h = attr_float(n, YETTY_YSVG_ATTR_HEIGHT, 0.0f);
        if (w <= 0.0f || h <= 0.0f) {
            return 0;
        }
        *out_x0 = x;
        *out_y0 = y;
        *out_x1 = x + w;
        *out_y1 = y + h;
        return 1;
    }
    case YETTY_YSVG_ELEM_CIRCLE: {
        float cx = attr_float(n, YETTY_YSVG_ATTR_CX, 0.0f);
        float cy = attr_float(n, YETTY_YSVG_ATTR_CY, 0.0f);
        float radius = attr_float(n, YETTY_YSVG_ATTR_R, 0.0f);
        if (radius <= 0.0f) {
            return 0;
        }
        *out_x0 = cx - radius;
        *out_y0 = cy - radius;
        *out_x1 = cx + radius;
        *out_y1 = cy + radius;
        return 1;
    }
    case YETTY_YSVG_ELEM_ELLIPSE: {
        float cx = attr_float(n, YETTY_YSVG_ATTR_CX, 0.0f);
        float cy = attr_float(n, YETTY_YSVG_ATTR_CY, 0.0f);
        float rx = attr_float(n, YETTY_YSVG_ATTR_RX, 0.0f);
        float ry = attr_float(n, YETTY_YSVG_ATTR_RY, 0.0f);
        if (rx <= 0.0f || ry <= 0.0f) {
            return 0;
        }
        *out_x0 = cx - rx;
        *out_y0 = cy - ry;
        *out_x1 = cx + rx;
        *out_y1 = cy + ry;
        return 1;
    }
    case YETTY_YSVG_ELEM_LINE: {
        float x1 = attr_float(n, YETTY_YSVG_ATTR_X1, 0.0f);
        float y1 = attr_float(n, YETTY_YSVG_ATTR_Y1, 0.0f);
        float x2 = attr_float(n, YETTY_YSVG_ATTR_X2, 0.0f);
        float y2 = attr_float(n, YETTY_YSVG_ATTR_Y2, 0.0f);
        *out_x0 = x1 < x2 ? x1 : x2;
        *out_y0 = y1 < y2 ? y1 : y2;
        *out_x1 = x1 > x2 ? x1 : x2;
        *out_y1 = y1 > y2 ? y1 : y2;
        return 1;
    }
    case YETTY_YSVG_ELEM_POLYLINE:
    case YETTY_YSVG_ELEM_POLYGON:
    case YETTY_YSVG_ELEM_PATH: {
        const struct yetty_ysvg_attr *data = yetty_ysvg_attr_find(
            n, n->elem == YETTY_YSVG_ELEM_PATH ? YETTY_YSVG_ATTR_D : YETTY_YSVG_ATTR_POINTS);
        if (!data || data->value_len == 0) {
            return 0;
        }
        float min_x = INFINITY, min_y = INFINITY;
        float max_x = -INFINITY, max_y = -INFINITY;
        int axis_x = 1, seen_pair = 0;
        const char *cursor = data->value;
        const char *end = data->value + data->value_len;
        while (cursor < end) {
            char *next = NULL;
            float value = strtof(cursor, &next);
            if (next == cursor) {
                cursor++;
                continue;
            }
            if (axis_x) {
                if (value < min_x) {
                    min_x = value;
                }
                if (value > max_x) {
                    max_x = value;
                }
            } else {
                if (value < min_y) {
                    min_y = value;
                }
                if (value > max_y) {
                    max_y = value;
                }
                seen_pair = 1;
            }
            axis_x = !axis_x;
            cursor = next;
        }
        if (!seen_pair) {
            return 0;
        }
        *out_x0 = min_x;
        *out_y0 = min_y;
        *out_x1 = max_x;
        *out_y1 = max_y;
        return 1;
    }
    default:
        return 0;
    }
}

/* Paint a <use> element: instantiate the referenced subtree here, with the
 * <use>'s cascaded style as the inherited context and its x/y as an extra
 * translation. The <use>'s own transform is already folded into ps->ctm by
 * walk() before we get here. */
/* <image href="..."> — a raster embedded in the scene. ysvg has no
 * network or codec access, so the pixels come from the embedder's
 * resolver (browser: href resolved against the document base, fetched
 * through the loader, decoded there). The placement rect maps through
 * the CTM like <rect>; rotation/skew degrade to the axis-aligned frame
 * (complex records carry an axis-aligned bounds box). */
static struct yetty_ycore_void_result emit_image(struct ysvg_paint_state *ps,
                                                 const struct yetty_ysvg_node *n)
{
    if (!ps->ctx->image_resolver.resolve) {
        ydebug("ysvg: <image> skipped — no resolver supplied by the embedder");
        return YETTY_OK_VOID();
    }
    float x = attr_float(n, YETTY_YSVG_ATTR_X, 0.0f);
    float y = attr_float(n, YETTY_YSVG_ATTR_Y, 0.0f);
    float w = attr_float(n, YETTY_YSVG_ATTR_WIDTH, 0.0f);
    float h = attr_float(n, YETTY_YSVG_ATTR_HEIGHT, 0.0f);
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ysvg_attr *href = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_HREF);
    if (!href) {
        href = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_XLINK_HREF);
    }
    if (!href || href->value_len == 0) {
        return YETTY_OK_VOID();
    }
    char href_buf[2048];
    if (href->value_len >= sizeof(href_buf)) {
        ywarn("ysvg: <image> href longer than %zu bytes — skipped", sizeof(href_buf) - 1);
        return YETTY_OK_VOID();
    }
    memcpy(href_buf, href->value, href->value_len);
    href_buf[href->value_len] = '\0';

    const uint32_t *pixels = NULL;
    int pixel_w = 0, pixel_h = 0;
    if (!ps->ctx->image_resolver.resolve(ps->ctx->image_resolver.userdata, href_buf, &pixels,
                                         &pixel_w, &pixel_h) ||
        !pixels || pixel_w <= 0 || pixel_h <= 0) {
        ydebug("ysvg: <image> resolver could not serve '%.120s'", href_buf);
        return YETTY_OK_VOID();
    }

    /* Map the placement rect corners through the CTM; the complex
	 * bounds are the axis-aligned frame of the result. */
    float corner_x0, corner_y0, corner_x1, corner_y1;
    yetty_ysvg_xform_point(&ps->ctm, x, y, &corner_x0, &corner_y0);
    yetty_ysvg_xform_point(&ps->ctm, x + w, y + h, &corner_x1, &corner_y1);
    float bounds_x = corner_x0 < corner_x1 ? corner_x0 : corner_x1;
    float bounds_y = corner_y0 < corner_y1 ? corner_y0 : corner_y1;
    float bounds_w = fabsf(corner_x1 - corner_x0);
    float bounds_h = fabsf(corner_y1 - corner_y0);
    if (bounds_w <= 0.0f || bounds_h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    struct yetty_yimage_uniforms uniforms = {
        .bounds_x = bounds_x,
        .bounds_y = bounds_y,
        .bounds_w = bounds_w,
        .bounds_h = bounds_h,
        .image_w = (uint32_t)pixel_w,
        .image_h = (uint32_t)pixel_h,
    };
    struct yetty_yimage_buffers buffers = {
        .pixels = pixels,
        .pixels_len = (size_t)pixel_w * (size_t)pixel_h,
    };
    size_t need = yetty_yimage_uniforms_serialized_size(&uniforms, &buffers);
    uint8_t *record = malloc(need);
    if (!record) {
        return YETTY_ERR(yetty_ycore_void, "ysvg: <image> record oom");
    }
    struct yetty_ycore_size_result serialize_res =
        yetty_yimage_uniforms_serialize(&uniforms, &buffers, record, need);
    if (YETTY_IS_ERR(serialize_res)) {
        free(record);
        return YETTY_ERR(yetty_ycore_void, "ysvg: <image> serialize", serialize_res);
    }
    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(ps->ctx->buf, record, need);
    free(record);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "ysvg: <image> add_prim");
    /* Scene bounds stay viewBox-driven (ctx init) — same as every other
	 * element; an out-of-viewBox image overflows like any shape would. */
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_use(struct ysvg_paint_state *ps,
                                               const struct yetty_ysvg_node *n)
{
    if (ps->use_depth >= YSVG_USE_DEPTH_MAX) {
        ywarn("ysvg: <use> nesting exceeds %d levels — skipping to break a cycle",
              YSVG_USE_DEPTH_MAX);
        return YETTY_OK_VOID();
    }
    const struct yetty_ysvg_attr *href = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_XLINK_HREF);
    if (!href) {
        href = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_HREF);
    }
    if (!href || href->value_len < 2 || href->value[0] != '#') {
        ywarn("ysvg: <use> without a local #id reference — skipped");
        return YETTY_OK_VOID();
    }
    const struct yetty_ysvg_node *target =
        find_node_by_id(ps->doc->root, href->value + 1, href->value_len - 1);
    if (!target || target == n) {
        ywarn("ysvg: <use> references missing id '%.*s'", (int)(href->value_len - 1),
              href->value + 1);
        return YETTY_OK_VOID();
    }

    struct ysvg_paint_state use_ctx = *ps;
    use_ctx.use_depth = ps->use_depth + 1;
    float ux = attr_float(n, YETTY_YSVG_ATTR_X, 0.0f);
    float uy = attr_float(n, YETTY_YSVG_ATTR_Y, 0.0f);
    if (ux != 0.0f || uy != 0.0f) {
        struct yetty_ysvg_xform offset;
        yetty_ysvg_xform_identity(&offset);
        offset.e = ux;
        offset.f = uy;
        struct yetty_ysvg_xform composed;
        yetty_ysvg_xform_multiply(&composed, &use_ctx.ctm, &offset);
        use_ctx.ctm = composed;
    }
    return walk(&use_ctx, target);
}

/* soft_fail — one element failed to emit (bad path data, unresolvable image,
 * …). Per the SVG error-processing model a broken element must not blank the
 * whole document: log it, release the error's cause chain, and let the walk
 * carry on with the rest of the tree. */
static void soft_fail(struct yetty_ycore_void_result result, const char *what)
{
    ywarn("ysvg: skipping <%s>: %s", what, result.error.msg ? result.error.msg : "(emit failed)");
    yetty_ycore_error_destroy(result.error);
}

static struct yetty_ycore_void_result walk(struct ysvg_paint_state *parent,
                                           const struct yetty_ysvg_node *n)
{
    if (!n) {
        return YETTY_OK_VOID();
    }

    struct ysvg_paint_state ps = *parent;
    yetty_ysvg_style_resolve(&ps.style, &parent->style, parent->doc, n);
    /* Bake any url(#gradient) fill/stroke down to a representative solid so the
     * shape emitters (which only understand colours) render it. */
    resolve_url_paint(parent->doc, &ps.style.fill);
    resolve_url_paint(parent->doc, &ps.style.stroke);
    compose_node_transform(&ps.ctm, &parent->ctm, n);
    ps.text_y = parent->text_y;

    if (!ps.style.display || !ps.style.visibility) {
        return YETTY_OK_VOID();
    }

    /* Anchor click regions: any geometry under an <a> widens that
	 * anchor's region by its CTM-mapped bounds (axis-aligned frame). */
    if (ps.active_link >= 0) {
        float user_x0, user_y0, user_x1, user_y1;
        if (node_user_bounds(n, &user_x0, &user_y0, &user_x1, &user_y1)) {
            float corner_x[4], corner_y[4];
            yetty_ysvg_xform_point(&ps.ctm, user_x0, user_y0, &corner_x[0], &corner_y[0]);
            yetty_ysvg_xform_point(&ps.ctm, user_x1, user_y0, &corner_x[1], &corner_y[1]);
            yetty_ysvg_xform_point(&ps.ctm, user_x0, user_y1, &corner_x[2], &corner_y[2]);
            yetty_ysvg_xform_point(&ps.ctm, user_x1, user_y1, &corner_x[3], &corner_y[3]);
            float px_min_x = corner_x[0], px_max_x = corner_x[0];
            float px_min_y = corner_y[0], px_max_y = corner_y[0];
            for (int corner = 1; corner < 4; corner++) {
                if (corner_x[corner] < px_min_x) {
                    px_min_x = corner_x[corner];
                }
                if (corner_x[corner] > px_max_x) {
                    px_max_x = corner_x[corner];
                }
                if (corner_y[corner] < px_min_y) {
                    px_min_y = corner_y[corner];
                }
                if (corner_y[corner] > px_max_y) {
                    px_max_y = corner_y[corner];
                }
            }
            link_extend(&ps, px_min_x, px_min_y, px_max_x, px_max_y);
        }
    }

    switch (n->elem) {
    case YETTY_YSVG_ELEM_RECT: {
        struct yetty_ycore_void_result r = emit_rect(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "rect");
        }
        break;
    }
    case YETTY_YSVG_ELEM_CIRCLE: {
        struct yetty_ycore_void_result r = emit_circle(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "circle");
        }
        break;
    }
    case YETTY_YSVG_ELEM_ELLIPSE: {
        struct yetty_ycore_void_result r = emit_ellipse(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "ellipse");
        }
        break;
    }
    case YETTY_YSVG_ELEM_LINE: {
        struct yetty_ycore_void_result r = emit_line(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "line");
        }
        break;
    }
    case YETTY_YSVG_ELEM_POLYLINE: {
        struct yetty_ycore_void_result r = emit_points_shape(&ps, n, 0);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "polyline");
        }
        break;
    }
    case YETTY_YSVG_ELEM_POLYGON: {
        struct yetty_ycore_void_result r = emit_points_shape(&ps, n, 1);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "polygon");
        }
        break;
    }
    case YETTY_YSVG_ELEM_PATH: {
        struct yetty_ycore_void_result r = emit_path(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "path");
        }
        break;
    }
    case YETTY_YSVG_ELEM_TEXT: {
        struct yetty_ycore_void_result r = emit_text(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "text");
        }
        break;
    }
    case YETTY_YSVG_ELEM_USE: {
        /* <use> paints the referenced subtree itself; it has no renderable
         * children of its own, so return rather than recurse. */
        struct yetty_ycore_void_result r = emit_use(&ps, n);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "use");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YSVG_ELEM_IMAGE: {
        struct yetty_ycore_void_result image_res = emit_image(&ps, n);
        if (YETTY_IS_ERR(image_res)) {
            soft_fail(image_res, "image");
        }
        return YETTY_OK_VOID();
    }
    case YETTY_YSVG_ELEM_A: {
        /* Register a click region for this anchor and make it the
		 * innermost active one for the child recursion below. Only
		 * when the embedder asked for link collection. */
        struct yetty_ysvg_paint_ctx *paint_ctx = ps.ctx;
        if (paint_ctx->collect_link_regions) {
            const struct yetty_ysvg_attr *href = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_HREF);
            if (!href) {
                href = yetty_ysvg_attr_find(n, YETTY_YSVG_ATTR_XLINK_HREF);
            }
            if (href && href->value_len > 0) {
                if (paint_ctx->link_count == paint_ctx->link_cap) {
                    size_t new_cap = paint_ctx->link_cap ? paint_ctx->link_cap * 2 : 8;
                    struct yetty_ysvg_link_region *grown =
                        realloc(paint_ctx->links, new_cap * sizeof(*grown));
                    if (!grown) {
                        break; /* no region — subtree still renders */
                    }
                    paint_ctx->links = grown;
                    paint_ctx->link_cap = new_cap;
                }
                char *href_copy = malloc(href->value_len + 1);
                if (!href_copy) {
                    break;
                }
                memcpy(href_copy, href->value, href->value_len);
                href_copy[href->value_len] = '\0';
                struct yetty_ysvg_link_region *region = &paint_ctx->links[paint_ctx->link_count];
                region->href = href_copy;
                region->min_x = INFINITY;
                region->min_y = INFINITY;
                region->max_x = -INFINITY;
                region->max_y = -INFINITY;
                ps.active_link = (int)paint_ctx->link_count;
                paint_ctx->link_count++;
                ydebug("ysvg: <a> region %d href=%.60s", ps.active_link, href_copy);
            }
        }
        break; /* containers recurse below with ps.active_link set */
    }
    case YETTY_YSVG_ELEM_DEFS:
    case YETTY_YSVG_ELEM_TITLE:
    case YETTY_YSVG_ELEM_DESC:
    case YETTY_YSVG_ELEM_METADATA:
    case YETTY_YSVG_ELEM_LINEARGRADIENT:
    case YETTY_YSVG_ELEM_RADIALGRADIENT:
    case YETTY_YSVG_ELEM_STOP:
    case YETTY_YSVG_ELEM_FONT:
        return YETTY_OK_VOID(); /* not rendered */
    default:
        break; /* containers + unknown: fall through to recurse */
    }

    for (struct yetty_ysvg_node *c = n->first_child; c; c = c->next_sibling) {
        struct yetty_ycore_void_result r = walk(&ps, c);
        if (YETTY_IS_ERR(r)) {
            soft_fail(r, "subtree");
        }
    }
    parent->text_y = ps.text_y;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ysvg_paint(const struct yetty_ysvg_doc *doc,
                                                struct yetty_ysvg_paint_ctx *ctx)
{
    if (!doc || !doc->root) {
        return YETTY_ERR(yetty_ycore_void, "ysvg-paint: empty doc");
    }
    struct ysvg_paint_state root = {0};
    root.ctx = ctx;
    root.doc = doc;
    root.active_link = -1;
    /* Start from the viewBox → pixel transform (a uniform scale +
     * translate set up by ysvg.c). Walk recursion composes element
     * transforms on top of this. */
    if (ctx->user_to_pixel_scale > 0.0f) {
        root.ctm = ctx->root_ctm;
    } else {
        yetty_ysvg_xform_identity(&root.ctm);
    }
    yetty_ysvg_style_init_root(&root.style, ctx->default_font_size);
    root.text_y = 0.0f;
    return walk(&root, doc->root);
}
