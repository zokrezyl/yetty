#ifndef YETTY_YTERM_TEXT_LAYER_H
#define YETTY_YTERM_TEXT_LAYER_H

#include <stdint.h>
#include <yetty/yterm/terminal.h>
#include <yetty/ywire/wire-statemachine.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Text layer - renders terminal text via libvterm */
struct yetty_yterm_terminal_layer_result yetty_yterm_terminal_text_layer_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterm_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata);

/* Wire-SM dispatch entry for the default (raw passthrough) sink. Pass
 * the layer's base pointer as userdata when registering via
 * yetty_ywire_wire_statemachine_set_default. */
struct yetty_ycore_void_result yetty_yterm_text_layer_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

/* Borrow the GPU cell buffer (12 bytes per cell, see text-layer.wgsl).
 * Pointer is owned by the text layer (or libvterm). Valid only until the
 * next call into text-layer that mutates the screen — read inside one
 * get_gpu_resource_set / render call only. */
void yetty_yterm_terminal_layer_terminal_text_layer_get_cells(
    const struct yetty_yrender_terminal_layer *self, const uint8_t **out_data, size_t *out_size);

/* Fill `out` with the text-layer's cell source — the per-cell rich-handle
 * accessor + the ref table — so the ydraw canvas can route per-cell drawable
 * refs through the libvterm cells. Both stay owned by the text layer. */
struct yetty_ydraw_cell_source;
void yetty_yterm_text_layer_fill_cell_source(struct yetty_yrender_terminal_layer *self,
                                             struct yetty_ydraw_cell_source *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_TEXT_LAYER_H */
