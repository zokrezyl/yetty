#ifndef YETTY_YTERM_YMGUI_LAYER_H
#define YETTY_YTERM_YMGUI_LAYER_H

#include <stdint.h>
#include <yetty/yterm/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ymgui layer — renders Dear ImGui frames produced by client apps.
 *
 * v2 model: the layer hosts a registry of "cards" — placed sub-regions
 * of the terminal grid, each with its own ImGui frame and font atlas.
 * Cards are addressed by client-allocated u32 IDs. Wire details live
 * in include/yetty/ymgui/wire.h.
 *
 * Each card is anchored at a rolling_row at placement time and scrolls
 * with terminal content (same anchoring model the ydraw canvas uses).
 * Width/height are in grid cells; the layer derives pixel size from
 * cell_size. CARD_PLACE on an unknown id creates the card; on a known
 * id moves/resizes it.
 */

/* Result of a hit-test against the live cards. figure_id == 0 means no
 * card was under the queried point (or the topmost card had been
 * scrolled off-screen). When figure_id != 0, local_x/y are the cursor
 * coordinates expressed in the card's own pixel space (origin at the
 * card's top-left). */
struct yetty_yterm_ymgui_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};

struct yetty_yterm_terminal_layer_result yetty_yterm_ymgui_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height,
    const struct yetty_context *context, yetty_yterm_request_render_fn request_render_fn,
    void *request_render_userdata, yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata,
    yetty_yterm_cursor_fn cursor_fn, void *cursor_userdata);

/* Hit-test: which card sits under the terminal-pane pixel (px, py)?
 * Returns {figure_id=0} if none. The pane-pixel coordinate space is the
 * terminal's view-local space (already de-offset against view bounds). */
struct yetty_yterm_ymgui_hit yetty_yterm_terminal_layer_ymgui_layer_hit_test(
    const struct yetty_yrender_terminal_layer *layer, float px, float py);

/* Currently focused card, or 0 if no card has focus. */
uint32_t yetty_yterm_terminal_layer_ymgui_layer_focused_card(
    const struct yetty_yrender_terminal_layer *layer);

/* Set focus to `figure_id` (0 = no focus). If this differs from the
 * current focus, the layer fires FOCUS-lost on the old card and
 * FOCUS-gained on the new card via the layer's emit_osc_fn. No-op
 * when the new id matches the current focus. */
struct yetty_ycore_void_result yetty_yterm_terminal_layer_ymgui_layer_set_focus(
    struct yetty_yrender_terminal_layer *layer, uint32_t figure_id);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_YMGUI_LAYER_H */
