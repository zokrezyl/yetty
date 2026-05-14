#ifndef YETTY_YDIAGRAM_YDIAGRAM_H
#define YETTY_YDIAGRAM_YDIAGRAM_H

/*
 * ydiagram — text → ypaint buffer for Mermaid (and later Graphviz) diagrams.
 *
 * One-shot API: hand over a UTF-8 input string, get back a fresh ypaint
 * buffer populated with MSD shape primitives and MSDF text spans. The
 * caller owns the buffer and frees it via yetty_ypaint_core_buffer_destroy.
 *
 * For finer control (custom layout params, custom rendering options),
 * compose the lower-level modules directly:
 *   - graph-ir.h     : build / inspect the IR
 *   - mermaid-parser.h : Mermaid → IR
 *   - layout.h       : Sugiyama layered layout
 *   - renderer.h     : IR → ypaint buffer
 */

#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydiagram/renderer.h>
#include <yetty/ypaint-core/buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result type for the high-level entry. On success, .value is a freshly
 * allocated ypaint buffer the caller owns. */
YETTY_YRESULT_DECLARE(yetty_ydiagram_buffer, struct yetty_ypaint_core_buffer *);

/* High-level: parse Mermaid text, lay it out, emit MSD/MSDF primitives.
 * Returns an owned buffer. Uses default layout/render options and a crude
 * text-width fallback (font_size * 0.6 * char_count).
 *
 * For best label rendering, call the three-step API (parse → layout →
 * render) with a real measure callback. */
struct yetty_ydiagram_buffer_result yetty_ydiagram_render_mermaid(const char *input, size_t len);

/* Same but allows custom layout / render params and a real text-measure
 * function (used for centring labels and sizing nodes). Pass NULL for any
 * of the optional pointers to take defaults. */
struct yetty_ydiagram_buffer_result yetty_ydiagram_render_mermaid_full(
    const char *input, size_t len, const struct yetty_ydiagram_layout_params *layout_params,
    const struct yetty_ydiagram_render_options *render_options,
    yetty_ydiagram_measure_text_fn measure, void *measure_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDIAGRAM_YDIAGRAM_H */
