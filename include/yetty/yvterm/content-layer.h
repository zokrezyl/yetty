#ifndef YETTY_YVTERM_CONTENT_LAYER_H
#define YETTY_YVTERM_CONTENT_LAYER_H

#include <stdint.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ywire/wire-statemachine.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Content layer — the single terminal_layer a terminal renders through.
 *
 * It owns and drives two sub-renderers that used to be sibling layers in
 * terminal->layers[]:
 *
 *   - the libvterm text grid   (text-layer.c)
 *   - the ydraw rich-content canvas (ydraw-content.c)
 *
 * Both keep their own GPU resource set + shader, so the content layer's
 * render op drives TWO render handles (text-layer.wgsl then ydraw-layer.wgsl)
 * into the same target — the render-target binder cache is keyed per layer
 * pointer, so two shaders need two handles. Everything that used to round-trip
 * text<->ydraw through the terminal (scroll, cursor, alt-screen, clear,
 * selection, view-top, resize, visual-zoom) is now internal wiring here; the
 * terminal sees one layer.
 *
 * The text grid is the unit that becomes a yfigure in a follow-up step, which
 * is why it stays in its own file rather than being inlined here.
 */
struct yetty_yterminal_layer_result yetty_yvterm_content_layer_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata);

/* Register the content layer's wire-SM handlers: the text grid as the default
 * (raw passthrough) sink, and the ydraw canvas for the YDRAW OSC codes
 * (CLEAR / BIN / OVERLAY). `self` is the base returned by _create. */
struct yetty_ycore_void_result yetty_yvterm_content_layer_register_wire(
    struct yetty_yrender_terminal_layer *self, struct yetty_ywire_wire_statemachine *sm);

/* Optional hook fired after the sub-renderers are cleared on a full-screen erase
 * (CSI 2J/3J or RIS). The terminal registers it to also clear its root figure
 * container — positioned compositor figures the sub-renderers don't own. */
typedef struct yetty_ycore_void_result (*yetty_yvterm_content_clear_hook_fn)(void *userdata);
void yetty_yvterm_content_layer_set_clear_hook(struct yetty_yrender_terminal_layer *self,
                                               yetty_yvterm_content_clear_hook_fn fn,
                                               void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_CONTENT_LAYER_H */
