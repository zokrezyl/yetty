#ifndef YETTY_YVTERM_TEXT_LAYER_H
#define YETTY_YVTERM_TEXT_LAYER_H

#include <stdint.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ywire/wire-statemachine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Text layer - renders terminal text via libvterm */
struct yetty_yterminal_layer_result yetty_yvterm_text_layer_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterminal_cursor_fn cursor_fn,
    void *cursor_userdata);

/* Wire-SM dispatch entry for the default (raw passthrough) sink. Pass
 * the layer's base pointer as userdata when registering via
 * yetty_ywire_wire_statemachine_set_default. */
struct yetty_ycore_void_result yetty_yvterm_text_layer_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

/* Borrow the GPU cell buffer (sizeof(VTermScreenCell), currently 16 bytes;
 * see text-layer.wgsl).
 * Pointer is owned by the text layer (or libvterm). Valid only until the
 * next call into text-layer that mutates the screen — read inside one
 * get_gpu_resource_set / render call only. */
void yetty_yvterm_text_layer_get_cells(
    const struct yetty_yrender_terminal_layer *self, const uint8_t **out_data, size_t *out_size);

/* Row within the borrowed cell buffer where the visible screen's row 0 sits.
 * libvterm allocates the live buffer 2*rows tall and bumps this offset on
 * full-screen scroll-up instead of memmoving. Siblings that read the same
 * cells (shader-glyph figure) must add this offset to locate visible cells,
 * exactly as text-layer.wgsl does. Returns 0 in scrollback-view mode, where
 * get_cells hands back a staging buffer already laid out from row 0. */
uint32_t yetty_yvterm_text_layer_get_root_row(
    const struct yetty_yrender_terminal_layer *self);

/* TEMP isolation: render the text-layer's owned figures (shader-glyph). */
struct yetty_ydraw_target;
struct yetty_ycore_void_result yetty_yvterm_text_layer_render_figures(
    struct yetty_yrender_terminal_layer *self, struct yetty_ydraw_target *target);

/* Wire the text-layer's per-cell rich-handle table into the ydraw layer so the
 * ydraw canvas can route per-cell drawable refs through the libvterm cells.
 * The handle accessor + table stay private to the text-layer (built into a
 * local cell source here, never exposed). Collapses to internal wiring once
 * the ydraw primitives are folded in. */
struct yetty_ycore_void_result yetty_yvterm_text_layer_bind_ydraw(
    struct yetty_yrender_terminal_layer *self, struct yetty_yrender_terminal_layer *ydraw_layer);


#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_TEXT_LAYER_H */
