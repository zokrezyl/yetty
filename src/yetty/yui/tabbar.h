#ifndef YETTY_YUI_TABBAR_H
#define YETTY_YUI_TABBAR_H

/*
 * yetty_yui_tabbar — Chrome-like tab strip that owns N workspaces.
 *
 * Replaces the single-workspace ownership in yetty.c. The strip sits at the
 * top of the window (height = YETTY_YUI_TABBAR_HEIGHT) and lets the user
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

struct yetty_yui_tabbar;
struct yetty_yui_workspace;
struct yetty_yconfig_config;
struct yetty_context;
struct yetty_yui_event;
struct yetty_ypaint_core_target;

YETTY_YRESULT_DECLARE(yetty_yui_tabbar_ptr, struct yetty_yui_tabbar *);

/* Reserved top-strip pixel height. Matches Chrome's ~36-logical-px tab
 * strip, leaving room for unmistakably rounded top corners on the tab
 * cells (TABBAR_TAB_RADIUS lives in tabbar.c). On HiDPI displays this
 * scales with framebuffer pixels so it stays visually proportional. */
#define YETTY_YUI_TABBAR_HEIGHT 36.0f

/* Create the tabbar. `config` is borrowed and outlives the tabbar; the
 * tabbar reads style/yui from it during construction to overlay brand
 * defaults. NULL is allowed (defaults stand). */
struct yetty_yui_tabbar_ptr_result yetty_yui_tabbar_create(
    const struct yetty_yconfig_config *config);

struct yetty_ycore_void_result yetty_yui_tabbar_destroy(struct yetty_yui_tabbar *bar);

struct yetty_ycore_void_result yetty_yui_tabbar_render(
    struct yetty_yui_tabbar *bar, struct yetty_ypaint_core_target *render_target);

struct yetty_ycore_void_result yetty_yui_tabbar_resize(struct yetty_yui_tabbar *bar, float width,
                                                       float height);

struct yetty_ycore_int_result yetty_yui_tabbar_on_event(struct yetty_yui_tabbar *bar,
                                                        const struct yetty_yui_event *event);

/* Append a workspace by loading the layout from config (mirrors the
 * single-workspace path that workspace_load_layout used to drive). The new
 * workspace becomes the active one. */
struct yetty_ycore_void_result yetty_yui_tabbar_add_workspace_from_config(
    struct yetty_yui_tabbar *bar, const struct yetty_yconfig_config *config,
    const struct yetty_context *yetty_ctx);

/* Kinds the "v" dropdown menu can spawn. Each kind toggles a small set
 * of config keys (ssh/enabled, telnet/enabled, vnc/client) so the
 * existing pty-factory + workspace dispatch paths pick the right
 * backend. The toggles are sticky — subsequent Ctrl+Shift+T uses the
 * last selected kind. */
enum yetty_yui_tabbar_kind {
    YETTY_YUI_TAB_SHELL = 0, /* native fork-pty */
    YETTY_YUI_TAB_SSH,       /* libssh2-backed remote shell */
    YETTY_YUI_TAB_TELNET,    /* telnet TCP client */
    YETTY_YUI_TAB_YVNC,      /* yvnc terminal-side VNC client */
};

/* Spawn a new tab of the requested kind. Uses the config and yetty_ctx
 * cached by the first add_workspace_from_config call. */
struct yetty_ycore_void_result yetty_yui_tabbar_add_workspace_of_kind(
    struct yetty_yui_tabbar *bar, enum yetty_yui_tabbar_kind kind);

/* Accessors — used by yetty.c for diagnostics and screenshot routing. */
struct yetty_yui_workspace *yetty_yui_tabbar_active_workspace(const struct yetty_yui_tabbar *bar);
size_t yetty_yui_tabbar_count(const struct yetty_yui_tabbar *bar);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_TABBAR_H */
