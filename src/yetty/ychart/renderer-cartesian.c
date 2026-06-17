/*
 * renderer-cartesian.c — bar / column / line / area / scatter.
 *
 * One value axis and one category axis. COLUMN / LINE / AREA / SCATTER put the
 * value axis vertical (categories along the bottom); BAR transposes it
 * (categories down the left, values growing right). A "nice" tick step is
 * chosen so the gridlines land on round numbers.
 */

#include "render-state.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*=============================================================================
 * Axis helpers
 *===========================================================================*/

static void fmt_num(char *buf, size_t cap, double v)
{
    double rounded = (double)llround(v);
    if (fabs(v - rounded) < 1e-9 && fabs(v) < 1e15) {
        snprintf(buf, cap, "%lld", (long long)rounded);
    } else {
        snprintf(buf, cap, "%g", v);
    }
}

/* Round a range to a "nice" magnitude (1/2/5 × 10^n). */
static double nice_num(double range, int round)
{
    if (range <= 0.0) {
        return 1.0;
    }
    double exponent = floor(log10(range));
    double fraction = range / pow(10.0, exponent);
    double nice;
    if (round) {
        nice = fraction < 1.5 ? 1.0 : fraction < 3.0 ? 2.0 : fraction < 7.0 ? 5.0 : 10.0;
    } else {
        nice = fraction <= 1.0 ? 1.0 : fraction <= 2.0 ? 2.0 : fraction <= 5.0 ? 5.0 : 10.0;
    }
    return nice * pow(10.0, exponent);
}

/* Data extent across all series, always including the zero baseline. For
 * stacked charts the positive extent is the max per-category stack sum. */
static void value_extent(const struct yetty_ychart_chart *chart, double *out_min, double *out_max)
{
    double lo = 0.0, hi = 0.0;
    if (chart->stacked) {
        for (size_t i = 0; i < chart->category_count; i++) {
            double sum_pos = 0.0, sum_neg = 0.0;
            for (size_t s = 0; s < chart->series_count; s++) {
                double v = i < chart->series[s].value_count ? chart->series[s].values[i] : 0.0;
                if (v >= 0.0) {
                    sum_pos += v;
                } else {
                    sum_neg += v;
                }
            }
            if (sum_pos > hi) {
                hi = sum_pos;
            }
            if (sum_neg < lo) {
                lo = sum_neg;
            }
        }
    } else {
        for (size_t s = 0; s < chart->series_count; s++) {
            for (size_t i = 0; i < chart->series[s].value_count; i++) {
                double v = chart->series[s].values[i];
                if (v > hi) {
                    hi = v;
                }
                if (v < lo) {
                    lo = v;
                }
            }
        }
    }
    if (hi == lo) {
        hi = lo + 1.0;
    }
    *out_min = lo;
    *out_max = hi;
}

/* Compute axis_min/axis_max/tick from data extent. */
static void nice_axis(double data_min, double data_max, double *axis_min, double *axis_max,
                      double *tick)
{
    double range = nice_num(data_max - data_min, 0);
    double step = nice_num(range / 4.0, 1);
    *axis_min = floor(data_min / step) * step;
    *axis_max = ceil(data_max / step) * step;
    *tick = step;
}

/*=============================================================================
 * Legend entries (series names)
 *===========================================================================*/

static size_t build_legend(const struct yetty_ychart_chart *chart, const char **labels,
                           uint32_t *colors, size_t max)
{
    size_t count = 0;
    for (size_t s = 0; s < chart->series_count && count < max; s++) {
        labels[count] = chart->series[s].name ? chart->series[s].name : "";
        colors[count] = yetty_ychart_resolve_color(chart, s);
        count++;
    }
    return count;
}

/*=============================================================================
 * Vertical-value charts: COLUMN / LINE / AREA / SCATTER
 *===========================================================================*/

static struct yetty_ycore_void_result render_vertical(struct yetty_ychart_render_state *state,
                                                      enum yetty_ychart_kind kind, float px0,
                                                      float py0, float px1, float py1)
{
    const struct yetty_ychart_chart *chart = state->chart;
    float fs = state->opt->label_font_size;
    size_t ncat = chart->category_count;
    size_t nseries = chart->series_count;
    if (ncat == 0 || nseries == 0) {
        return YETTY_OK_VOID();
    }

    double data_min, data_max, axis_min, axis_max, tick;
    value_extent(chart, &data_min, &data_max);
    nice_axis(data_min, data_max, &axis_min, &axis_max, &tick);
    double span = axis_max - axis_min;
    float plot_h = py1 - py0;
    float plot_w = px1 - px0;

#define VALUE_TO_Y(v) (py1 - (float)(((v) - axis_min) / span) * plot_h)

    /* Gridlines + y tick labels. */
    for (double v = axis_min; v <= axis_max + tick * 0.5; v += tick) {
        float gy = VALUE_TO_Y(v);
        struct yetty_ycore_void_result gl =
            yetty_ychart_emit_segment(state, px0, gy, px1, gy, YETTY_YCHART_COLOR_GRID, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gl, "cartesian: gridline");
        char buf[32];
        fmt_num(buf, sizeof(buf), v);
        struct yetty_ycore_void_result tl =
            yetty_ychart_emit_label(state, px0 - 6.0f, gy, buf, fs, YETTY_YCHART_COLOR_TEXT_MUTED,
                                    YETTY_YCHART_ANCHOR_RIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tl, "cartesian: y tick label");
    }

    float baseline_y = VALUE_TO_Y(0.0);
    /* Axes. */
    struct yetty_ycore_void_result ax =
        yetty_ychart_emit_segment(state, px0, py0, px0, py1, YETTY_YCHART_COLOR_AXIS, 1.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ax, "cartesian: y axis");
    struct yetty_ycore_void_result bx = yetty_ychart_emit_segment(
        state, px0, baseline_y, px1, baseline_y, YETTY_YCHART_COLOR_AXIS, 1.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bx, "cartesian: x axis");

    float slot = plot_w / (float)ncat;

    /* Category labels. */
    for (size_t i = 0; i < ncat; i++) {
        float cx = px0 + ((float)i + 0.5f) * slot;
        struct yetty_ycore_void_result cl =
            yetty_ychart_emit_label(state, cx, py1 + fs * 0.5f + 6.0f, chart->categories[i], fs,
                                    YETTY_YCHART_COLOR_TEXT_MUTED, YETTY_YCHART_ANCHOR_CENTER);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cl, "cartesian: x label");
    }

    if (kind == YETTY_YCHART_KIND_COLUMN) {
        float group_w = slot * 0.7f;
        float group_x0_off = (slot - group_w) * 0.5f;
        float bar_w = chart->stacked ? group_w : group_w / (float)nseries;
        for (size_t i = 0; i < ncat; i++) {
            float slot_x0 = px0 + (float)i * slot + group_x0_off;
            float stack_pos = 0.0f, stack_neg = 0.0f;
            for (size_t s = 0; s < nseries; s++) {
                double v = i < chart->series[s].value_count ? chart->series[s].values[i] : 0.0;
                uint32_t color = yetty_ychart_resolve_color(chart, s);
                float bx0, bx1, top, bottom;
                if (chart->stacked) {
                    bx0 = slot_x0;
                    bx1 = slot_x0 + bar_w;
                    if (v >= 0.0) {
                        bottom = VALUE_TO_Y(stack_pos);
                        top = VALUE_TO_Y(stack_pos + v);
                        stack_pos += (float)v;
                    } else {
                        bottom = VALUE_TO_Y(stack_neg);
                        top = VALUE_TO_Y(stack_neg + v);
                        stack_neg += (float)v;
                    }
                } else {
                    bx0 = slot_x0 + (float)s * bar_w;
                    bx1 = bx0 + bar_w * 0.92f;
                    bottom = baseline_y;
                    top = VALUE_TO_Y(v);
                }
                struct yetty_ycore_void_result br =
                    yetty_ychart_emit_box(state, bx0, top, bx1, bottom, 2.0f, color, 0, 0.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "cartesian: column");
                if (chart->show_values && !chart->stacked) {
                    char buf[32];
                    fmt_num(buf, sizeof(buf), v);
                    struct yetty_ycore_void_result vl = yetty_ychart_emit_label(
                        state, (bx0 + bx1) * 0.5f, top - fs * 0.6f, buf, fs * 0.85f,
                        YETTY_YCHART_COLOR_TEXT, YETTY_YCHART_ANCHOR_CENTER);
                    YETTY_RETURN_IF_ERR(yetty_ycore_void, vl, "cartesian: column value");
                }
            }
        }
        return YETTY_OK_VOID();
    }

    /* LINE / AREA / SCATTER: one polyline (and optional fill) per series. */
    for (size_t s = 0; s < nseries; s++) {
        const struct yetty_ychart_series *series = &chart->series[s];
        uint32_t color = yetty_ychart_resolve_color(chart, s);
        size_t n = series->value_count < ncat ? series->value_count : ncat;
        if (n == 0) {
            continue;
        }

        if (kind == YETTY_YCHART_KIND_AREA) {
            for (size_t i = 0; i + 1 < n; i++) {
                float x0 = px0 + ((float)i + 0.5f) * slot;
                float x1 = px0 + ((float)i + 1.5f) * slot;
                float y0 = VALUE_TO_Y(series->values[i]);
                float y1 = VALUE_TO_Y(series->values[i + 1]);
                uint32_t fill = yetty_ychart_with_alpha(color, 0x66);
                struct yetty_ycore_void_result t1 =
                    yetty_ychart_emit_triangle(state, x0, y0, x1, y1, x0, baseline_y, fill);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, t1, "cartesian: area tri a");
                struct yetty_ycore_void_result t2 =
                    yetty_ychart_emit_triangle(state, x1, y1, x1, baseline_y, x0, baseline_y, fill);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, t2, "cartesian: area tri b");
            }
        }

        if (kind == YETTY_YCHART_KIND_LINE || kind == YETTY_YCHART_KIND_AREA) {
            for (size_t i = 0; i + 1 < n; i++) {
                float x0 = px0 + ((float)i + 0.5f) * slot;
                float x1 = px0 + ((float)i + 1.5f) * slot;
                float y0 = VALUE_TO_Y(series->values[i]);
                float y1 = VALUE_TO_Y(series->values[i + 1]);
                struct yetty_ycore_void_result ln =
                    yetty_ychart_emit_segment(state, x0, y0, x1, y1, color, 2.0f);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ln, "cartesian: line segment");
            }
        }

        /* Markers (always for scatter; small dots for line). */
        float radius = kind == YETTY_YCHART_KIND_SCATTER ? 4.0f : 3.0f;
        for (size_t i = 0; i < n; i++) {
            float cx;
            if (kind == YETTY_YCHART_KIND_SCATTER && series->x_values) {
                /* x from explicit x value, mapped across the category span. */
                double xv = series->x_values[i];
                cx = px0 + (float)((xv) / (double)(ncat > 1 ? ncat - 1 : 1)) * plot_w;
            } else {
                cx = px0 + ((float)i + 0.5f) * slot;
            }
            float cy = VALUE_TO_Y(series->values[i]);
            struct yetty_ycore_void_result mk =
                yetty_ychart_emit_circle(state, cx, cy, radius, color, 0, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, mk, "cartesian: marker");
        }
    }
#undef VALUE_TO_Y
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Horizontal bars: BAR
 *===========================================================================*/

static struct yetty_ycore_void_result render_bar(struct yetty_ychart_render_state *state, float px0,
                                                 float py0, float px1, float py1)
{
    const struct yetty_ychart_chart *chart = state->chart;
    float fs = state->opt->label_font_size;
    size_t ncat = chart->category_count;
    size_t nseries = chart->series_count;
    if (ncat == 0 || nseries == 0) {
        return YETTY_OK_VOID();
    }

    double data_min, data_max, axis_min, axis_max, tick;
    value_extent(chart, &data_min, &data_max);
    nice_axis(data_min, data_max, &axis_min, &axis_max, &tick);
    double span = axis_max - axis_min;
    float plot_w = px1 - px0;
    float plot_h = py1 - py0;

#define VALUE_TO_X(v) (px0 + (float)(((v) - axis_min) / span) * plot_w)

    for (double v = axis_min; v <= axis_max + tick * 0.5; v += tick) {
        float gx = VALUE_TO_X(v);
        struct yetty_ycore_void_result gl =
            yetty_ychart_emit_segment(state, gx, py0, gx, py1, YETTY_YCHART_COLOR_GRID, 1.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gl, "bar: gridline");
        char buf[32];
        fmt_num(buf, sizeof(buf), v);
        struct yetty_ycore_void_result tl =
            yetty_ychart_emit_label(state, gx, py1 + fs * 0.5f + 6.0f, buf, fs,
                                    YETTY_YCHART_COLOR_TEXT_MUTED, YETTY_YCHART_ANCHOR_CENTER);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tl, "bar: x tick label");
    }

    float zero_x = VALUE_TO_X(0.0);
    struct yetty_ycore_void_result ax =
        yetty_ychart_emit_segment(state, zero_x, py0, zero_x, py1, YETTY_YCHART_COLOR_AXIS, 1.5f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ax, "bar: value axis");

    float slot = plot_h / (float)ncat;
    float group_h = slot * 0.7f;
    float group_off = (slot - group_h) * 0.5f;
    float bar_h = chart->stacked ? group_h : group_h / (float)nseries;

    for (size_t i = 0; i < ncat; i++) {
        float slot_y0 = py0 + (float)i * slot;
        float center_y = slot_y0 + slot * 0.5f;
        struct yetty_ycore_void_result cl =
            yetty_ychart_emit_label(state, px0 - 6.0f, center_y, chart->categories[i], fs,
                                    YETTY_YCHART_COLOR_TEXT_MUTED, YETTY_YCHART_ANCHOR_RIGHT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cl, "bar: category label");

        float stack_pos = 0.0f, stack_neg = 0.0f;
        for (size_t s = 0; s < nseries; s++) {
            double v = i < chart->series[s].value_count ? chart->series[s].values[i] : 0.0;
            uint32_t color = yetty_ychart_resolve_color(chart, s);
            float by0, by1, left, right;
            if (chart->stacked) {
                by0 = slot_y0 + group_off;
                by1 = by0 + bar_h;
                if (v >= 0.0) {
                    left = VALUE_TO_X(stack_pos);
                    right = VALUE_TO_X(stack_pos + v);
                    stack_pos += (float)v;
                } else {
                    left = VALUE_TO_X(stack_neg);
                    right = VALUE_TO_X(stack_neg + v);
                    stack_neg += (float)v;
                }
            } else {
                by0 = slot_y0 + group_off + (float)s * bar_h;
                by1 = by0 + bar_h * 0.92f;
                left = zero_x;
                right = VALUE_TO_X(v);
            }
            struct yetty_ycore_void_result br =
                yetty_ychart_emit_box(state, left, by0, right, by1, 2.0f, color, 0, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "bar: bar");
            if (chart->show_values && !chart->stacked) {
                char buf[32];
                fmt_num(buf, sizeof(buf), v);
                struct yetty_ycore_void_result vl = yetty_ychart_emit_label(
                    state, right + 4.0f, (by0 + by1) * 0.5f, buf, fs * 0.85f,
                    YETTY_YCHART_COLOR_TEXT, YETTY_YCHART_ANCHOR_LEFT);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, vl, "bar: value");
            }
        }
    }
#undef VALUE_TO_X
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Entry
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_render_cartesian(
    struct yetty_ychart_render_state *state)
{
    const struct yetty_ychart_chart *chart = state->chart;
    float fs = state->opt->label_font_size;

    /* Legend (series). */
    const char *labels[64];
    uint32_t colors[64];
    size_t legend_count = build_legend(chart, labels, colors, 64);
    float legend_h = yetty_ychart_legend_height(state, legend_count);
    float legend_top = state->content_y1 - legend_h;

    /* Gutters: left for value/category labels, bottom for the other axis. */
    float left_gutter = 48.0f;
    float bottom_gutter = fs + 12.0f;

    float px0 = state->content_x0 + left_gutter;
    float px1 = state->content_x1 - 8.0f;
    float py0 = state->content_y0 + 4.0f;
    float py1 = legend_top - bottom_gutter;
    if (py1 <= py0 || px1 <= px0) {
        return YETTY_OK_VOID(); /* degenerate canvas */
    }

    struct yetty_ycore_void_result body;
    if (chart->kind == YETTY_YCHART_KIND_BAR) {
        body = render_bar(state, px0, py0, px1, py1);
    } else {
        body = render_vertical(state, chart->kind, px0, py0, px1, py1);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body, "render_cartesian: body");

    return yetty_ychart_emit_legend(state, labels, colors, legend_count, legend_top);
}
