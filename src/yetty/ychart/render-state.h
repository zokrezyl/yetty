#ifndef YETTY_YCHART_RENDER_STATE_H
#define YETTY_YCHART_RENDER_STATE_H

/*
 * render-state.h — PRIVATE shared state and helpers for the chart renderers.
 *
 * Not installed (lives in src/, not include/). The public entry
 * yetty_ychart_render() in render-common.c builds a render_state, computes the
 * content rect, and dispatches to one family renderer; the family renderers
 * (renderer-cartesian.c / renderer-polar.c / renderer-hier.c) emit primitives
 * through the helpers declared here.
 *
 * Coordinates are pixels with y growing DOWN (the ydraw / screen convention).
 * Value axes therefore map larger values to smaller y.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ychart/chart-ir.h>
#include <yetty/ychart/renderer.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

struct yetty_ychart_render_state {
    struct yetty_ydraw_drawable_list *buf;
    const struct yetty_ychart_chart *chart;
    const struct yetty_ychart_render_options *opt;
    yetty_ychart_measure_text_fn measure;
    void *userdata;
    uint32_t z;

    float width; /* full canvas */
    float height;

    /* content rect — inside outer margin, below the title, above the legend.
     * Family renderers inset further (e.g. cartesian reserves axis gutters). */
    float content_x0, content_y0, content_x1, content_y1;
};

/* Text anchor for emit_label. */
enum yetty_ychart_anchor {
    YETTY_YCHART_ANCHOR_LEFT,
    YETTY_YCHART_ANCHOR_CENTER,
    YETTY_YCHART_ANCHOR_RIGHT,
};

/*=============================================================================
 * Shared helpers (render-common.c)
 *===========================================================================*/

float yetty_ychart_measure(struct yetty_ychart_render_state *state, const char *text,
                            float font_size);

/* Replace a colour's alpha byte (ARGB). */
uint32_t yetty_ychart_with_alpha(uint32_t color, uint8_t alpha);

struct yetty_ycore_void_result yetty_ychart_emit_box(struct yetty_ychart_render_state *state,
                                                      float x0, float y0, float x1, float y1,
                                                      float corner_radius, uint32_t fill_color,
                                                      uint32_t stroke_color, float stroke_width);

struct yetty_ycore_void_result yetty_ychart_emit_segment(struct yetty_ychart_render_state *state,
                                                          float x0, float y0, float x1, float y1,
                                                          uint32_t color, float width);

struct yetty_ycore_void_result yetty_ychart_emit_triangle(struct yetty_ychart_render_state *state,
                                                           float ax, float ay, float bx, float by,
                                                           float cx, float cy, uint32_t color);

struct yetty_ycore_void_result yetty_ychart_emit_circle(struct yetty_ychart_render_state *state,
                                                         float cx, float cy, float radius,
                                                         uint32_t fill_color, uint32_t stroke_color,
                                                         float stroke_width);

/* A poly-line through `count` points (no fill). */
struct yetty_ycore_void_result yetty_ychart_emit_polyline(struct yetty_ychart_render_state *state,
                                                           const float *xs, const float *ys,
                                                           size_t count, uint32_t color,
                                                           float width);

/* Emit text with baseline at `baseline_y`, left edge at `x`. */
struct yetty_ycore_void_result yetty_ychart_emit_text(struct yetty_ychart_render_state *state,
                                                       float x, float baseline_y, const char *text,
                                                       float font_size, uint32_t color);

/* Emit text anchored horizontally around `x`, vertically centred on `cy`. */
struct yetty_ycore_void_result yetty_ychart_emit_label(struct yetty_ychart_render_state *state,
                                                        float x, float cy, const char *text,
                                                        float font_size, uint32_t color,
                                                        enum yetty_ychart_anchor anchor);

/* A filled / outlined annular wedge (pie slice when inner_radius == 0).
 * Angles in radians, measured clockwise from the top (12 o'clock). */
struct yetty_ycore_void_result yetty_ychart_emit_wedge(struct yetty_ychart_render_state *state,
                                                        float cx, float cy, float inner_radius,
                                                        float outer_radius, float angle_start,
                                                        float angle_end, uint32_t color);

/* Title + legend (legend shows one swatch+label per entry). */
struct yetty_ycore_void_result yetty_ychart_emit_title(struct yetty_ychart_render_state *state);

struct yetty_ycore_void_result yetty_ychart_emit_legend(struct yetty_ychart_render_state *state,
                                                         const char *const *labels,
                                                         const uint32_t *colors, size_t count,
                                                         float legend_top);

/* Pixel height reserved at the bottom for a legend of `count` entries (0 when
 * the legend is disabled / empty). */
float yetty_ychart_legend_height(const struct yetty_ychart_render_state *state, size_t count);

/* Convenience: the largest absolute value across all series (>= 1e-9). */
double yetty_ychart_max_value(const struct yetty_ychart_chart *chart);

/*=============================================================================
 * Family entry points (one per .c)
 *===========================================================================*/

struct yetty_ycore_void_result yetty_ychart_render_cartesian(
    struct yetty_ychart_render_state *state);
struct yetty_ycore_void_result yetty_ychart_render_pie(struct yetty_ychart_render_state *state,
                                                        bool donut);
struct yetty_ycore_void_result yetty_ychart_render_radar(struct yetty_ychart_render_state *state);
struct yetty_ycore_void_result yetty_ychart_render_treemap(
    struct yetty_ychart_render_state *state);
struct yetty_ycore_void_result yetty_ychart_render_sankey(
    struct yetty_ychart_render_state *state);

#endif /* YETTY_YCHART_RENDER_STATE_H */
