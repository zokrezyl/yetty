#ifndef YETTY_YTERM_TERMINAL_H
#define YETTY_YTERM_TERMINAL_H

#include <stddef.h>
#include <stdint.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yetty.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct yetty_yterm_terminal;
struct yetty_yrender_terminal_layer;
struct yetty_yterm_terminal_layer_ops;
struct yetty_yplatform_event_loop;
struct yetty_platform_pty;
struct yetty_yterm_view;
struct yetty_yrender_gpu_resource_binder;
struct yetty_ypaint_core_target;

/* Render layer function - stateless, renders layer to target texture.
 * Returns early with OK if layer is not dirty. */
typedef struct yetty_ycore_void_result (*yetty_yrender_layer_fn)(
    struct yetty_yrender_terminal_layer *layer, WGPUTextureView target,
    struct yetty_yrender_gpu_resource_binder *binder, struct yetty_gpu_context *gpu);

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yterm_terminal, struct yetty_yterm_terminal *);
YETTY_YRESULT_DECLARE(yetty_yterm_terminal_layer, struct yetty_yrender_terminal_layer *);

/* PTY write callback - called when layer needs to send data to PTY */
typedef void (*yetty_yterm_pty_write_fn)(const char *data, size_t len, void *userdata);

/* Request render callback - called when layer needs a render frame */
typedef void (*yetty_yterm_request_render_fn)(void *userdata);

/* Scroll callback - called when layer scrolls, passes source layer and line
 * count */
typedef struct yetty_ycore_void_result (*yetty_yterm_scroll_fn)(
    struct yetty_yrender_terminal_layer *source, int lines, void *userdata);

/* Cursor callback - called when layer moves cursor, passes source layer and
 * position */
typedef void (*yetty_yterm_cursor_fn)(struct yetty_yrender_terminal_layer *source,
                                      struct yetty_ycore_grid_cursor_pos cursor_pos, void *userdata);

/* Mouse-subscription callback - fired when libvterm flips DEC mode 1500
 * (card click, click_enabled) or 1501 (card move, move_enabled). The two
 * args carry the *current* subscription state for both modes; the layer
 * fires this whenever either changes. */
typedef void (*yetty_yterm_mouse_sub_fn)(int click_enabled, int move_enabled, void *userdata);

/* OSC emit callback - fires when a layer needs to send an OSC envelope
 * back to the client app (PTY child). Used by ymgui-layer to deliver
 * focus events, by future bidirectional layers, etc. The terminal
 * implements this via terminal_yface_emit. */
typedef void (*yetty_yterm_emit_osc_fn)(int osc_code, const void *payload, size_t len,
                                        void *userdata);

/* Alt-screen-toggle callback - fired by the text-layer when libvterm
 * switches in/out of alternate-screen mode (DEC ?1047/?1049/?47). The
 * terminal forwards the new state to every layer via its set_alt_screen
 * op so each layer can save/restore its content. */
typedef void (*yetty_yterm_alt_screen_fn)(int active, void *userdata);

/* Layer ops */
struct yetty_yterm_terminal_layer_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_yrender_terminal_layer *self);
    /* OSC payload from pty-reader. `osc_code` lets the layer dispatch by
   * code (one layer can register for multiple codes); the body is the
   * full OSC payload after the leading "<code>;" — i.e. the yface body
   * "<flag>;<base64...>" for yface-encoded messages. */
    struct yetty_ycore_void_result (*write)(struct yetty_yrender_terminal_layer *self, int osc_code,
                                            const char *data, size_t len);
    struct yetty_ycore_void_result (*resize_grid)(struct yetty_yrender_terminal_layer *self,
                                                  struct yetty_ycore_grid_size grid_size);
    /* Change the layer's cell pixel size. Implementations must also push the
   * new value into any GPU uniform the layer maintains. Used by structural
   * zoom (YETTY_EVENT_ZOOM_CELL_SIZE). */
    struct yetty_ycore_void_result (*set_cell_size)(struct yetty_yrender_terminal_layer *self,
                                                    struct yetty_ycore_pixel_size cell_size);
    /* Push visual (shader-level) zoom state into the layer's uniforms. The
   * layer's fragment shader applies the transform at the start of fs_main so
   * all downstream MSDF/SDF math runs at the *transformed* pixel — unlike
   * blend-stage zoom which is a post-rasterization bitmap stretch. */
    struct yetty_ycore_void_result (*set_visual_zoom)(struct yetty_yrender_terminal_layer *self,
                                                      float scale, float offset_x, float offset_y);
    struct yetty_yrender_gpu_resource_set_result (*get_gpu_resource_set)(
        const struct yetty_yrender_terminal_layer *self);
    /* Render layer to target */
    struct yetty_ycore_void_result (*render)(struct yetty_yrender_terminal_layer *self,
                                             struct yetty_ypaint_core_target *target);
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
   * the top of the screen); ypaint-layer returns its canvas rolling_row_0.
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
};

/* Terminal context - contains yetty context plus terminal-owned objects */
struct yetty_yterm_terminal_context {
    struct yetty_context yetty_context;
    struct yetty_platform_pty *pty;
};

/* Terminal creation/destruction */
struct yetty_yterm_terminal_result yetty_yterm_terminal_create(
    struct yetty_ycore_grid_size grid_size, const struct yetty_context *yetty_context);
struct yetty_ycore_void_result yetty_yterm_terminal_destroy(struct yetty_yterm_terminal *terminal);

/* Get terminal as yui view (for pushing into pane) */
struct yetty_yterm_view *yetty_yterm_terminal_as_view(struct yetty_yterm_terminal *terminal);

/* Terminal input */
void yetty_yterm_terminal_write(struct yetty_yterm_terminal *terminal, const char *data,
                                size_t len);

void yetty_yterm_terminal_resize_grid(struct yetty_yterm_terminal *terminal,
                                      struct yetty_ycore_grid_size grid_size);

/* Terminal state */
uint32_t yetty_yterm_terminal_get_cols(const struct yetty_yterm_terminal *terminal);
uint32_t yetty_yterm_terminal_get_rows(const struct yetty_yterm_terminal *terminal);

/* Layer management */
void yetty_yterm_terminal_layer_add(struct yetty_yterm_terminal *terminal,
                                    struct yetty_yrender_terminal_layer *layer);
void yetty_yterm_terminal_layer_remove(struct yetty_yterm_terminal *terminal,
                                       struct yetty_yrender_terminal_layer *layer);
size_t yetty_yterm_terminal_layer_count(const struct yetty_yterm_terminal *terminal);
struct yetty_yrender_terminal_layer *yetty_yterm_terminal_layer_get(
    const struct yetty_yterm_terminal *terminal, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTERM_TERMINAL_H */
