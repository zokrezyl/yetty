#ifndef YETTY_YTERM_YPAINT_LAYER_H
#define YETTY_YTERM_YPAINT_LAYER_H

#include <stdint.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * YPaint layer - renders SDF primitives as overlay on terminal text.
 *
 * This layer implements the same terminal_layer_ops interface as text-layer.
 * It receives ypaint data via the write() method (already processed/uncompressed).
 *
 * The write method expects data in the format:
 *   "args;payload" where:
 *   - args: comma-separated options (e.g., "--yaml", "--clear")
 *   - payload: primitive data (YAML or binary format)
 *
 * Scrolling mode: primitives are positioned relative to cursor and scroll
 * with terminal content. The layer syncs scrolling via scroll_lines().
 */

/* Create ypaint layer
 * @param cols, rows: grid dimensions
 * @param cell_width, cell_height: cell size in pixels
 * @param scrolling_mode: true for scrolling layer (syncs with text), false for overlay
 * @param context: yetty context for config access (fonts path, etc.)
 * @param request_render_fn: callback to request re-render
 * @param request_render_userdata: userdata for callback
 */
struct yetty_yterm_terminal_layer_result yetty_yterm_ypaint_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height, int scrolling_mode,
    const struct yetty_context *context, yetty_yterm_request_render_fn request_render_fn,
    void *request_render_userdata, yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata,
    yetty_yterm_cursor_fn cursor_fn, void *cursor_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_YPAINT_LAYER_H */
