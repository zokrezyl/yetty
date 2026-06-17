/*
 * renderer-polar.c — pie / donut / radar.
 *
 * Pie and donut tessellate each slice as an annular wedge (a fan of triangles,
 * since the SDF pie primitive is fixed to the vertical axis and cannot be
 * rotated to an arbitrary start angle). Radar lays the categories out as spokes
 * and draws one filled polygon per series.
 *
 * Angles are measured clockwise from 12 o'clock (see emit_wedge).
 */

#include "render-state.h"

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*=============================================================================
 * Pie / donut
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_render_pie(struct yetty_ychart_render_state *state,
                                                        bool donut)
{
    const struct yetty_ychart_chart *chart = state->chart;
    size_t ncat = chart->category_count;
    if (ncat == 0 || chart->series_count == 0) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ychart_series *series = &chart->series[0];

    double total = 0.0;
    for (size_t i = 0; i < ncat && i < series->value_count; i++) {
        total += fabs(series->values[i]);
    }
    if (total <= 1e-9) {
        return YETTY_OK_VOID();
    }

    /* Legend lists categories. */
    const char *labels[128];
    uint32_t colors[128];
    size_t legend_count = ncat < 128 ? ncat : 128;
    for (size_t i = 0; i < legend_count; i++) {
        labels[i] = chart->categories[i];
        colors[i] = yetty_ychart_resolve_color(chart, i);
    }
    float legend_h = yetty_ychart_legend_height(state, legend_count);
    float legend_top = state->content_y1 - legend_h;

    float area_w = state->content_x1 - state->content_x0;
    float area_h = legend_top - state->content_y0;
    float cx = (state->content_x0 + state->content_x1) * 0.5f;
    float cy = state->content_y0 + area_h * 0.5f;
    float radius = 0.42f * (area_w < area_h ? area_w : area_h);
    if (radius < 8.0f) {
        return YETTY_OK_VOID();
    }
    float inner = donut ? radius * 0.58f : 0.0f;
    float fs = state->opt->label_font_size;

    float angle = 0.0f; /* start at the top */
    for (size_t i = 0; i < ncat; i++) {
        double v = i < series->value_count ? fabs(series->values[i]) : 0.0;
        float sweep = (float)(v / total) * (float)(2.0 * M_PI);
        if (sweep <= 0.0f) {
            continue;
        }
        uint32_t color = yetty_ychart_resolve_color(chart, i);
        struct yetty_ycore_void_result w =
            yetty_ychart_emit_wedge(state, cx, cy, inner, radius, angle, angle + sweep, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, w, "pie: wedge");

        /* Percentage label inside the slice when it's big enough to fit. */
        if (sweep > 0.18f) {
            float mid = angle + sweep * 0.5f;
            float label_r = donut ? (inner + radius) * 0.5f : radius * 0.62f;
            float lx = cx + label_r * sinf(mid);
            float ly = cy - label_r * cosf(mid);
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0f%%", v / total * 100.0);
            struct yetty_ycore_void_result pl = yetty_ychart_emit_label(
                state, lx, ly, buf, fs, 0xFF0B1014u, YETTY_YCHART_ANCHOR_CENTER);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, pl, "pie: percent label");
        }
        angle += sweep;
    }

    /* Donut centre total. */
    if (donut) {
        char buf[32];
        double rounded = (double)llround(total);
        if (fabs(total - rounded) < 1e-9) {
            snprintf(buf, sizeof(buf), "%lld", (long long)rounded);
        } else {
            snprintf(buf, sizeof(buf), "%g", total);
        }
        struct yetty_ycore_void_result cl = yetty_ychart_emit_label(
            state, cx, cy, buf, fs * 1.3f, YETTY_YCHART_COLOR_TEXT, YETTY_YCHART_ANCHOR_CENTER);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cl, "donut: total");
    }

    return yetty_ychart_emit_legend(state, labels, colors, legend_count, legend_top);
}

/*=============================================================================
 * Radar
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_render_radar(struct yetty_ychart_render_state *state)
{
    const struct yetty_ychart_chart *chart = state->chart;
    size_t naxis = chart->category_count;
    size_t nseries = chart->series_count;
    if (naxis < 3 || nseries == 0) {
        return YETTY_OK_VOID();
    }

    /* Legend lists series. */
    const char *labels[64];
    uint32_t colors[64];
    size_t legend_count = nseries < 64 ? nseries : 64;
    for (size_t s = 0; s < legend_count; s++) {
        labels[s] = chart->series[s].name ? chart->series[s].name : "";
        colors[s] = yetty_ychart_resolve_color(chart, s);
    }
    float legend_h = yetty_ychart_legend_height(state, legend_count);
    float legend_top = state->content_y1 - legend_h;

    float area_w = state->content_x1 - state->content_x0;
    float area_h = legend_top - state->content_y0;
    float cx = (state->content_x0 + state->content_x1) * 0.5f;
    float cy = state->content_y0 + area_h * 0.5f;
    float radius = 0.38f * (area_w < area_h ? area_w : area_h);
    if (radius < 8.0f) {
        return YETTY_OK_VOID();
    }
    float fs = state->opt->label_font_size;
    double max = yetty_ychart_max_value(chart);

    /* angle of axis k */
#define AXIS_ANGLE(k) ((float)(k) * (float)(2.0 * M_PI) / (float)naxis)

    /* Concentric grid rings. */
    const int rings = 4;
    for (int r = 1; r <= rings; r++) {
        float rr = radius * (float)r / (float)rings;
        for (size_t k = 0; k < naxis; k++) {
            float a0 = AXIS_ANGLE(k);
            float a1 = AXIS_ANGLE((k + 1) % naxis);
            float x0 = cx + rr * sinf(a0);
            float y0 = cy - rr * cosf(a0);
            float x1 = cx + rr * sinf(a1);
            float y1 = cy - rr * cosf(a1);
            struct yetty_ycore_void_result gl =
                yetty_ychart_emit_segment(state, x0, y0, x1, y1, YETTY_YCHART_COLOR_GRID, 1.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, gl, "radar: ring");
        }
    }

    /* Spokes + axis labels. */
    for (size_t k = 0; k < naxis; k++) {
        float a = AXIS_ANGLE(k);
        float ex = cx + radius * sinf(a);
        float ey = cy - radius * cosf(a);
        struct yetty_ycore_void_result sp =
            yetty_ychart_emit_segment(state, cx, cy, ex, ey, YETTY_YCHART_COLOR_AXIS, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sp, "radar: spoke");
        float lx = cx + (radius + 12.0f) * sinf(a);
        float ly = cy - (radius + 12.0f) * cosf(a);
        enum yetty_ychart_anchor anchor = YETTY_YCHART_ANCHOR_CENTER;
        float sx = sinf(a);
        if (sx > 0.3f) {
            anchor = YETTY_YCHART_ANCHOR_LEFT;
        } else if (sx < -0.3f) {
            anchor = YETTY_YCHART_ANCHOR_RIGHT;
        }
        struct yetty_ycore_void_result al = yetty_ychart_emit_label(
            state, lx, ly, chart->categories[k], fs, YETTY_YCHART_COLOR_TEXT_MUTED, anchor);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, al, "radar: axis label");
    }

    /* One polygon per series. */
    for (size_t s = 0; s < nseries; s++) {
        const struct yetty_ychart_series *series = &chart->series[s];
        uint32_t color = yetty_ychart_resolve_color(chart, s);
        uint32_t fill = yetty_ychart_with_alpha(color, 0x55);

        float xs[64 + 1];
        float ys[64 + 1];
        size_t count = naxis < 64 ? naxis : 64;
        for (size_t k = 0; k < count; k++) {
            double v = k < series->value_count ? series->values[k] : 0.0;
            float rr = radius * (float)(v / max);
            if (rr < 0.0f) {
                rr = 0.0f;
            }
            float a = AXIS_ANGLE(k);
            xs[k] = cx + rr * sinf(a);
            ys[k] = cy - rr * cosf(a);
        }
        /* Fill (fan from centre — radar polygons are star-shaped). */
        for (size_t k = 0; k < count; k++) {
            size_t next = (k + 1) % count;
            struct yetty_ycore_void_result tf =
                yetty_ychart_emit_triangle(state, cx, cy, xs[k], ys[k], xs[next], ys[next], fill);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tf, "radar: fill facet");
        }
        /* Outline (closed). */
        xs[count] = xs[0];
        ys[count] = ys[0];
        struct yetty_ycore_void_result ol =
            yetty_ychart_emit_polyline(state, xs, ys, count + 1, color, 2.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ol, "radar: outline");
        /* Vertex markers. */
        for (size_t k = 0; k < count; k++) {
            struct yetty_ycore_void_result mk =
                yetty_ychart_emit_circle(state, xs[k], ys[k], 3.0f, color, 0, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, mk, "radar: vertex");
        }
    }
#undef AXIS_ANGLE

    return yetty_ychart_emit_legend(state, labels, colors, legend_count, legend_top);
}
