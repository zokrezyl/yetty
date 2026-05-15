#ifndef YETTY_YTERM_YDRAW_LAYER_H
#define YETTY_YTERM_YDRAW_LAYER_H

#include <stdint.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * YPaint layer — renders SDF primitives as overlay on terminal text.
 * Implements the same terminal_layer_ops interface as text-layer.
 *
 * The layer is canvas-agnostic — it holds a `struct yetty_ydraw_canvas *`
 * and uses only the polymorphic surface. The variant is chosen at create
 * time via `kind`:
 *   - SCROLLING — primitives are cursor-relative and scroll with the
 *     terminal text. This is the "rich content" overlay (PDF, SVG, etc.).
 *   - STATIC    — primitives at absolute coordinates; no scrolling. Used
 *     by yui chrome (popups, statusbar). The layer sits in the terminal
 *     stack as a placeholder until yui content is wired in.
 */
enum yetty_yterm_ydraw_layer_kind {
    YETTY_YDRAW_LAYER_KIND_SCROLLING,
    YETTY_YDRAW_LAYER_KIND_STATIC,
};

struct yetty_yterm_terminal_layer_result yetty_yterm_ydraw_layer_create(
    enum yetty_yterm_ydraw_layer_kind kind, uint32_t cols, uint32_t rows,
    float cell_width, float cell_height,
    const struct yetty_context *context, yetty_yterm_request_render_fn request_render_fn,
    void *request_render_userdata, yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata,
    yetty_yterm_cursor_fn cursor_fn, void *cursor_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_YDRAW_LAYER_H */
