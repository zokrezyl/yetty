/*
 * renderer.c — graph IR → ypaint buffer.
 *
 * Emission is single-pass and z-order ascends as we go:
 *   clusters → edges → nodes → labels
 *
 * Every primitive is an MSD shape (yetty_ysdf_add_*) or an MSDF text run
 * (yetty_ypaint_core_buffer_add_text). The buffer's font_id = -1 means
 * "canvas default" — producers don't need to embed fonts themselves.
 */

#include <yetty/ydiagram/renderer.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-core/cmds.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

struct yetty_ydiagram_render_options yetty_ydiagram_default_render_options(void)
{
    struct yetty_ydiagram_render_options o = {
        .arrow_size       = 8.0f,
        .dash_length      = 6.0f,
        .dash_gap         = 4.0f,
        .background_color = 0,
    };
    return o;
}

/*=============================================================================
 * Per-frame state — `z` increments as primitives are emitted so the
 * fragment shader z-orders them correctly.
 *===========================================================================*/

struct render_state {
    struct yetty_ypaint_core_buffer            *buf;
    const struct yetty_ydiagram_render_options *opt;
    yetty_ydiagram_measure_text_fn              measure;
    void                                       *userdata;
    uint32_t                                    z;
};

static float measure_text(struct render_state *s, const char *text, float font_size)
{
    if (!text || !text[0]) return 0.0f;
    size_t n = strlen(text);
    if (s->measure) {
        return s->measure(text, n, font_size, s->userdata);
    }
    return font_size * 0.6f * (float)n;
}

/*=============================================================================
 * Shape emitters
 *===========================================================================*/

static void emit_rect(struct render_state *s, const struct yetty_ydiagram_node *n,
                      float corner_radius)
{
    struct yetty_ysdf_box geom = {
        .center_x      = n->x,
        .center_y      = n->y,
        .half_width    = n->width * 0.5f,
        .half_height   = n->height * 0.5f,
        .corner_radius = corner_radius,
    };
    yetty_ysdf_add_box(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                       n->style.stroke_width, &geom);
}

static void emit_circle(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    float r = (n->width < n->height ? n->width : n->height) * 0.5f;
    struct yetty_ysdf_circle geom = {.center_x = n->x, .center_y = n->y, .radius = r};
    yetty_ysdf_add_circle(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                          n->style.stroke_width, &geom);
}

static void emit_diamond(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    struct yetty_ysdf_rhombus geom = {
        .center_x    = n->x,
        .center_y    = n->y,
        .half_width  = n->width * 0.5f,
        .half_height = n->height * 0.5f,
    };
    yetty_ysdf_add_rhombus(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                           n->style.stroke_width, &geom);
}

static void emit_ellipse(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    struct yetty_ysdf_ellipse geom = {
        .center_x = n->x,
        .center_y = n->y,
        .radius_x = n->width * 0.5f,
        .radius_y = n->height * 0.5f,
    };
    yetty_ysdf_add_ellipse(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                           n->style.stroke_width, &geom);
}

static void emit_hexagon(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    float                     r    = (n->width < n->height ? n->width : n->height) * 0.5f;
    struct yetty_ysdf_hexagon geom = {.center_x = n->x, .center_y = n->y, .radius = r};
    yetty_ysdf_add_hexagon(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                           n->style.stroke_width, &geom);
}

static void emit_capsule(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    float r = n->height * 0.5f;
    struct yetty_ysdf_capsule geom = {
        .start_x = n->x - n->width * 0.5f + r,
        .start_y = n->y,
        .end_x   = n->x + n->width * 0.5f - r,
        .end_y   = n->y,
        .radius  = r,
    };
    yetty_ysdf_add_capsule(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                           n->style.stroke_width, &geom);
}

static void emit_cylinder(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    /* Top ellipse + side rect + bottom ellipse approximation. */
    float ellipse_h = n->height * 0.15f;
    float body_h    = n->height - ellipse_h;

    struct yetty_ysdf_box body = {
        .center_x      = n->x,
        .center_y      = n->y,
        .half_width    = n->width * 0.5f,
        .half_height   = body_h * 0.5f,
        .corner_radius = 0.0f,
    };
    yetty_ysdf_add_box(s->buf, s->z++, n->style.fill_color, 0, 0.0f, &body);

    struct yetty_ysdf_ellipse top = {
        .center_x = n->x,
        .center_y = n->y - body_h * 0.5f,
        .radius_x = n->width * 0.5f,
        .radius_y = ellipse_h,
    };
    yetty_ysdf_add_ellipse(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                           n->style.stroke_width, &top);

    struct yetty_ysdf_ellipse bottom = {
        .center_x = n->x,
        .center_y = n->y + body_h * 0.5f,
        .radius_x = n->width * 0.5f,
        .radius_y = ellipse_h,
    };
    yetty_ysdf_add_ellipse(s->buf, s->z++, n->style.fill_color, n->style.stroke_color,
                           n->style.stroke_width, &bottom);

    struct yetty_ysdf_segment l = {
        .start_x = n->x - n->width * 0.5f,
        .start_y = n->y - body_h * 0.5f,
        .end_x   = n->x - n->width * 0.5f,
        .end_y   = n->y + body_h * 0.5f,
    };
    yetty_ysdf_add_segment(s->buf, s->z++, 0, n->style.stroke_color, n->style.stroke_width, &l);
    struct yetty_ysdf_segment r = {
        .start_x = n->x + n->width * 0.5f,
        .start_y = n->y - body_h * 0.5f,
        .end_x   = n->x + n->width * 0.5f,
        .end_y   = n->y + body_h * 0.5f,
    };
    yetty_ysdf_add_segment(s->buf, s->z++, 0, n->style.stroke_color, n->style.stroke_width, &r);
}

static void emit_node_shape(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    switch (n->shape) {
    case YETTY_YDIAGRAM_SHAPE_RECTANGLE:
        emit_rect(s, n, 0.0f);
        break;
    case YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT:
        emit_rect(s, n, n->style.corner_radius);
        break;
    case YETTY_YDIAGRAM_SHAPE_CIRCLE:
    case YETTY_YDIAGRAM_SHAPE_DOUBLE_CIRCLE:
        emit_circle(s, n);
        break;
    case YETTY_YDIAGRAM_SHAPE_DIAMOND:
        emit_diamond(s, n);
        break;
    case YETTY_YDIAGRAM_SHAPE_ELLIPSE:
        emit_ellipse(s, n);
        break;
    case YETTY_YDIAGRAM_SHAPE_HEXAGON:
        emit_hexagon(s, n);
        break;
    case YETTY_YDIAGRAM_SHAPE_PARALLELOGRAM:
    case YETTY_YDIAGRAM_SHAPE_TRAPEZOID:
        /* No native SDF parallelogram/trapezoid — approximate with a box.
         * TODO: emit four segments tracing the actual quad. */
        emit_rect(s, n, 0.0f);
        break;
    case YETTY_YDIAGRAM_SHAPE_CYLINDER:
        emit_cylinder(s, n);
        break;
    case YETTY_YDIAGRAM_SHAPE_STADIUM:
        emit_capsule(s, n);
        break;
    }
}

/*=============================================================================
 * Edge geometry — attach points, arrowheads, dashed segments.
 *===========================================================================*/

static void edge_attach_point(const struct yetty_ydiagram_node *node, float toward_x,
                              float toward_y, float *out_x, float *out_y)
{
    float dx    = toward_x - node->x;
    float dy    = toward_y - node->y;
    float angle = atan2f(dy, dx);
    float hw    = node->width * 0.5f;
    float hh    = node->height * 0.5f;
    switch (node->shape) {
    case YETTY_YDIAGRAM_SHAPE_CIRCLE:
    case YETTY_YDIAGRAM_SHAPE_DOUBLE_CIRCLE: {
        float r = hw < hh ? hw : hh;
        *out_x  = node->x + r * cosf(angle);
        *out_y  = node->y + r * sinf(angle);
        return;
    }
    case YETTY_YDIAGRAM_SHAPE_DIAMOND: {
        float ax = fabsf(cosf(angle));
        float ay = fabsf(sinf(angle));
        float t  = 1.0f / (ax / hw + ay / hh);
        *out_x   = node->x + t * cosf(angle);
        *out_y   = node->y + t * sinf(angle);
        return;
    }
    default: {
        float tx = hw / (fabsf(cosf(angle)) + 1e-3f);
        float ty = hh / (fabsf(sinf(angle)) + 1e-3f);
        float t  = tx < ty ? tx : ty;
        *out_x   = node->x + t * cosf(angle);
        *out_y   = node->y + t * sinf(angle);
        return;
    }
    }
}

static void emit_dashed_line(struct render_state *s, float x0, float y0, float x1, float y1,
                             uint32_t color, float width)
{
    float dx     = x1 - x0;
    float dy     = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.01f) return;
    float ux  = dx / length;
    float uy  = dy / length;
    float dl  = s->opt->dash_length;
    float gl  = s->opt->dash_gap;
    float seg = dl + gl;
    uint32_t z = s->z;
    for (float pos = 0.0f; pos < length; pos += seg) {
        float end = pos + dl;
        if (end > length) end = length;
        struct yetty_ysdf_segment g = {
            .start_x = x0 + ux * pos,
            .start_y = y0 + uy * pos,
            .end_x   = x0 + ux * end,
            .end_y   = y0 + uy * end,
        };
        yetty_ysdf_add_segment(s->buf, z, 0, color, width, &g);
    }
    s->z = z + 1;
}

static void emit_arrowhead(struct render_state *s, float x, float y, float angle,
                           enum yetty_ydiagram_arrow_style style, uint32_t color, float size)
{
    if (style == YETTY_YDIAGRAM_ARROW_NONE) return;

    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    float spread = 0.4f;

    float bx = x - cos_a * size;
    float by = y - sin_a * size;
    float px = -sin_a * size * spread;
    float py = cos_a * size * spread;

    struct yetty_ysdf_triangle geom = {
        .vertex_a_x = x,
        .vertex_a_y = y,
        .vertex_b_x = bx + px,
        .vertex_b_y = by + py,
        .vertex_c_x = bx - px,
        .vertex_c_y = by - py,
    };
    yetty_ysdf_add_triangle(s->buf, s->z++, color, 0, 0.0f, &geom);
}

/*=============================================================================
 * Text emission. Builds a yetty_ycore_buffer view over the label bytes and
 * forwards to the canvas; font_id = -1 selects the canvas's default font.
 *===========================================================================*/

static void emit_text(struct render_state *s, float x, float y, const char *text,
                      float font_size, uint32_t color)
{
    if (!text || !text[0]) return;
    size_t n                       = strlen(text);
    struct yetty_ycore_buffer view = {
        .data     = (uint8_t *)(uintptr_t)text,
        .capacity = n,
        .size     = n,
    };
    (void)yetty_ypaint_core_buffer_add_text(s->buf, x, y, &view, font_size, color, s->z++, -1,
                                            0.0f);
}

static void emit_node_label(struct render_state *s, const struct yetty_ydiagram_node *n)
{
    if (!n->label || !n->label[0]) return;
    float tw = measure_text(s, n->label, n->style.font_size);
    float tx = n->x - tw * 0.5f;
    float ty = n->y + n->style.font_size / 3.0f;
    emit_text(s, tx, ty, n->label, n->style.font_size, n->style.text_color);
}

static void emit_edge_label(struct render_state *s, const struct yetty_ydiagram_edge *e)
{
    if (!e->label || !e->label[0]) return;
    float fs      = e->style.label_font_size;
    float tw      = measure_text(s, e->label, fs);
    float padding = 3.0f;

    struct yetty_ysdf_box bg = {
        .center_x      = e->label_position.x,
        .center_y      = e->label_position.y,
        .half_width    = tw * 0.5f + padding,
        .half_height   = fs * 0.5f + padding,
        .corner_radius = 2.0f,
    };
    yetty_ysdf_add_box(s->buf, s->z++, 0xFF1A1A2Eu, 0, 0.0f, &bg);
    emit_text(s, e->label_position.x - tw * 0.5f, e->label_position.y + fs / 3.0f, e->label, fs,
              e->style.label_color);
}

static void emit_edge(struct render_state *s, const struct yetty_ydiagram_graph *g,
                      const struct yetty_ydiagram_edge *e)
{
    /* Look up endpoints (we accept non-const graph view for find_node — see
     * the const cast in callers below). */
    struct yetty_ydiagram_node *src =
        yetty_ydiagram_graph_find_node((struct yetty_ydiagram_graph *)g, e->source_id);
    struct yetty_ydiagram_node *tgt =
        yetty_ydiagram_graph_find_node((struct yetty_ydiagram_graph *)g, e->target_id);
    if (!src || !tgt) return;

    float sx, sy, tx, ty;
    edge_attach_point(src, tgt->x, tgt->y, &sx, &sy);
    edge_attach_point(tgt, src->x, src->y, &tx, &ty);

    if (e->style.line_style == YETTY_YDIAGRAM_LINE_DASHED) {
        emit_dashed_line(s, sx, sy, tx, ty, e->style.stroke_color, e->style.stroke_width);
    } else {
        struct yetty_ysdf_segment seg = {
            .start_x = sx,
            .start_y = sy,
            .end_x   = tx,
            .end_y   = ty,
        };
        yetty_ysdf_add_segment(s->buf, s->z++, 0, e->style.stroke_color, e->style.stroke_width,
                               &seg);
    }

    if (e->style.target_arrow != YETTY_YDIAGRAM_ARROW_NONE) {
        float angle = atan2f(ty - sy, tx - sx);
        emit_arrowhead(s, tx, ty, angle, e->style.target_arrow, e->style.stroke_color,
                       s->opt->arrow_size);
    }
    if (e->style.source_arrow != YETTY_YDIAGRAM_ARROW_NONE) {
        float angle = atan2f(sy - ty, sx - tx);
        emit_arrowhead(s, sx, sy, angle, e->style.source_arrow, e->style.stroke_color,
                       s->opt->arrow_size);
    }

    emit_edge_label(s, e);
}

/*=============================================================================
 * Top-level
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydiagram_render(
    const struct yetty_ydiagram_graph *g, struct yetty_ypaint_core_buffer *buffer,
    const struct yetty_ydiagram_render_options *options,
    yetty_ydiagram_measure_text_fn measure, void *userdata)
{
    if (!g || !buffer) {
        return YETTY_ERR(yetty_ycore_void, "render: NULL graph or buffer");
    }
    struct yetty_ydiagram_render_options opts =
        options ? *options : yetty_ydiagram_default_render_options();

    struct render_state st = {
        .buf      = buffer,
        .opt      = &opts,
        .measure  = measure,
        .userdata = userdata,
        .z        = 0,
    };

    yetty_ypaint_core_buffer_set_scene_bounds(buffer, g->min_x, g->min_y, g->max_x, g->max_y);

    /* CMD_ZERO at the start of every full-redraw buffer — clears the
     * receiving canvas + resets cursor as a side effect of decoding.
     * Replaces the obsolete separate YPAINT_CLEAR OSC envelope (see
     * yetty/ypaint-core/cmds.h). Sending CLEAR + BIN as two envelopes
     * currently freezes yetty's OSC SM (CLEAR handler doesn't drain the
     * body terminator), so we use the single-envelope form. */
    (void)yetty_ypaint_core_buffer_add_cmd_zero(buffer);

    /* Optional fullscreen background. */
    if (opts.background_color) {
        struct yetty_ysdf_box bg = {
            .center_x      = (g->min_x + g->max_x) * 0.5f,
            .center_y      = (g->min_y + g->max_y) * 0.5f,
            .half_width    = (g->max_x - g->min_x) * 0.5f,
            .half_height   = (g->max_y - g->min_y) * 0.5f,
            .corner_radius = 0.0f,
        };
        yetty_ysdf_add_box(buffer, st.z++, opts.background_color, 0, 0.0f, &bg);
    }

    /* Clusters first. */
    for (size_t i = 0; i < g->cluster_count; i++) {
        const struct yetty_ydiagram_cluster *c = &g->clusters[i];
        if (c->width <= 0.0f || c->height <= 0.0f) continue;
        struct yetty_ysdf_box geom = {
            .center_x      = c->x + c->width * 0.5f,
            .center_y      = c->y + c->height * 0.5f,
            .half_width    = c->width * 0.5f,
            .half_height   = c->height * 0.5f,
            .corner_radius = 5.0f,
        };
        yetty_ysdf_add_box(buffer, st.z++, c->fill_color, c->stroke_color, 1.0f, &geom);
    }

    /* Edges go under nodes. */
    for (size_t i = 0; i < g->edge_count; i++) {
        emit_edge(&st, g, &g->edges[i]);
    }

    /* Nodes (skip dummies). */
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].is_dummy) continue;
        emit_node_shape(&st, &g->nodes[i]);
    }

    /* Labels on top so they aren't covered by adjacent fills. */
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].is_dummy) continue;
        emit_node_label(&st, &g->nodes[i]);
    }

    return YETTY_OK_VOID();
}
