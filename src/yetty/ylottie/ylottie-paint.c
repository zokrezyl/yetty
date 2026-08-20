/*
 * ylottie-paint.c — walk the layers and shape groups of a Lottie document at
 * one frame and emit ydraw primitives.
 *
 * Painting order: Lottie's layers[0] is the topmost layer (drawn last), so we
 * iterate the layer array back to front. Within a shape layer, items are
 * processed in document order; a group's transform (`tr`) and opacity wrap its
 * children, and the group's fill/stroke (with any inherited from an enclosing
 * group) decide how each geometry item paints.
 *
 * Geometry → SDF:
 *   ellipse  → SDF circle (rotation-invariant) / SDF ellipse (axis-aligned) /
 *              flattened polyline (rotated, non-circular)
 *   rect     → SDF box / rounded_box (axis-aligned) / 4-segment outline
 *   path/sh  → cubic-bezier flattened polyline
 *   polystar → analytic vertices → closed polyline
 *
 * The SDF set has no arbitrary-polygon fill, so a filled `sh`/polystar/rotated
 * shape is approximated by tracing its perimeter in the fill colour (same
 * compromise ysvg makes); ellipses and axis-aligned rects fill natively.
 *
 * Text layers (ty 5) emit MSDF TEXT_DRAWABLE_LIST drawable-list entries via add_text.
 */

#include "ylottie-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define YLOTTIE_PATH_TOLERANCE 0.5f
#define YLOTTIE_MAX_PARENT_DEPTH 16
#define YLOTTIE_MAX_GROUP_DEPTH 32

/* A resolved paint (fill or stroke) at a point in the shape tree. */
struct ylottie_paint {
    bool present;
    uint32_t color; /* ABGR, already alpha-combined */
    float width;    /* stroke width in user units; 0 for a fill */
};

struct ylottie_walk {
    const struct yetty_ylottie_paint_ctx *ctx;
};

/*=============================================================================
 * Small helpers
 *===========================================================================*/

static const char *str_field(const struct yetty_ylottie_json *obj, const char *key)
{
    const struct yetty_ylottie_json *v = yetty_ylottie_json_get(obj, key);
    if (v && v->type == YETTY_YLOTTIE_JSON_STRING) {
        return v->string;
    }
    return NULL;
}

static bool ty_is(const struct yetty_ylottie_json *item, const char *lit)
{
    const char *ty = str_field(item, "ty");
    return ty && strcmp(ty, lit) == 0;
}

static bool item_hidden(const struct yetty_ylottie_json *item)
{
    const struct yetty_ylottie_json *hd = yetty_ylottie_json_get(item, "hd");
    return hd && hd->type == YETTY_YLOTTIE_JSON_BOOL && hd->boolean;
}

/*=============================================================================
 * Segment emit
 *===========================================================================*/

static struct yetty_ycore_void_result emit_segment(struct yetty_ydraw_drawable_list *buf,
                                                   const struct yetty_ylottie_xform *m, float x0,
                                                   float y0, float x1, float y1, uint32_t color,
                                                   float width)
{
    float ax, ay, bx, by;
    yetty_ylottie_xform_point(m, x0, y0, &ax, &ay);
    yetty_ylottie_xform_point(m, x1, y1, &bx, &by);
    struct yetty_ysdf_segment seg = {.start_x = ax, .start_y = ay, .end_x = bx, .end_y = by};
    struct yetty_ycore_void_result r =
        yetty_ydraw_drawable_list_add_cmd_add_segment(buf, 0, 0, 0, color, width, &seg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: segment emit failed");
    return YETTY_OK_VOID();
}

/* Emit a polyline as either a stroked outline (when a stroke is present) or a
 * perimeter trace in the fill colour (the SDF-fill compromise). */
static struct yetty_ycore_void_result emit_polyline_shape(struct yetty_ydraw_drawable_list *buf,
                                                          const struct yetty_ylottie_xform *m,
                                                          const struct yetty_ylottie_polyline *pl,
                                                          const struct ylottie_paint *fill,
                                                          const struct ylottie_paint *stroke)
{
    if (pl->count < 2) {
        return YETTY_OK_VOID();
    }
    float scale = yetty_ylottie_xform_avg_scale(m);
    uint32_t color = 0;
    float width = 0.0f;
    if (stroke->present && stroke->color != 0) {
        color = stroke->color;
        width = stroke->width * scale;
    } else if (fill->present && fill->color != 0) {
        color = fill->color;
        width = scale; /* one user unit — perimeter trace standing in for fill */
    }
    if (color == 0) {
        return YETTY_OK_VOID();
    }
    if (width < 0.75f) {
        width = 0.75f;
    }
    for (size_t i = 1; i < pl->count; i++) {
        struct yetty_ycore_void_result r =
            emit_segment(buf, m, pl->points[i - 1].x, pl->points[i - 1].y, pl->points[i].x,
                         pl->points[i].y, color, width);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: polyline segment failed");
    }
    if (pl->closed) {
        struct yetty_ycore_void_result r =
            emit_segment(buf, m, pl->points[pl->count - 1].x, pl->points[pl->count - 1].y,
                         pl->points[0].x, pl->points[0].y, color, width);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: polyline close failed");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Paint resolution
 *===========================================================================*/

/* Read a fill ("fl") item into a paint. */
static struct ylottie_paint resolve_fill(const struct yetty_ylottie_json *item, float frame,
                                         float opacity)
{
    struct ylottie_paint p = {0};
    float o =
        yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(item, "o"), frame, 100.0f) * 0.01f;
    p.color = yetty_ylottie_color_eval(yetty_ylottie_json_get(item, "c"), frame, opacity * o);
    p.present = true;
    p.width = 0.0f;
    return p;
}

/* Read a stroke ("st") item into a paint. */
static struct ylottie_paint resolve_stroke(const struct yetty_ylottie_json *item, float frame,
                                           float opacity)
{
    struct ylottie_paint p = {0};
    float o =
        yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(item, "o"), frame, 100.0f) * 0.01f;
    p.color = yetty_ylottie_color_eval(yetty_ylottie_json_get(item, "c"), frame, opacity * o);
    p.width = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(item, "w"), frame, 1.0f);
    p.present = true;
    return p;
}

/* Approximate a gradient fill/stroke ("gf"/"gs") by its first colour stop.
 * The gradient colour array is a flat [t0,r0,g0,b0, t1,...] list. */
static struct ylottie_paint resolve_gradient(const struct yetty_ylottie_json *item, float frame,
                                             float opacity, bool is_stroke)
{
    struct ylottie_paint p = {0};
    float o =
        yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(item, "o"), frame, 100.0f) * 0.01f;
    const struct yetty_ylottie_json *g = yetty_ylottie_json_get(item, "g");
    const struct yetty_ylottie_json *gk = yetty_ylottie_json_get(g, "k");
    float stop[4] = {0, 0, 0, 0};
    yetty_ylottie_prop_eval(gk, frame, stop, 4);
    p.color = yetty_ylottie_rgba_to_abgr(stop[1], stop[2], stop[3], opacity * o);
    if (is_stroke) {
        p.width = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(item, "w"), frame, 1.0f);
    }
    p.present = true;
    return p;
}

/*=============================================================================
 * Geometry emitters
 *===========================================================================*/

static struct yetty_ycore_void_result emit_ellipse(struct ylottie_walk *w,
                                                   const struct yetty_ylottie_json *item,
                                                   const struct yetty_ylottie_xform *m, float frame,
                                                   const struct ylottie_paint *fill,
                                                   const struct ylottie_paint *stroke)
{
    float size[2] = {0, 0};
    float pos[2] = {0, 0};
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(item, "s"), frame, size, 2);
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(item, "p"), frame, pos, 2);
    float rx = size[0] * 0.5f, ry = size[1] * 0.5f;
    if (rx <= 0.0f || ry <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_drawable_list *buf = w->ctx->buf;
    float cx, cy;
    yetty_ylottie_xform_point(m, pos[0], pos[1], &cx, &cy);
    float sx = sqrtf(m->a * m->a + m->b * m->b);
    float sy = sqrtf(m->c * m->c + m->d * m->d);
    uint32_t fc = fill->present ? fill->color : 0;
    uint32_t sc = stroke->present ? stroke->color : 0;
    float sw = stroke->present ? stroke->width * yetty_ylottie_xform_avg_scale(m) : 0.0f;

    bool is_circle = fabsf(rx - ry) < 1e-3f && fabsf(sx - sy) < 1e-3f;
    if (is_circle) {
        struct yetty_ysdf_circle geom = {.center_x = cx, .center_y = cy, .radius = rx * sx};
        struct yetty_ycore_void_result r =
            yetty_ydraw_drawable_list_add_cmd_add_circle(buf, 0, 0, fc, sc, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: ellipse(circle) emit failed");
        return YETTY_OK_VOID();
    }
    if (yetty_ylottie_xform_axis_aligned(m)) {
        struct yetty_ysdf_ellipse geom = {
            .center_x = cx, .center_y = cy, .radius_x = rx * sx, .radius_y = ry * sy};
        struct yetty_ycore_void_result r =
            yetty_ydraw_drawable_list_add_cmd_add_ellipse(buf, 0, 0, fc, sc, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: ellipse emit failed");
        return YETTY_OK_VOID();
    }
    /* Rotated non-circular ellipse: sample a closed polyline in local space. */
    struct yetty_ylottie_polyline pl = {0};
    int segs = 48;
    for (int i = 0; i < segs; i++) {
        float a = (float)i / (float)segs * (float)(2.0 * M_PI);
        if (yetty_ylottie_polyline_push(&pl, pos[0] + rx * cosf(a), pos[1] + ry * sinf(a)) < 0) {
            yetty_ylottie_polyline_destroy(&pl);
            return YETTY_ERR(yetty_ycore_void, "ylottie: ellipse polyline OOM");
        }
    }
    pl.closed = true;
    struct yetty_ycore_void_result r = emit_polyline_shape(buf, m, &pl, fill, stroke);
    yetty_ylottie_polyline_destroy(&pl);
    return r;
}

static struct yetty_ycore_void_result emit_rect(struct ylottie_walk *w,
                                                const struct yetty_ylottie_json *item,
                                                const struct yetty_ylottie_xform *m, float frame,
                                                const struct ylottie_paint *fill,
                                                const struct ylottie_paint *stroke)
{
    float size[2] = {0, 0};
    float pos[2] = {0, 0};
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(item, "s"), frame, size, 2);
    yetty_ylottie_prop_eval(yetty_ylottie_json_get(item, "p"), frame, pos, 2);
    float radius = yetty_ylottie_prop_eval_scalar(yetty_ylottie_json_get(item, "r"), frame, 0.0f);
    float hw = size[0] * 0.5f, hh = size[1] * 0.5f;
    if (hw <= 0.0f || hh <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ydraw_drawable_list *buf = w->ctx->buf;
    uint32_t fc = fill->present ? fill->color : 0;
    uint32_t sc = stroke->present ? stroke->color : 0;
    float sw = stroke->present ? stroke->width * yetty_ylottie_xform_avg_scale(m) : 0.0f;

    if (yetty_ylottie_xform_axis_aligned(m)) {
        float cx, cy;
        yetty_ylottie_xform_point(m, pos[0], pos[1], &cx, &cy);
        float scx = fabsf(m->a), scy = fabsf(m->d);
        if (radius > 0.0f) {
            float rr = radius * scx;
            struct yetty_ysdf_rounded_box geom = {.center_x = cx,
                                                  .center_y = cy,
                                                  .half_width = hw * scx,
                                                  .half_height = hh * scy,
                                                  .radius_top_right = rr,
                                                  .radius_bottom_right = rr,
                                                  .radius_top_left = rr,
                                                  .radius_bottom_left = rr};
            struct yetty_ycore_void_result r =
                yetty_ydraw_drawable_list_add_cmd_add_rounded_box(buf, 0, 0, fc, sc, sw, &geom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: rounded-rect emit failed");
            return YETTY_OK_VOID();
        }
        struct yetty_ysdf_box geom = {.center_x = cx,
                                      .center_y = cy,
                                      .half_width = hw * scx,
                                      .half_height = hh * scy,
                                      .corner_radius = 0.0f};
        struct yetty_ycore_void_result r =
            yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, 0, fc, sc, sw, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: rect emit failed");
        return YETTY_OK_VOID();
    }
    /* Rotated rect: 4-segment outline (corner radius dropped). */
    struct yetty_ylottie_polyline pl = {0};
    float cx0 = pos[0], cy0 = pos[1];
    const float corners[4][2] = {
        {cx0 - hw, cy0 - hh}, {cx0 + hw, cy0 - hh}, {cx0 + hw, cy0 + hh}, {cx0 - hw, cy0 + hh}};
    for (int i = 0; i < 4; i++) {
        if (yetty_ylottie_polyline_push(&pl, corners[i][0], corners[i][1]) < 0) {
            yetty_ylottie_polyline_destroy(&pl);
            return YETTY_ERR(yetty_ycore_void, "ylottie: rect polyline OOM");
        }
    }
    pl.closed = true;
    struct yetty_ycore_void_result r = emit_polyline_shape(buf, m, &pl, fill, stroke);
    yetty_ylottie_polyline_destroy(&pl);
    return r;
}

static struct yetty_ycore_void_result emit_path(struct ylottie_walk *w,
                                                const struct yetty_ylottie_json *item,
                                                const struct yetty_ylottie_xform *m, float frame,
                                                const struct ylottie_paint *fill,
                                                const struct ylottie_paint *stroke)
{
    const struct yetty_ylottie_json *shape_value =
        yetty_ylottie_path_eval(yetty_ylottie_json_get(item, "ks"), frame);
    if (!shape_value) {
        return YETTY_OK_VOID();
    }
    float scale = yetty_ylottie_xform_avg_scale(m);
    float tol = YLOTTIE_PATH_TOLERANCE / (scale > 0.0f ? scale : 1.0f);
    if (tol < 0.05f) {
        tol = 0.05f;
    }
    struct yetty_ylottie_polyline pl = {0};
    if (!yetty_ylottie_bezier_flatten(&pl, shape_value, tol)) {
        return YETTY_ERR(yetty_ycore_void, "ylottie: bezier flatten OOM");
    }
    struct yetty_ycore_void_result r = emit_polyline_shape(w->ctx->buf, m, &pl, fill, stroke);
    yetty_ylottie_polyline_destroy(&pl);
    return r;
}

static struct yetty_ycore_void_result emit_polystar(struct ylottie_walk *w,
                                                    const struct yetty_ylottie_json *item,
                                                    const struct yetty_ylottie_xform *m,
                                                    float frame, const struct ylottie_paint *fill,
                                                    const struct ylottie_paint *stroke)
{
    struct yetty_ylottie_polyline pl = {0};
    if (!yetty_ylottie_polystar_build(&pl, item, frame)) {
        return YETTY_ERR(yetty_ycore_void, "ylottie: polystar OOM");
    }
    struct yetty_ycore_void_result r = emit_polyline_shape(w->ctx->buf, m, &pl, fill, stroke);
    yetty_ylottie_polyline_destroy(&pl);
    return r;
}

/*=============================================================================
 * Shape-tree walk
 *===========================================================================*/

static struct yetty_ycore_void_result process_items(struct ylottie_walk *w,
                                                    const struct yetty_ylottie_json *items,
                                                    const struct yetty_ylottie_xform *ctm,
                                                    float frame, float opacity,
                                                    struct ylottie_paint fill,
                                                    struct ylottie_paint stroke, int depth);

/* A group ("gr") wraps its items with its own transform (`tr`, always present
 * as one of the items) and opacity. */
static struct yetty_ycore_void_result process_group(struct ylottie_walk *w,
                                                    const struct yetty_ylottie_json *group,
                                                    const struct yetty_ylottie_xform *ctm,
                                                    float frame, float opacity,
                                                    struct ylottie_paint fill,
                                                    struct ylottie_paint stroke, int depth)
{
    const struct yetty_ylottie_json *it = yetty_ylottie_json_get(group, "it");
    if (!it || it->type != YETTY_YLOTTIE_JSON_ARRAY) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ylottie_json *tr = NULL;
    for (const struct yetty_ylottie_json *e = it->first_child; e; e = e->next_sibling) {
        if (ty_is(e, "tr")) {
            tr = e;
            break;
        }
    }
    struct yetty_ylottie_xform gm;
    yetty_ylottie_transform_build(tr, frame, &gm);
    struct yetty_ylottie_xform gctm;
    yetty_ylottie_xform_multiply(&gctm, ctm, &gm);
    float gopacity = opacity * yetty_ylottie_transform_opacity(tr, frame);
    return process_items(w, it, &gctm, frame, gopacity, fill, stroke, depth + 1);
}

static struct yetty_ycore_void_result process_items(struct ylottie_walk *w,
                                                    const struct yetty_ylottie_json *items,
                                                    const struct yetty_ylottie_xform *ctm,
                                                    float frame, float opacity,
                                                    struct ylottie_paint fill,
                                                    struct ylottie_paint stroke, int depth)
{
    if (depth > YLOTTIE_MAX_GROUP_DEPTH || !items) {
        return YETTY_OK_VOID();
    }
    /* Resolve this level's paints (last fill / last stroke win). */
    for (const struct yetty_ylottie_json *e = items->first_child; e; e = e->next_sibling) {
        if (item_hidden(e)) {
            continue;
        }
        if (ty_is(e, "fl")) {
            fill = resolve_fill(e, frame, opacity);
        } else if (ty_is(e, "st")) {
            stroke = resolve_stroke(e, frame, opacity);
        } else if (ty_is(e, "gf")) {
            fill = resolve_gradient(e, frame, opacity, false);
        } else if (ty_is(e, "gs")) {
            stroke = resolve_gradient(e, frame, opacity, true);
        }
    }
    /* Emit geometry / recurse into nested groups in document order. */
    for (const struct yetty_ylottie_json *e = items->first_child; e; e = e->next_sibling) {
        if (item_hidden(e)) {
            continue;
        }
        struct yetty_ycore_void_result r = YETTY_OK_VOID();
        if (ty_is(e, "gr")) {
            r = process_group(w, e, ctm, frame, opacity, fill, stroke, depth);
        } else if (ty_is(e, "el")) {
            r = emit_ellipse(w, e, ctm, frame, &fill, &stroke);
        } else if (ty_is(e, "rc")) {
            r = emit_rect(w, e, ctm, frame, &fill, &stroke);
        } else if (ty_is(e, "sh")) {
            r = emit_path(w, e, ctm, frame, &fill, &stroke);
        } else if (ty_is(e, "sr")) {
            r = emit_polystar(w, e, ctm, frame, &fill, &stroke);
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie: shape item emit failed");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Layers
 *===========================================================================*/

static const struct yetty_ylottie_json *find_layer_by_ind(const struct yetty_ylottie_json *layers,
                                                          double ind)
{
    if (!layers) {
        return NULL;
    }
    for (const struct yetty_ylottie_json *l = layers->first_child; l; l = l->next_sibling) {
        if (yetty_ylottie_json_num_key(l, "ind", -1.0) == ind) {
            return l;
        }
    }
    return NULL;
}

/* Composition-space transform of a layer: its own transform pre-multiplied by
 * the chain of its parents (by `ind`). Cycles / runaway depth are bounded. */
static void build_layer_ctm(const struct yetty_ylottie_json *layers,
                            const struct yetty_ylottie_json *layer, float frame, int depth,
                            struct yetty_ylottie_xform *out)
{
    struct yetty_ylottie_xform local;
    yetty_ylottie_transform_build(yetty_ylottie_json_get(layer, "ks"), frame, &local);
    const struct yetty_ylottie_json *parent = yetty_ylottie_json_get(layer, "parent");
    if (parent && parent->type == YETTY_YLOTTIE_JSON_NUMBER && depth < YLOTTIE_MAX_PARENT_DEPTH) {
        const struct yetty_ylottie_json *pl = find_layer_by_ind(layers, parent->number);
        if (pl && pl != layer) {
            struct yetty_ylottie_xform pm;
            build_layer_ctm(layers, pl, frame, depth + 1, &pm);
            yetty_ylottie_xform_multiply(out, &pm, &local);
            return;
        }
    }
    *out = local;
}

/* The text document active at `frame`: t.d.k is an array of {s,t}; pick the
 * keyframe whose time bounds the frame from below. */
static const struct yetty_ylottie_json *text_document(const struct yetty_ylottie_json *layer,
                                                      float frame)
{
    const struct yetty_ylottie_json *d =
        yetty_ylottie_json_get(yetty_ylottie_json_get(layer, "t"), "d");
    const struct yetty_ylottie_json *k = yetty_ylottie_json_get(d, "k");
    if (!k || k->type != YETTY_YLOTTIE_JSON_ARRAY || !k->first_child) {
        return NULL;
    }
    const struct yetty_ylottie_json *chosen = k->first_child;
    for (const struct yetty_ylottie_json *e = k->first_child; e; e = e->next_sibling) {
        if ((float)yetty_ylottie_json_num_key(e, "t", 0.0) <= frame) {
            chosen = e;
        }
    }
    return yetty_ylottie_json_get(chosen, "s");
}

static struct yetty_ycore_void_result emit_text_layer(struct ylottie_walk *w,
                                                      const struct yetty_ylottie_json *layer,
                                                      const struct yetty_ylottie_xform *ctm,
                                                      float frame, float opacity)
{
    const struct yetty_ylottie_json *doc = text_document(layer, frame);
    if (!doc) {
        return YETTY_OK_VOID();
    }
    const char *text = str_field(doc, "t");
    if (!text || !*text) {
        return YETTY_OK_VOID();
    }
    float font_size = (float)yetty_ylottie_json_num_key(doc, "s", w->ctx->default_font_size);
    if (font_size <= 0.0f) {
        font_size = w->ctx->default_font_size;
    }
    /* Fill colour `fc` is an [r,g,b] (0..1) array; default white. */
    const struct yetty_ylottie_json *fc = yetty_ylottie_json_get(doc, "fc");
    uint32_t color;
    if (fc && fc->type == YETTY_YLOTTIE_JSON_ARRAY) {
        float r = (float)yetty_ylottie_json_num_at(fc, 0, 1.0);
        float g = (float)yetty_ylottie_json_num_at(fc, 1, 1.0);
        float b = (float)yetty_ylottie_json_num_at(fc, 2, 1.0);
        color = yetty_ylottie_rgba_to_abgr(r, g, b, opacity);
    } else {
        color = yetty_ylottie_rgba_to_abgr(1.0f, 1.0f, 1.0f, opacity);
    }
    double justify = yetty_ylottie_json_num_key(doc, "j", 0.0);

    float scale = yetty_ylottie_xform_avg_scale(ctm);
    float pixel_size = font_size * scale;
    float line_h = pixel_size * 1.2f;

    /* Lottie breaks lines on '\r'. Emit each line as its own span, stacked. */
    float ox, oy;
    yetty_ylottie_xform_point(ctm, 0.0f, 0.0f, &ox, &oy);

    const char *cursor = text;
    float line_y = oy;
    while (*cursor) {
        const char *nl = cursor;
        while (*nl && *nl != '\r' && *nl != '\n') {
            nl++;
        }
        size_t len = (size_t)(nl - cursor);
        if (len > 0) {
            float line_x = ox;
            if (justify == 1.0) { /* right */
                line_x -= (float)len * pixel_size * 0.55f;
            } else if (justify == 2.0) { /* center */
                line_x -= (float)len * pixel_size * 0.55f * 0.5f;
            }
            struct yetty_ycore_buffer tb = {
                .data = (uint8_t *)(uintptr_t)cursor, .size = len, .capacity = len};
            struct yetty_ycore_void_result tr = yetty_ydraw_drawable_list_add_text(
                w->ctx->buf, line_x, line_y, &tb, pixel_size, color, 0, -1, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "ylottie: text span emit failed");
        }
        if (!*nl) {
            break;
        }
        cursor = nl + 1;
        line_y += line_h;
    }
    return YETTY_OK_VOID();
}

/* A solid-colour layer (ty 1): full-bleed rectangle of sw×sh in colour sc
 * (a "#rrggbb" string). */
static struct yetty_ycore_void_result emit_solid_layer(struct ylottie_walk *w,
                                                       const struct yetty_ylottie_json *layer,
                                                       const struct yetty_ylottie_xform *ctm,
                                                       float opacity)
{
    float sw = (float)yetty_ylottie_json_num_key(layer, "sw", 0.0);
    float sh = (float)yetty_ylottie_json_num_key(layer, "sh", 0.0);
    if (sw <= 0.0f || sh <= 0.0f) {
        return YETTY_OK_VOID();
    }
    const char *sc = str_field(layer, "sc");
    float r = 0, g = 0, b = 0;
    if (sc && sc[0] == '#' && strlen(sc) >= 7) {
        unsigned ri = 0, gi = 0, bi = 0;
        if (sscanf(sc + 1, "%02x%02x%02x", &ri, &gi, &bi) == 3) {
            r = (float)ri / 255.0f;
            g = (float)gi / 255.0f;
            b = (float)bi / 255.0f;
        }
    }
    uint32_t color = yetty_ylottie_rgba_to_abgr(r, g, b, opacity);
    float cx, cy;
    yetty_ylottie_xform_point(ctm, sw * 0.5f, sh * 0.5f, &cx, &cy);
    float scx = fabsf(ctm->a), scy = fabsf(ctm->d);
    struct yetty_ysdf_box geom = {.center_x = cx,
                                  .center_y = cy,
                                  .half_width = sw * 0.5f * scx,
                                  .half_height = sh * 0.5f * scy,
                                  .corner_radius = 0.0f};
    struct yetty_ycore_void_result rr =
        yetty_ydraw_drawable_list_add_cmd_add_box(w->ctx->buf, 0, 0, color, 0, 0.0f, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "ylottie: solid layer emit failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result walk_layer(struct ylottie_walk *w,
                                                 const struct yetty_ylottie_json *layer)
{
    if (item_hidden(layer)) {
        return YETTY_OK_VOID();
    }
    float frame = w->ctx->frame;
    /* Respect the layer's active range [ip, op]. */
    const struct yetty_ylottie_json *ip = yetty_ylottie_json_get(layer, "ip");
    const struct yetty_ylottie_json *op = yetty_ylottie_json_get(layer, "op");
    if (ip && ip->type == YETTY_YLOTTIE_JSON_NUMBER && frame < (float)ip->number) {
        return YETTY_OK_VOID();
    }
    if (op && op->type == YETTY_YLOTTIE_JSON_NUMBER && frame > (float)op->number) {
        return YETTY_OK_VOID();
    }

    struct yetty_ylottie_xform comp;
    build_layer_ctm(w->ctx->layers, layer, frame, 0, &comp);
    struct yetty_ylottie_xform ctm;
    yetty_ylottie_xform_multiply(&ctm, &w->ctx->root_ctm, &comp);
    float opacity = yetty_ylottie_transform_opacity(yetty_ylottie_json_get(layer, "ks"), frame);

    double ty = yetty_ylottie_json_num_key(layer, "ty", -1.0);
    if (ty == 4.0) { /* shape */
        const struct yetty_ylottie_json *shapes = yetty_ylottie_json_get(layer, "shapes");
        struct ylottie_paint no_fill = {0};
        struct ylottie_paint no_stroke = {0};
        return process_items(w, shapes, &ctm, frame, opacity, no_fill, no_stroke, 0);
    }
    if (ty == 5.0) { /* text */
        return emit_text_layer(w, layer, &ctm, frame, opacity);
    }
    if (ty == 1.0) { /* solid */
        return emit_solid_layer(w, layer, &ctm, opacity);
    }
    /* ty 0 (precomp), 2 (image), 3 (null): not rendered here. */
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ylottie_paint(const struct yetty_ylottie_paint_ctx *ctx)
{
    if (!ctx || !ctx->buf) {
        return YETTY_ERR(yetty_ycore_void, "ylottie-paint: missing context");
    }
    const struct yetty_ylottie_json *layers = ctx->layers;
    if (!layers || layers->type != YETTY_YLOTTIE_JSON_ARRAY) {
        return YETTY_OK_VOID(); /* nothing to draw */
    }
    struct ylottie_walk w = {.ctx = ctx};
    /* Back-to-front: layers[0] is topmost, so paint from the last entry up. */
    size_t n = layers->child_count;
    for (size_t i = n; i > 0; i--) {
        const struct yetty_ylottie_json *layer = yetty_ylottie_json_at(layers, i - 1);
        struct yetty_ycore_void_result r = walk_layer(&w, layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ylottie-paint: layer walk failed");
    }
    return YETTY_OK_VOID();
}
