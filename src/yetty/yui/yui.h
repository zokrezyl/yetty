/* yui.h — app-level yui singleton.
 *
 * Internal header (only yetty.c includes it). Owns the producer + transport +
 * consumer chain for the app's top-z yui layer:
 *
 *   ygui_engine  (producer, future)
 *      │  yface-encoded OSC envelope
 *      ▼
 *   memory-pty  (yui-side endpoint ↔ render-side endpoint)
 *      ▼
 *   osc_statemachine  (on render-side endpoint)
 *      ▼
 *   ydraw-layer KIND_SCENE  (consumer, owns a scene-canvas)
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

/* Forward decl — yui_on_event takes the host's event by pointer; the
 * full union lives in yevent/event.h and we don't want to pull it into
 * everyone who includes yui.h. */
struct yetty_yui_event;

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui;
struct yetty_context;
struct yetty_ydraw_target;
struct yetty_yclass_object;
struct yetty_ygui_framework;
struct yetty_yui_tabbar_model;

YETTY_YRESULT_DECLARE(yetty_yui_ptr, struct yetty_yui *);

/* View kinds the v-menu can start. Mirrors yetty_yui_tabbar_model_kind 1:1 —
 * kept as a separate type so yui doesn't need to depend on tabbar's
 * header. The yetty.c bridge translates between them. */
enum yetty_yui_view_kind {
    YETTY_YUI_VIEW_SHELL = 0, /* No dialog — spawns the system default shell. */
    YETTY_YUI_VIEW_SSH,
    YETTY_YUI_VIEW_TELNET,
    YETTY_YUI_VIEW_YVNC,
    YETTY_YUI_VIEW_EXEC, /* Dialog with one field: path of executable
                              * to run instead of the default shell. */
};

#define YETTY_YUI_VIEW_KIND_COUNT 5

/* Connect callback fired when the user clicks "Connect" in a view's
 * config dialog. yetty.c wires this to tabbar->add_workspace_of_kind.
 * Per-kind config fields (host, port, ...) are still TODO — first pass
 * just delivers the kind. */
typedef void (*yetty_yui_connect_cb)(void *userdata, enum yetty_yui_view_kind kind);

/* Fired when the user picks a view kind from a Split V / Split H
 * submenu in the right-click context menu. The host (yetty.c) splits
 * the currently-focused pane in the active workspace using the given
 * orientation and creates the kind-specific view in the new sibling.
 * horizontal == 1 means the new sibling sits to the right; 0 means
 * below. */
typedef void (*yetty_yui_split_cb)(void *userdata, enum yetty_yui_view_kind kind, int horizontal);

/* Read the current value of a dialog's textinput. `kind` selects the
 * dialog; `field_idx` indexes into s_views[kind].fields[] (0-based).
 * Returns NULL when the dialog wasn't built, the slot is empty, or the
 * indices are out of range. The returned pointer is owned by the
 * textinput widget — copy if it needs to outlive the next mutation. */
struct yetty_ycore_const_char_ptr_result yetty_yui_get_field_text(const struct yetty_yui *yui,
                                                                  enum yetty_yui_view_kind kind,
                                                                  int field_idx);

/* Sugar for the EXEC dialog's one field. Identical to
 * yetty_yui_get_field_text(yui, YETTY_YUI_VIEW_EXEC, 0). */
struct yetty_ycore_const_char_ptr_result yetty_yui_get_exec_command(const struct yetty_yui *yui);

/* True iff yui currently has any interactive widget on screen — v-menu
 * open or any dialog visible. Use this in the host's mouse dispatch to
 * decide whether to route the event to yui first (yes ⇒ yui owns the
 * pointer; no ⇒ fall through to the workspace below). */
struct yetty_ycore_int_result yetty_yui_is_active(const struct yetty_yui *yui);

/* Route a platform mouse / mouse-scroll event into yui's ygui engine.
 * Returns 1 if yui consumed it (an open menu / dialog was hit-tested or
 * the click closed a popup), 0 if it should fall through. Non-mouse
 * events are passed through unchanged (returns 0). */
struct yetty_ycore_int_result yetty_yui_on_event(struct yetty_yui *yui,
                                                 const struct yetty_yui_event *event);

/* Construct the app-level yui singleton.
 *
 * Sets up: memory-pty pair, osc_statemachine on the render endpoint,
 * ydraw-layer KIND_SCENE registered for YDRAW_CLEAR/BIN/OVERLAY on
 * that SM, and the memory-pty wake bridged to the event loop. The
 * ygui_engine producer is wired later (separate task).
 *
 * `surface_w`/`surface_h` are the initial window framebuffer dims; the
 * layer is sized to fit. `cell_w`/`cell_h` set the scene-canvas grid
 * stride — for app-level yui these are mostly arbitrary; the
 * defaults follow the terminal text-layer's font metrics.
 */
struct yetty_yui_ptr_result yetty_yui_create(const struct yetty_context *context,
                                             uint32_t surface_w, uint32_t surface_h, float cell_w,
                                             float cell_h);

struct yetty_ycore_void_result yetty_yui_destroy(struct yetty_yui *yui);

/* Composite the yui into the render target. Called from yetty's
 * RENDER handler after every terminal layer has rendered. */
struct yetty_ycore_void_result yetty_yui_render(struct yetty_yui *yui,
                                                struct yetty_ydraw_target *target);

/* True iff the yui scene-canvas needs to repaint this frame — its ygui
 * engine has pending widget mutations (tabbar reconcile, dialog
 * visibility, statusbar text, menu open/close, …) that yui_render
 * hasn't drained yet. yetty's RENDER handler reads this BEFORE
 * tabbar_render so it can force every pane underneath to redraw — any
 * pixels the chrome is about to vacate would otherwise show its
 * previous frame. Cheap query (one bit on the engine). 0 when yui is
 * NULL. */
struct yetty_ycore_int_result yetty_yui_is_dirty(const struct yetty_yui *yui);

/* Update the scene-canvas grid to match a new framebuffer size. Called
 * from the RESIZE handler. Cell stride stays as set at create time. */
struct yetty_ycore_void_result yetty_yui_resize(struct yetty_yui *yui, uint32_t surface_w,
                                                uint32_t surface_h);

/* Open the view-launcher menu at (anchor_x, anchor_y) in window pixels.
 * Wired to the tabbar's v-button click. Items: shell, ssh, telnet, yvnc;
 * each opens the corresponding config dialog. */
struct yetty_ycore_void_result yetty_yui_show_view_menu(struct yetty_yui *yui, float anchor_x,
                                                        float anchor_y);

/* Open the right-click context menu at (anchor_x, anchor_y). Items:
 *   GPU info…
 *   Split vertically  ▸  (drills into view-kind list, fires split_cb)
 *   Split horizontally ▸ (drills into view-kind list, fires split_cb)
 * The split_cb installed via yetty_yui_set_split_callback receives the
 * chosen kind + orientation; the host is responsible for finding the
 * target pane (typically the one most recently focused / right-clicked)
 * and performing the workspace split. */
struct yetty_ycore_void_result yetty_yui_show_context_menu(struct yetty_yui *yui, float anchor_x,
                                                           float anchor_y);

/* Application statusbar — STATUSBAR widget pinned to the bottom of the
 * engine canvas by engine_set_statusbar. Lives for the whole yui
 * lifetime. The default content is two flex-laid-out labels (left flush,
 * right flush). The simple setters update those labels' text; callers
 * that need richer widgets in the bar add children directly to the
 * widget returned by yetty_yui_statusbar(). NULL when ygui engine
 * allocation failed. */
struct yetty_yclass_object *yetty_yui_statusbar(struct yetty_yui *yui);

/* Direct access to yui's ygui engine — for consumers that want to add
 * free-floating widgets (yplot, custom layouts) outside the tabbar /
 * statusbar / menu chrome that yui owns. NULL if engine allocation
 * failed at create time. Caller must not destroy. */
struct yetty_ygui_framework *yetty_yui_engine(struct yetty_yui *yui);

/* Dispatch a platform input event into yui (forwards mouse/key/char
 * into the ygui engine for widget hit-testing and click handling).
 * Returns 1 if the event was consumed by a widget / overlay; 0 if it
 * passed through and the caller should try its own handler. */
struct yetty_ycore_int_result yetty_yui_on_event(struct yetty_yui *yui,
                                                 const struct yetty_yui_event *event);
struct yetty_ycore_void_result yetty_yui_set_status_left(struct yetty_yui *yui, const char *text);
struct yetty_ycore_void_result yetty_yui_set_status_right(struct yetty_yui *yui, const char *text);

/* Pixel height of the statusbar strip, or 0 when no statusbar is
 * attached. Used by the tabbar / workspace layout so terminal cells
 * don't render under the bar. Mirrors the tabbar's own
 * YETTY_YUI_TABBAR_HEIGHT_DP — yui carves out the bottom strip the way
 * the tabbar carves out the top. */
float yetty_yui_statusbar_height(const struct yetty_yui *yui);

/* Subscribe to "Connect" presses in any view's config dialog. The
 * registered callback is called from the ygui widget tree at click
 * time. Pass NULL to disarm. The yui does not own `userdata`. */
/* Install the split-handler invoked when the user picks a kind under
 * the context menu's "Split V/H ▸" submenu. Pass NULL to disarm. */
void yetty_yui_set_split_callback(struct yetty_yui *yui, yetty_yui_split_cb cb, void *userdata);

/* Bind yui to the tabbar model. Once bound, yui builds an engine-pinned
 * titlebar widget tree (hamburger + tabs + + + drag spacer + min/max/
 * close) and reconciles it with the model on every render. Pass NULL to
 * unbind (used during yetty teardown). Safe to call before or after
 * yetty_yui_create; if called before the engine is built it just
 * stashes the pointer for the engine-construction path. */
void yetty_yui_set_tabbar_model(struct yetty_yui *yui, struct yetty_yui_tabbar_model *tabbar);

void yetty_yui_set_connect_callback(struct yetty_yui *yui, yetty_yui_connect_cb cb, void *userdata);

/* For posting a toast notification from anywhere in the codebase use
 * the standalone `ynotify(...)` primitive in <yetty/ynotify/ynotify.h>.
 * yui installs itself as the global handler in yetty_yui_create, so
 * producers don't need to know yui exists. */

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_YUI_H */
