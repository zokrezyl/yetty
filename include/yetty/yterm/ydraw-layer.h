#ifndef YETTY_YTERM_YDRAW_LAYER_H
#define YETTY_YTERM_YDRAW_LAYER_H

#include <stdint.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * YDraw layer — renders SDF primitives as overlay on terminal text.
 * Implements the same terminal_layer_ops interface as text-layer.
 *
 * The layer is canvas-agnostic — it holds a `struct yetty_ydraw_canvas *`
 * and uses only the polymorphic surface. Only one variant exists now:
 *   - SCROLLING — primitives are cursor-relative and scroll with the
 *     terminal text. This is the "rich content" overlay (PDF, SVG, etc.).
 *
 * KIND_SCENE was retired with the ycompositor migration — yui's chrome
 * and ygui-emitted content (popups, statusbar, ygreeter, …) now flow
 * through root container instead of through a scene-canvas-backed
 * ydraw_layer. The enum stays single-valued for source compat with
 * existing call sites that pass KIND_SCROLLING explicitly.
 */
enum yetty_yterm_ydraw_layer_kind {
    YETTY_YDRAW_LAYER_KIND_SCROLLING,
};

struct yetty_yterm_terminal_layer_result yetty_yterm_ydraw_layer_create(
    enum yetty_yterm_ydraw_layer_kind kind, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, const struct yetty_context *context,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_YDRAW_LAYER_H */
