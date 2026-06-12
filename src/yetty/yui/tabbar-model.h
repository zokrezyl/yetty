#ifndef YETTY_YUI_TABBAR_H
#define YETTY_YUI_TABBAR_H

/*
 * yetty_yui_tabbar_model — browser-style tab strip that owns N workspaces.
 *
 * Replaces the single-workspace ownership in yetty.c. The strip sits at the
 * top of the window (height = YETTY_YUI_TABBAR_HEIGHT_DP × content_scale) and lets the user
 * switch / create / close workspaces. Because the OS window decoration is
 * disabled on desktop (see yplatform/window/default.c), the strip area is
 * also the only place where the window can be dragged to move.
 *
 * Layout
 *   bounds.y =                 0 .. tab_height-1   -> tab strip
 *            tab_height        .. bounds.h-1       -> active workspace
 *
 * Rendering
 *   1. Active workspace renders into its (smaller) bounds.
 *   2. The strip itself is left at the clear color for now — visual tab cells
 *      are a follow-up (TODO: solid-color WebGPU overlay pass). Until then
 *      the user switches tabs by keyboard shortcuts, which keeps every other
 *      piece of plumbing exercisable end-to-end.
 *
 * Events
 *   - Mouse events with y < tab_height          → tab strip hit-test (switch
 *                                                  workspace / window drag).
 *   - Ctrl+T                                    → new workspace
 *   - Ctrl+W                                    → close current workspace
 *   - Ctrl+Tab / Ctrl+Shift+Tab                 → cycle next/prev
 *   - Ctrl+<digit 1..9>                         → jump to workspace N
 *   - Everything else                            → forwarded to active ws
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_tabbar_model;
struct yetty_yui_workspace;
struct yetty_yconfig_config;
struct yetty_context;
struct yetty_yui_event;
struct yetty_ydraw_target;

YETTY_YRESULT_DECLARE(yetty_yui_tabbar_model_ptr, struct yetty_yui_tabbar_model *);

/* Reserved top-strip height in *logical* (CSS-pixel) units. ~36 logical
 * px matches typical browser tab strips and leaves room for the rounded
 * top corners on the tab cells (TABBAR_TAB_RADIUS lives in tabbar.c).
 * Multiply through yetty_dp_to_px() at use sites so HiDPI / Retina
 * framebuffers render the strip at the correct physical size. */
#define YETTY_YUI_TABBAR_HEIGHT_DP 36.0f

/* Create the tabbar. `config` is borrowed and outlives the tabbar; the
 * tabbar reads style/yui from it during construction to overlay brand
 * defaults. NULL is allowed (defaults stand). */
struct yetty_yui_tabbar_model_ptr_result yetty_yui_tabbar_model_create(
    const struct yetty_yconfig_config *config);

struct yetty_ycore_void_result yetty_yui_tabbar_model_destroy(struct yetty_yui_tabbar_model *bar);

/* force_redraw is the global "yui scene-canvas is dirty this frame"
 * signal coming from yetty_event_handler — propagated down to every
 * pane's terminal_render_frame so panes wipe stale pixels left behind
 * by yui chrome that's about to repaint. */
struct yetty_ycore_void_result yetty_yui_tabbar_model_render(
    struct yetty_yui_tabbar_model *bar, struct yetty_ydraw_target *render_target, int force_redraw);

/* `width` × `height` is the workspace region (the strip occupies the top,
 * `height` already excludes any bottom statusbar inset). `total_height`
 * is the full window height including everything below the workspace —
 * the edge-resize hit-test reaches down to the real window bottom so the
 * user can grab the statusbar edge rather than the invisible workspace
 * bottom above it. */
struct yetty_ycore_void_result yetty_yui_tabbar_model_resize(struct yetty_yui_tabbar_model *bar,
                                                             float width, float height,
                                                             float total_height);

struct yetty_ycore_int_result yetty_yui_tabbar_model_on_event(struct yetty_yui_tabbar_model *bar,
                                                              const struct yetty_yui_event *event);

/* Returns the cursor shape (yetty_ycore_cursor_shape) appropriate for the
 * given mouse position over the tabbar's invisible edge-resize bands:
 * HRESIZE in the right band / bottom-right corner, VRESIZE in the bottom
 * band, DEFAULT otherwise. yui_compute_cursor_shape calls this so a
 * single code path owns the cursor decision (no race with widget cursors). */
int yetty_yui_tabbar_model_edge_cursor_at(const struct yetty_yui_tabbar_model *bar, float x,
                                          float y);

/* Append a workspace by loading the layout from config (mirrors the
 * single-workspace path that workspace_load_layout used to drive). The new
 * workspace becomes the active one. */
struct yetty_ycore_void_result yetty_yui_tabbar_model_add_workspace_from_config(
    struct yetty_yui_tabbar_model *bar, const struct yetty_yconfig_config *config,
    const struct yetty_context *yetty_ctx);

/* Chrome-driven workspace creation: append an empty workspace (no
 * layout, no tile tree yet) with the caller-supplied id. The new
 * workspace becomes active. Use together with a subsequent PANE_CREATE
 * (which fills in the first pane) when chrome wants to own all ids
 * from the very first moment. Returns the new workspace via *out_ws
 * (borrowed; the tabbar owns it). */
struct yetty_ycore_void_result yetty_yui_tabbar_model_attach_empty_workspace(
    struct yetty_yui_tabbar_model *bar, yetty_ycore_object_id workspace_id,
    const struct yetty_yconfig_config *config, const struct yetty_context *yetty_ctx,
    struct yetty_yui_workspace **out_ws);

/* Find a workspace by id. NULL if not present. */
struct yetty_yui_workspace *yetty_yui_tabbar_model_find_workspace(
    const struct yetty_yui_tabbar_model *bar, yetty_ycore_object_id workspace_id);

/* Kinds the "v" dropdown menu can spawn. Each kind toggles a small set
 * of config keys (ssh/enabled, telnet/enabled, vnc/client) so the
 * existing pty-factory + workspace dispatch paths pick the right
 * backend. The toggles are sticky — subsequent Ctrl+Shift+T uses the
 * last selected kind. */
enum yetty_yui_tabbar_model_kind {
    YETTY_YUI_TAB_SHELL = 0, /* native fork-pty */
    YETTY_YUI_TAB_SSH,       /* libssh2-backed remote shell */
    YETTY_YUI_TAB_TELNET,    /* telnet TCP client */
    YETTY_YUI_TAB_YVNC,      /* yvnc terminal-side VNC client */
};

/* Spawn a new tab of the requested kind. Uses the config and yetty_ctx
 * cached by the first add_workspace_from_config call. */
struct yetty_ycore_void_result yetty_yui_tabbar_model_add_workspace_of_kind(
    struct yetty_yui_tabbar_model *bar, enum yetty_yui_tabbar_model_kind kind);

/* Accessors — used by yetty.c for diagnostics and screenshot routing,
 * and by yui's ygui titlebar sync helper that mirrors the model into
 * widgets. The "active index" is the 0-based slot in the workspaces
 * array; the active workspace pointer is just workspaces[active]. */
struct yetty_yui_workspace *yetty_yui_tabbar_model_active_workspace(
    const struct yetty_yui_tabbar_model *bar);
size_t yetty_yui_tabbar_model_count(const struct yetty_yui_tabbar_model *bar);
size_t yetty_yui_tabbar_model_active_index(const struct yetty_yui_tabbar_model *bar);

/* Workspace at slot idx (borrowed; the tabbar owns it). NULL when idx is
 * out of range. */
struct yetty_yui_workspace *yetty_yui_tabbar_model_workspace_at(
    const struct yetty_yui_tabbar_model *bar, size_t idx);

/* Model mutators usable from outside (ygui tab-widget callbacks). All
 * are no-ops when idx is out of range; switch_to additionally no-ops
 * when idx is already active. */
struct yetty_ycore_void_result yetty_yui_tabbar_model_switch_to(struct yetty_yui_tabbar_model *bar,
                                                                size_t idx);
struct yetty_ycore_void_result yetty_yui_tabbar_model_close_at(struct yetty_yui_tabbar_model *bar,
                                                               size_t idx);

/* Window-control wrappers — yui's ygui titlebar buttons (_, □, ✕)
 * trampoline through here so the click path doesn't have to know about
 * the platform window manager. NULL-safe; no-op when no wm is bound. */
void yetty_yui_tabbar_model_iconify(struct yetty_yui_tabbar_model *bar);
void yetty_yui_tabbar_model_toggle_maximize(struct yetty_yui_tabbar_model *bar);
void yetty_yui_tabbar_model_close_window(struct yetty_yui_tabbar_model *bar);

/* Callback invoked when the v-button on the tabbar is clicked. The
 * tabbar reports the on-screen anchor (the lower-left corner of the
 * v-button, in window coordinates) so the listener can position a
 * popup. Used by yetty.c to bridge the click to yui's ygui popup_menu.
 * Pass NULL to disarm. The tabbar does not own `userdata`. */
typedef void (*yetty_yui_tabbar_model_v_menu_cb)(void *userdata, float anchor_x, float anchor_y);
void yetty_yui_tabbar_model_set_v_menu_callback(struct yetty_yui_tabbar_model *bar,
                                                yetty_yui_tabbar_model_v_menu_cb cb,
                                                void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_TABBAR_H */
