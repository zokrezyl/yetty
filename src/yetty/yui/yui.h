/* yui.h — app-level chrome singleton.
 *
 * Internal header (only yetty.c includes it). Owns the producer + transport +
 * consumer chain for the app's top-z chrome layer:
 *
 *   ygui_engine  (producer, future)
 *      │  yface-encoded OSC envelope
 *      ▼
 *   memory-pty  (yui-side endpoint ↔ render-side endpoint)
 *      ▼
 *   osc_statemachine  (on render-side endpoint)
 *      ▼
 *   ypaint-layer KIND_STATIC  (consumer, owns a static-canvas)
 *      ▼
 *   render target  (composited above all terminals in the frame loop)
 *
 * The wake side of the memory-pty is bridged to the event loop via
 * post_to_loop — that defers even in the same-thread case, so the
 * wiring stays identical when the eventual yui/render thread split lands.
 */
#ifndef YETTY_YUI_YUI_H
#define YETTY_YUI_YUI_H

#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui;
struct yetty_context;
struct yetty_ypaint_core_target;

YETTY_YRESULT_DECLARE(yetty_yui_ptr, struct yetty_yui *);

/* View kinds the v-menu can start. Mirrors yetty_yui_tabbar_kind 1:1 —
 * kept as a separate type so yui doesn't need to depend on tabbar's
 * header. The yetty.c bridge translates between them. */
enum yetty_yui_view_kind {
    YETTY_YUI_VIEW_SHELL = 0,
    YETTY_YUI_VIEW_SSH,
    YETTY_YUI_VIEW_TELNET,
    YETTY_YUI_VIEW_YVNC,
};

/* Connect callback fired when the user clicks "Connect" in a view's
 * config dialog. yetty.c wires this to tabbar->add_workspace_of_kind.
 * Per-kind config fields (host, port, ...) are still TODO — first pass
 * just delivers the kind. */
typedef void (*yetty_yui_connect_cb)(void *userdata, enum yetty_yui_view_kind kind);

/* Construct the app-level chrome singleton.
 *
 * Sets up: memory-pty pair, osc_statemachine on the render endpoint,
 * ypaint-layer KIND_STATIC registered for YPAINT_CLEAR/BIN/OVERLAY on
 * that SM, and the memory-pty wake bridged to the event loop. The
 * ygui_engine producer is wired later (separate task).
 *
 * `surface_w`/`surface_h` are the initial window framebuffer dims; the
 * layer is sized to fit. `cell_w`/`cell_h` set the static-canvas grid
 * stride — for app-level chrome these are mostly arbitrary; the
 * defaults follow the terminal text-layer's font metrics.
 */
struct yetty_yui_ptr_result yetty_yui_create(const struct yetty_context *context,
                                             uint32_t surface_w, uint32_t surface_h,
                                             float cell_w, float cell_h);

struct yetty_ycore_void_result yetty_yui_destroy(struct yetty_yui *yui);

/* Composite the yui chrome into the render target. Called from yetty's
 * RENDER handler after every terminal layer has rendered. */
struct yetty_ycore_void_result yetty_yui_render(struct yetty_yui *yui,
                                                struct yetty_ypaint_core_target *target);

/* Update the static-canvas grid to match a new framebuffer size. Called
 * from the RESIZE handler. Cell stride stays as set at create time. */
struct yetty_ycore_void_result yetty_yui_resize(struct yetty_yui *yui, uint32_t surface_w,
                                                uint32_t surface_h);

/* Accessor for the producer-side memory-pty endpoint. Future task: the
 * ygui_engine writes its OSC envelopes to this fd-less pty via the
 * existing pty->ops->write. */
struct yetty_platform_pty *yetty_yui_producer_pty(struct yetty_yui *yui);

/* Open the view-launcher menu at (anchor_x, anchor_y) in window pixels.
 * Wired to the tabbar's v-button click. Items: shell, ssh, telnet, yvnc;
 * each opens the corresponding config dialog. */
void yetty_yui_show_view_menu(struct yetty_yui *yui, float anchor_x, float anchor_y);

/* Subscribe to "Connect" presses in any view's config dialog. The
 * registered callback is called from the ygui widget tree at click
 * time. Pass NULL to disarm. The yui does not own `userdata`. */
void yetty_yui_set_connect_callback(struct yetty_yui *yui, yetty_yui_connect_cb cb,
                                    void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_YUI_H */
