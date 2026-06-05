#ifndef YETTY_YVTERM_YDRAW_CONTENT_H
#define YETTY_YVTERM_YDRAW_CONTENT_H

#include <stdint.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ywire/wire-statemachine.h>

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
 * ydraw_content. The enum stays single-valued for source compat with
 * existing call sites that pass KIND_SCROLLING explicitly.
 */
enum yetty_yvterm_ydraw_content_kind {
    YETTY_YVTERM_YDRAW_CONTENT_KIND_SCROLLING,
};

struct yetty_yterminal_layer_result yetty_yvterm_ydraw_content_create(
    enum yetty_yvterm_ydraw_content_kind kind, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, const struct yetty_context *context,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterminal_cursor_fn cursor_fn,
    void *cursor_userdata);

/* Wire-SM dispatch entry. Pass the layer's base pointer as userdata
 * when registering via yetty_ywire_wire_statemachine_register. */
struct yetty_ycore_void_result yetty_yvterm_ydraw_content_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

/* Bind the text-grid cell source onto the layer's canvas so per-cell
 * rich-handles route through the text-layer-owned table. Forwarded to
 * canvas->ops->set_cell_source. */
struct yetty_ydraw_cell_source;
struct yetty_ycore_void_result yetty_yvterm_ydraw_content_set_cell_source(
    struct yetty_yrender_terminal_layer *self, const struct yetty_ydraw_cell_source *source);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_YDRAW_CONTENT_H */
