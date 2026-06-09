#ifndef YETTY_YVTERM_CONTENT_LAYER_H
#define YETTY_YVTERM_CONTENT_LAYER_H

#include <stdint.h>
#include <yetty/yfigure/figure.h>
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

/* ---- yvterm:grid — the terminal content as a yfigure ---------------------
 *
 * Stage 1 of retiring the "layer" concept: the content (libvterm text grid +
 * ydraw canvas) is seated as the lowest-z child of the terminal's root
 * container and rendered through the normal figure path, instead of a bespoke
 * pre-container pass. The grid figure borrows the content layer handed to
 * _create (the terminal still owns and destroys it).
 *
 * `yetty_yvterm_grid` is a yclass figure (class@yvterm:grid,
 * parent@yfigure:figure); `grid.h` (generated) carries its class accessor. */
struct yetty_yvterm_grid;
YETTY_YRESULT_DECLARE(yetty_yvterm_grid_ptr, struct yetty_yvterm_grid *);

/* Wrap an already-created content layer as the grid figure. `rect` is the
 * figure's pixel space in the root container — (0,0)-(cols*cw, rows*ch). */
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_figure_create(
    struct yetty_yrender_terminal_layer *content, struct yetty_ycore_rectangle rect);

/* Upcast to the figure base (stable pointer) for add_child / render. */
struct yetty_yfigure_figure *yetty_yvterm_grid_as_figure(struct yetty_yvterm_grid *grid);

/* Borrow the owned content layer (for the terminal's direct scroll / selection
 * / anchor / resize calls during the transition). */
struct yetty_yrender_terminal_layer *yetty_yvterm_grid_content(struct yetty_yvterm_grid *grid);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_CONTENT_LAYER_H */
