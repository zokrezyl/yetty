#ifndef YETTY_YCHART_YCHART_H
#define YETTY_YCHART_YCHART_H

/*
 * ychart — data (CSV / JSON / YAML) → ydraw buffer of MSD/MSDF primitives.
 *
 * Modelled on ydiagram: hand over a UTF-8 input string, get back a fresh
 * ydraw buffer populated with shape primitives and text spans. The caller
 * owns the buffer and frees it via yetty_ydraw_drawable_list_destroy.
 *
 * Supported chart families: bar / column / line / area / scatter / pie /
 * donut / radar / treemap / sankey. See chart-ir.h for the data model and
 * data-parser.h for the accepted input formats.
 *
 * For finer control, compose the lower-level modules directly:
 *   - chart-ir.h     : build / inspect the IR
 *   - data-parser.h  : CSV / JSON / YAML → IR
 *   - renderer.h     : IR → ydraw buffer
 */

#include <stddef.h>
#include <yetty/ychart/chart-ir.h>
#include <yetty/ychart/renderer.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result type for the high-level entry. On success, .value is a freshly
 * allocated ydraw buffer the caller owns. */
YETTY_YRESULT_DECLARE(yetty_ychart_buffer, struct yetty_ydraw_drawable_list *);

/* High-level: detect the data format, parse it, pick a chart kind, render.
 * Uses default render options and the crude text-width fallback. */
struct yetty_ychart_buffer_result yetty_ychart_render_data(const char *input, size_t len);

/* Same but with control over the chart kind (pass KIND_AUTO to let the data /
 * directive decide), render options, the source path (extension hint for
 * format detection — may be NULL), and a real text-measure callback (pass
 * NULL for the fallback). Any of the optional pointers may be NULL. */
struct yetty_ychart_buffer_result yetty_ychart_render_data_full(
    const char *input, size_t len, const char *path, enum yetty_ychart_kind kind_override,
    const struct yetty_ychart_render_options *render_options,
    yetty_ychart_measure_text_fn measure, void *measure_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCHART_YCHART_H */
