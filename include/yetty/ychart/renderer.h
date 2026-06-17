#ifndef YETTY_YCHART_RENDERER_H
#define YETTY_YCHART_RENDERER_H

/*
 * renderer — emit a chart IR into a ydraw buffer using MSD (SDF shape) and
 * MSDF (text) primitives.
 *
 * Caller owns the ydraw buffer; we only append primitives and set the scene
 * bounds from the chart's computed canvas size. Each family (cartesian /
 * polar / hierarchical) lives in its own translation unit but shares the
 * private render state and helpers in src/yetty/ychart/render-state.h.
 *
 * For text measurement (centring labels, sizing the legend) the renderer
 * accepts a callback; passing NULL gives a crude 0.6 * font_size * len
 * fallback — adequate for diagnostics, loose for production.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ychart/chart-ir.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Measure a UTF-8 string at the given font size, in pixels. The supplied
 * data is NOT NUL-terminated. Same signature as ydiagram's measure callback,
 * so a host can share one font measurer between the two. */
typedef float (*yetty_ychart_measure_text_fn)(const char *text, size_t text_len, float font_size,
                                               void *userdata);

struct yetty_ychart_render_options {
    float width;  /* canvas width in px (0 → default 900) */
    float height; /* canvas height in px (0 → default 540) */

    float title_font_size; /* px, default 18 */
    float label_font_size; /* px, default 12 */

    uint32_t background_color; /* 0 = transparent; otherwise paint a backdrop */

    /* Prepend a CMD_ZERO that clears the receiving canvas and resets its
     * cursor to (0,0) on decode — full-redraw / replace-the-pane semantics
     * (default true; what a re-rendering widget wants). Set false for
     * cat-like one-shot use (ycat): the chart then flows inline at the
     * current cursor instead of jumping to the pane origin. */
    bool clear_canvas;
};

struct yetty_ychart_render_options yetty_ychart_default_render_options(void);

/* Walk the chart IR and emit primitives into `buffer`. `chart->kind` selects
 * the family; KIND_AUTO must already have been resolved to a concrete kind by
 * the caller (the high-level entry does this). */
struct yetty_ycore_void_result yetty_ychart_render(
    const struct yetty_ychart_chart *chart, struct yetty_ydraw_drawable_list *buffer,
    const struct yetty_ychart_render_options *options, yetty_ychart_measure_text_fn measure,
    void *measure_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCHART_RENDERER_H */
