/*
 * yetty_yui_tabbar — see tabbar.h for the design.
 *
 * Today this file only implements the architecture: data, sizing, event
 * routing, keyboard/mouse shortcuts. The visual tab cells (rounded
 * rectangles + labels + close button, browser-tab style) need a
 * dedicated WebGPU pipeline that draws into the strip region after
 * workspace render — see the render() entry point for the integration
 * seam.
 */

#include <yetty/yui/workspace.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/util.h>
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yplatform/window-manager.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#include "tabbar.h"

/* GLFW modifier bit layout (matches glfw-main.c, ydvnc, yetty.c). Duplicated
 * here so tabbar.c doesn't pull in GLFW headers. */
#define YETTY_MOD_SHIFT 0x0001
#define YETTY_MOD_CONTROL 0x0002

/* GLFW key codes used for tab shortcuts. Inlining the few constants is far
 * cheaper than including GLFW from this layer. */
#define KEY_T 84
#define KEY_W 87
#define KEY_TAB 258
#define KEY_1 49
#define KEY_9 57

#define INITIAL_CAPACITY 4

struct yetty_yui_tabbar;
static void tabbar_request_render(const struct yetty_yui_tabbar *bar);

struct yetty_yui_tabbar {
    struct yetty_yui_workspace **workspaces;
    size_t count;
    size_t capacity;
    size_t active;
    float width;
    float height;

    /* Cached at the first add — needed so Ctrl+Shift+T can spawn a new
     * workspace from the same config without yetty.c plumbing each shortcut.
     * The pointers are borrowed (config + ctx outlive the tabbar by
     * construction in yetty.c). */
    const struct yetty_yconfig_config *config;
    const struct yetty_context *yetty_ctx;

    /* Window-drag state. The OS title bar is gone, so the strip's empty
     * area is the only place the user can grab to move the window. We
     * record the cursor position (in window-relative coords) at
     * MOUSE_DOWN; on each MOUSE_MOVE the delta from that anchor goes
     * to the platform window manager. Because we set the window
     * position absolutely on the main thread, after each move the
     * cursor's window-relative pos is back at the anchor, so the
     * anchor stays valid for the whole drag. */
    int dragging;
    int drag_move_grab_sent; /* begin_interactive_move fired once for this drag */
    float drag_anchor_x;
    float drag_anchor_y;

    /* Edge/corner resize state. GLFW_DECORATED=FALSE removed the OS
     * resize handles; we re-implement the bottom + right + bottom-right
     * grips in software here. dir_x and dir_y are each ±1 / 0 telling
     * the resize_by handler which axes grow. We track resize_last_*
     * (not an anchor) and emit per-MOUSE_MOVE step deltas — the window
     * top-left stays fixed during resize, so the cursor's
     * window-relative position grows in lockstep with the window's
     * right/bottom edge as we expand. */
    int resizing;
    int resize_dir_x;
    int resize_dir_y;
    float resize_last_x;
    float resize_last_y;

    /* Hamburger-menu hook. Clicking the ≡ button on the left fires this
     * callback so an external subscriber (yetty.c → yui's ygui
     * popup_menu) paints a widget-based menu anchored under the button.
     * NULL means "no menu attached", in which case the click is a
     * no-op. Field name retained for binary compatibility with the
     * existing callback typedef. */
    yetty_yui_tabbar_v_menu_cb v_menu_cb;
    void *v_menu_userdata;

    /* Style colors used to live here for the custom tab-strip painter.
     * Now that ygui widgets render the chrome, theming flows through
     * yui's engine theme (BRAND_* via apply_css). Field block removed. */
};

/*---------------------------------------------------------------------------
 * Allocation helpers
 *--------------------------------------------------------------------------*/

static struct yetty_ycore_void_result tabbar_grow(struct yetty_yui_tabbar *bar)
{
    size_t new_cap = bar->capacity ? bar->capacity * 2 : INITIAL_CAPACITY;
    struct yetty_yui_workspace **arr = realloc(bar->workspaces, new_cap * sizeof(*bar->workspaces));
    if (!arr) {
        return YETTY_ERR(yetty_ycore_void, "tabbar grow: realloc failed");
    }
    bar->workspaces = arr;
    bar->capacity = new_cap;
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Create / destroy
 *--------------------------------------------------------------------------*/

struct yetty_yui_tabbar_ptr_result yetty_yui_tabbar_create(
    const struct yetty_yconfig_config *config)
{
    (void)config; /* style keys are now consumed by yui's ygui theme. */
    struct yetty_yui_tabbar *bar = calloc(1, sizeof(struct yetty_yui_tabbar));
    if (!bar) {
        return YETTY_ERR(yetty_yui_tabbar_ptr, "tabbar_create: allocation failed");
    }
    return YETTY_OK(yetty_yui_tabbar_ptr, bar);
}

struct yetty_ycore_void_result yetty_yui_tabbar_destroy(struct yetty_yui_tabbar *bar)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_destroy: NULL");
    }

    for (size_t i = 0; i < bar->count; i++) {
        if (!bar->workspaces[i]) {
            continue;
        }
        struct yetty_ycore_void_result r = yetty_yui_workspace_destroy(bar->workspaces[i]);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    free(bar->workspaces);
    free(bar);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_destroy: workspace destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Render
 *--------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------
 * Browser-tab colors + geometry. Picked to read as a familiar dark-mode
 * tab strip: dark gray strip, the active tab "rises out" of the strip as
 * a lighter rounded shape, inactive tabs sit flush and slightly
 * translucent. SDF rounding gives us crisp antialiased corners at any
 * zoom level (see solid-rects.wgsl).
 *--------------------------------------------------------------------------*/

/* The browser-tab-style custom painter (and its rect-batch + window-button
 * helpers) used to live here. It has been replaced by ygui widgets pinned
 * to the engine titlebar slot — see yui.c::yui_titlebar_build. */

struct yetty_ycore_void_result yetty_yui_tabbar_render(struct yetty_yui_tabbar *bar,
                                                       struct yetty_ydraw_target *render_target)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_render: NULL");
    }
    if (bar->count == 0) {
        return YETTY_OK_VOID();
    }

    /* tabbar.c no longer paints the strip — yui's ygui titlebar widget
     * tree (engine_set_titlebar) now draws the hamburger, tabs, +,
     * drag-area, and window-control buttons. tabbar_render is just a
     * thin shim that draws the active workspace; the chrome is
     * composited above by yui_render in the engine's pinned titlebar
     * slot. */
    struct yetty_yui_workspace *ws = bar->workspaces[bar->active];
    if (ws) {
        struct yetty_ycore_void_result r = yetty_yui_workspace_render(ws, render_target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "tabbar_render: workspace render failed");
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Resize
 *
 * The strip claims a fixed height at the top; each workspace gets a smaller
 * rect underneath. Y-origin is shifted so workspace-internal coordinates
 * already exclude the strip — tile hit-test and rendering need no further
 * changes.
 *--------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yui_tabbar_resize(struct yetty_yui_tabbar *bar, float width,
                                                       float height)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_resize: NULL");
    }

    bar->width = width;
    bar->height = height;

    float strip = YETTY_YUI_TABBAR_HEIGHT;
    if (strip > height) {
        /* Window smaller than strip — give workspaces zero rows rather than
         * negative bounds, which would NaN through the renderer. */
        strip = height;
    }
    float ws_h = height - strip;

    for (size_t i = 0; i < bar->count; i++) {
        if (!bar->workspaces[i]) {
            continue;
        }
        /* Origin first so the resize's set_bounds lands below the strip
         * instead of at y=0 (where it would be overdrawn by the tab cells). */
        struct yetty_ycore_void_result o =
            yetty_yui_workspace_set_origin(bar->workspaces[i], 0, strip);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, o, "tabbar_resize: workspace set_origin failed");
        struct yetty_ycore_void_result r =
            yetty_yui_workspace_resize(bar->workspaces[i], width, ws_h);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "tabbar_resize: workspace resize failed");

        /* set_bounds only STORES the bounds on the terminal view
         * (terminal.c::terminal_view_set_bounds — comment: "Terminal
         * handles resize via YETTY_EVENT_RESIZE from event loop"). The
         * actual layer-target framebuffer resize + cols/rows re-derive
         * only happens when terminal_view_on_event sees a RESIZE event.
         *
         * yetty.c calls tabbar_on_event(RESIZE) after us, but that only
         * forwards to the ACTIVE workspace — inactive tabs would keep
         * their initial small layer-target size and, when later
         * activated, composite that small framebuffer up into the now-
         * larger workspace, making everything (notably the font) look
         * ~2× bigger. Fan the synthetic RESIZE here so every tab's
         * terminal stays in sync. */
        struct yetty_yui_event resize_ev = {0};
        resize_ev.type = YETTY_YCORE_RESIZE;
        resize_ev.resize.width = width;
        resize_ev.resize.height = ws_h;
        yetty_yui_workspace_on_event(bar->workspaces[i], &resize_ev);
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Workspace management
 *--------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yui_tabbar_add_workspace_from_config(
    struct yetty_yui_tabbar *bar, const struct yetty_yconfig_config *config,
    const struct yetty_context *yetty_ctx)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: NULL bar");
    }

    /* Remember config + ctx the first time we're called so keyboard-driven
     * new-tab (Ctrl+Shift+T) can spawn a fresh workspace without yetty.c
     * having to plumb a separate API call for each shortcut. */
    if (!bar->config) {
        bar->config = config;
        bar->yetty_ctx = yetty_ctx;
    }

    if (bar->count == bar->capacity) {
        struct yetty_ycore_void_result gr = tabbar_grow(bar);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "tabbar_add_ws: grow failed");
    }

    struct yetty_yui_workspace_ptr_result wr = yetty_yui_workspace_create();
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: workspace_create failed", wr);
    }
    struct yetty_yui_workspace *ws = wr.value;

    /* Size + origin the fresh workspace to the per-tab area before loading
     * layout so the loaded views start with bounds that already sit below
     * the strip. */
    float strip = YETTY_YUI_TABBAR_HEIGHT;
    if (strip > bar->height) {
        strip = bar->height;
    }
    if (bar->width > 0 && bar->height > 0) {
        struct yetty_ycore_void_result oo = yetty_yui_workspace_set_origin(ws, 0, strip);
        if (YETTY_IS_ERR(oo)) {
            yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: set_origin failed", oo);
        }
        struct yetty_ycore_void_result rr =
            yetty_yui_workspace_resize(ws, bar->width, bar->height - strip);
        if (YETTY_IS_ERR(rr)) {
            yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: initial resize failed", rr);
        }
    }

    struct yetty_ycore_void_result lr = yetty_yui_workspace_load_layout(ws, config, yetty_ctx);
    if (YETTY_IS_ERR(lr)) {
        yetty_yui_workspace_destroy(ws);
        return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: load_layout failed", lr);
    }

    /* Deactivate the workspace we're switching away from (if any) so its
     * leaf view gets a focus-out notification before the new one's
     * focus-in. */
    if (bar->count > 0 && bar->workspaces[bar->active]) {
        yetty_yui_workspace_set_active(bar->workspaces[bar->active], 0);
    }

    bar->workspaces[bar->count++] = ws;
    bar->active = bar->count - 1;

    /* The new workspace becomes active immediately — cascade focus down
     * to its terminal so it knows it's the foreground view. */
    yetty_yui_workspace_set_active(ws, 1);

    /* Critical: the initial layout sets bounds via set_bounds, which the
     * terminal view only stores. The terminal grid (cols/rows) and the
     * per-layer GPU targets only re-derive their pixel size when they see
     * a RESIZE event (terminal.c::terminal_view_on_event). Without this
     * synthetic event a brand-new tab keeps its 80×24 default grid at
     * default cell_size, producing the "tab N has different / huge font
     * relative to tab 1" symptom: tab 1 was sized by the startup RESIZE
     * that fires from glfw.c after yetty_create returns; new tabs never
     * saw one. */
    if (bar->width > 0 && bar->height > 0) {
        struct yetty_yui_event resize_ev = {0};
        resize_ev.type = YETTY_YCORE_RESIZE;
        resize_ev.resize.width = bar->width;
        resize_ev.resize.height = bar->height - strip;
        yetty_yui_workspace_on_event(ws, &resize_ev);
    }

    /* Kick a render so the just-created tab actually shows up. Otherwise
     * the screen keeps the previous frame until the first PTY echo, which
     * makes the new tab look "stuck on the old one until I type". */
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tabbar_attach_empty_workspace(
    struct yetty_yui_tabbar *bar, yetty_ycore_object_id workspace_id,
    const struct yetty_yconfig_config *config, const struct yetty_context *yetty_ctx,
    struct yetty_yui_workspace **out_ws)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: NULL bar");
    }
    if (out_ws) {
        *out_ws = NULL;
    }

    /* Cache config + ctx the first time we're called so later spawn
     * paths (Ctrl+Shift+T) still work. */
    if (!bar->config) {
        bar->config = config;
        bar->yetty_ctx = yetty_ctx;
    }

    if (bar->count == bar->capacity) {
        struct yetty_ycore_void_result gr = tabbar_grow(bar);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "tabbar_attach_empty_ws: grow failed");
    }

    struct yetty_yui_workspace_ptr_result wr =
        yetty_yui_workspace_create_with_id(workspace_id);
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: workspace_create failed",
                         wr);
    }
    struct yetty_yui_workspace *ws = wr.value;

    float strip = YETTY_YUI_TABBAR_HEIGHT;
    if (strip > bar->height) {
        strip = bar->height;
    }
    if (bar->width > 0 && bar->height > 0) {
        struct yetty_ycore_void_result oo = yetty_yui_workspace_set_origin(ws, 0, strip);
        if (YETTY_IS_ERR(oo)) {
            yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: set_origin failed", oo);
        }
        struct yetty_ycore_void_result rr =
            yetty_yui_workspace_resize(ws, bar->width, bar->height - strip);
        if (YETTY_IS_ERR(rr)) {
            yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: initial resize failed",
                             rr);
        }
    }

    if (bar->count > 0 && bar->workspaces[bar->active]) {
        yetty_yui_workspace_set_active(bar->workspaces[bar->active], 0);
    }
    bar->workspaces[bar->count++] = ws;
    bar->active = bar->count - 1;

    if (out_ws) {
        *out_ws = ws;
    }
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

struct yetty_yui_workspace *yetty_yui_tabbar_find_workspace(
    const struct yetty_yui_tabbar *bar, yetty_ycore_object_id workspace_id)
{
    if (!bar) {
        return NULL;
    }
    for (size_t i = 0; i < bar->count; i++) {
        if (bar->workspaces[i] &&
            yetty_yui_workspace_id(bar->workspaces[i]) == workspace_id) {
            return bar->workspaces[i];
        }
    }
    return NULL;
}

/* Map a tabbar_kind onto the small set of config keys the existing PTY
 * factory + workspace.c dispatch consult. Each kind sets its own flag
 * and clears the others so the spawn is unambiguous.
 *
 *   SHELL  → clear ssh/telnet flags, clear vnc/client → forkpty default
 *   SSH    → ssh/enabled=true, clear telnet, clear vnc/client
 *   TELNET → telnet/enabled=true (+ host/port defaults), clear ssh, clear vnc/client
 *   YVNC   → set vnc/client to localhost:5900 if currently empty,
 *            clear ssh/telnet flags
 *
 * The config is shared with the rest of yetty by design — set_string is
 * part of its public API and other subsystems mutate it too (e.g. shell
 * command from -e flag). The const-cast on bar->config reflects that:
 * the field is declared const for borrow-discipline, but the underlying
 * object is mutable. */
struct yetty_ycore_void_result yetty_yui_tabbar_add_workspace_of_kind(
    struct yetty_yui_tabbar *bar, enum yetty_yui_tabbar_kind kind)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_add_of_kind: NULL bar");
    }
    if (!bar->config || !bar->yetty_ctx) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_add_of_kind: config/ctx not cached yet");
    }

    struct yetty_yconfig_config *cfg = (struct yetty_yconfig_config *)bar->config;
    const struct yetty_yconfig_config_ops *ops = cfg->ops;

    /* Always clear the three exclusive flags up front. The kind branch
     * below re-enables exactly one (or none, for SHELL). */
    ops->set_string(cfg, YETTY_YCONFIG_KEY_SSH, "false");
    ops->set_string(cfg, YETTY_YCONFIG_KEY_TELNET, "false");
    ops->set_string(cfg, "vnc/client", "");

    /* TODO: open a ygui dialog to collect host/port/credentials and
     * apply them to the config before delegating to add_workspace_from_config.
     * Today we only flip the kind flags; if the relevant config keys
     * (ssh/host, telnet/host+port, vnc/client) are unset the spawn will
     * fail loudly at PTY-create time. The dialog work is intentionally
     * separate — see follow-up task. */
    switch (kind) {
    case YETTY_YUI_TAB_SHELL:
        /* Forkpty default — no extra keys to set. */
        break;
    case YETTY_YUI_TAB_SSH:
        ops->set_string(cfg, YETTY_YCONFIG_KEY_SSH, "true");
        break;
    case YETTY_YUI_TAB_TELNET:
        ops->set_string(cfg, YETTY_YCONFIG_KEY_TELNET, "true");
        break;
    case YETTY_YUI_TAB_YVNC:
        /* vnc/client must be set by the dialog before we get here.
         * Leaving it empty falls through to a terminal view, which is
         * confusing — better to surface the missing input. */
        if (!ops->has(cfg, "vnc/client")) {
            return YETTY_ERR(yetty_ycore_void, "yvnc: vnc/client not set (dialog not yet wired)");
        }
        break;
    }

    return yetty_yui_tabbar_add_workspace_from_config(bar, cfg, bar->yetty_ctx);
}

static struct yetty_ycore_void_result tabbar_close_active(struct yetty_yui_tabbar *bar)
{
    if (bar->count == 0) {
        return YETTY_OK_VOID();
    }
    /* Refuse to close the last workspace — most browsers close the window
     * in that case, but yetty's lifecycle is owned by yetty.c (which posts
     * SHUTDOWN on window-close). Leaving the last workspace open keeps the UI in a
     * defined state until the user explicitly asks to quit. */
    if (bar->count == 1) {
        ydebug("tabbar: refusing to close last workspace");
        return YETTY_OK_VOID();
    }

    size_t idx = bar->active;
    struct yetty_yui_workspace *ws = bar->workspaces[idx];
    if (ws) {
        struct yetty_ycore_void_result r = yetty_yui_workspace_destroy(ws);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void, "tabbar close: workspace destroy failed", r);
        }
    }

    /* Compact array. */
    for (size_t i = idx + 1; i < bar->count; i++) {
        bar->workspaces[i - 1] = bar->workspaces[i];
    }
    bar->count--;
    if (bar->active >= bar->count) {
        bar->active = bar->count - 1;
    }
    /* Whichever workspace is now in the active slot just gained focus
     * (it might be the one that was already there if we closed a tab to
     * its right, or the previous neighbour if we closed the rightmost).
     * Either way, refresh its focus cascade so its terminal knows it's
     * now the foreground view. */
    if (bar->count > 0 && bar->workspaces[bar->active]) {
        yetty_yui_workspace_set_active(bar->workspaces[bar->active], 1);
    }
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

/* Ask the event loop for a fresh render. Without this, switching tabs only
 * shows the new workspace once some unrelated event (PTY output, mouse
 * move, resize) happens to fire the next RENDER — the user sees the old
 * tab until their first keystroke, which then routes to the new
 * workspace's terminal but only paints after the PTY echoes. The result
 * looks exactly like "typing jumped me back to the old tab".
 *
 * yetty_ctx is borrowed from the cached pointer add_workspace_from_config
 * stored on the first call, which always happens before the first switch
 * (the initial workspace setup populates it). */
static void tabbar_request_render(const struct yetty_yui_tabbar *bar)
{
    if (!bar || !bar->yetty_ctx || !bar->yetty_ctx->event_loop) {
        return;
    }
    struct yetty_yevent_event_loop *loop = bar->yetty_ctx->event_loop;
    if (loop->ops && loop->ops->request_render) {
        loop->ops->request_render(loop);
    }
}

static void tabbar_switch(struct yetty_yui_tabbar *bar, size_t idx)
{
    if (idx >= bar->count || idx == bar->active) {
        return;
    }
    /* Deactivate the previously active workspace, then activate the new
     * one. Both calls cascade SET_FOCUS down through the focused pane to
     * its active view — without this the leaf view never learns that a
     * tab switch happened, and per-view focus behaviour (terminal
     * cursor blink, future focus reporting CSEQ, etc.) lags one step
     * behind the visible tab. */
    if (bar->workspaces[bar->active]) {
        yetty_yui_workspace_set_active(bar->workspaces[bar->active], 0);
    }
    bar->active = idx;
    if (bar->workspaces[bar->active]) {
        yetty_yui_workspace_set_active(bar->workspaces[bar->active], 1);
    }
    ydebug("tabbar: switched to workspace %zu/%zu", idx + 1, bar->count);
    tabbar_request_render(bar);
}

/*---------------------------------------------------------------------------
 * Event handling
 *--------------------------------------------------------------------------*/

static int event_y(const struct yetty_yui_event *e, float *out)
{
    switch (e->type) {
    case YETTY_YCORE_MOUSE_DOWN:
    case YETTY_YCORE_MOUSE_UP:
    case YETTY_YCORE_MOUSE_MOVE:
    case YETTY_YCORE_MOUSE_DRAG:
    case YETTY_YCORE_MOUSE_DOUBLE_CLICK:
        *out = e->mouse.y;
        return 1;
    case YETTY_YCORE_MOUSE_SCROLL:
        *out = e->mouse_scroll.y;
        return 1;
    default:
        return 0;
    }
}

struct yetty_ycore_int_result yetty_yui_tabbar_on_event(struct yetty_yui_tabbar *bar,
                                                        const struct yetty_yui_event *event)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_int, "tabbar_on_event: NULL");
    }
    if (bar->count == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Keyboard shortcuts — only on KEY_DOWN. Char events with Ctrl held may
     * still produce control codes (Ctrl+T → 0x14) which we want the terminal
     * to see, so we deliberately don't filter on YCORE_CHAR. The shortcut
     * set matches the common browser defaults. */
    if (event->type == YETTY_YCORE_KEY_DOWN && (event->key.mods & YETTY_MOD_CONTROL)) {
        int k = event->key.key;
        int shift = (event->key.mods & YETTY_MOD_SHIFT) != 0;

        /* Ctrl+Shift+T → new tab. Ctrl+T alone is a common shell binding
         * (FZF, etc.), so we put the new-tab on Ctrl+Shift+T to leave Ctrl+T
         * for the inner program. */
        if (shift && k == KEY_T) {
            if (bar->config && bar->yetty_ctx) {
                struct yetty_ycore_void_result r =
                    yetty_yui_tabbar_add_workspace_from_config(bar, bar->config, bar->yetty_ctx);
                if (YETTY_IS_ERR(r)) {
                    yerror("tabbar: new tab failed: %s", r.error.msg);
                    yetty_ycore_error_destroy(r.error);
                } else {
                    ydebug("tabbar: Ctrl+Shift+T → workspace %zu", bar->count);
                }
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (shift && k == KEY_W) {
            struct yetty_ycore_void_result r = tabbar_close_active(bar);
            if (YETTY_IS_ERR(r)) {
                yerror("tabbar: close failed: %s", r.error.msg);
                yetty_ycore_error_destroy(r.error);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (k == KEY_TAB) {
            if (bar->count > 1) {
                size_t next = shift ? (bar->active + bar->count - 1) % bar->count
                                    : (bar->active + 1) % bar->count;
                /* Route through tabbar_switch so the SET_FOCUS cascade
                 * runs — direct bar->active assignment would skip it. */
                tabbar_switch(bar, next);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (k >= KEY_1 && k <= KEY_9) {
            tabbar_switch(bar, (size_t)(k - KEY_1));
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* Mouse hit-test on the strip region. Anything in y < strip belongs to
     * the tabbar; we don't forward it to the workspace. Once the strip has
     * actual tab cells, this is where the per-tab hit / drag-to-move /
     * close-button logic will live. */
    float strip = YETTY_YUI_TABBAR_HEIGHT;
    if (strip > bar->height) {
        strip = bar->height;
    }
    float y;
    int in_strip = event_y(event, &y) && y < strip;

    /* Strip-area MOUSE_DOWN that wasn't consumed by ygui (yui_on_event
     * forwards to the engine first; the engine's spatial-grid hit-test
     * routes clicks to whichever titlebar widget is under the cursor).
     * A click reaching here is either:
     *   - on the drag-spacer in the middle of the titlebar (no widget
     *     covers that area), or
     *   - on completely-empty strip pixels at unusual sizes.
     * Either way → start a window drag. */
    if (in_strip && event->type == YETTY_YCORE_MOUSE_DOWN && bar->count > 0) {
        struct yetty_yplatform_window_manager *wm =
            bar->yetty_ctx ? bar->yetty_ctx->app_context.window_manager : NULL;
        if (wm) {
            bar->dragging = 1;
            bar->drag_move_grab_sent = 0;
            bar->drag_anchor_x = event->mouse.x;
            bar->drag_anchor_y = event->mouse.y;
            ydebug("tabbar: drag start at (%.1f, %.1f)", event->mouse.x, event->mouse.y);
            /* DON'T fire begin_interactive_move yet — see the MOUSE_MOVE
             * branch below. Triggering the Wayland compositor grab on
             * MOUSE_DOWN means a clean click (no motion) is interpreted
             * as a zero-distance interactive move, the compositor ends
             * the grab on release, and the second press of a real
             * double-click never reaches us as a normal MOUSE_DOWN —
             * killing the double-click → toggle-maximize gesture. */
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Strip-area double-click → toggle window maximize. The platform
     * event loop already paired this DOWN with the prior one (timer +
     * position check in os-event-loop/default.c) so all we do is act on
     * it. Cancel any drag the preceding MOUSE_DOWN started so we don't
     * leak a dangling drag state. */
    if (in_strip && event->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK &&
        event->mouse.button == 0 && bar->count > 0) {
        struct yetty_yplatform_window_manager *wm =
            bar->yetty_ctx ? bar->yetty_ctx->app_context.window_manager : NULL;
        if (wm && wm->ops && wm->ops->toggle_maximize) {
            wm->ops->toggle_maximize(wm);
        }
        bar->dragging = 0;
        ydebug("tabbar: double-click → toggle maximize");
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Continue an in-progress drag. We don't gate on `in_strip` because
     * once the user has the window grabbed, the cursor will travel
     * across the whole screen; everything else gets swallowed for the
     * duration of the drag. */
    if (bar->dragging) {
        struct yetty_yplatform_window_manager *wm =
            bar->yetty_ctx ? bar->yetty_ctx->app_context.window_manager : NULL;
        if (event->type == YETTY_YCORE_MOUSE_MOVE || event->type == YETTY_YCORE_MOUSE_DRAG) {
            int dx = (int)(event->mouse.x - bar->drag_anchor_x);
            int dy = (int)(event->mouse.y - bar->drag_anchor_y);
            /* Drag-vs-click threshold. Below this the gesture is still
             * potentially a click (or the first half of a double-click)
             * — swallow the MOVE but don't move/grab the window yet.
             * 3 px matches common desktop slop. */
            const int DRAG_SLOP_PX = 3;
            if (dx * dx + dy * dy < DRAG_SLOP_PX * DRAG_SLOP_PX) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            if (wm && !bar->drag_move_grab_sent && wm->ops->begin_interactive_move) {
                /* Threshold crossed — commit to a drag. On Wayland the
                 * compositor grabs the pointer here and we won't see
                 * further MOVE events until the user releases. On X11
                 * it's a no-op and the per-pixel drag_by below keeps
                 * driving glfwSetWindowPos. */
                bar->drag_move_grab_sent = 1;
                wm->ops->begin_interactive_move(wm);
            }
            ydebug("DRAGTRACE: [render-thread] tabbar MOVE during drag: "
                   "mouse=(%.1f,%.1f) anchor=(%.1f,%.1f) dx=%d dy=%d wm=%p",
                   event->mouse.x, event->mouse.y, bar->drag_anchor_x, bar->drag_anchor_y,
                   dx, dy, (void *)wm);
            if (wm && (dx != 0 || dy != 0)) {
                wm->ops->drag_by(wm, dx, dy);
                /* Don't update the anchor: glfwSetWindowPos absolutely
                 * repositions, which leaves the cursor exactly at the
                 * anchor in the moved window's frame. Resetting would
                 * accumulate rounding error each frame. */
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (event->type == YETTY_YCORE_MOUSE_UP) {
            ydebug("tabbar: drag end (grab_sent=%d)", bar->drag_move_grab_sent);
            bar->dragging = 0;
            bar->drag_move_grab_sent = 0;
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* Any other event in the strip still belongs to the tabbar — swallow
     * so the workspace below doesn't see e.g. mouse-moves while hovering
     * over the strip. */
    if (in_strip) {
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Edge / corner resize. GLFW_DECORATED=FALSE means the OS no longer
     * paints (or accepts hit-tests on) resize grips, so we implement
     * them ourselves. Only bottom + right + bottom-right are wired —
     * top and left would conflict with the tab strip and with workspace
     * mouse usage respectively. The hit-test fires only on the WORKSPACE
     * region (strip already returned above), so the cursor-to-edge
     * distance matches what the user visually grabs. */
    {
        const float EDGE = 6.0f;
        float mx = 0.0f, my = 0.0f;
        int have_xy = 0;
        switch (event->type) {
        case YETTY_YCORE_MOUSE_DOWN:
        case YETTY_YCORE_MOUSE_UP:
        case YETTY_YCORE_MOUSE_MOVE:
        case YETTY_YCORE_MOUSE_DRAG:
            mx = event->mouse.x;
            my = event->mouse.y;
            have_xy = 1;
            break;
        default:
            break;
        }
        struct yetty_yplatform_window_manager *wm =
            bar->yetty_ctx ? bar->yetty_ctx->app_context.window_manager : NULL;

        if (have_xy && event->type == YETTY_YCORE_MOUSE_DOWN && !bar->resizing && wm) {
            int right = mx >= bar->width - EDGE && mx <= bar->width;
            int bottom = my >= bar->height - EDGE && my <= bar->height;
            if (right || bottom) {
                bar->resizing = 1;
                bar->resize_dir_x = right ? 1 : 0;
                bar->resize_dir_y = bottom ? 1 : 0;
                bar->resize_last_x = mx;
                bar->resize_last_y = my;
                ydebug("tabbar: resize start dirs=(%d,%d) at (%.1f, %.1f)", bar->resize_dir_x,
                       bar->resize_dir_y, mx, my);
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        if (bar->resizing) {
            if (have_xy &&
                (event->type == YETTY_YCORE_MOUSE_MOVE || event->type == YETTY_YCORE_MOUSE_DRAG)) {
                int step_dx = (int)(mx - bar->resize_last_x) * bar->resize_dir_x;
                int step_dy = (int)(my - bar->resize_last_y) * bar->resize_dir_y;
                if (wm && (step_dx != 0 || step_dy != 0)) {
                    wm->ops->resize_by(wm, step_dx, step_dy);
                }
                bar->resize_last_x = mx;
                bar->resize_last_y = my;
                return YETTY_OK(yetty_ycore_int, 1);
            }
            if (event->type == YETTY_YCORE_MOUSE_UP) {
                ydebug("tabbar: resize end");
                bar->resizing = 0;
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
    }

    /* Anything else — including keys without a shortcut, resize broadcasts,
     * zoom apply — goes to the active workspace. The workspace's tile tree
     * is anchored at y = strip (workspace_set_origin in resize), so
     * screen-space mouse coordinates land in the same frame the tile tree
     * uses; no y-shift needed. */
    struct yetty_yui_workspace *ws = bar->workspaces[bar->active];
    if (!ws) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return yetty_yui_workspace_on_event(ws, event);
}

/*---------------------------------------------------------------------------
 * Accessors
 *--------------------------------------------------------------------------*/

struct yetty_yui_workspace *yetty_yui_tabbar_active_workspace(const struct yetty_yui_tabbar *bar)
{
    if (!bar || bar->count == 0) {
        return NULL;
    }
    return bar->workspaces[bar->active];
}

size_t yetty_yui_tabbar_count(const struct yetty_yui_tabbar *bar)
{
    return bar ? bar->count : 0;
}

size_t yetty_yui_tabbar_active_index(const struct yetty_yui_tabbar *bar)
{
    return bar ? bar->active : 0;
}

struct yetty_ycore_void_result yetty_yui_tabbar_switch_to(struct yetty_yui_tabbar *bar, size_t idx)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_switch_to: NULL");
    }
    tabbar_switch(bar, idx);
    return YETTY_OK_VOID();
}

/* Index-aware close; mirrors the bookkeeping of the static tabbar_close_active
 * but lets the caller pick which tab to drop. The "refuse to close the last
 * workspace" guard stays — it's a UX invariant, not specific to active-vs-N. */
struct yetty_ycore_void_result yetty_yui_tabbar_close_at(struct yetty_yui_tabbar *bar, size_t idx)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_close_at: NULL");
    }
    if (idx >= bar->count) {
        return YETTY_OK_VOID();
    }
    if (bar->count == 1) {
        ydebug("tabbar: refusing to close last workspace");
        return YETTY_OK_VOID();
    }
    struct yetty_yui_workspace *ws = bar->workspaces[idx];
    if (ws) {
        struct yetty_ycore_void_result r = yetty_yui_workspace_destroy(ws);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ycore_void, "tabbar_close_at: workspace destroy failed", r);
        }
    }
    for (size_t i = idx + 1; i < bar->count; i++) {
        bar->workspaces[i - 1] = bar->workspaces[i];
    }
    bar->count--;
    if (bar->active >= bar->count) {
        bar->active = bar->count - 1;
    } else if (idx < bar->active) {
        /* Closing a tab to the left of the active one shifts the active
         * index down by 1 so the same workspace stays focused. */
        bar->active--;
    }
    if (bar->count > 0 && bar->workspaces[bar->active]) {
        yetty_yui_workspace_set_active(bar->workspaces[bar->active], 1);
    }
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

void yetty_yui_tabbar_iconify(struct yetty_yui_tabbar *bar)
{
    if (!bar || !bar->yetty_ctx) {
        return;
    }
    struct yetty_yplatform_window_manager *wm = bar->yetty_ctx->app_context.window_manager;
    if (wm && wm->ops && wm->ops->iconify) {
        wm->ops->iconify(wm);
    }
}

void yetty_yui_tabbar_toggle_maximize(struct yetty_yui_tabbar *bar)
{
    if (!bar || !bar->yetty_ctx) {
        return;
    }
    struct yetty_yplatform_window_manager *wm = bar->yetty_ctx->app_context.window_manager;
    if (wm && wm->ops && wm->ops->toggle_maximize) {
        wm->ops->toggle_maximize(wm);
    }
}

void yetty_yui_tabbar_close_window(struct yetty_yui_tabbar *bar)
{
    if (!bar || !bar->yetty_ctx) {
        return;
    }
    struct yetty_yplatform_window_manager *wm = bar->yetty_ctx->app_context.window_manager;
    if (wm && wm->ops && wm->ops->request_close) {
        wm->ops->request_close(wm);
    }
}

void yetty_yui_tabbar_set_v_menu_callback(struct yetty_yui_tabbar *bar,
                                          yetty_yui_tabbar_v_menu_cb cb, void *userdata)
{
    if (!bar) {
        return;
    }
    bar->v_menu_cb = cb;
    bar->v_menu_userdata = userdata;
}
