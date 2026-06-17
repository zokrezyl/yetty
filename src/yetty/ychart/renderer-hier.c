/*
 * renderer-hier.c — treemap (squarified) + sankey (flow diagram).
 *
 * Treemap packs the first series' values into nested rectangles with the
 * squarified algorithm (Bruls, Huizing, van Wijk) so cells stay close to
 * square. Sankey lays nodes out in columns by longest-path layer and draws
 * each flow as a constant-thickness S-curve band woven under the node bars.
 */

#include "render-state.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Treemap
 *===========================================================================*/

struct tm_item {
    double value;
    size_t orig; /* index into categories */
};

struct tm_rect {
    float x, y, w, h;
    bool placed;
};

static int tm_cmp_desc(const void *a, const void *b)
{
    double va = ((const struct tm_item *)a)->value;
    double vb = ((const struct tm_item *)b)->value;
    if (va < vb) {
        return 1;
    }
    if (va > vb) {
        return -1;
    }
    return 0;
}

/* Worst aspect ratio of a candidate row of scaled areas laid along `side`. */
static double tm_worst(double row_sum, double row_min, double row_max, double side)
{
    double s2 = row_sum * row_sum;
    double w2 = side * side;
    double a = (w2 * row_max) / s2;
    double b = s2 / (w2 * row_min);
    return a > b ? a : b;
}

struct yetty_ycore_void_result yetty_ychart_render_treemap(struct yetty_ychart_render_state *state)
{
    const struct yetty_ychart_chart *chart = state->chart;
    size_t ncat = chart->category_count;
    if (ncat == 0 || chart->series_count == 0) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ychart_series *series = &chart->series[0];
    float fs = state->opt->label_font_size;

    /* Treemap labels live inside the cells; no separate legend. */
    float rx = state->content_x0;
    float ry = state->content_y0;
    float rw = state->content_x1 - state->content_x0;
    float rh = state->content_y1 - state->content_y0;
    if (rw < 8.0f || rh < 8.0f) {
        return YETTY_OK_VOID();
    }

    struct tm_item *items = calloc(ncat, sizeof(*items));
    struct tm_rect *rects = calloc(ncat, sizeof(*rects));
    if (!items || !rects) {
        free(items);
        free(rects);
        return YETTY_ERR(yetty_ycore_void, "treemap: out of memory");
    }

    size_t m = 0;
    double total = 0.0;
    for (size_t i = 0; i < ncat; i++) {
        double v = i < series->value_count ? fabs(series->values[i]) : 0.0;
        if (v > 0.0) {
            items[m].value = v;
            items[m].orig = i;
            m++;
            total += v;
        }
    }
    if (m == 0 || total <= 0.0) {
        free(items);
        free(rects);
        return YETTY_OK_VOID();
    }
    qsort(items, m, sizeof(*items), tm_cmp_desc);

    double scale = ((double)rw * (double)rh) / total;
    for (size_t i = 0; i < m; i++) {
        items[i].value *= scale; /* now in pixel-area units */
    }

    /* Squarify into the working rect. */
    float cx = rx, cy = ry, cw = rw, ch = rh;
    size_t i = 0;
    while (i < m && cw > 0.5f && ch > 0.5f) {
        double side = cw < ch ? cw : ch;
        size_t j = i;
        double row_sum = 0.0, row_min = 0.0, row_max = 0.0, best = 0.0;
        while (j < m) {
            double a = items[j].value;
            double new_sum = row_sum + a;
            double new_min = (j == i) ? a : (a < row_min ? a : row_min);
            double new_max = (j == i) ? a : (a > row_max ? a : row_max);
            double new_worst = tm_worst(new_sum, new_min, new_max, side);
            if (j == i || new_worst <= best) {
                row_sum = new_sum;
                row_min = new_min;
                row_max = new_max;
                best = new_worst;
                j++;
            } else {
                break;
            }
        }
        double thickness = row_sum / side;
        if (cw >= ch) {
            /* Column on the left, full current height. */
            float oy = cy;
            for (size_t k = i; k < j; k++) {
                float cell_h = (float)(items[k].value / thickness);
                rects[items[k].orig] = (struct tm_rect){cx, oy, (float)thickness, cell_h, true};
                oy += cell_h;
            }
            cx += (float)thickness;
            cw -= (float)thickness;
        } else {
            /* Row on top, full current width. */
            float ox = cx;
            for (size_t k = i; k < j; k++) {
                float cell_w = (float)(items[k].value / thickness);
                rects[items[k].orig] = (struct tm_rect){ox, cy, cell_w, (float)thickness, true};
                ox += cell_w;
            }
            cy += (float)thickness;
            ch -= (float)thickness;
        }
        i = j;
    }

    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    for (size_t c = 0; c < ncat && YETTY_IS_OK(result); c++) {
        if (!rects[c].placed) {
            continue;
        }
        float x0 = rects[c].x;
        float y0 = rects[c].y;
        float x1 = x0 + rects[c].w;
        float y1 = y0 + rects[c].h;
        uint32_t color = yetty_ychart_resolve_color(chart, c);
        result = yetty_ychart_emit_box(state, x0 + 1.0f, y0 + 1.0f, x1 - 1.0f, y1 - 1.0f, 2.0f,
                                       color, 0xFF0B1014u, 1.0f);
        if (YETTY_IS_ERR(result)) {
            break;
        }
        /* Label when the cell is roomy enough. */
        if (rects[c].w > 44.0f && rects[c].h > fs + 4.0f) {
            float lcx = (x0 + x1) * 0.5f;
            float lcy = (y0 + y1) * 0.5f;
            result = yetty_ychart_emit_label(
                state, lcx, lcy - (rects[c].h > 2 * fs ? fs * 0.6f : 0), chart->categories[c], fs,
                0xFF0B1014u, YETTY_YCHART_ANCHOR_CENTER);
            if (YETTY_IS_OK(result) && rects[c].h > 2 * fs) {
                char buf[32];
                double v = c < series->value_count ? series->values[c] : 0.0;
                double rounded = (double)llround(v);
                if (fabs(v - rounded) < 1e-9) {
                    snprintf(buf, sizeof(buf), "%lld", (long long)rounded);
                } else {
                    snprintf(buf, sizeof(buf), "%g", v);
                }
                result = yetty_ychart_emit_label(state, lcx, lcy + fs * 0.6f, buf, fs * 0.85f,
                                                 0xCC0B1014u, YETTY_YCHART_ANCHOR_CENTER);
            }
        }
    }

    free(items);
    free(rects);
    return result;
}

/*=============================================================================
 * Sankey
 *===========================================================================*/

struct sk_node {
    const char *name; /* borrowed from a flow string */
    int layer;
    double in_sum, out_sum, value;
    float x0, x1, y0, y1;
    float out_cursor, in_cursor;
    uint32_t color;
};

static int sk_find_or_add(struct sk_node *nodes, size_t *count, const char *name)
{
    for (size_t i = 0; i < *count; i++) {
        if (strcmp(nodes[i].name, name) == 0) {
            return (int)i;
        }
    }
    size_t idx = (*count)++;
    memset(&nodes[idx], 0, sizeof(nodes[idx]));
    nodes[idx].name = name;
    return (int)idx;
}

/* Smoothstep interpolation a→b. */
static float sk_smooth(float a, float b, float t)
{
    float s = t * t * (3.0f - 2.0f * t);
    return a + (b - a) * s;
}

/* One constant-thickness band from (sx, sy_top) to (tx, ty_top). */
static struct yetty_ycore_void_result sk_band(struct yetty_ychart_render_state *state, float sx,
                                              float sy_top, float tx, float ty_top, float thickness,
                                              uint32_t color)
{
    const int steps = 24;
    float prev_x = sx;
    float prev_top = sy_top;
    for (int k = 1; k <= steps; k++) {
        float t = (float)k / (float)steps;
        float x = sx + (tx - sx) * t;
        float top = sk_smooth(sy_top, ty_top, t);
        struct yetty_ycore_void_result t1 =
            yetty_ychart_emit_triangle(state, prev_x, prev_top, x, top, x, top + thickness, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, t1, "sankey: band tri a");
        struct yetty_ycore_void_result t2 = yetty_ychart_emit_triangle(
            state, prev_x, prev_top, x, top + thickness, prev_x, prev_top + thickness, color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, t2, "sankey: band tri b");
        prev_x = x;
        prev_top = top;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ychart_render_sankey(struct yetty_ychart_render_state *state)
{
    const struct yetty_ychart_chart *chart = state->chart;
    size_t nflows = chart->flow_count;
    if (nflows == 0) {
        return YETTY_OK_VOID();
    }
    float fs = state->opt->label_font_size;

    size_t max_nodes = nflows * 2;
    struct sk_node *nodes = calloc(max_nodes, sizeof(*nodes));
    int *src_idx = calloc(nflows, sizeof(*src_idx));
    int *dst_idx = calloc(nflows, sizeof(*dst_idx));
    if (!nodes || !src_idx || !dst_idx) {
        free(nodes);
        free(src_idx);
        free(dst_idx);
        return YETTY_ERR(yetty_ycore_void, "sankey: out of memory");
    }

    size_t node_count = 0;
    for (size_t f = 0; f < nflows; f++) {
        src_idx[f] = sk_find_or_add(nodes, &node_count, chart->flows[f].source);
        dst_idx[f] = sk_find_or_add(nodes, &node_count, chart->flows[f].target);
        nodes[src_idx[f]].out_sum += chart->flows[f].value;
        nodes[dst_idx[f]].in_sum += chart->flows[f].value;
    }
    for (size_t n = 0; n < node_count; n++) {
        nodes[n].value = nodes[n].in_sum > nodes[n].out_sum ? nodes[n].in_sum : nodes[n].out_sum;
        nodes[n].color = yetty_ychart_resolve_color(chart, n);
    }

    /* Longest-path layering (cap iterations to survive accidental cycles). */
    for (size_t iter = 0; iter < node_count; iter++) {
        bool changed = false;
        for (size_t f = 0; f < nflows; f++) {
            if (nodes[dst_idx[f]].layer < nodes[src_idx[f]].layer + 1) {
                nodes[dst_idx[f]].layer = nodes[src_idx[f]].layer + 1;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    int max_layer = 0;
    for (size_t n = 0; n < node_count; n++) {
        if (nodes[n].layer > max_layer) {
            max_layer = nodes[n].layer;
        }
    }
    int n_layers = max_layer + 1;

    float area_x0 = state->content_x0;
    float area_x1 = state->content_x1;
    float area_y0 = state->content_y0;
    float area_y1 = state->content_y1;
    float area_h = area_y1 - area_y0;
    float node_w = 16.0f;
    float gap = 10.0f;

    /* Global value→pixel scale: the busiest column must fit the height. */
    double scale = 1e9;
    for (int l = 0; l < n_layers; l++) {
        double total = 0.0;
        size_t count = 0;
        for (size_t n = 0; n < node_count; n++) {
            if (nodes[n].layer == l) {
                total += nodes[n].value;
                count++;
            }
        }
        if (total <= 0.0 || count == 0) {
            continue;
        }
        double usable = (double)area_h - (double)(count - 1) * gap;
        if (usable < 1.0) {
            usable = 1.0;
        }
        double s = usable / total;
        if (s < scale) {
            scale = s;
        }
    }
    if (scale >= 1e9) {
        scale = 1.0;
    }

    /* Assign node rects per column (centred vertically). */
    for (int l = 0; l < n_layers; l++) {
        size_t count = 0;
        double total_val = 0.0;
        for (size_t n = 0; n < node_count; n++) {
            if (nodes[n].layer == l) {
                count++;
                total_val += nodes[n].value;
            }
        }
        if (count == 0) {
            continue;
        }
        float col_h = (float)(total_val * scale) + (float)(count - 1) * gap;
        float y = area_y0 + (area_h - col_h) * 0.5f;
        float x0 = (n_layers > 1)
                       ? area_x0 + (float)l * (area_x1 - area_x0 - node_w) / (float)(n_layers - 1)
                       : area_x0;
        for (size_t n = 0; n < node_count; n++) {
            if (nodes[n].layer != l) {
                continue;
            }
            float h = (float)(nodes[n].value * scale);
            if (h < 1.0f) {
                h = 1.0f;
            }
            nodes[n].x0 = x0;
            nodes[n].x1 = x0 + node_w;
            nodes[n].y0 = y;
            nodes[n].y1 = y + h;
            nodes[n].out_cursor = y;
            nodes[n].in_cursor = y;
            y += h + gap;
        }
    }

    struct yetty_ycore_void_result result = YETTY_OK_VOID();

    /* Flow bands first (so node bars draw on top). */
    for (size_t f = 0; f < nflows && YETTY_IS_OK(result); f++) {
        struct sk_node *src = &nodes[src_idx[f]];
        struct sk_node *dst = &nodes[dst_idx[f]];
        float thickness = (float)(chart->flows[f].value * scale);
        if (thickness < 0.5f) {
            thickness = 0.5f;
        }
        uint32_t color = yetty_ychart_with_alpha(src->color, 0x66);
        result =
            sk_band(state, src->x1, src->out_cursor, dst->x0, dst->in_cursor, thickness, color);
        src->out_cursor += thickness;
        dst->in_cursor += thickness;
    }

    /* Node bars + labels. */
    for (size_t n = 0; n < node_count && YETTY_IS_OK(result); n++) {
        result = yetty_ychart_emit_box(state, nodes[n].x0, nodes[n].y0, nodes[n].x1, nodes[n].y1,
                                       1.0f, nodes[n].color, 0, 0.0f);
        if (YETTY_IS_ERR(result)) {
            break;
        }
        float cy = (nodes[n].y0 + nodes[n].y1) * 0.5f;
        if (nodes[n].layer == max_layer) {
            result = yetty_ychart_emit_label(state, nodes[n].x0 - 5.0f, cy, nodes[n].name, fs,
                                             YETTY_YCHART_COLOR_TEXT, YETTY_YCHART_ANCHOR_RIGHT);
        } else {
            result = yetty_ychart_emit_label(state, nodes[n].x1 + 5.0f, cy, nodes[n].name, fs,
                                             YETTY_YCHART_COLOR_TEXT, YETTY_YCHART_ANCHOR_LEFT);
        }
    }

    free(nodes);
    free(src_idx);
    free(dst_idx);
    return result;
}
