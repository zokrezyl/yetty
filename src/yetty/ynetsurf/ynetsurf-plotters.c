/*
 * Plotter callbacks — translate NetSurf plot ops into ydraw primitives.
 *
 * Strategy:
 *   - clip()   : track CPU rect; AABB-cull every later prim against it.
 *   - rectangle: ysdf_box (rounded if pstyle wants).
 *   - disc     : ysdf_circle.
 *   - line     : ysdf_segment (stroke_width carried).
 *   - arc      : ysdf_arc.
 *   - polygon  : fan-triangulate -> ysdf_triangle (works for convex; CSS
 *                fills are mostly convex once libsvgtiny has done its thing).
 *   - path     : flatten cubic Beziers to short segments -> ysdf_segment;
 *                concatenate into closed runs and fan-triangulate fills
 *                the same way as polygon.
 *   - bitmap   : MVP — placeholder filled box at the bitmap's slot.
 *                A real impl needs a yimage complex atlas upload.
 *   - text     : TEXT_DRAWABLE_LIST drawable-list entry prim (font_id = -1 = canvas default).
 */

#include "ynetsurf-internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "netsurf/types.h"
#include "netsurf/bitmap.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ysdf/funcs.gen.h>

/* Translate a drawable-list append failure into the netsurf error domain. The
 * plotter entry points below have signatures dictated by netsurf's
 * plotter_table vtable, so they cannot return a yetty Result; an append failure
 * (out of memory growing the list) surfaces to netsurf as NSERROR_NOMEM instead
 * of being silently dropped. The yetty error chain has no consumer here, so it
 * is freed. */
static nserror ns_from_void_result(struct yetty_ycore_void_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
        return NSERROR_NOMEM;
    }
    return NSERROR_OK;
}

/* ----- color ------------------------------------------------------------
 * NetSurf colour is 0x00BBGGRR (R in low byte, no alpha). ydraw shader
 * unpacks as { R = packed & 0xff, G = (packed>>8)&0xff, B = (packed>>16)&0xff,
 * A = (packed>>24)&0xff } — same byte order — so we just keep the 24-bit
 * RGB layout and slot 0xFF into the alpha byte. */

static uint32_t to_rgba(colour c)
{
    if (c == NS_TRANSPARENT) {
        return 0;
    }
    return (c & 0x00ffffffu) | 0xff000000u;
}

static uint32_t fill_rgba(const plot_style_t *s)
{
    return s->fill_type == PLOT_OP_TYPE_NONE ? 0 : to_rgba(s->fill_colour);
}
static uint32_t stroke_rgba(const plot_style_t *s)
{
    return s->stroke_type == PLOT_OP_TYPE_NONE ? 0 : to_rgba(s->stroke_colour);
}
static float stroke_w(const plot_style_t *s)
{
    if (s->stroke_type == PLOT_OP_TYPE_NONE) {
        return 0;
    }
    float w = plot_style_fixed_to_float(s->stroke_width);
    return w < 1.0f ? 1.0f : w;
}

/* ----- clip cull --------------------------------------------------------- */

static bool aabb_visible(const struct yetty_ynetsurf *ns, int x0, int y0, int x1, int y1)
{
    if (x1 < ns->cur_clip.x0 || x0 > ns->cur_clip.x1) {
        return false;
    }
    if (y1 < ns->cur_clip.y0 || y0 > ns->cur_clip.y1) {
        return false;
    }
    return true;
}

/* ----- callbacks --------------------------------------------------------- */

static nserror p_clip(const struct redraw_context *ctx, const struct rect *r)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    ns->cur_clip = *r;
    return NSERROR_OK;
}

YETTY_EXTERNAL_CALLBACK
static nserror p_rectangle(const struct redraw_context *ctx, const plot_style_t *s,
                           const struct rect *r)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    if (!aabb_visible(ns, r->x0, r->y0, r->x1, r->y1)) {
        return NSERROR_OK;
    }

    float cx = (r->x0 + r->x1) * 0.5f;
    float cy = (r->y0 + r->y1) * 0.5f;
    float hw = (r->x1 - r->x0) * 0.5f;
    float hh = (r->y1 - r->y0) * 0.5f;

    struct yetty_ysdf_box box = {
        .center_x = cx,
        .center_y = cy,
        .half_width = hw,
        .half_height = hh,
        .corner_radius = 0,
    };
    return ns_from_void_result(yetty_ydraw_drawable_list_add_cmd_add_box(
        ns->cur_buf, 0, ns->z_counter++, fill_rgba(s), stroke_rgba(s), stroke_w(s), &box));
}

YETTY_EXTERNAL_CALLBACK
static nserror p_disc(const struct redraw_context *ctx, const plot_style_t *s, int x, int y,
                      int radius)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    if (!aabb_visible(ns, x - radius, y - radius, x + radius, y + radius)) {
        return NSERROR_OK;
    }

    struct yetty_ysdf_circle c = {
        .center_x = x,
        .center_y = y,
        .radius = radius,
    };
    return ns_from_void_result(yetty_ydraw_drawable_list_add_cmd_add_circle(
        ns->cur_buf, 0, ns->z_counter++, fill_rgba(s), stroke_rgba(s), stroke_w(s), &c));
}

YETTY_EXTERNAL_CALLBACK
static nserror p_line(const struct redraw_context *ctx, const plot_style_t *s, const struct rect *l)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    int xmin = l->x0 < l->x1 ? l->x0 : l->x1;
    int xmax = l->x0 > l->x1 ? l->x0 : l->x1;
    int ymin = l->y0 < l->y1 ? l->y0 : l->y1;
    int ymax = l->y0 > l->y1 ? l->y0 : l->y1;
    if (!aabb_visible(ns, xmin, ymin, xmax, ymax)) {
        return NSERROR_OK;
    }

    struct yetty_ysdf_segment seg = {
        .start_x = l->x0,
        .start_y = l->y0,
        .end_x = l->x1,
        .end_y = l->y1,
    };
    uint32_t col = stroke_rgba(s);
    if (col == 0) {
        col = to_rgba(s->stroke_colour);
    }
    return ns_from_void_result(yetty_ydraw_drawable_list_add_cmd_add_segment(
        ns->cur_buf, 0, ns->z_counter++, 0, col, stroke_w(s), &seg));
}

YETTY_EXTERNAL_CALLBACK
static nserror p_arc(const struct redraw_context *ctx, const plot_style_t *s, int x, int y,
                     int radius, int angle1, int angle2)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    if (!aabb_visible(ns, x - radius, y - radius, x + radius, y + radius)) {
        return NSERROR_OK;
    }

    float a0 = angle1 * (float)M_PI / 180.0f;
    float a1 = angle2 * (float)M_PI / 180.0f;
    float ac = (a0 + a1) * 0.5f;
    float ah = (a1 - a0) * 0.5f;

    struct yetty_ysdf_arc arc = {
        .center_x = x,
        .center_y = y,
        .aperture_x = sinf(ah),
        .aperture_y = cosf(ah),
        .radius = radius,
        .thickness = stroke_w(s),
    };
    (void)ac; /* arc orientation is implicit in aperture vector */
    return ns_from_void_result(yetty_ydraw_drawable_list_add_cmd_add_arc(
        ns->cur_buf, 0, ns->z_counter++, 0, stroke_rgba(s), stroke_w(s), &arc));
}

/* fan-triangulate around vertex[0]. Convex assumption — CSS borders and
 * libsvgtiny output are convex once flattened; complex concave shapes
 * lose interior detail, an acceptable MVP trade-off. */
static struct yetty_ycore_void_result fan_triangulate(struct yetty_ynetsurf *ns, const float *xs,
                                                      const float *ys, unsigned n, uint32_t fill,
                                                      uint32_t stroke, float sw)
{
    if (n < 3) {
        return YETTY_OK_VOID();
    }
    for (unsigned i = 1; i + 1 < n; i++) {
        struct yetty_ysdf_triangle t = {
            .vertex_a_x = xs[0],
            .vertex_a_y = ys[0],
            .vertex_b_x = xs[i],
            .vertex_b_y = ys[i],
            .vertex_c_x = xs[i + 1],
            .vertex_c_y = ys[i + 1],
        };
        struct yetty_ycore_void_result add_result = yetty_ydraw_drawable_list_add_cmd_add_triangle(
            ns->cur_buf, 0, ns->z_counter++, fill, stroke, sw, &t);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "fan_triangulate: add triangle");
    }
    return YETTY_OK_VOID();
}

YETTY_EXTERNAL_CALLBACK
static nserror p_polygon(const struct redraw_context *ctx, const plot_style_t *s, const int *p,
                         unsigned int n)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    if (n < 3) {
        return NSERROR_OK;
    }

    float *xs = malloc(sizeof(float) * n);
    float *ys = malloc(sizeof(float) * n);
    if (xs == NULL || ys == NULL) {
        free(xs);
        free(ys);
        return NSERROR_NOMEM;
    }

    int xmin = p[0], xmax = p[0], ymin = p[1], ymax = p[1];
    for (unsigned i = 0; i < n; i++) {
        xs[i] = (float)p[i * 2];
        ys[i] = (float)p[i * 2 + 1];
        if (p[i * 2] < xmin) {
            xmin = p[i * 2];
        }
        if (p[i * 2] > xmax) {
            xmax = p[i * 2];
        }
        if (p[i * 2 + 1] < ymin) {
            ymin = p[i * 2 + 1];
        }
        if (p[i * 2 + 1] > ymax) {
            ymax = p[i * 2 + 1];
        }
    }
    struct yetty_ycore_void_result fan_result = YETTY_OK_VOID();
    if (aabb_visible(ns, xmin, ymin, xmax, ymax)) {
        fan_result = fan_triangulate(ns, xs, ys, n, fill_rgba(s), stroke_rgba(s), stroke_w(s));
    }
    free(xs);
    free(ys);
    return ns_from_void_result(fan_result);
}

/* ----- path: cubic-Bezier flattening -> segments ------------------------- */

#define BEZIER_STEPS 16

static void apply_xform(const float t[6], float x, float y, float *ox, float *oy)
{
    if (t == NULL) {
        *ox = x;
        *oy = y;
        return;
    }
    *ox = t[0] * x + t[2] * y + t[4];
    *oy = t[1] * x + t[3] * y + t[5];
}

YETTY_EXTERNAL_CALLBACK
static nserror p_path(const struct redraw_context *ctx, const plot_style_t *s, const float *p,
                      unsigned int n, const float t[6])
{
    struct yetty_ynetsurf *ns = ctx->priv;

    /* Walk the command stream; collect a flat polyline; on CLOSE or end,
	 * fan-triangulate (if filled) and emit perimeter segments (if
	 * stroked). */
    float *xs = NULL, *ys = NULL;
    unsigned cap = 0, cnt = 0;
#define PUSH(x_, y_)                                                                               \
    do {                                                                                           \
        if (cnt == cap) {                                                                          \
            cap = cap ? cap * 2 : 64;                                                              \
            xs = realloc(xs, cap * sizeof(float));                                                 \
            ys = realloc(ys, cap * sizeof(float));                                                 \
        }                                                                                          \
        xs[cnt] = (x_);                                                                            \
        ys[cnt] = (y_);                                                                            \
        cnt++;                                                                                     \
    } while (0)

    float cx = 0, cy = 0; /* current position */

    unsigned i = 0;
    while (i < n) {
        unsigned cmd = (unsigned)p[i++];
        if (cmd == PLOTTER_PATH_MOVE) {
            float tx, ty;
            apply_xform(t, p[i], p[i + 1], &tx, &ty);
            i += 2;
            cx = tx;
            cy = ty;
            PUSH(cx, cy);
        } else if (cmd == PLOTTER_PATH_LINE) {
            float tx, ty;
            apply_xform(t, p[i], p[i + 1], &tx, &ty);
            i += 2;
            cx = tx;
            cy = ty;
            PUSH(cx, cy);
        } else if (cmd == PLOTTER_PATH_BEZIER) {
            float c1x, c1y, c2x, c2y, ex, ey;
            apply_xform(t, p[i], p[i + 1], &c1x, &c1y);
            apply_xform(t, p[i + 2], p[i + 3], &c2x, &c2y);
            apply_xform(t, p[i + 4], p[i + 5], &ex, &ey);
            i += 6;
            float x0 = cx, y0 = cy;
            for (int k = 1; k <= BEZIER_STEPS; k++) {
                float u = (float)k / BEZIER_STEPS;
                float v = 1.0f - u;
                float bx =
                    v * v * v * x0 + 3 * v * v * u * c1x + 3 * v * u * u * c2x + u * u * u * ex;
                float by =
                    v * v * v * y0 + 3 * v * v * u * c1y + 3 * v * u * u * c2y + u * u * u * ey;
                PUSH(bx, by);
            }
            cx = ex;
            cy = ey;
        } else if (cmd == PLOTTER_PATH_CLOSE) {
            if (cnt > 0) {
                PUSH(xs[0], ys[0]);
            }
        } else {
            break; /* unknown -> bail */
        }
    }

    if (cnt >= 3) {
        float xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
        for (unsigned k = 1; k < cnt; k++) {
            if (xs[k] < xmin) {
                xmin = xs[k];
            }
            if (xs[k] > xmax) {
                xmax = xs[k];
            }
            if (ys[k] < ymin) {
                ymin = ys[k];
            }
            if (ys[k] > ymax) {
                ymax = ys[k];
            }
        }
        if (aabb_visible(ns, (int)xmin, (int)ymin, (int)xmax, (int)ymax)) {
            if (s->fill_type != PLOT_OP_TYPE_NONE) {
                struct yetty_ycore_void_result fan_result =
                    fan_triangulate(ns, xs, ys, cnt, fill_rgba(s), 0, 0);
                if (YETTY_IS_ERR(fan_result)) {
                    free(xs);
                    free(ys);
                    return ns_from_void_result(fan_result);
                }
            }
            if (s->stroke_type != PLOT_OP_TYPE_NONE) {
                uint32_t col = stroke_rgba(s);
                float sw = stroke_w(s);
                for (unsigned k = 1; k < cnt; k++) {
                    struct yetty_ysdf_segment seg = {
                        .start_x = xs[k - 1],
                        .start_y = ys[k - 1],
                        .end_x = xs[k],
                        .end_y = ys[k],
                    };
                    struct yetty_ycore_void_result seg_result =
                        yetty_ydraw_drawable_list_add_cmd_add_segment(
                            ns->cur_buf, 0, ns->z_counter++, 0, col, sw, &seg);
                    if (YETTY_IS_ERR(seg_result)) {
                        free(xs);
                        free(ys);
                        return ns_from_void_result(seg_result);
                    }
                }
            }
        }
    }

    free(xs);
    free(ys);
#undef PUSH
    return NSERROR_OK;
}

/* ----- bitmap: placeholder filled box --------------------------------- */

YETTY_EXTERNAL_CALLBACK
static nserror p_bitmap(const struct redraw_context *ctx, struct bitmap *bm, int x, int y,
                        int width, int height, colour bg, bitmap_flags_t flags)
{
    (void)flags;
    (void)bg;
    struct yetty_ynetsurf *ns = ctx->priv;
    if (!aabb_visible(ns, x, y, x + width, y + height)) {
        return NSERROR_OK;
    }

    struct yetty_ysdf_box box = {
        .center_x = x + width * 0.5f,
        .center_y = y + height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 0,
    };
    uint32_t fill = bm != NULL ? 0xc0c0c0ffu : 0xe0e0e0ffu;
    return ns_from_void_result(yetty_ydraw_drawable_list_add_cmd_add_box(
        ns->cur_buf, 0, ns->z_counter++, fill, 0, 0, &box));
}

/* ----- text: TEXT_DRAWABLE_LIST drawable-list entry prim ----------------------------------- */

YETTY_EXTERNAL_CALLBACK
static nserror p_text(const struct redraw_context *ctx, const plot_font_style_t *fstyle, int x,
                      int y, const char *text, size_t length)
{
    struct yetty_ynetsurf *ns = ctx->priv;
    if (length == 0) {
        return NSERROR_OK;
    }

    float pt = plot_style_fixed_to_float(fstyle->size);
    int approx_w = (int)(pt * 0.55f * length);
    if (!aabb_visible(ns, x, (int)(y - pt), x + approx_w, y + 4)) {
        return NSERROR_OK;
    }

    struct yetty_ycore_buffer txt = {
        .data = (uint8_t *)text,
        .capacity = length,
        .size = length,
    };
    return ns_from_void_result(yetty_ydraw_drawable_list_add_text(
        ns->cur_buf, (float)x, (float)y, &txt, pt, to_rgba(fstyle->foreground), ns->z_counter++,
        ns->default_font_id, 0.0f));
}

const struct plotter_table yetty_ynetsurf_plotters = {
    .clip = p_clip,
    .arc = p_arc,
    .disc = p_disc,
    .line = p_line,
    .rectangle = p_rectangle,
    .polygon = p_polygon,
    .path = p_path,
    .bitmap = p_bitmap,
    .text = p_text,
    .group_start = NULL,
    .group_end = NULL,
    .flush = NULL,
    .option_knockout = false,
};
