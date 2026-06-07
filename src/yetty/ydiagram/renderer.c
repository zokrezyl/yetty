/*
 * renderer.c — graph IR → ydraw buffer.
 *
 * Emission is single-pass and z-order ascends as we go:
 *   clusters → edges → nodes → labels
 *
 * Every primitive is an MSD shape (yetty_ydraw_drawable_list_add_cmd_add_*) or an MSDF text run
 * (yetty_ydraw_drawable_list_add_text). The buffer's font_id = -1 means
 * "canvas default" — producers don't need to embed fonts themselves.
 *
 * Every drawable-list append can fail (the backing storage grows on demand), so
 * each emitter returns a Result and bails on the first failed append rather than
 * silently dropping a primitive and leaving a half-drawn diagram.
 */

#include <yetty/ydiagram/renderer.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

struct yetty_ydiagram_render_options yetty_ydiagram_default_render_options(void)
{
    struct yetty_ydiagram_render_options o = {
        .arrow_size = 8.0f,
        .dash_length = 6.0f,
        .dash_gap = 4.0f,
        .background_color = 0,
        .clear_canvas = true, /* preserve full-redraw default for widgets */
    };
    return o;
}

/*=============================================================================
 * Per-frame state — `z` increments as primitives are emitted so the
 * fragment shader z-orders them correctly.
 *===========================================================================*/

struct render_state {
    struct yetty_ydraw_drawable_list *buf;
    const struct yetty_ydiagram_render_options *opt;
    yetty_ydiagram_measure_text_fn measure;
    void *userdata;
    uint32_t z;
};

static float measure_text(struct render_state *s, const char *text, float font_size)
{
    if (!text || !text[0]) {
        return 0.0f;
    }
    size_t n = strlen(text);
    if (s->measure) {
        return s->measure(text, n, font_size, s->userdata);
    }
    return font_size * 0.6f * (float)n;
}

/* Forward declarations — record/compartment rendering (defined with the shape
 * emitters) reuses the text and segment helpers defined further down. */
static struct yetty_ycore_void_result emit_text(struct render_state *s, float x, float y,
                                                const char *text, float font_size, uint32_t color);
static struct yetty_ycore_void_result emit_segment_xy(struct render_state *s, float x0, float y0,
                                                      float x1, float y1, uint32_t color,
                                                      float width);

/*=============================================================================
 * Shape emitters
 *===========================================================================*/

static struct yetty_ycore_void_result emit_rect(struct render_state *s,
                                                const struct yetty_ydiagram_node *n,
                                                float corner_radius)
{
    struct yetty_ysdf_box geom = {
        .center_x = n->x,
        .center_y = n->y,
        .half_width = n->width * 0.5f,
        .half_height = n->height * 0.5f,
        .corner_radius = corner_radius,
    };
    struct yetty_ycore_void_result add_result = yetty_ydraw_drawable_list_add_cmd_add_box(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width,
        &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_rect: add box");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_circle(struct render_state *s,
                                                  const struct yetty_ydiagram_node *n)
{
    float r = (n->width < n->height ? n->width : n->height) * 0.5f;
    struct yetty_ysdf_circle geom = {.center_x = n->x, .center_y = n->y, .radius = r};
    struct yetty_ycore_void_result add_result = yetty_ydraw_drawable_list_add_cmd_add_circle(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width,
        &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_circle: add circle");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_diamond(struct render_state *s,
                                                   const struct yetty_ydiagram_node *n)
{
    struct yetty_ysdf_rhombus geom = {
        .center_x = n->x,
        .center_y = n->y,
        .half_width = n->width * 0.5f,
        .half_height = n->height * 0.5f,
    };
    struct yetty_ycore_void_result add_result = yetty_ydraw_drawable_list_add_cmd_add_rhombus(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width,
        &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_diamond: add rhombus");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_ellipse(struct render_state *s,
                                                   const struct yetty_ydiagram_node *n)
{
    struct yetty_ysdf_ellipse geom = {
        .center_x = n->x,
        .center_y = n->y,
        .radius_x = n->width * 0.5f,
        .radius_y = n->height * 0.5f,
    };
    struct yetty_ycore_void_result add_result = yetty_ydraw_drawable_list_add_cmd_add_ellipse(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width,
        &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_ellipse: add ellipse");
    return YETTY_OK_VOID();
}

/* Mermaid-style stretched hexagon — flat-top, slanted left/right sides.
 *
 * The ysdf hexagon primitive is regular (radius-only), so a wide label
 * either gets clipped (with radius = min/2) or wastes huge vertical space
 * (with radius = max/2). We compose the shape from a centre rectangle +
 * two side triangles for the fill, and 6 segments for the outline. The
 * slant is `slant = min(h/2, w/4)`, capping at the height so very wide
 * boxes still look like hexagons rather than 1px-tipped arrows. */
static struct yetty_ycore_void_result emit_hexagon(struct render_state *s,
                                                   const struct yetty_ydiagram_node *n)
{
    float w = n->width;
    float h = n->height;
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float slant = hh;
    if (slant > w * 0.25f) {
        slant = w * 0.25f;
    }

    float cx_l = n->x - hw + slant; /* x where the left slant meets top/bottom edges */
    float cx_r = n->x + hw - slant;
    float ty = n->y - hh; /* top y of centre rectangle */
    float by = n->y + hh; /* bottom y */

    /* Fill: centre rectangle (no stroke, the segments below stroke the
     * full outline). */
    struct yetty_ysdf_box body = {
        .center_x = n->x,
        .center_y = n->y,
        .half_width = (w - 2.0f * slant) * 0.5f,
        .half_height = hh,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result body_result = yetty_ydraw_drawable_list_add_cmd_add_box(
        s->buf, 0, s->z++, n->style.fill_color, 0, 0.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_result, "emit_hexagon: add body box");

    /* Fill: left and right triangles. */
    struct yetty_ysdf_triangle left = {
        .vertex_a_x = n->x - hw,
        .vertex_a_y = n->y,
        .vertex_b_x = cx_l,
        .vertex_b_y = ty,
        .vertex_c_x = cx_l,
        .vertex_c_y = by,
    };
    struct yetty_ycore_void_result left_result = yetty_ydraw_drawable_list_add_cmd_add_triangle(
        s->buf, 0, s->z++, n->style.fill_color, 0, 0.0f, &left);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, left_result, "emit_hexagon: add left triangle");
    struct yetty_ysdf_triangle right = {
        .vertex_a_x = n->x + hw,
        .vertex_a_y = n->y,
        .vertex_b_x = cx_r,
        .vertex_b_y = ty,
        .vertex_c_x = cx_r,
        .vertex_c_y = by,
    };
    struct yetty_ycore_void_result right_result = yetty_ydraw_drawable_list_add_cmd_add_triangle(
        s->buf, 0, s->z++, n->style.fill_color, 0, 0.0f, &right);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, right_result, "emit_hexagon: add right triangle");

    /* Stroke: trace the six outline segments. */
    if (n->style.stroke_width > 0.0f && n->style.stroke_color != 0) {
        uint32_t sc = n->style.stroke_color;
        float sw = n->style.stroke_width;
        struct yetty_ysdf_segment edges[6] = {
            {cx_l, ty, cx_r, ty},        /* top */
            {cx_l, by, cx_r, by},        /* bottom */
            {n->x - hw, n->y, cx_l, ty}, /* top-left slant */
            {cx_l, by, n->x - hw, n->y}, /* bottom-left slant */
            {cx_r, ty, n->x + hw, n->y}, /* top-right slant */
            {n->x + hw, n->y, cx_r, by}, /* bottom-right slant */
        };
        uint32_t z = s->z;
        for (size_t i = 0; i < 6; i++) {
            struct yetty_ycore_void_result edge_result =
                yetty_ydraw_drawable_list_add_cmd_add_segment(s->buf, 0, z, 0, sc, sw, &edges[i]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, edge_result, "emit_hexagon: add outline segment");
        }
        s->z = z + 1;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_capsule(struct render_state *s,
                                                   const struct yetty_ydiagram_node *n)
{
    float r = n->height * 0.5f;
    struct yetty_ysdf_capsule geom = {
        .start_x = n->x - n->width * 0.5f + r,
        .start_y = n->y,
        .end_x = n->x + n->width * 0.5f - r,
        .end_y = n->y,
        .radius = r,
    };
    struct yetty_ycore_void_result add_result = yetty_ydraw_drawable_list_add_cmd_add_capsule(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width,
        &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_capsule: add capsule");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_cylinder(struct render_state *s,
                                                    const struct yetty_ydiagram_node *n)
{
    /* Top ellipse + side rect + bottom ellipse approximation. */
    float ellipse_h = n->height * 0.15f;
    float body_h = n->height - ellipse_h;

    struct yetty_ysdf_box body = {
        .center_x = n->x,
        .center_y = n->y,
        .half_width = n->width * 0.5f,
        .half_height = body_h * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result body_result = yetty_ydraw_drawable_list_add_cmd_add_box(
        s->buf, 0, s->z++, n->style.fill_color, 0, 0.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_result, "emit_cylinder: add body box");

    struct yetty_ysdf_ellipse top = {
        .center_x = n->x,
        .center_y = n->y - body_h * 0.5f,
        .radius_x = n->width * 0.5f,
        .radius_y = ellipse_h,
    };
    struct yetty_ycore_void_result top_result = yetty_ydraw_drawable_list_add_cmd_add_ellipse(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width, &top);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, top_result, "emit_cylinder: add top ellipse");

    struct yetty_ysdf_ellipse bottom = {
        .center_x = n->x,
        .center_y = n->y + body_h * 0.5f,
        .radius_x = n->width * 0.5f,
        .radius_y = ellipse_h,
    };
    struct yetty_ycore_void_result bottom_result = yetty_ydraw_drawable_list_add_cmd_add_ellipse(
        s->buf, 0, s->z++, n->style.fill_color, n->style.stroke_color, n->style.stroke_width,
        &bottom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bottom_result, "emit_cylinder: add bottom ellipse");

    struct yetty_ysdf_segment l = {
        .start_x = n->x - n->width * 0.5f,
        .start_y = n->y - body_h * 0.5f,
        .end_x = n->x - n->width * 0.5f,
        .end_y = n->y + body_h * 0.5f,
    };
    struct yetty_ycore_void_result left_result = yetty_ydraw_drawable_list_add_cmd_add_segment(
        s->buf, 0, s->z++, 0, n->style.stroke_color, n->style.stroke_width, &l);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, left_result, "emit_cylinder: add left segment");
    struct yetty_ysdf_segment r = {
        .start_x = n->x + n->width * 0.5f,
        .start_y = n->y - body_h * 0.5f,
        .end_x = n->x + n->width * 0.5f,
        .end_y = n->y + body_h * 0.5f,
    };
    struct yetty_ycore_void_result right_result = yetty_ydraw_drawable_list_add_cmd_add_segment(
        s->buf, 0, s->z++, 0, n->style.stroke_color, n->style.stroke_width, &r);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, right_result, "emit_cylinder: add right segment");
    return YETTY_OK_VOID();
}

/* Concentric ring + filled core — UML/state final pseudostate. */
static struct yetty_ycore_void_result emit_double_circle(struct render_state *s,
                                                         const struct yetty_ydiagram_node *n)
{
    float r = (n->width < n->height ? n->width : n->height) * 0.5f;
    struct yetty_ysdf_circle outer = {.center_x = n->x, .center_y = n->y, .radius = r};
    struct yetty_ycore_void_result outer_result = yetty_ydraw_drawable_list_add_cmd_add_circle(
        s->buf, 0, s->z++, 0, n->style.stroke_color, n->style.stroke_width, &outer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, outer_result, "emit_double_circle: add outer circle");
    struct yetty_ysdf_circle inner = {.center_x = n->x, .center_y = n->y, .radius = r * 0.55f};
    struct yetty_ycore_void_result inner_result = yetty_ydraw_drawable_list_add_cmd_add_circle(
        s->buf, 0, s->z++, n->style.stroke_color, 0, 0.0f, &inner);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, inner_result, "emit_double_circle: add inner circle");
    return YETTY_OK_VOID();
}

/* Multi-line text run, top-aligned and left-padded inside a band. */
static struct yetty_ycore_void_result emit_left_text(struct render_state *s, float x,
                                                     float baseline, const char *text,
                                                     float font_size, uint32_t color)
{
    return emit_text(s, x, baseline, text, font_size, color);
}

/* UML class / ER entity: outer box, a title compartment (optional stereotype
 * line + name), then one line per body row, split into two compartments at
 * `method_start` (attributes / methods). */
static struct yetty_ycore_void_result emit_record(struct render_state *s,
                                                  const struct yetty_ydiagram_node *n)
{
    float fs = n->style.font_size;
    float left = n->x - n->width * 0.5f;
    float right = n->x + n->width * 0.5f;
    float top = n->y - n->height * 0.5f;
    float pad_x = 8.0f;

    struct yetty_ycore_void_result box_result = emit_rect(s, n, 3.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, box_result, "emit_record: outer box");

    /* Title compartment. */
    float title_cy = top + n->header_h * 0.5f;
    if (n->stereotype && n->stereotype[0]) {
        float sw = measure_text(s, n->stereotype, fs - 2.0f);
        struct yetty_ycore_void_result stereotype_result =
            emit_text(s, n->x - sw * 0.5f, top + (fs - 2.0f) + 2.0f, n->stereotype, fs - 2.0f,
                      n->style.text_color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, stereotype_result, "emit_record: stereotype text");
        title_cy = top + (fs - 2.0f) + (n->header_h - (fs - 2.0f)) * 0.5f;
    }
    if (n->label && n->label[0]) {
        float tw = measure_text(s, n->label, fs);
        struct yetty_ycore_void_result label_result =
            emit_text(s, n->x - tw * 0.5f, title_cy + fs / 3.0f, n->label, fs, n->style.text_color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_result, "emit_record: title text");
    }

    /* Divider under the title. */
    struct yetty_ycore_void_result divider_result =
        emit_segment_xy(s, left, top + n->header_h, right, top + n->header_h, n->style.stroke_color,
                        n->style.stroke_width);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, divider_result, "emit_record: title divider");

    if (n->row_count == 0) {
        return YETTY_OK_VOID();
    }
    float line_h = (n->height - n->header_h) / (float)n->row_count;
    for (size_t r = 0; r < n->row_count; r++) {
        float band_top = top + n->header_h + (float)r * line_h;
        struct yetty_ycore_void_result row_result =
            emit_left_text(s, left + pad_x, band_top + line_h * 0.5f + fs / 3.0f, n->rows[r], fs,
                           n->style.text_color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, row_result, "emit_record: row text");
        if (r == n->method_start && n->method_start > 0 && n->method_start < n->row_count) {
            struct yetty_ycore_void_result compartment_result = emit_segment_xy(
                s, left, band_top, right, band_top, n->style.stroke_color, n->style.stroke_width);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, compartment_result,
                                "emit_record: compartment divider");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_node_shape(struct render_state *s,
                                                      const struct yetty_ydiagram_node *n)
{
    if (n->is_record) {
        return emit_record(s, n);
    }
    switch (n->shape) {
    case YETTY_YDIAGRAM_SHAPE_RECTANGLE:
        return emit_rect(s, n, 0.0f);
    case YETTY_YDIAGRAM_SHAPE_ROUNDED_RECT:
        return emit_rect(s, n, n->style.corner_radius);
    case YETTY_YDIAGRAM_SHAPE_CIRCLE:
        return emit_circle(s, n);
    case YETTY_YDIAGRAM_SHAPE_DOUBLE_CIRCLE:
        return emit_double_circle(s, n);
    case YETTY_YDIAGRAM_SHAPE_DIAMOND:
        return emit_diamond(s, n);
    case YETTY_YDIAGRAM_SHAPE_ELLIPSE:
        return emit_ellipse(s, n);
    case YETTY_YDIAGRAM_SHAPE_HEXAGON:
        return emit_hexagon(s, n);
    case YETTY_YDIAGRAM_SHAPE_PARALLELOGRAM:
    case YETTY_YDIAGRAM_SHAPE_TRAPEZOID:
        /* No native SDF parallelogram/trapezoid — approximate with a box.
         * TODO: emit four segments tracing the actual quad. */
        return emit_rect(s, n, 0.0f);
    case YETTY_YDIAGRAM_SHAPE_CYLINDER:
        return emit_cylinder(s, n);
    case YETTY_YDIAGRAM_SHAPE_STADIUM:
        return emit_capsule(s, n);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Edge geometry — attach points, arrowheads, dashed segments.
 *===========================================================================*/

static void edge_attach_point(const struct yetty_ydiagram_node *node, float toward_x,
                              float toward_y, float *out_x, float *out_y)
{
    float dx = toward_x - node->x;
    float dy = toward_y - node->y;
    float angle = atan2f(dy, dx);
    float hw = node->width * 0.5f;
    float hh = node->height * 0.5f;
    switch (node->shape) {
    case YETTY_YDIAGRAM_SHAPE_CIRCLE:
    case YETTY_YDIAGRAM_SHAPE_DOUBLE_CIRCLE: {
        float r = hw < hh ? hw : hh;
        *out_x = node->x + r * cosf(angle);
        *out_y = node->y + r * sinf(angle);
        return;
    }
    case YETTY_YDIAGRAM_SHAPE_DIAMOND: {
        float ax = fabsf(cosf(angle));
        float ay = fabsf(sinf(angle));
        float t = 1.0f / (ax / hw + ay / hh);
        *out_x = node->x + t * cosf(angle);
        *out_y = node->y + t * sinf(angle);
        return;
    }
    default: {
        float tx = hw / (fabsf(cosf(angle)) + 1e-3f);
        float ty = hh / (fabsf(sinf(angle)) + 1e-3f);
        float t = tx < ty ? tx : ty;
        *out_x = node->x + t * cosf(angle);
        *out_y = node->y + t * sinf(angle);
        return;
    }
    }
}

static struct yetty_ycore_void_result emit_dashed_line(struct render_state *s, float x0, float y0,
                                                       float x1, float y1, uint32_t color,
                                                       float width)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float length = sqrtf(dx * dx + dy * dy);
    if (length < 0.01f) {
        return YETTY_OK_VOID();
    }
    float ux = dx / length;
    float uy = dy / length;
    float dl = s->opt->dash_length;
    float gl = s->opt->dash_gap;
    float seg = dl + gl;
    uint32_t z = s->z;
    for (float pos = 0.0f; pos < length; pos += seg) {
        float end = pos + dl;
        if (end > length) {
            end = length;
        }
        struct yetty_ysdf_segment g = {
            .start_x = x0 + ux * pos,
            .start_y = y0 + uy * pos,
            .end_x = x0 + ux * end,
            .end_y = y0 + uy * end,
        };
        struct yetty_ycore_void_result dash_result =
            yetty_ydraw_drawable_list_add_cmd_add_segment(s->buf, 0, z, 0, color, width, &g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dash_result, "emit_dashed_line: add dash segment");
    }
    s->z = z + 1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_segment_xy(struct render_state *s, float x0, float y0,
                                                      float x1, float y1, uint32_t color,
                                                      float width)
{
    struct yetty_ysdf_segment g = {.start_x = x0, .start_y = y0, .end_x = x1, .end_y = y1};
    struct yetty_ycore_void_result add_result =
        yetty_ydraw_drawable_list_add_cmd_add_segment(s->buf, 0, s->z++, 0, color, width, &g);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_segment_xy: add segment");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_filled_triangle(struct render_state *s, float ax,
                                                           float ay, float bx, float by, float cx,
                                                           float cy, uint32_t color)
{
    struct yetty_ysdf_triangle geom = {
        .vertex_a_x = ax,
        .vertex_a_y = ay,
        .vertex_b_x = bx,
        .vertex_b_y = by,
        .vertex_c_x = cx,
        .vertex_c_y = cy,
    };
    struct yetty_ycore_void_result add_result =
        yetty_ydraw_drawable_list_add_cmd_add_triangle(s->buf, 0, s->z++, color, 0, 0.0f, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_filled_triangle: add triangle");
    return YETTY_OK_VOID();
}

/* Arrowhead / line terminal at `(x, y)`, pointing along `angle` (the edge's
 * travel direction, i.e. into the node the terminal sits on). Supports the
 * flowchart arrows plus the UML (triangle / diamond) and ER (crow's-foot)
 * terminals. */
static struct yetty_ycore_void_result emit_arrowhead(struct render_state *s, float x, float y,
                                                     float angle,
                                                     enum yetty_ydiagram_arrow_style style,
                                                     uint32_t color, float size)
{
    if (style == YETTY_YDIAGRAM_ARROW_NONE) {
        return YETTY_OK_VOID();
    }

    float cos_a = cosf(angle);
    float sin_a = sinf(angle);
    /* Unit vectors: `b` points back along the edge (away from the node),
     * `p` is perpendicular. */
    float bxu = -cos_a, byu = -sin_a;
    float pxu = -sin_a, pyu = cos_a;
    float lw = 1.6f;

    switch (style) {
    case YETTY_YDIAGRAM_ARROW_NORMAL:
    case YETTY_YDIAGRAM_ARROW_OPEN:
    default: {
        float spread = 0.4f;
        float bx = x + bxu * size, by = y + byu * size;
        struct yetty_ycore_void_result tri_result =
            emit_filled_triangle(s, x, y, bx + pxu * size * spread, by + pyu * size * spread,
                                 bx - pxu * size * spread, by - pyu * size * spread, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tri_result, "emit_arrowhead: normal head");
        break;
    }
    case YETTY_YDIAGRAM_ARROW_TRIANGLE: {
        /* Hollow triangle (UML generalization / realization). */
        float d = size * 1.7f;
        float hw = size * 0.85f;
        float bx = x + bxu * d, by = y + byu * d;
        float lx = bx + pxu * hw, ly = by + pyu * hw;
        float rx = bx - pxu * hw, ry = by - pyu * hw;
        struct yetty_ycore_void_result side_a = emit_segment_xy(s, x, y, lx, ly, color, lw);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, side_a, "emit_arrowhead: triangle side a");
        struct yetty_ycore_void_result side_b = emit_segment_xy(s, lx, ly, rx, ry, color, lw);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, side_b, "emit_arrowhead: triangle side b");
        struct yetty_ycore_void_result side_c = emit_segment_xy(s, rx, ry, x, y, color, lw);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, side_c, "emit_arrowhead: triangle side c");
        break;
    }
    case YETTY_YDIAGRAM_ARROW_DIAMOND:
    case YETTY_YDIAGRAM_ARROW_DIAMOND_OPEN: {
        float d = size * 1.1f;
        float mx = x + bxu * d, my = y + byu * d;               /* mid  */
        float fx = x + bxu * d * 2.0f, fy = y + byu * d * 2.0f; /* far */
        float lx = mx + pxu * d * 0.7f, ly = my + pyu * d * 0.7f;
        float rx = mx - pxu * d * 0.7f, ry = my - pyu * d * 0.7f;
        if (style == YETTY_YDIAGRAM_ARROW_DIAMOND) {
            struct yetty_ycore_void_result half_a =
                emit_filled_triangle(s, x, y, lx, ly, fx, fy, color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, half_a, "emit_arrowhead: diamond half a");
            struct yetty_ycore_void_result half_b =
                emit_filled_triangle(s, x, y, fx, fy, rx, ry, color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, half_b, "emit_arrowhead: diamond half b");
        } else {
            struct yetty_ycore_void_result side_a = emit_segment_xy(s, x, y, lx, ly, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, side_a, "emit_arrowhead: diamond side a");
            struct yetty_ycore_void_result side_b = emit_segment_xy(s, lx, ly, fx, fy, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, side_b, "emit_arrowhead: diamond side b");
            struct yetty_ycore_void_result side_c = emit_segment_xy(s, fx, fy, rx, ry, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, side_c, "emit_arrowhead: diamond side c");
            struct yetty_ycore_void_result side_d = emit_segment_xy(s, rx, ry, x, y, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, side_d, "emit_arrowhead: diamond side d");
        }
        break;
    }
    case YETTY_YDIAGRAM_ARROW_CIRCLE:
    case YETTY_YDIAGRAM_ARROW_DOT: {
        float r = size * 0.4f;
        float cx = x + bxu * r, cy = y + byu * r;
        struct yetty_ysdf_circle g = {.center_x = cx, .center_y = cy, .radius = r};
        uint32_t fill = style == YETTY_YDIAGRAM_ARROW_DOT ? color : 0;
        struct yetty_ycore_void_result dot_result =
            yetty_ydraw_drawable_list_add_cmd_add_circle(s->buf, 0, s->z++, fill, color, lw, &g);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dot_result, "emit_arrowhead: dot/circle head");
        break;
    }
    case YETTY_YDIAGRAM_ARROW_CROW_ONE:
    case YETTY_YDIAGRAM_ARROW_CROW_ONE_OPT:
    case YETTY_YDIAGRAM_ARROW_CROW_MANY:
    case YETTY_YDIAGRAM_ARROW_CROW_MANY_OPT: {
        bool many = (style == YETTY_YDIAGRAM_ARROW_CROW_MANY ||
                     style == YETTY_YDIAGRAM_ARROW_CROW_MANY_OPT);
        bool optional = (style == YETTY_YDIAGRAM_ARROW_CROW_ONE_OPT ||
                         style == YETTY_YDIAGRAM_ARROW_CROW_MANY_OPT);
        float span = size * 1.6f; /* distance from node edge to the apex */
        float foot = size * 0.9f;
        if (many) {
            float ax = x + bxu * span, ay = y + byu * span;
            struct yetty_ycore_void_result spine = emit_segment_xy(s, ax, ay, x, y, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, spine, "emit_arrowhead: crow spine");
            struct yetty_ycore_void_result foot_a =
                emit_segment_xy(s, ax, ay, x + pxu * foot, y + pyu * foot, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, foot_a, "emit_arrowhead: crow foot a");
            struct yetty_ycore_void_result foot_b =
                emit_segment_xy(s, ax, ay, x - pxu * foot, y - pyu * foot, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, foot_b, "emit_arrowhead: crow foot b");
        } else {
            /* "exactly one": a single bar one step back from the edge. */
            float bx = x + bxu * span * 0.6f, by = y + byu * span * 0.6f;
            struct yetty_ycore_void_result bar = emit_segment_xy(
                s, bx + pxu * foot, by + pyu * foot, bx - pxu * foot, by - pyu * foot, color, lw);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, bar, "emit_arrowhead: crow bar");
        }
        if (optional) {
            float r = size * 0.42f;
            float cx = x + bxu * (span + r * 1.4f), cy = y + byu * (span + r * 1.4f);
            struct yetty_ysdf_circle g = {.center_x = cx, .center_y = cy, .radius = r};
            struct yetty_ycore_void_result opt_result =
                yetty_ydraw_drawable_list_add_cmd_add_circle(s->buf, 0, s->z++, 0, color, lw, &g);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, opt_result, "emit_arrowhead: optional marker");
        }
        break;
    }
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Text emission. Builds a yetty_ycore_buffer view over the label bytes and
 * forwards to the canvas; font_id = -1 selects the canvas's default font.
 *===========================================================================*/

static struct yetty_ycore_void_result emit_text(struct render_state *s, float x, float y,
                                                const char *text, float font_size, uint32_t color)
{
    if (!text || !text[0]) {
        return YETTY_OK_VOID();
    }
    size_t n = strlen(text);
    struct yetty_ycore_buffer view = {
        .data = (uint8_t *)(uintptr_t)text,
        .capacity = n,
        .size = n,
    };
    struct yetty_ycore_void_result add_result =
        yetty_ydraw_drawable_list_add_text(s->buf, x, y, &view, font_size, color, s->z++, -1, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_result, "emit_text: add text run");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result emit_node_label(struct render_state *s,
                                                      const struct yetty_ydiagram_node *n)
{
    if (n->is_record) {
        return YETTY_OK_VOID(); /* record draws its own multi-compartment text */
    }
    if (!n->label || !n->label[0]) {
        return YETTY_OK_VOID();
    }
    float tw = measure_text(s, n->label, n->style.font_size);
    float tx = n->x - tw * 0.5f;
    float ty = n->y + n->style.font_size / 3.0f;
    return emit_text(s, tx, ty, n->label, n->style.font_size, n->style.text_color);
}

static struct yetty_ycore_void_result emit_edge_label(struct render_state *s,
                                                      const struct yetty_ydiagram_edge *e)
{
    if (!e->label || !e->label[0]) {
        return YETTY_OK_VOID();
    }
    float fs = e->style.label_font_size;
    float tw = measure_text(s, e->label, fs);
    float padding = 3.0f;

    struct yetty_ysdf_box bg = {
        .center_x = e->label_position.x,
        .center_y = e->label_position.y,
        .half_width = tw * 0.5f + padding,
        .half_height = fs * 0.5f + padding,
        .corner_radius = 2.0f,
    };
    struct yetty_ycore_void_result bg_result =
        yetty_ydraw_drawable_list_add_cmd_add_box(s->buf, 0, s->z++, 0xFF1A1A2Eu, 0, 0.0f, &bg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bg_result, "emit_edge_label: add background box");
    return emit_text(s, e->label_position.x - tw * 0.5f, e->label_position.y + fs / 3.0f, e->label,
                     fs, e->style.label_color);
}

static struct yetty_ycore_void_result emit_edge(struct render_state *s,
                                                const struct yetty_ydiagram_graph *g,
                                                const struct yetty_ydiagram_edge *e)
{
    /* Look up endpoints (we accept non-const graph view for find_node — see
     * the const cast in callers below). */
    struct yetty_ydiagram_node *src =
        yetty_ydiagram_graph_find_node((struct yetty_ydiagram_graph *)g, e->source_id);
    struct yetty_ydiagram_node *tgt =
        yetty_ydiagram_graph_find_node((struct yetty_ydiagram_graph *)g, e->target_id);
    if (!src || !tgt) {
        return YETTY_OK_VOID();
    }

    float sx, sy, tx, ty;
    edge_attach_point(src, tgt->x, tgt->y, &sx, &sy);
    edge_attach_point(tgt, src->x, src->y, &tx, &ty);

    if (e->style.line_style == YETTY_YDIAGRAM_LINE_DASHED) {
        struct yetty_ycore_void_result dashed_result =
            emit_dashed_line(s, sx, sy, tx, ty, e->style.stroke_color, e->style.stroke_width);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dashed_result, "emit_edge: dashed line");
    } else {
        struct yetty_ysdf_segment seg = {
            .start_x = sx,
            .start_y = sy,
            .end_x = tx,
            .end_y = ty,
        };
        struct yetty_ycore_void_result line_result = yetty_ydraw_drawable_list_add_cmd_add_segment(
            s->buf, 0, s->z++, 0, e->style.stroke_color, e->style.stroke_width, &seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, line_result, "emit_edge: solid line");
    }

    if (e->style.target_arrow != YETTY_YDIAGRAM_ARROW_NONE) {
        float angle = atan2f(ty - sy, tx - sx);
        struct yetty_ycore_void_result target_result = emit_arrowhead(
            s, tx, ty, angle, e->style.target_arrow, e->style.stroke_color, s->opt->arrow_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, target_result, "emit_edge: target arrowhead");
    }
    if (e->style.source_arrow != YETTY_YDIAGRAM_ARROW_NONE) {
        float angle = atan2f(sy - ty, sx - tx);
        struct yetty_ycore_void_result source_result = emit_arrowhead(
            s, sx, sy, angle, e->style.source_arrow, e->style.stroke_color, s->opt->arrow_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, source_result, "emit_edge: source arrowhead");
    }

    return emit_edge_label(s, e);
}

/*=============================================================================
 * Top-level
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ydiagram_render(
    const struct yetty_ydiagram_graph *g, struct yetty_ydraw_drawable_list *buffer,
    const struct yetty_ydiagram_render_options *options, yetty_ydiagram_measure_text_fn measure,
    void *userdata)
{
    if (!g || !buffer) {
        return YETTY_ERR(yetty_ycore_void, "render: NULL graph or buffer");
    }
    struct yetty_ydiagram_render_options opts =
        options ? *options : yetty_ydiagram_default_render_options();

    struct render_state st = {
        .buf = buffer,
        .opt = &opts,
        .measure = measure,
        .userdata = userdata,
        .z = 0,
    };

    yetty_ydraw_drawable_list_set_scene_bounds(buffer, g->min_x, g->min_y, g->max_x, g->max_y);

    /* CMD_ZERO clears the receiving canvas + resets its cursor to (0,0) on
     * decode — full-redraw semantics. Skipped in cat-like (inline) mode so the
     * diagram flows at the current cursor instead of jumping to the origin. */
    if (opts.clear_canvas) {
        struct yetty_ycore_void_result zero_result = yetty_ydraw_drawable_list_add_cmd_zero(buffer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zero_result, "render: clear canvas");
    }

    /* Optional fullscreen background. */
    if (opts.background_color) {
        struct yetty_ysdf_box bg = {
            .center_x = (g->min_x + g->max_x) * 0.5f,
            .center_y = (g->min_y + g->max_y) * 0.5f,
            .half_width = (g->max_x - g->min_x) * 0.5f,
            .half_height = (g->max_y - g->min_y) * 0.5f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result bg_result = yetty_ydraw_drawable_list_add_cmd_add_box(
            buffer, 0, st.z++, opts.background_color, 0, 0.0f, &bg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bg_result, "render: background");
    }

    /* Clusters first. */
    for (size_t i = 0; i < g->cluster_count; i++) {
        const struct yetty_ydiagram_cluster *c = &g->clusters[i];
        if (c->width <= 0.0f || c->height <= 0.0f) {
            continue;
        }
        struct yetty_ysdf_box geom = {
            .center_x = c->x + c->width * 0.5f,
            .center_y = c->y + c->height * 0.5f,
            .half_width = c->width * 0.5f,
            .half_height = c->height * 0.5f,
            .corner_radius = 5.0f,
        };
        struct yetty_ycore_void_result cluster_result = yetty_ydraw_drawable_list_add_cmd_add_box(
            buffer, 0, st.z++, c->fill_color, c->stroke_color, 1.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cluster_result, "render: cluster box");
    }

    /* Edges go under nodes. */
    for (size_t i = 0; i < g->edge_count; i++) {
        struct yetty_ycore_void_result edge_result = emit_edge(&st, g, &g->edges[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, edge_result, "render: edge");
    }

    /* Nodes (skip dummies). */
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].is_dummy) {
            continue;
        }
        struct yetty_ycore_void_result shape_result = emit_node_shape(&st, &g->nodes[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_result, "render: node shape");
    }

    /* Labels on top so they aren't covered by adjacent fills. */
    for (size_t i = 0; i < g->node_count; i++) {
        if (g->nodes[i].is_dummy) {
            continue;
        }
        struct yetty_ycore_void_result label_result = emit_node_label(&st, &g->nodes[i]);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_result, "render: node label");
    }

    return YETTY_OK_VOID();
}
