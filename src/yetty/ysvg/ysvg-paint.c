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
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ycore/types.h>

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YSVG_PATH_TOLERANCE 0.5f

struct ysvg_paint_state {
    struct yetty_ysvg_paint_ctx *ctx;
    const struct yetty_ysvg_doc *doc; /* needed for CSS rule lookup */
    struct yetty_ysvg_xform ctm;
    struct yetty_ysvg_style style;
    /* y cursor for stacking text lines when no explicit y attribute. */
    float text_y;
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
        struct yetty_ycore_void_result q =
            yetty_ydraw_drawable_list_add_cmd_add_circle(ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, q, "ysvg: circle emit failed");
    } else {
        struct yetty_ysdf_ellipse geom = {
            .center_x = cx, .center_y = cy, .radius_x = r * sx, .radius_y = r * sy};
        struct yetty_ycore_void_result q =
            yetty_ydraw_drawable_list_add_cmd_add_ellipse(ps->ctx->buf, 0, 0, fill, stroke, sw, &geom);
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
    uint32_t stroke = resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
    uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
    float sw_scale = xform_avg_scale(&ps->ctm);
    uint32_t color = 0;
    float sw = 0.0f;
    if (stroke != 0) {
        color = stroke;
        sw = ps->style.stroke_width * sw_scale;
    } else if (fill != 0) {
        /* No real polygon-fill primitive in the SDF set — approximate a
         * filled polygon by tracing its perimeter in the fill colour. */
        color = fill;
        sw = sw_scale; /* one user unit, scaled to pixels */
    }
    if (color == 0) {
        yetty_ysvg_path_destroy(&path);
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < path.sub_count; i++) {
        struct yetty_ycore_void_result r = emit_subpath_segments(ps, &path.subs[i], color, sw);
        if (YETTY_IS_ERR(r)) {
            yetty_ysvg_path_destroy(&path);
            return r;
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
    uint32_t stroke = resolve_color(&ps->style.stroke, ps->style.opacity, ps->style.stroke_opacity);
    uint32_t fill = resolve_color(&ps->style.fill, ps->style.opacity, ps->style.fill_opacity);
    float sw_scale = xform_avg_scale(&ps->ctm);
    uint32_t color = 0;
    float sw = 0.0f;
    if (stroke != 0) {
        color = stroke;
        sw = ps->style.stroke_width * sw_scale;
    } else if (fill != 0) {
        color = fill;
        sw = sw_scale;
    }
    if (color == 0) {
        yetty_ysvg_path_destroy(&path);
        return YETTY_OK_VOID();
    }
    for (size_t i = 0; i < path.sub_count; i++) {
        struct yetty_ycore_void_result r = emit_subpath_segments(ps, &path.subs[i], color, sw);
        if (YETTY_IS_ERR(r)) {
            yetty_ysvg_path_destroy(&path);
            return r;
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
    struct yetty_ycore_void_result tr =
        yetty_ydraw_drawable_list_add_text(ps->ctx->buf, tx, ty, &text, font_size, color, 0, -1, 0.0f);
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
                                           const struct yetty_ysvg_node *n)
{
    if (!n) {
        return YETTY_OK_VOID();
    }

    struct ysvg_paint_state ps = *parent;
    yetty_ysvg_style_resolve(&ps.style, &parent->style, parent->doc, n);
    compose_node_transform(&ps.ctm, &parent->ctm, n);
    ps.text_y = parent->text_y;

    if (!ps.style.display || !ps.style.visibility) {
        return YETTY_OK_VOID();
    }

    switch (n->elem) {
    case YETTY_YSVG_ELEM_RECT: {
        struct yetty_ycore_void_result r = emit_rect(&ps, n);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_CIRCLE: {
        struct yetty_ycore_void_result r = emit_circle(&ps, n);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_ELLIPSE: {
        struct yetty_ycore_void_result r = emit_ellipse(&ps, n);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_LINE: {
        struct yetty_ycore_void_result r = emit_line(&ps, n);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_POLYLINE: {
        struct yetty_ycore_void_result r = emit_points_shape(&ps, n, 0);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_POLYGON: {
        struct yetty_ycore_void_result r = emit_points_shape(&ps, n, 1);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_PATH: {
        struct yetty_ycore_void_result r = emit_path(&ps, n);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
    }
    case YETTY_YSVG_ELEM_TEXT: {
        struct yetty_ycore_void_result r = emit_text(&ps, n);
        if (YETTY_IS_ERR(r)) {
            return r;
        }
        break;
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
            return r;
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
