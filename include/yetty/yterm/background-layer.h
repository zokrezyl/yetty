#ifndef YETTY_YTERM_BACKGROUND_LAYER_H
#define YETTY_YTERM_BACKGROUND_LAYER_H

#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Background layer — opaque RGBA fill across the pane viewport.
 *
 * Sits at index 0 in the terminal's layer stack. Its job is to provide the
 * "wipe" for its own pane region every frame: with multi-pane layouts on a
 * shared big target, no layer pass may issue a full-attachment LoadOp_Clear
 * (it would stomp the other panes), so the per-pane wipe has to come from a
 * scissored draw — which is exactly what this layer does.
 *
 * `color_rgba` is the fill colour, with components in 0..1. Pass NULL to get
 * the default (opaque black). The layer copies the values, so the caller's
 * array does not need to outlive the call.
 */
struct yetty_yterm_terminal_layer_result yetty_yterm_background_layer_create(
    const struct yetty_context *yetty_context, const float color_rgba[4]);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_BACKGROUND_LAYER_H */
