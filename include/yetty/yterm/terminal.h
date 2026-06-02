#ifndef YETTY_YTERM_TERMINAL_H
#define YETTY_YTERM_TERMINAL_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yetty/yetty.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct yetty_yterm_terminal;
struct yetty_yrender_terminal_layer;
struct yetty_yterm_terminal_layer_ops;
struct yetty_ywire_wire_statemachine;
struct yetty_yevent_event_loop;
struct yetty_platform_pty;
struct yetty_yui_view;
struct yetty_yrender_gpu_resource_binder;
struct yetty_ydraw_target;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yterm_terminal, struct yetty_yterm_terminal *);
YETTY_YRESULT_DECLARE(yetty_yterm_terminal_layer, struct yetty_yrender_terminal_layer *);

/* PTY write callback - called when layer needs to send data to PTY */
typedef struct yetty_ycore_void_result (*yetty_yterm_pty_write_fn)(const char *data, size_t len,
                                                                   void *userdata);

/* Request render callback - called when layer needs a render frame */
typedef struct yetty_ycore_void_result (*yetty_yterm_request_render_fn)(void *userdata);

/* Scroll callback - called when layer scrolls, passes source layer and line
 * count */
typedef struct yetty_ycore_void_result (*yetty_yterm_scroll_fn)(
    struct yetty_yrender_terminal_layer *source, int lines, void *userdata);

/* Cursor callback - called when layer moves cursor, passes source layer and
 * position */
typedef struct yetty_ycore_void_result (*yetty_yterm_cursor_fn)(
    struct yetty_yrender_terminal_layer *source, struct yetty_ycore_grid_cursor_pos cursor_pos,
    void *userdata);

/* Mouse-subscription callback - fired when libvterm flips DEC mode 1500
 * (card click, click_enabled) or 1501 (card move, move_enabled). The two
 * args carry the *current* subscription state for both modes; the layer
 * fires this whenever either changes. */
typedef struct yetty_ycore_void_result (*yetty_yterm_mouse_sub_fn)(int click_enabled,
                                                                   int move_enabled,
                                                                   void *userdata);

/* Terminal-wide input subscription callback — fired when a layer
 * receives YETTY_OSC_CS_CLIENT_INPUT_SUB from the inferior. `flags` is the
 * new (post-update) bitmask of YETTY_YMGUI_TERM_SUB_* bits. flags == 0
 * means full unsubscribe. */
typedef struct yetty_ycore_void_result (*yetty_yterm_term_input_sub_fn)(uint32_t flags,
                                                                        void *userdata);

/* OSC emit callback - fires when a layer needs to send an OSC envelope
 * back to the client app (PTY child). Used by ymgui-layer to deliver
 * focus events, by future bidirectional layers, etc. The terminal
 * implements this via terminal_yface_emit. */
typedef struct yetty_ycore_void_result (*yetty_yterm_emit_osc_fn)(int osc_code, const void *payload,
                                                                  size_t len, void *userdata);

/* Alt-screen-toggle callback - fired by the text-layer when libvterm
 * switches in/out of alternate-screen mode (DEC ?1047/?1049/?47). The
 * terminal forwards the new state to every layer via its set_alt_screen
 * op so each layer can save/restore its content. */
typedef struct yetty_ycore_void_result (*yetty_yterm_alt_screen_fn)(int active, void *userdata);

/* Clear-screen callback - fired by the text-layer when libvterm processes
 * a full-screen erase (CSI 2J / 3J) on the currently active screen. The
 * terminal forwards via each layer's clear_screen op; the layer is expected
 * to wipe the active half of its content (primary or alternate — the layer
 * already tracks which is current via set_alt_screen). */
typedef struct yetty_ycore_void_result (*yetty_yterm_clear_screen_fn)(void *userdata);

/* Layer ops */
struct yetty_yterm_terminal_layer_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_yrender_terminal_layer *self);
    /* Single atomic update: grid (cols/rows) AND cell pixel size. The
     * canvas's grid_pixel area is `cols * cell_w x rows * cell_h`, and
     * widget/primitive coordinates are mapped to fragments via that
     * product — so the two must move together or primitives at
     * the right/bottom edge get scaled or clipped (statusbar at H-22
     * was the trigger). Used for both window resize (caller picks
     * rows = round(client_h / cell_h_target), cell_h = client_h / rows
     * so the product equals the framebuffer exactly) and structural
     * zoom (caller passes scaled cell_size with the current grid).
     * Implementations propagate cell_size into the canvas via
     * canvas->ops->set_cell_size, into the GPU uniform, and ask the
     * font to re-rasterise where relevant. */
    struct yetty_ycore_void_result (*resize_grid)(struct yetty_yrender_terminal_layer *self,
                                                  struct yetty_ycore_grid_size grid_size,
                                                  struct yetty_ycore_pixel_size cell_size);
    /* Push visual (shader-level) zoom state into the layer's uniforms. The
   * layer's fragment shader applies the transform at the start of fs_main so
   * all downstream MSDF/SDF math runs at the *transformed* pixel — unlike
   * blend-stage zoom which is a post-rasterization bitmap stretch. */
    struct yetty_ycore_void_result (*set_visual_zoom)(struct yetty_yrender_terminal_layer *self,
                                                      float scale, float offset_x, float offset_y);
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        const struct yetty_yrender_terminal_layer *self);
    /* Render layer to target.
     *
     * `force` == 1 → a lower layer has just re-rendered into the same
     * shared big_target, so this layer's previous-frame pixels in the
     * regions it owns are gone. The layer MUST redraw all of its
     * content (simple prims + every figure) regardless of dirty bits.
     *
     * Returns 1 if anything was actually drawn (so the caller flips
     * its cascade flag for layers above), 0 if the layer was clean
     * and not forced and skipped entirely. Returns an error if any
     * inner step failed — propagate, never aggregate or silently
     * skip. */
    struct yetty_ycore_int_result (*render)(struct yetty_yrender_terminal_layer *self,
                                            struct yetty_ydraw_target *target, int force);
    /* Returns 1 if the layer has anything to repaint this frame — its own
     * dirty bit, a dirty sub-canvas, or any dirty figure/widget the layer
     * owns. Used by the terminal's pre-render scan: if any layer reports
     * dirty, every layer is invoked with force=1, so stale pixels from a
     * higher layer's old position (e.g. a moved ymgui window) get covered
     * by the lower layers underneath. Must mirror the conditions the
     * layer's render method itself checks — anything that would make
     * render redraw must also flip is_dirty. */
    int (*is_dirty)(const struct yetty_yrender_terminal_layer *self);
    /* Returns 1 if layer has no content to render (skip rendering, use
   * transparent texture) */
    int (*is_empty)(const struct yetty_yrender_terminal_layer *self);
    /* Keyboard input - returns 1 if handled */
    int (*on_key)(struct yetty_yrender_terminal_layer *self, int key, int mods);
    int (*on_char)(struct yetty_yrender_terminal_layer *self, uint32_t codepoint, int mods);
    /* Scroll - called when another layer scrolls, lines > 0 = scroll down */
    struct yetty_ycore_void_result (*scroll)(struct yetty_yrender_terminal_layer *self, int lines);
    /* Cursor - called when another layer moves cursor */
    struct yetty_ycore_void_result (*set_cursor)(struct yetty_yrender_terminal_layer *self, int col,
                                                 int row);
    /* Live anchor — absolute line index that represents "top of live viewport"
   * right now. Text-layer returns its scrollback row count (lines pushed off
   * the top of the screen); ydraw-layer returns its canvas rolling_row_0.
   * The terminal uses this value to convert mouse-wheel deltas into a stable
   * absolute view_top_total_idx that doesn't drift when new content arrives
   * during scrollback view. Optional — NULL means the layer can't anchor a
   * scrollback view (it'll be skipped by set_view_top). */
    uint32_t (*get_live_anchor)(const struct yetty_yrender_terminal_layer *self);
    /* Pin the viewport to the absolute line index `view_top_total_idx` while
   * `active` is non-zero. When active=0, return to live (track the live
   * anchor). The layer is expected to freeze its display at the given
   * historical position even as new content keeps arriving. Optional. */
    struct yetty_ycore_void_result (*set_view_top)(struct yetty_yrender_terminal_layer *self,
                                                   int active, uint32_t view_top_total_idx);
    /* Switch in/out of alternate-screen mode. active=1 → save current
   * content and present a fresh empty surface; active=0 → drop the alt
   * content and restore what was saved. Mirrors libvterm's behavior on
   * DEC ?1047/?1049/?47. Optional — layers without persistent content
   * (text-layer, since libvterm itself owns the swap) may leave NULL. */
    struct yetty_ycore_void_result (*set_alt_screen)(struct yetty_yrender_terminal_layer *self,
                                                     int active);
    /* Full-screen erase on the currently active screen (CSI 2J / 3J).
     * The layer should wipe the active half of its content; the
     * alt/primary split is already encoded in the layer's own state.
     * Optional — text-layer leaves NULL (libvterm clears its own
     * cells); layers that hold orthogonal state (ydraw canvas,
     * future ypaint) implement this. */
    struct yetty_ycore_void_result (*clear_screen)(struct yetty_yrender_terminal_layer *self);
    /* Selection — clipboard copy + highlight rendering.
     *
     * The selection has cell precision: an anchor (the cell the user
     * clicked) and a head (the cell the cursor is now over). Reading
     * order is anchor→head, top-down. Each endpoint is (row, col) in
     * visible-grid coordinates. row in [0, grid_size.rows), col in
     * [0, grid_size.cols].
     *
     * How each layer interprets that depends on the layer:
     *
     *   text-layer  — xterm-style stream selection. Cells are tinted and
     *                 extracted from anchor to head (full rows in the
     *                 middle, partial first/last rows).
     *   ydraw-layer — the column part is meaningless on rich content,
     *                 so the layer treats the row span [min_row, max_row]
     *                 as the "touched rows" and selects the first
     *                 primitive overlapping each touched row, as a whole.
     *
     * Both ops are optional — NULL means "this layer doesn't participate
     * in selection". active=0 clears any prior selection on the layer.
     */
    struct yetty_ycore_void_result (*set_selection)(struct yetty_yrender_terminal_layer *self,
                                                    int active, uint32_t anchor_row,
                                                    uint32_t anchor_col, uint32_t head_row,
                                                    uint32_t head_col);
    /* Append this layer's contribution to the current selection to out
     * as UTF-8. Called once per copy; the layer drives both row iteration
     * and intra-row column handling internally. */
    struct yetty_ycore_void_result (*get_selection_text)(
        const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out);
};

/* Layer base - embed as first member in subclasses */
struct yetty_yrender_terminal_layer {
    const struct yetty_yterm_terminal_layer_ops *ops;
    struct yetty_ycore_grid_size grid_size;
    struct yetty_ycore_pixel_size cell_size;

    int dirty;
    int in_external_scroll; /* Set when receiving scroll from another layer */
    /* PTY write callback - set by creator */
    yetty_yterm_pty_write_fn pty_write_fn;
    void *pty_write_userdata;
    /* Request render callback - set by creator */
    yetty_yterm_request_render_fn request_render_fn;
    void *request_render_userdata;
    /* Scroll callback - set by creator */
    yetty_yterm_scroll_fn scroll_fn;
    void *scroll_userdata;
    /* Cursor callback - set by creator */
    yetty_yterm_cursor_fn cursor_fn;
    void *cursor_userdata;
    /* Mouse-subscription callback - set by creator. Optional. */
    yetty_yterm_mouse_sub_fn mouse_sub_fn;
    void *mouse_sub_userdata;
    /* Terminal-wide input subscription callback - set by creator. Optional.
     * Fired when a layer receives YETTY_OSC_CS_CLIENT_INPUT_SUB. */
    yetty_yterm_term_input_sub_fn term_input_sub_fn;
    void *term_input_sub_userdata;
    /* OSC emit callback - set by creator. Optional. Used by layers that
   * need to send envelopes back to the PTY child (e.g. ymgui-layer for
   * focus events). */
    yetty_yterm_emit_osc_fn emit_osc_fn;
    void *emit_osc_userdata;
    /* Alt-screen-toggle callback - set by creator. Fired by the layer
   * that hosts libvterm (text-layer) when the inferior toggles DEC
   * ?1049 / ?1047 / ?47. The terminal hooks this and broadcasts via
   * each layer's set_alt_screen op. */
    yetty_yterm_alt_screen_fn alt_screen_fn;
    void *alt_screen_userdata;
    /* Clear-screen callback - set by creator. Fired by the text-layer
   * when libvterm reports a full-screen erase. The terminal hooks this
   * and broadcasts via each layer's clear_screen op. */
    yetty_yterm_clear_screen_fn clear_screen_fn;
    void *clear_screen_userdata;
};

/* Terminal creation/destruction.
 *
 * The pane background colour is read from the config in yetty_context as
 * "terminal/background-color" (hex string: "#RGB", "#RGBA", "#RRGGBB" or
 * "#RRGGBBAA"). Default when unset or unparseable is opaque black. */
struct yetty_yterm_terminal_result yetty_yterm_terminal_create(
    struct yetty_ycore_grid_size grid_size, const struct yetty_context *yetty_context);
struct yetty_ycore_void_result yetty_yterm_terminal_destroy(struct yetty_yterm_terminal *terminal);

/* Get terminal as yui view (for pushing into pane) */
struct yetty_yui_view *yetty_yterm_terminal_as_view(struct yetty_yterm_terminal *terminal);

/* Reverse of _as_view: returns the owning terminal pointer if `view` is
 * a yterm view, NULL for any other view kind (VNC viewer, ydvnc, …) or
 * NULL input. Implemented by comparing view->ops against the static
 * terminal_view_ops table in terminal.c, so it's safe to call on any
 * yetty_yui_view without prior knowledge of its type. */
struct yetty_yterm_terminal *yetty_yterm_terminal_from_view(struct yetty_yui_view *view);

/* Borrowed accessor for the terminal's wire statemachine (one per
 * terminal — same lifetime). NULL when terminal is NULL. Used by the
 * yui debug window to pull rolling envelope traffic stats. */
struct yetty_ywire_wire_statemachine *yetty_yterm_terminal_wire_sm(
    struct yetty_yterm_terminal *terminal);

struct yetty_ycore_void_result yetty_yterm_terminal_resize_grid(
    struct yetty_yterm_terminal *terminal, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);

/* Terminal state */
uint32_t yetty_yterm_terminal_get_cols(const struct yetty_yterm_terminal *terminal);
uint32_t yetty_yterm_terminal_get_rows(const struct yetty_yterm_terminal *terminal);

/* Layer management */
struct yetty_ycore_void_result yetty_yterm_terminal_layer_add(
    struct yetty_yterm_terminal *terminal, struct yetty_yrender_terminal_layer *layer);
struct yetty_ycore_void_result yetty_yterm_terminal_layer_remove(
    struct yetty_yterm_terminal *terminal, struct yetty_yrender_terminal_layer *layer);
size_t yetty_yterm_terminal_layer_count(const struct yetty_yterm_terminal *terminal);
struct yetty_yrender_terminal_layer *yetty_yterm_terminal_layer_get(
    const struct yetty_yterm_terminal *terminal, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_TERMINAL_H */
