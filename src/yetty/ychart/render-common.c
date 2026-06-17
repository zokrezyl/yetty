/*
 * render-common.c — chart render dispatch + primitives shared by the family
 * renderers (cartesian / polar / hierarchical).
 *
 * Emission is single-pass and z ascends as primitives are added, so the
 * fragment shader z-orders them in draw order. Every drawable-list append can
 * fail (the backing storage grows on demand); each helper returns a Result and
 * bails on the first failure rather than leaving a half-drawn chart.
 *
 * The buffer's font_id = -1 means "canvas default" — producers don't embed
 * fonts themselves.
 */

#include "render-state.h"

#include <math.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/*=============================================================================
 * Options
 *===========================================================================*/

struct yetty_ychart_render_options yetty_ychart_default_render_options(void)
{
    struct yetty_ychart_render_options o = {
        .width = 0.0f,  /* → 900 */
        .height = 0.0f, /* → 540 */
        .title_font_size = 18.0f,
        .label_font_size = 12.0f,
        .background_color = 0u,
        .clear_canvas = true,
    };
    return o;
}

/*=============================================================================
 * Colour / text measurement
 *===========================================================================*/

uint32_t yetty_ychart_with_alpha(uint32_t color, uint8_t alpha)
{
    return (color & 0x00FFFFFFu) | ((uint32_t)alpha << 24);
}

float yetty_ychart_measure(struct yetty_ychart_render_state *state, const char *text,
                            float font_size)
{
    if (!text || !text[0]) {
        return 0.0f;
    }
    size_t n = strlen(text);
    if (state->measure) {
        return state->measure(text, n, font_size, state->userdata);
    }
    return font_size * 0.6f * (float)n;
}

double yetty_ychart_max_value(const struct yetty_ychart_chart *chart)
{
    double max = 1e-9;
    for (size_t s = 0; s < chart->series_count; s++) {
        for (size_t i = 0; i < chart->series[s].value_count; i++) {
            double v = fabs(chart->series[s].values[i]);
            if (v > max) {
                max = v;
            }
        }
    }
    return max;
}

/*=============================================================================
 * Shape emitters
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_emit_box(struct yetty_ychart_render_state *state,
                                                      float x0, float y0, float x1, float y1,
                                                      float corner_radius, uint32_t fill_color,
                                                      uint32_t stroke_color, float stroke_width)
{
    if (x1 < x0) {
        float t = x0;
        x0 = x1;
        x1 = t;
    }
    if (y1 < y0) {
        float t = y0;
        y0 = y1;
        y1 = t;
    }
    struct yetty_ysdf_box geom = {
        .center_x = (x0 + x1) * 0.5f,
        .center_y = (y0 + y1) * 0.5f,
        .half_width = (x1 - x0) * 0.5f,
        .half_height = (y1 - y0) * 0.5f,
        .corner_radius = corner_radius,
    };
    struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_box(
        state->buf, 0, state->z++, fill_color, stroke_color, stroke_width, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_box: add box");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_emit_segment(struct yetty_ychart_render_state *state,
                                                          float x0, float y0, float x1, float y1,
                                                          uint32_t color, float width)
{
    struct yetty_ysdf_segment geom = {.start_x = x0, .start_y = y0, .end_x = x1, .end_y = y1};
    struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_segment(
        state->buf, 0, state->z++, 0, color, width, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_segment: add segment");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_emit_triangle(struct yetty_ychart_render_state *state,
                                                           float ax, float ay, float bx, float by,
                                                           float cx, float cy, uint32_t color)
{
    struct yetty_ysdf_triangle geom = {
        .vertex_a_x = ax,
        .vertex_a_y = ay,
        .vertex_b_x = bx,
        .vertex_b_y = by,
        .vertex_c_x = cx,
        .vertex_c_y = cy,
    };
    struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_triangle(
        state->buf, 0, state->z++, color, 0, 0.0f, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_triangle: add triangle");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_emit_circle(struct yetty_ychart_render_state *state,
                                                         float cx, float cy, float radius,
                                                         uint32_t fill_color, uint32_t stroke_color,
                                                         float stroke_width)
{
    struct yetty_ysdf_circle geom = {.center_x = cx, .center_y = cy, .radius = radius};
    struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_circle(
        state->buf, 0, state->z++, fill_color, stroke_color, stroke_width, &geom);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_circle: add circle");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_emit_polyline(struct yetty_ychart_render_state *state,
                                                           const float *xs, const float *ys,
                                                           size_t count, uint32_t color,
                                                           float width)
{
    for (size_t i = 1; i < count; i++) {
        struct yetty_ycore_void_result r =
            yetty_ychart_emit_segment(state, xs[i - 1], ys[i - 1], xs[i], ys[i], color, width);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_polyline: segment");
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Text
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_emit_text(struct yetty_ychart_render_state *state,
                                                       float x, float baseline_y, const char *text,
                                                       float font_size, uint32_t color)
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
    struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_text(
        state->buf, x, baseline_y, &view, font_size, color, state->z++, -1, 0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_text: add text run");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_emit_label(struct yetty_ychart_render_state *state,
                                                        float x, float cy, const char *text,
                                                        float font_size, uint32_t color,
                                                        enum yetty_ychart_anchor anchor)
{
    if (!text || !text[0]) {
        return YETTY_OK_VOID();
    }
    float w = yetty_ychart_measure(state, text, font_size);
    float tx = x;
    if (anchor == YETTY_YCHART_ANCHOR_CENTER) {
        tx = x - w * 0.5f;
    } else if (anchor == YETTY_YCHART_ANCHOR_RIGHT) {
        tx = x - w;
    }
    float baseline = cy + font_size * 0.35f;
    return yetty_ychart_emit_text(state, tx, baseline, text, font_size, color);
}

/*=============================================================================
 * Annular wedge (pie slice / donut segment)
 *===========================================================================*/

static void wedge_point(float cx, float cy, float radius, float theta, float *out_x, float *out_y)
{
    /* theta measured clockwise from the top (12 o'clock). With y growing down,
     * (sin, -cos) traces top → right → bottom → left, i.e. clockwise. */
    *out_x = cx + radius * sinf(theta);
    *out_y = cy - radius * cosf(theta);
}

struct yetty_ycore_void_result yetty_ychart_emit_wedge(struct yetty_ychart_render_state *state,
                                                        float cx, float cy, float inner_radius,
                                                        float outer_radius, float angle_start,
                                                        float angle_end, uint32_t color)
{
    float span = angle_end - angle_start;
    if (span <= 1e-5f) {
        return YETTY_OK_VOID();
    }
    /* ~64 facets over a full turn keeps the arc smooth. */
    int steps = (int)ceilf(span / 0.10f);
    if (steps < 1) {
        steps = 1;
    }
    float step = span / (float)steps;
    uint32_t z = state->z; /* one z for the whole wedge */

    for (int i = 0; i < steps; i++) {
        float t0 = angle_start + step * (float)i;
        float t1 = t0 + step;
        if (inner_radius <= 0.5f) {
            float ox0, oy0, ox1, oy1;
            wedge_point(cx, cy, outer_radius, t0, &ox0, &oy0);
            wedge_point(cx, cy, outer_radius, t1, &ox1, &oy1);
            struct yetty_ysdf_triangle geom = {
                .vertex_a_x = cx,
                .vertex_a_y = cy,
                .vertex_b_x = ox0,
                .vertex_b_y = oy0,
                .vertex_c_x = ox1,
                .vertex_c_y = oy1,
            };
            struct yetty_ycore_void_result r = yetty_ydraw_drawable_list_add_cmd_add_triangle(
                state->buf, 0, z, color, 0, 0.0f, &geom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "emit_wedge: pie facet");
        } else {
            float ox0, oy0, ox1, oy1, ix0, iy0, ix1, iy1;
            wedge_point(cx, cy, outer_radius, t0, &ox0, &oy0);
            wedge_point(cx, cy, outer_radius, t1, &ox1, &oy1);
            wedge_point(cx, cy, inner_radius, t0, &ix0, &iy0);
            wedge_point(cx, cy, inner_radius, t1, &ix1, &iy1);
            struct yetty_ysdf_triangle tri_a = {
                .vertex_a_x = ix0,
                .vertex_a_y = iy0,
                .vertex_b_x = ox0,
                .vertex_b_y = oy0,
                .vertex_c_x = ox1,
                .vertex_c_y = oy1,
            };
            struct yetty_ycore_void_result ra = yetty_ydraw_drawable_list_add_cmd_add_triangle(
                state->buf, 0, z, color, 0, 0.0f, &tri_a);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, ra, "emit_wedge: ring facet a");
            struct yetty_ysdf_triangle tri_b = {
                .vertex_a_x = ix0,
                .vertex_a_y = iy0,
                .vertex_b_x = ox1,
                .vertex_b_y = oy1,
                .vertex_c_x = ix1,
                .vertex_c_y = iy1,
            };
            struct yetty_ycore_void_result rb = yetty_ydraw_drawable_list_add_cmd_add_triangle(
                state->buf, 0, z, color, 0, 0.0f, &tri_b);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, rb, "emit_wedge: ring facet b");
        }
    }
    state->z = z + 1;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Title + legend
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_emit_title(struct yetty_ychart_render_state *state)
{
    if (!state->chart->title || !state->chart->title[0]) {
        return YETTY_OK_VOID();
    }
    float fs = state->opt->title_font_size;
    float cx = (state->content_x0 + state->content_x1) * 0.5f;
    float cy = state->content_y0 - fs * 0.5f - 4.0f;
    return yetty_ychart_emit_label(state, cx, cy, state->chart->title, fs,
                                    YETTY_YCHART_COLOR_TITLE, YETTY_YCHART_ANCHOR_CENTER);
}

float yetty_ychart_legend_height(const struct yetty_ychart_render_state *state, size_t count)
{
    if (!state->chart->show_legend || count == 0) {
        return 0.0f;
    }
    return state->opt->label_font_size + 20.0f;
}

struct yetty_ycore_void_result yetty_ychart_emit_legend(struct yetty_ychart_render_state *state,
                                                         const char *const *labels,
                                                         const uint32_t *colors, size_t count,
                                                         float legend_top)
{
    if (!state->chart->show_legend || count == 0) {
        return YETTY_OK_VOID();
    }
    float fs = state->opt->label_font_size;
    float swatch = fs;
    float gap = 6.0f; /* swatch → text */
    float item_gap = 18.0f;

    /* Measure total width to centre the row. */
    float total = 0.0f;
    for (size_t i = 0; i < count; i++) {
        total += swatch + gap + yetty_ychart_measure(state, labels[i] ? labels[i] : "", fs);
        if (i + 1 < count) {
            total += item_gap;
        }
    }
    float available = state->content_x1 - state->content_x0;
    float x = (state->content_x0 + state->content_x1) * 0.5f - total * 0.5f;
    if (total > available) {
        x = state->content_x0; /* overflow: left-align, let it clip */
    }
    float cy = legend_top + (fs + 20.0f) * 0.5f;

    for (size_t i = 0; i < count; i++) {
        struct yetty_ycore_void_result sw = yetty_ychart_emit_box(
            state, x, cy - swatch * 0.5f, x + swatch, cy + swatch * 0.5f, 2.0f, colors[i], 0, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sw, "emit_legend: swatch");
        x += swatch + gap;
        const char *label = labels[i] ? labels[i] : "";
        struct yetty_ycore_void_result tx = yetty_ychart_emit_label(
            state, x, cy, label, fs, YETTY_YCHART_COLOR_TEXT, YETTY_YCHART_ANCHOR_LEFT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tx, "emit_legend: label");
        x += yetty_ychart_measure(state, label, fs) + item_gap;
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Top-level dispatch
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_render(
    const struct yetty_ychart_chart *chart, struct yetty_ydraw_drawable_list *buffer,
    const struct yetty_ychart_render_options *options, yetty_ychart_measure_text_fn measure,
    void *measure_userdata)
{
    if (!chart || !buffer) {
        return YETTY_ERR(yetty_ycore_void, "render: NULL chart or buffer");
    }
    struct yetty_ychart_render_options opt =
        options ? *options : yetty_ychart_default_render_options();
    float width = opt.width > 0.0f ? opt.width : 900.0f;
    float height = opt.height > 0.0f ? opt.height : 540.0f;

    struct yetty_ychart_render_state state = {
        .buf = buffer,
        .chart = chart,
        .opt = &opt,
        .measure = measure,
        .userdata = measure_userdata,
        .z = 0,
        .width = width,
        .height = height,
    };

    /* CMD_ZERO clears the receiving canvas + resets its cursor to (0,0) on
     * decode — full-redraw semantics for a re-rendering widget. ycat clears it
     * for cat-like inline flow. Must be the first primitive in the buffer. */
    if (opt.clear_canvas) {
        struct yetty_ycore_void_result zero = yetty_ydraw_drawable_list_add_cmd_zero(buffer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, zero, "render: clear canvas");
    }

    /* Content rect: outer margin, with room for the title at the top. */
    float margin = 18.0f;
    state.content_x0 = margin;
    state.content_x1 = width - margin;
    state.content_y0 =
        margin + ((chart->title && chart->title[0]) ? opt.title_font_size + 14.0f : 0.0f);
    state.content_y1 = height - margin;

    /* Optional opaque backdrop. */
    if (opt.background_color != 0u) {
        struct yetty_ycore_void_result bg = yetty_ychart_emit_box(
            &state, 0.0f, 0.0f, width, height, 0.0f, opt.background_color, 0, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, bg, "render: backdrop");
    }

    struct yetty_ycore_void_result tr = yetty_ychart_emit_title(&state);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "render: title");

    struct yetty_ycore_void_result body;
    switch (chart->kind) {
    case YETTY_YCHART_KIND_PIE:
        body = yetty_ychart_render_pie(&state, false);
        break;
    case YETTY_YCHART_KIND_DONUT:
        body = yetty_ychart_render_pie(&state, true);
        break;
    case YETTY_YCHART_KIND_RADAR:
        body = yetty_ychart_render_radar(&state);
        break;
    case YETTY_YCHART_KIND_TREEMAP:
        body = yetty_ychart_render_treemap(&state);
        break;
    case YETTY_YCHART_KIND_SANKEY:
        body = yetty_ychart_render_sankey(&state);
        break;
    case YETTY_YCHART_KIND_BAR:
    case YETTY_YCHART_KIND_COLUMN:
    case YETTY_YCHART_KIND_LINE:
    case YETTY_YCHART_KIND_AREA:
    case YETTY_YCHART_KIND_SCATTER:
        body = yetty_ychart_render_cartesian(&state);
        break;
    case YETTY_YCHART_KIND_AUTO:
    default:
        return YETTY_ERR(yetty_ycore_void, "render: unresolved chart kind (expected a concrete "
                                           "kind, KIND_AUTO should be resolved by the caller)");
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body, "render: chart body");

    /* The renderer emitted with the (0,0)-(width,height) scene; record bounds
     * so the receiving canvas can place / scale the chart. */
    yetty_ydraw_drawable_list_set_scene_bounds(buffer, 0.0f, 0.0f, width, height);
    return YETTY_OK_VOID();
}
