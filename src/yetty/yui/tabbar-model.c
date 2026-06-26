/*
 * yetty_yui_tabbar_model — see tabbar-model.h for the design.
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
#include <yetty/yframework/yframework.h>
#include <yetty/yplatform/ywindow-chrome/window-chrome.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#include "tabbar-model.h"

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

struct yetty_yui_tabbar_model;
static void tabbar_request_render(const struct yetty_yui_tabbar_model *bar);

struct yetty_yui_tabbar_model {
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
     * resize handles; we re-implement them in software here for all four
     * margins + corners. Right/bottom keep the top-left fixed → track
     * incrementally from resize_last. Left/top move the origin (handled by the
     * window_chrome from fresh geometry) → after each move the window-relative
     * cursor snaps back to the press anchor, so they measure from resize_anchor
     * (not updated) to avoid a feedback loop. Matches ychrome's engine. */
    int resizing;
    int resize_left;
    int resize_right;
    int resize_top;
    int resize_bottom;
    float resize_last_x;
    float resize_last_y;
    float resize_anchor_x; /* MOUSE_DOWN position — slop check + left/top ref */
    float resize_anchor_y;
    int resize_edge;      /* yetty_ycore_resize_edge bitmask (xdg-shell) */
    int resize_grab_sent; /* begin_interactive_resize fired once per gesture */

    /* Full window height including the bottom statusbar inset. `height`
     * above is the workspace region only (window_height - statusbar_h),
     * so the bottom resize-grip band has to be tested against this larger
     * value — that's the y-coordinate of the user-visible window edge.
     * 0 until the first resize event; the hit-test falls back to `height`
     * when unset. */
    float total_height;

    /* Hamburger-menu hook. Clicking the ≡ button on the left fires this
     * callback so an external subscriber (yetty.c → yui's ygui
     * popup_menu) paints a widget-based menu anchored under the button.
     * NULL means "no menu attached", in which case the click is a
     * no-op. Field name retained for binary compatibility with the
     * existing callback typedef. */
    yetty_yui_tabbar_model_v_menu_cb v_menu_cb;
    void *v_menu_userdata;

    /* Style colors used to live here for the custom tab-strip painter.
     * Now that ygui widgets render the chrome, theming flows through
     * yui's engine theme (BRAND_* via apply_css). Field block removed. */
};

/* Convert a logical (DP) chrome dimension to framebuffer pixels using
 * the bound context's HiDPI content_scale (see
 * yetty_yplatform_gpu_context::content_scale). Falls back to 1× when ctx
 * isn't bound yet — pre-first-add the strip is never hit-tested or
 * laid out anyway, so this keeps callers free of NULL guards. */
static inline float tabbar_dp(const struct yetty_yui_tabbar_model *bar, float dp)
{
    if (!bar || !bar->yetty_ctx) {
        return dp;
    }
    return yetty_dp_to_px(&bar->yetty_ctx->runtime->gpu.app_gpu_context, dp);
}

/*---------------------------------------------------------------------------
 * Allocation helpers
 *--------------------------------------------------------------------------*/

static struct yetty_ycore_void_result tabbar_grow(struct yetty_yui_tabbar_model *bar)
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

struct yetty_yui_tabbar_model_ptr_result yetty_yui_tabbar_model_create(
    const struct yetty_yconfig_config *config)
{
    (void)config; /* style keys are now consumed by yui's ygui theme. */
    struct yetty_yui_tabbar_model *bar = calloc(1, sizeof(struct yetty_yui_tabbar_model));
    if (!bar) {
        return YETTY_ERR(yetty_yui_tabbar_model_ptr, "tabbar_create: allocation failed");
    }
    return YETTY_OK(yetty_yui_tabbar_model_ptr, bar);
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_destroy(struct yetty_yui_tabbar_model *bar)
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

struct yetty_ycore_void_result yetty_yui_tabbar_model_render(
    struct yetty_yui_tabbar_model *bar, struct yetty_ydraw_target *render_target, int force_redraw)
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
        struct yetty_ycore_void_result r =
            yetty_yui_workspace_render(ws, render_target, force_redraw);
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

int yetty_yui_tabbar_model_edge_cursor_at(const struct yetty_yui_tabbar_model *bar, float x,
                                          float y)
{
    /* Same EDGE band and edge selection rules as the resize hit-test in
     * the event handler — keep in sync if one changes. All four margins +
     * corners resize; the thin top band wins over strip-drag. */
    if (!bar || bar->width <= 0 || bar->height <= 0) {
        return YETTY_YCORE_CURSOR_DEFAULT;
    }
    const float EDGE = 8.0f;
    const float bottom_y = bar->total_height > 0 ? bar->total_height : bar->height;
    int left = x >= 0.0f && x < EDGE;
    int right = x > bar->width - EDGE && x <= bar->width;
    int top = y >= 0.0f && y < EDGE;
    int bottom = y > bottom_y - EDGE && y <= bottom_y;
    /* Corners fall back to HRESIZE — the cursor enum has no diagonal shape yet. */
    if (left || right) {
        return YETTY_YCORE_CURSOR_HRESIZE;
    }
    if (top || bottom) {
        return YETTY_YCORE_CURSOR_VRESIZE;
    }
    return YETTY_YCORE_CURSOR_DEFAULT;
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_resize(struct yetty_yui_tabbar_model *bar,
                                                             float width, float height,
                                                             float total_height)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_resize: NULL");
    }

    bar->width = width;
    bar->height = height;
    bar->total_height = total_height > 0 ? total_height : height;

    float strip = tabbar_dp(bar, YETTY_YUI_TABBAR_HEIGHT_DP);
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
        {
            struct yetty_ycore_int_result drop_r =
                yetty_yui_workspace_on_event(bar->workspaces[i], &resize_ev);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_on_event");
        }
    }
    return YETTY_OK_VOID();
}

/*---------------------------------------------------------------------------
 * Workspace management
 *--------------------------------------------------------------------------*/

struct yetty_ycore_void_result yetty_yui_tabbar_model_add_workspace_from_config(
    struct yetty_yui_tabbar_model *bar, const struct yetty_yconfig_config *config,
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
    float strip = tabbar_dp(bar, YETTY_YUI_TABBAR_HEIGHT_DP);
    if (strip > bar->height) {
        strip = bar->height;
    }
    if (bar->width > 0 && bar->height > 0) {
        struct yetty_ycore_void_result oo = yetty_yui_workspace_set_origin(ws, 0, strip);
        if (YETTY_IS_ERR(oo)) {
            (void)yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: set_origin failed", oo);
        }
        struct yetty_ycore_void_result rr =
            yetty_yui_workspace_resize(ws, bar->width, bar->height - strip);
        if (YETTY_IS_ERR(rr)) {
            (void)yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: initial resize failed", rr);
        }
    }

    struct yetty_ycore_void_result lr = yetty_yui_workspace_load_layout(ws, config, yetty_ctx);
    if (YETTY_IS_ERR(lr)) {
        (void)yetty_yui_workspace_destroy(ws);
        return YETTY_ERR(yetty_ycore_void, "tabbar_add_ws: load_layout failed", lr);
    }

    /* Deactivate the workspace we're switching away from (if any) so its
     * leaf view gets a focus-out notification before the new one's
     * focus-in. */
    if (bar->count > 0 && bar->workspaces[bar->active]) {
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yui_workspace_set_active(bar->workspaces[bar->active], 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_set_active");
        }
    }

    bar->workspaces[bar->count++] = ws;
    bar->active = bar->count - 1;

    /* The new workspace becomes active immediately — cascade focus down
     * to its terminal so it knows it's the foreground view. */
    {
        struct yetty_ycore_void_result drop_r = yetty_yui_workspace_set_active(ws, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_set_active");
    }

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
        {
            struct yetty_ycore_int_result drop_r = yetty_yui_workspace_on_event(ws, &resize_ev);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_on_event");
        }
    }

    /* Kick a render so the just-created tab actually shows up. Otherwise
     * the screen keeps the previous frame until the first PTY echo, which
     * makes the new tab look "stuck on the old one until I type". */
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_attach_empty_workspace(
    struct yetty_yui_tabbar_model *bar, yetty_ycore_object_id workspace_id,
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

    struct yetty_yui_workspace_ptr_result wr = yetty_yui_workspace_create_with_id(workspace_id);
    if (YETTY_IS_ERR(wr)) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: workspace_create failed", wr);
    }
    struct yetty_yui_workspace *ws = wr.value;

    float strip = tabbar_dp(bar, YETTY_YUI_TABBAR_HEIGHT_DP);
    if (strip > bar->height) {
        strip = bar->height;
    }
    if (bar->width > 0 && bar->height > 0) {
        struct yetty_ycore_void_result oo = yetty_yui_workspace_set_origin(ws, 0, strip);
        if (YETTY_IS_ERR(oo)) {
            (void)yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: set_origin failed", oo);
        }
        struct yetty_ycore_void_result rr =
            yetty_yui_workspace_resize(ws, bar->width, bar->height - strip);
        if (YETTY_IS_ERR(rr)) {
            (void)yetty_yui_workspace_destroy(ws);
            return YETTY_ERR(yetty_ycore_void, "tabbar_attach_empty_ws: initial resize failed", rr);
        }
    }

    if (bar->count > 0 && bar->workspaces[bar->active]) {
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yui_workspace_set_active(bar->workspaces[bar->active], 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_set_active");
        }
    }
    bar->workspaces[bar->count++] = ws;
    bar->active = bar->count - 1;

    if (out_ws) {
        *out_ws = ws;
    }
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

struct yetty_yui_workspace *yetty_yui_tabbar_model_find_workspace(
    const struct yetty_yui_tabbar_model *bar, yetty_ycore_object_id workspace_id)
{
    if (!bar) {
        return NULL;
    }
    for (size_t i = 0; i < bar->count; i++) {
        if (bar->workspaces[i] && yetty_yui_workspace_id(bar->workspaces[i]) == workspace_id) {
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
struct yetty_ycore_void_result yetty_yui_tabbar_model_add_workspace_of_kind(
    struct yetty_yui_tabbar_model *bar, enum yetty_yui_tabbar_model_kind kind)
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

    return yetty_yui_tabbar_model_add_workspace_from_config(bar, cfg, bar->yetty_ctx);
}

static struct yetty_ycore_void_result tabbar_close_active(struct yetty_yui_tabbar_model *bar)
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
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yui_workspace_set_active(bar->workspaces[bar->active], 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_set_active");
        }
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
static void tabbar_request_render(const struct yetty_yui_tabbar_model *bar)
{
    if (!bar || !bar->yetty_ctx || !bar->yetty_ctx->event_loop) {
        return;
    }
    struct yetty_yevent_event_loop *loop = bar->yetty_ctx->event_loop;
    if (loop->ops && loop->ops->request_render) {
        loop->ops->request_render(loop);
    }
}

static struct yetty_ycore_void_result tabbar_switch(struct yetty_yui_tabbar_model *bar, size_t idx)
{
    if (idx >= bar->count || idx == bar->active) {
        return YETTY_OK_VOID();
    }
    /* Deactivate the previously active workspace, then activate the new
     * one. Both calls cascade SET_FOCUS down through the focused pane to
     * its active view — without this the leaf view never learns that a
     * tab switch happened, and per-view focus behaviour (terminal
     * cursor blink, future focus reporting CSEQ, etc.) lags one step
     * behind the visible tab. */
    if (bar->workspaces[bar->active]) {
        struct yetty_ycore_void_result deactivate_result =
            yetty_yui_workspace_set_active(bar->workspaces[bar->active], 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, deactivate_result,
                            "tabbar_switch: deactivate previous workspace");
    }
    bar->active = idx;
    if (bar->workspaces[bar->active]) {
        struct yetty_ycore_void_result activate_result =
            yetty_yui_workspace_set_active(bar->workspaces[bar->active], 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, activate_result,
                            "tabbar_switch: activate new workspace");
    }
    ydebug("tabbar: switched to workspace %zu/%zu", idx + 1, bar->count);
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
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

/* Window-manager slots are fire-and-forget from the render thread (they post a
 * request onto the output_pipe). A failure here can only be an object-resolve
 * error, which is non-recoverable at this call site — absorb the chain so it
 * isn't leaked. */
static void wm_absorb(struct yetty_ycore_void_result window_chrome_result)
{
    if (YETTY_IS_ERR(window_chrome_result)) {
        yetty_ycore_error_destroy(window_chrome_result.error);
    }
}

struct yetty_ycore_int_result yetty_yui_tabbar_model_on_event(struct yetty_yui_tabbar_model *bar,
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
                struct yetty_ycore_void_result r = yetty_yui_tabbar_model_add_workspace_from_config(
                    bar, bar->config, bar->yetty_ctx);
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
                struct yetty_ycore_void_result switch_result = tabbar_switch(bar, next);
                if (YETTY_IS_ERR(switch_result)) {
                    yerror("tabbar: switch failed: %s", switch_result.error.msg);
                    yetty_ycore_error_destroy(switch_result.error);
                }
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
        if (k >= KEY_1 && k <= KEY_9) {
            struct yetty_ycore_void_result switch_result = tabbar_switch(bar, (size_t)(k - KEY_1));
            if (YETTY_IS_ERR(switch_result)) {
                yerror("tabbar: switch failed: %s", switch_result.error.msg);
                yetty_ycore_error_destroy(switch_result.error);
            }
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    /* Mouse hit-test on the strip region. Anything in y < strip belongs to
     * the tabbar; we don't forward it to the workspace. Once the strip has
     * actual tab cells, this is where the per-tab hit / drag-to-move /
     * close-button logic will live. */
    float strip = tabbar_dp(bar, YETTY_YUI_TABBAR_HEIGHT_DP);
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
        struct yetty_yclass_object *wm =
            bar->yetty_ctx ? bar->yetty_ctx->runtime->window_chrome : NULL;
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
    if (in_strip && event->type == YETTY_YCORE_MOUSE_DOUBLE_CLICK && event->mouse.button == 0 &&
        bar->count > 0) {
        struct yetty_yclass_object *wm =
            bar->yetty_ctx ? bar->yetty_ctx->runtime->window_chrome : NULL;
        if (wm) {
            wm_absorb(yetty_yplatform_window_chrome_toggle_maximize(wm));
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
        struct yetty_yclass_object *wm =
            bar->yetty_ctx ? bar->yetty_ctx->runtime->window_chrome : NULL;
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
            if (wm && !bar->drag_move_grab_sent) {
                /* Threshold crossed — commit to a drag. On Wayland the
                 * compositor grabs the pointer here and we won't see
                 * further MOVE events until the user releases. On X11
                 * it's a no-op and the per-pixel drag_by below keeps
                 * driving glfwSetWindowPos. */
                bar->drag_move_grab_sent = 1;
                wm_absorb(yetty_yplatform_window_chrome_begin_interactive_move(wm));
            }
            ydebug("DRAGTRACE: [render-thread] tabbar MOVE during drag: "
                   "mouse=(%.1f,%.1f) anchor=(%.1f,%.1f) dx=%d dy=%d wm=%p",
                   event->mouse.x, event->mouse.y, bar->drag_anchor_x, bar->drag_anchor_y, dx, dy,
                   (void *)wm);
            if (wm && (dx != 0 || dy != 0)) {
                wm_absorb(yetty_yplatform_window_chrome_drag_by(wm, dx, dy));
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
        /* 8 px is a touch wider than common desktop slop (5–6 px) — with
         * no visible CSD frame the user can't see exactly where the grip
         * is, so an extra couple of pixels makes it findable. */
        const float EDGE = 8.0f;
        /* Bottom band reaches to the actual window bottom (= total_height,
         * which includes the statusbar inset). bar->height alone is the
         * workspace bottom, which sits a statusbar's worth of pixels
         * above the user-visible edge. Falls back to bar->height if
         * total_height was never set. */
        const float bottom_y = bar->total_height > 0 ? bar->total_height : bar->height;
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
        struct yetty_yclass_object *wm =
            bar->yetty_ctx ? bar->yetty_ctx->runtime->window_chrome : NULL;

        /* No `!bar->resizing` guard. On Wayland the compositor consumes
         * the entire gesture after begin_interactive_resize — including
         * the MOUSE_UP that would clear `bar->resizing`. Without the
         * guard, every fresh edge-press re-inits cleanly regardless of
         * stale state from a previous gesture. */
        if (have_xy && event->type == YETTY_YCORE_MOUSE_DOWN && wm) {
            /* Left/right/bottom margins + the two bottom corners. Top is the
             * tab strip (drag territory), so it's excluded here — this block
             * only runs for presses the strip-drag handler above didn't claim. */
            int left = mx >= 0.0f && mx < EDGE;
            int right = mx > bar->width - EDGE && mx <= bar->width;
            int bottom = my > bottom_y - EDGE && my <= bottom_y;
            if (left || right || bottom) {
                bar->resizing = 1;
                bar->resize_left = left;
                bar->resize_right = right;
                bar->resize_top = 0;
                bar->resize_bottom = bottom;
                bar->resize_last_x = mx;
                bar->resize_last_y = my;
                bar->resize_anchor_x = mx;
                bar->resize_anchor_y = my;
                bar->resize_grab_sent = 0;
                /* xdg-shell wire enum: top=1, bottom=2, left=4, right=8;
                 * corners are bitwise OR. The window_chrome applies the
                 * left-edge origin shift from fresh geometry. */
                bar->resize_edge = (left ? YETTY_YCORE_RESIZE_EDGE_LEFT : 0) |
                                   (right ? YETTY_YCORE_RESIZE_EDGE_RIGHT : 0) |
                                   (bottom ? YETTY_YCORE_RESIZE_EDGE_BOTTOM : 0);
                ydebug("tabbar: resize start edge=%d at (%.1f, %.1f)", bar->resize_edge, mx, my);
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        if (bar->resizing) {
            if (have_xy &&
                (event->type == YETTY_YCORE_MOUSE_MOVE || event->type == YETTY_YCORE_MOUSE_DRAG)) {
                /* Same drag-vs-click slop as the move path. Below the
                 * threshold the gesture might still be a clean click;
                 * only cross it before handing the gesture to the
                 * compositor or emitting per-pixel deltas. */
                const int RESIZE_SLOP_PX = 3;
                float ax = mx - bar->resize_anchor_x;
                float ay = my - bar->resize_anchor_y;
                if (ax * ax + ay * ay < RESIZE_SLOP_PX * RESIZE_SLOP_PX) {
                    return YETTY_OK(yetty_ycore_int, 1);
                }
                if (wm && !bar->resize_grab_sent && bar->resize_edge != 0) {
                    /* Threshold crossed — on Wayland the compositor grabs
                     * the pointer here and we won't see further MOVE/UP
                     * for this gesture. On X11 it's a no-op and the
                     * per-pixel resize_by below keeps driving
                     * glfwSetWindowSize. */
                    bar->resize_grab_sent = 1;
                    wm_absorb(yetty_yplatform_window_chrome_begin_interactive_resize(
                        wm, bar->resize_edge));
                }
                /* Per-axis delta: right/bottom track incrementally (top-left
                 * fixed); left measures from the fixed anchor (the window-move
                 * resets the window-relative cursor) to avoid a feedback loop. */
                int step_dx = 0, step_dy = 0;
                if (bar->resize_right) {
                    step_dx = (int)(mx - bar->resize_last_x);
                    bar->resize_last_x = mx;
                } else if (bar->resize_left) {
                    step_dx = (int)(mx - bar->resize_anchor_x);
                }
                if (bar->resize_bottom) {
                    step_dy = (int)(my - bar->resize_last_y);
                    bar->resize_last_y = my;
                }
                if (wm && (step_dx != 0 || step_dy != 0)) {
                    wm_absorb(yetty_yplatform_window_chrome_resize_by(wm, step_dx, step_dy,
                                                                      bar->resize_edge));
                }
                return YETTY_OK(yetty_ycore_int, 1);
            }
            if (event->type == YETTY_YCORE_MOUSE_UP) {
                ydebug("tabbar: resize end (grab_sent=%d)", bar->resize_grab_sent);
                bar->resizing = 0;
                bar->resize_grab_sent = 0;
                bar->resize_edge = 0;
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

struct yetty_yui_workspace *yetty_yui_tabbar_model_active_workspace(
    const struct yetty_yui_tabbar_model *bar)
{
    if (!bar || bar->count == 0) {
        return NULL;
    }
    return bar->workspaces[bar->active];
}

size_t yetty_yui_tabbar_model_count(const struct yetty_yui_tabbar_model *bar)
{
    return bar ? bar->count : 0;
}

size_t yetty_yui_tabbar_model_active_index(const struct yetty_yui_tabbar_model *bar)
{
    return bar ? bar->active : 0;
}

struct yetty_yui_workspace *yetty_yui_tabbar_model_workspace_at(
    const struct yetty_yui_tabbar_model *bar, size_t idx)
{
    if (!bar || idx >= bar->count) {
        return NULL;
    }
    return bar->workspaces[idx];
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_switch_to(struct yetty_yui_tabbar_model *bar,
                                                                size_t idx)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_switch_to: NULL");
    }
    struct yetty_ycore_void_result switch_result = tabbar_switch(bar, idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, switch_result, "tabbar_switch_to");
    return YETTY_OK_VOID();
}

/* Index-aware close; mirrors the bookkeeping of the static tabbar_close_active
 * but lets the caller pick which tab to drop. The "refuse to close the last
 * workspace" guard stays — it's a UX invariant, not specific to active-vs-N. */
struct yetty_ycore_void_result yetty_yui_tabbar_model_close_at(struct yetty_yui_tabbar_model *bar,
                                                               size_t idx)
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
        {
            struct yetty_ycore_void_result drop_r =
                yetty_yui_workspace_set_active(bar->workspaces[bar->active], 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, drop_r, "drop: yetty_yui_workspace_set_active");
        }
    }
    tabbar_request_render(bar);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_iconify(struct yetty_yui_tabbar_model *bar)
{
    if (!bar || !bar->yetty_ctx) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *wm = bar->yetty_ctx->runtime->window_chrome;
    if (wm) {
        struct yetty_ycore_void_result iconify_result = yetty_yplatform_window_chrome_iconify(wm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, iconify_result, "tabbar_iconify: window chrome");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_toggle_maximize(
    struct yetty_yui_tabbar_model *bar)
{
    if (!bar || !bar->yetty_ctx) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *wm = bar->yetty_ctx->runtime->window_chrome;
    if (wm) {
        struct yetty_ycore_void_result toggle_result =
            yetty_yplatform_window_chrome_toggle_maximize(wm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, toggle_result,
                            "tabbar_toggle_maximize: window chrome");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tabbar_model_close_window(
    struct yetty_yui_tabbar_model *bar)
{
    if (!bar || !bar->yetty_ctx) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *wm = bar->yetty_ctx->runtime->window_chrome;
    if (wm) {
        struct yetty_ycore_void_result close_result =
            yetty_yplatform_window_chrome_request_close(wm);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, close_result, "tabbar_close_window: window chrome");
    }
    return YETTY_OK_VOID();
}

void yetty_yui_tabbar_model_set_v_menu_callback(struct yetty_yui_tabbar_model *bar,
                                                yetty_yui_tabbar_model_v_menu_cb cb, void *userdata)
{
    if (!bar) {
        return;
    }
    bar->v_menu_cb = cb;
    bar->v_menu_userdata = userdata;
}
