/*
 * yetty_yui_tabbar — see tabbar.h for the design.
 *
 * Today this file only implements the architecture: data, sizing, event
 * routing, keyboard/mouse shortcuts. The visual tab cells (Chrome-style
 * rounded rectangles + labels + close button) need a dedicated WebGPU
 * pipeline that draws into the strip region after workspace render — see
 * the render() entry point for the integration seam.
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
#include "tabbar-paint.h"

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

    /* GPU pipeline that paints the strip + tab cells + buttons. Lazily
     * built on the first render so we don't pay for shader/pipeline
     * creation in tabbars that never get rendered (e.g. headless tests).
     * Owned by the tabbar; freed in destroy. */
    struct yetty_yui_tabbar_paint *paint;

    /* Window-drag state. The OS title bar is gone, so the strip's empty
     * area is the only place the user can grab to move the window. We
     * record the cursor position (in window-relative coords) at
     * MOUSE_DOWN; on each MOUSE_MOVE the delta from that anchor goes
     * to the platform window manager. Because we set the window
     * position absolutely on the main thread, after each move the
     * cursor's window-relative pos is back at the anchor, so the
     * anchor stays valid for the whole drag. */
    int dragging;
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

    /* v-menu hook. Clicking the v-button no longer toggles an in-bar
     * rect popup — instead the tabbar fires this callback so an external
     * subscriber (yetty.c → yui's ygui popup_menu) can paint a proper
     * widget-based menu on top. NULL means "no menu attached", in which
     * case the v-button click is a no-op. */
    yetty_yui_tabbar_v_menu_cb v_menu_cb;
    void                     *v_menu_userdata;

    /* Resolved style colors in straight float RGB. Defaults come from
     * the yetty brand palette and are overlaid by config's style/yui
     * block when add_workspace_from_config first runs (or
     * yetty_yui_tabbar_apply_style is called directly). Keeping the
     * resolved values on the struct keeps the render hot path free of
     * config lookups. */
    struct {
        float strip_r, strip_g, strip_b;
        float active_r, active_g, active_b;
        float inactive_r, inactive_g, inactive_b;
        float pill_r, pill_g, pill_b;
        float glyph_r, glyph_g, glyph_b;
        float accent_r, accent_g, accent_b;
        float winbtn_r, winbtn_g, winbtn_b;
        float close_r, close_g, close_b;
        float popup_body_r, popup_body_g, popup_body_b;
        float popup_row_r, popup_row_g, popup_row_b;
    } style;
};

/*---------------------------------------------------------------------------
 * Allocation helpers
 *--------------------------------------------------------------------------*/

static struct yetty_ycore_void_result tabbar_grow(struct yetty_yui_tabbar *bar)
{
    size_t new_cap = bar->capacity ? bar->capacity * 2 : INITIAL_CAPACITY;
    struct yetty_yui_workspace **arr =
        realloc(bar->workspaces, new_cap * sizeof(*bar->workspaces));
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

/* Resolve one RGB triplet: brand default from the literal arguments,
 * optionally overridden by a hex-string at `path` in config. Missing
 * or malformed values keep the default. */
static void style_resolve(const struct yetty_yconfig_config *config, const char *path,
                          float def_r, float def_g, float def_b,
                          float *r, float *g, float *b)
{
    *r = def_r; *g = def_g; *b = def_b;
    if (!config || !config->ops || !config->ops->get_string) {
        return;
    }
    const char *s = config->ops->get_string(config, path, NULL);
    if (!s || !*s) {
        return;
    }
    uint32_t v = 0;
    if (!yetty_ycore_parse_hex_color(s, &v)) {
        return;
    }
    *r = (float)(v & 0xFFu) / 255.0f;
    *g = (float)((v >> 8) & 0xFFu) / 255.0f;
    *b = (float)((v >> 16) & 0xFFu) / 255.0f;
}

struct yetty_yui_tabbar_ptr_result yetty_yui_tabbar_create(
    const struct yetty_yconfig_config *config)
{
    struct yetty_yui_tabbar *bar = calloc(1, sizeof(struct yetty_yui_tabbar));
    if (!bar) {
        return YETTY_ERR(yetty_yui_tabbar_ptr, "tabbar_create: allocation failed");
    }

    /* Brand-palette defaults — see ~/.claude/yetty-new/rules/08-branding.md
     * for the role names. config (when non-NULL) overlays these via the
     * style/yui block; see config-defaults.yaml for the key list. */
    style_resolve(config, "style/yui/strip",        0.078f, 0.102f, 0.122f,
                  &bar->style.strip_r,    &bar->style.strip_g,    &bar->style.strip_b);
    style_resolve(config, "style/yui/tab-active",   0.043f, 0.063f, 0.078f,
                  &bar->style.active_r,   &bar->style.active_g,   &bar->style.active_b);
    style_resolve(config, "style/yui/tab-inactive", 0.118f, 0.149f, 0.173f,
                  &bar->style.inactive_r, &bar->style.inactive_g, &bar->style.inactive_b);
    style_resolve(config, "style/yui/pill",         0.212f, 0.290f, 0.278f,
                  &bar->style.pill_r,     &bar->style.pill_g,     &bar->style.pill_b);
    style_resolve(config, "style/yui/glyph",        0.878f, 0.898f, 0.894f,
                  &bar->style.glyph_r,    &bar->style.glyph_g,    &bar->style.glyph_b);
    style_resolve(config, "style/yui/accent",       0.420f, 0.659f, 0.573f,
                  &bar->style.accent_r,   &bar->style.accent_g,   &bar->style.accent_b);
    style_resolve(config, "style/yui/winbtn-bg",    0.078f, 0.102f, 0.122f,
                  &bar->style.winbtn_r,   &bar->style.winbtn_g,   &bar->style.winbtn_b);
    style_resolve(config, "style/yui/close-bg",     0.35f,  0.10f,  0.11f,
                  &bar->style.close_r,    &bar->style.close_g,    &bar->style.close_b);
    style_resolve(config, "style/yui/popup-body",   0.078f, 0.102f, 0.122f,
                  &bar->style.popup_body_r, &bar->style.popup_body_g, &bar->style.popup_body_b);
    style_resolve(config, "style/yui/popup-row",    0.118f, 0.149f, 0.173f,
                  &bar->style.popup_row_r,  &bar->style.popup_row_g,  &bar->style.popup_row_b);

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
    if (bar->paint) {
        yetty_yui_tabbar_paint_destroy(bar->paint);
    }
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
 * Chrome-style colors + geometry. Picked to read as the same UI element a
 * user sees in Google Chrome dark mode: dark gray strip, the active tab
 * "rises out" of the strip as a lighter rounded shape, inactive tabs sit
 * flush and slightly translucent. SDF rounding gives us crisp antialiased
 * corners at any zoom level (see solid-rects.wgsl).
 *--------------------------------------------------------------------------*/

/* Tab geometry. Caps at ~240 px when there's room and shrinks to a floor
 * around ~60 px when many tabs compete; matches mainstream browser ranges
 * so users get familiar behaviour at any window width. */
#define TABBAR_TAB_MAX_WIDTH    240.0f
#define TABBAR_TAB_MIN_WIDTH    60.0f
#define TABBAR_TAB_OVERLAP      0.0f    /* gap between adjacent tab cells */
#define TABBAR_TAB_RADIUS       12.0f   /* top-corner radius for tab cells */
#define TABBAR_TOP_INSET        4.0f    /* strip padding above each tab */
#define TABBAR_NEWTAB_AREA      40.0f   /* footprint reserved for the new-tab "+" pill */
#define TABBAR_PLUS_THICKNESS   2.0f    /* arm thickness of the + glyph */

/* Left-edge "v" dropdown button — mirrors Chrome's hamburger / customize
 * menu. Lives between the window's left edge and the first tab. Same
 * pill footprint as the "+" button so the two read as siblings. */
#define TABBAR_MENU_AREA        40.0f   /* width reserved for the "v" pill on the left */
#define TABBAR_CHEVRON_LEN      10.0f   /* total span of the chevron arms */
#define TABBAR_CHEVRON_STROKE   2.0f    /* arm thickness of the chevron */

/* Window-control buttons (minimize, maximize-toggle, close) live at the
 * far right of the strip. Each button has the same square footprint; the
 * right-most one is close, leftmost minimize. Geometry is fixed so the
 * main thread (when wired through the output pipe) can derive identical
 * hit-rects without consulting the tabbar's internal state. */
#define TABBAR_WINBTN_WIDTH     46.0f   /* per-button slot width */
#define TABBAR_WINBTN_COUNT     3
#define TABBAR_WINBTN_AREA      (TABBAR_WINBTN_WIDTH * TABBAR_WINBTN_COUNT)
#define TABBAR_WINBTN_GLYPH_PX  10.0f   /* nominal glyph size in pixels */
#define TABBAR_WINBTN_STROKE    1.5f    /* glyph stroke thickness */

static float tab_width(const struct yetty_yui_tabbar *bar)
{
    if (bar->count == 0 || bar->width <= 0) {
        return 0;
    }
    /* Tabs share the strip with: the left "v" menu pill, the new-tab
     * "+" pill that sits right after the last tab, and the three
     * window-control buttons on the far right. Reserve all three so
     * that even with many tabs the "+" still has room next to the last
     * one without overlapping the close button. */
    float usable = bar->width - TABBAR_MENU_AREA - TABBAR_NEWTAB_AREA - TABBAR_WINBTN_AREA;
    if (usable <= 0) {
        usable = bar->width;
    }
    float ideal = usable / (float)bar->count - TABBAR_TAB_OVERLAP;
    if (ideal > TABBAR_TAB_MAX_WIDTH) {
        ideal = TABBAR_TAB_MAX_WIDTH;
    }
    if (ideal < TABBAR_TAB_MIN_WIDTH) {
        ideal = TABBAR_TAB_MIN_WIDTH;
    }
    return ideal;
}

/* Push (or replace) one rect into the batch. Caller is responsible for
 * bounds — we trust rect_count up front. */
static void push_rect(struct yetty_yui_tabbar_rect *rects, size_t *n,
                      struct yetty_yui_tabbar_rect r)
{
    rects[(*n)++] = r;
}

/* Build the right-side window-control buttons (minimize / maximize-toggle /
 * close). Footprint mirrors what most desktop window managers paint on the
 * title bar: 3 equal-width squares, the rightmost being a red-tinted close
 * with an "X" glyph rotated 45° in the same instanced draw call. The
 * actual click-to-act wiring (output pipe back to main thread) comes in a
 * follow-up; today this just paints. */
static void emit_window_buttons(const struct yetty_yui_tabbar *bar, float strip,
                                struct yetty_yui_tabbar_rect *rects, size_t *n)
{
    float right = bar->width;
    float btn_w = TABBAR_WINBTN_WIDTH;
    float btn_h = strip;
    /* Slot rects, in screen order (left to right): minimize, max, close.
     * We'll fill each with a glyph after. */
    float min_x = right - 3.0f * btn_w;
    float max_x = right - 2.0f * btn_w;
    float close_x = right - 1.0f * btn_w;

    /* Background fills + close-button tint come from the resolved style
     * (defaults match BRAND_BG_LIFTED for min/max, dark red for close). */
    push_rect(rects, n,
              (struct yetty_yui_tabbar_rect){.x = min_x, .y = 0, .w = btn_w, .h = btn_h,
                                             .r = bar->style.winbtn_r,
                                             .g = bar->style.winbtn_g,
                                             .b = bar->style.winbtn_b, .a = 1.0f});
    push_rect(rects, n,
              (struct yetty_yui_tabbar_rect){.x = max_x, .y = 0, .w = btn_w, .h = btn_h,
                                             .r = bar->style.winbtn_r,
                                             .g = bar->style.winbtn_g,
                                             .b = bar->style.winbtn_b, .a = 1.0f});
    push_rect(rects, n,
              (struct yetty_yui_tabbar_rect){.x = close_x, .y = 0, .w = btn_w, .h = btn_h,
                                             .r = bar->style.close_r,
                                             .g = bar->style.close_g,
                                             .b = bar->style.close_b, .a = 1.0f});

    /* Glyphs use the resolved style glyph color (default BRAND_TEXT_PRIMARY:
     * off-white with a cool tint, matching the wordmark's "ett" letters). */
    const float FG_R = bar->style.glyph_r, FG_G = bar->style.glyph_g, FG_B = bar->style.glyph_b;
    const float gpx = TABBAR_WINBTN_GLYPH_PX;
    const float stroke = TABBAR_WINBTN_STROKE;

    float cy = btn_h * 0.5f;

    /* Minimize: short horizontal stroke near the vertical center. Sits a
     * hair below center so it reads as "to the floor" not a divider. */
    {
        float cx = min_x + btn_w * 0.5f;
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = cx - gpx * 0.5f,
                                                 .y = cy + gpx * 0.25f - stroke * 0.5f,
                                                 .w = gpx,
                                                 .h = stroke,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f});
    }

    /* Maximize: a square outline made of 4 thin strokes (top, bottom, left,
     * right). One pipeline pass for all of them, no special glyph atlas.
     * Could later toggle to a "restore" pair-of-squares once we know the
     * window state via the output pipe. */
    {
        float cx = max_x + btn_w * 0.5f;
        float side = gpx;
        float left = cx - side * 0.5f;
        float top = cy - side * 0.5f;
        /* Top edge */
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = left, .y = top,
                                                 .w = side, .h = stroke,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f});
        /* Bottom edge */
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = left, .y = top + side - stroke,
                                                 .w = side, .h = stroke,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f});
        /* Left edge */
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = left, .y = top,
                                                 .w = stroke, .h = side,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f});
        /* Right edge */
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = left + side - stroke, .y = top,
                                                 .w = stroke, .h = side,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f});
    }

    /* Close: two thin strokes rotated ±45° to form an X. Lengths are scaled
     * by sqrt(2) so the endpoints meet the corners of a gpx-side bounding
     * square (otherwise the strokes look short compared to min/max glyphs).
     * The rotation lives in the same instance attribute the shader reads
     * — no second pipeline. */
    {
        float cx = close_x + btn_w * 0.5f;
        float diag = gpx * 1.41421356f; /* gpx * sqrt(2) */
        /* Each rect is laid out un-rotated centered at the glyph center;
         * the shader rotates around the rect's own center, so pos = topleft
         * of the un-rotated rect. */
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = cx - diag * 0.5f,
                                                 .y = cy - stroke * 0.5f,
                                                 .w = diag, .h = stroke,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f,
                                                 .rotation = 0.78539816f /* +45° */});
        push_rect(rects, n,
                  (struct yetty_yui_tabbar_rect){.x = cx - diag * 0.5f,
                                                 .y = cy - stroke * 0.5f,
                                                 .w = diag, .h = stroke,
                                                 .r = FG_R, .g = FG_G, .b = FG_B, .a = 1.0f,
                                                 .rotation = -0.78539816f /* -45° */});
    }
}

struct yetty_ycore_void_result yetty_yui_tabbar_render(
    struct yetty_yui_tabbar *bar, struct yetty_ydraw_target *render_target)
{
    if (!bar) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_render: NULL");
    }
    if (bar->count == 0) {
        return YETTY_OK_VOID();
    }

    /* Workspace pass first — the strip then paints on top. The workspace
     * was set_origin'd to (0, strip) at resize time so its tiles already
     * render below the strip and the overlay can be a thin band. */
    struct yetty_yui_workspace *ws = bar->workspaces[bar->active];
    if (ws) {
        struct yetty_ycore_void_result r = yetty_yui_workspace_render(ws, render_target);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "tabbar_render: workspace render failed");
    }

    /* Need device/queue from the yetty_ctx and the texture view + size
     * from the render target. Either missing → we silently skip the
     * overlay (the tabs are still switchable via keyboard). */
    if (!render_target || !render_target->ops || !render_target->ops->get_view) {
        return YETTY_OK_VOID();
    }
    if (!bar->yetty_ctx) {
        return YETTY_OK_VOID();
    }
    WGPUDevice device = bar->yetty_ctx->gpu_context.device;
    WGPUQueue queue = bar->yetty_ctx->gpu_context.queue;
    WGPUTextureFormat fmt = bar->yetty_ctx->gpu_context.surface_format;
    WGPUTextureView view = render_target->ops->get_view(render_target);
    if (!device || !queue || !view) {
        return YETTY_OK_VOID();
    }

    /* Lazy first-frame pipeline creation. */
    if (!bar->paint) {
        struct yetty_yui_tabbar_paint_ptr_result pr =
            yetty_yui_tabbar_paint_create(device, queue, fmt);
        if (YETTY_IS_ERR(pr)) {
            return YETTY_ERR(yetty_ycore_void, "tabbar_render: paint_create failed", pr);
        }
        bar->paint = pr.value;
    }

    float strip = YETTY_YUI_TABBAR_HEIGHT;
    if (strip > bar->height) {
        strip = bar->height;
    }
    if (strip <= 0 || bar->width <= 0) {
        return YETTY_OK_VOID();
    }

    /* Worst-case rect count:
     *   1 strip bg + N tabs + 3 plus-button rects (pill + 2 arms)
     *   + 3 "v" button rects (pill + 2 chevron arms)
     *   + 3 window-button bgs + 1 (min) + 4 (max outline) + 2 (close X) = 17
     * Total upper bound: 17 + N. Stack up to 64. */
    size_t rect_count = 17 + bar->count;
    struct yetty_yui_tabbar_rect stack_rects[64];
    struct yetty_yui_tabbar_rect *rects = stack_rects;
    struct yetty_yui_tabbar_rect *heap_rects = NULL;
    if (rect_count > sizeof(stack_rects) / sizeof(stack_rects[0])) {
        heap_rects = malloc(rect_count * sizeof(*heap_rects));
        if (!heap_rects) {
            return YETTY_ERR(yetty_ycore_void, "tabbar_render: rect alloc failed");
        }
        rects = heap_rects;
    }
    size_t n = 0;

    /* Resolved palette — defaults set in tabbar_create, optionally
     * overlaid by the style/yui block from config. See
     * rules/08-branding.md for role names and ABGR encoding details. */
    const float STRIP_R = bar->style.strip_r, STRIP_G = bar->style.strip_g, STRIP_B = bar->style.strip_b;
    const float ACTIVE_R = bar->style.active_r, ACTIVE_G = bar->style.active_g, ACTIVE_B = bar->style.active_b;
    const float INACTIVE_R = bar->style.inactive_r, INACTIVE_G = bar->style.inactive_g, INACTIVE_B = bar->style.inactive_b;
    const float ACCENT_R = bar->style.accent_r, ACCENT_G = bar->style.accent_g, ACCENT_B = bar->style.accent_b;
    const float GLYPH_R = bar->style.glyph_r, GLYPH_G = bar->style.glyph_g, GLYPH_B = bar->style.glyph_b;
    const float PILL_R = bar->style.pill_r, PILL_G = bar->style.pill_g, PILL_B = bar->style.pill_b;
    (void)GLYPH_R; (void)GLYPH_G; (void)GLYPH_B; /* used by emit_window_buttons via bar */

    /* 1. Strip background — sharp rect, no radius (full-width band). */
    push_rect(rects, &n,
              (struct yetty_yui_tabbar_rect){.x = 0, .y = 0, .w = bar->width, .h = strip,
                                             .r = STRIP_R, .g = STRIP_G, .b = STRIP_B, .a = 1.0f});

    float tw = tab_width(bar);
    float top_inset = TABBAR_TOP_INSET;
    if (top_inset > strip * 0.30f) {
        top_inset = strip * 0.30f;
    }
    float tab_h = strip - top_inset;
    /* Clamp corner radius to half the smaller side so degenerate narrow
     * tabs don't produce visual artifacts. */
    float r_corner = TABBAR_TAB_RADIUS;
    if (r_corner > tab_h * 0.5f) r_corner = tab_h * 0.5f;
    if (r_corner > tw * 0.5f)    r_corner = tw * 0.5f;

    /* 1b. "v" menu pill on the left — same footprint as the "+" pill so
     * the two read as siblings. Chevron glyph is two short rotated rects
     * forming a downward V, drawn in the same instanced pass. */
    {
        float btn = strip - top_inset - 4.0f;
        if (btn < 16.0f) btn = 16.0f;
        if (btn > 22.0f) btn = 22.0f;
        float bx = (TABBAR_MENU_AREA - btn) * 0.5f;
        float by = top_inset + (tab_h - btn) * 0.5f;
        float radius = btn * 0.5f;

        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){
                      .x = bx, .y = by, .w = btn, .h = btn,
                      .r = PILL_R, .g = PILL_G, .b = PILL_B, .a = 1.0f,
                      .radius_tl = radius, .radius_tr = radius,
                      .radius_br = radius, .radius_bl = radius,
                  });

        /* Chevron: two strokes meeting at the bottom-center of the
         * glyph box, each rotated 45°/-45° around its own center. The
         * chevron itself is the brand mint — it's the user-action
         * surface, so it picks up brand accent rather than off-white. */
        float arm_len = TABBAR_CHEVRON_LEN;
        float arm_th = TABBAR_CHEVRON_STROKE;
        float cx = bx + btn * 0.5f;
        float cy = by + btn * 0.5f;
        /* Pre-translate so each rotated arm's bottom-end lands at (cx, cy+offset). */
        float off_x = arm_len * 0.25f; /* horizontal offset of each arm's center */
        float off_y = 0.0f;
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){
                      .x = cx - off_x - arm_len * 0.5f,
                      .y = cy + off_y - arm_th * 0.5f,
                      .w = arm_len, .h = arm_th,
                      .r = ACCENT_R, .g = ACCENT_G, .b = ACCENT_B, .a = 1.0f,
                      .rotation = 0.78539816f /* +45° */
                  });
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){
                      .x = cx + off_x - arm_len * 0.5f,
                      .y = cy + off_y - arm_th * 0.5f,
                      .w = arm_len, .h = arm_th,
                      .r = ACCENT_R, .g = ACCENT_G, .b = ACCENT_B, .a = 1.0f,
                      .rotation = -0.78539816f /* -45° */
                  });
    }

    float x = TABBAR_MENU_AREA;
    for (size_t i = 0; i < bar->count; i++) {
        int active = (i == bar->active);
        /* Bottom corners stay square so the tab "merges" into the content
         * area below (CSS order: tl, tr, br, bl). */
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){
                      .x = x,
                      .y = top_inset,
                      .w = tw,
                      .h = tab_h,
                      .r = active ? ACTIVE_R   : INACTIVE_R,
                      .g = active ? ACTIVE_G   : INACTIVE_G,
                      .b = active ? ACTIVE_B   : INACTIVE_B,
                      .a = 1.0f,
                      .radius_tl = r_corner,
                      .radius_tr = r_corner,
                      .radius_br = 0.0f,
                      .radius_bl = 0.0f,
                  });
        x += tw + TABBAR_TAB_OVERLAP;
    }

    /* 2. New-tab "+" button — fully-rounded pill background + 2 thin
     * sharp rects forming the plus glyph. Positioned immediately to the
     * right of the last tab (Chrome-style), not at the far right edge. */
    {
        float btn = strip - top_inset - 4.0f;
        if (btn < 16.0f) btn = 16.0f;
        if (btn > 22.0f) btn = 22.0f;
        float bx = x + (TABBAR_NEWTAB_AREA - btn) * 0.5f;
        float by = top_inset + (tab_h - btn) * 0.5f;
        float radius = btn * 0.5f; /* perfect circle */

        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){
                      .x = bx, .y = by, .w = btn, .h = btn,
                      .r = PILL_R, .g = PILL_G, .b = PILL_B, .a = 1.0f,
                      .radius_tl = radius, .radius_tr = radius,
                      .radius_br = radius, .radius_bl = radius,
                  });

        /* Plus glyph in brand mint — paired with the chevron, signals
         * "action" against the BRAND_BG-family pill. */
        float arm_len = btn * 0.5f;
        float arm_th = TABBAR_PLUS_THICKNESS;
        float cx = bx + btn * 0.5f;
        float cy = by + btn * 0.5f;
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){.x = cx - arm_len * 0.5f,
                                                 .y = cy - arm_th * 0.5f,
                                                 .w = arm_len, .h = arm_th,
                                                 .r = ACCENT_R, .g = ACCENT_G, .b = ACCENT_B, .a = 1.0f});
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){.x = cx - arm_th * 0.5f,
                                                 .y = cy - arm_len * 0.5f,
                                                 .w = arm_th, .h = arm_len,
                                                 .r = ACCENT_R, .g = ACCENT_G, .b = ACCENT_B, .a = 1.0f});
    }

    /* 3. Window-control buttons (minimize / maximize / close) on the
     * far right. Visuals only at this stage — click → output_pipe wiring
     * comes in a follow-up. */
    emit_window_buttons(bar, strip, rects, &n);

    /* Texture size for NDC mapping. Asking the inner WebGPU texture is
     * the only reliable source — render_target->viewport gets clobbered
     * by per-pane scissor in pane_render(). */
    WGPUTexture tex = render_target->ops->get_texture
                          ? render_target->ops->get_texture(render_target)
                          : NULL;
    uint32_t tw_px = tex ? wgpuTextureGetWidth(tex) : (uint32_t)bar->width;
    uint32_t th_px = tex ? wgpuTextureGetHeight(tex) : (uint32_t)bar->height;

    struct yetty_ycore_void_result dr =
        yetty_yui_tabbar_paint_draw(bar->paint, view, tw_px, th_px, rects, n);
    if (heap_rects) {
        free(heap_rects);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "tabbar_render: paint_draw failed");

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
        return YETTY_ERR(yetty_ycore_void,
                         "tabbar_add_of_kind: config/ctx not cached yet");
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
            return YETTY_ERR(yetty_ycore_void,
                             "yvnc: vnc/client not set (dialog not yet wired)");
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
    /* Refuse to close the last workspace — Chrome closes the window in that
     * case, but yetty's lifecycle is owned by yetty.c (which posts SHUTDOWN
     * on window-close). Leaving the last workspace open keeps the UI in a
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
     * matches Chrome's defaults. */
    if (event->type == YETTY_YCORE_KEY_DOWN && (event->key.mods & YETTY_MOD_CONTROL)) {
        int k = event->key.key;
        int shift = (event->key.mods & YETTY_MOD_SHIFT) != 0;

        /* Ctrl+Shift+T → new tab. Ctrl+T alone is a common shell binding
         * (FZF, etc.), so we put the new-tab on Ctrl+Shift+T to leave Ctrl+T
         * for the inner program. */
        if (shift && k == KEY_T) {
            if (bar->config && bar->yetty_ctx) {
                struct yetty_ycore_void_result r = yetty_yui_tabbar_add_workspace_from_config(
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

    /* Strip-area MOUSE_DOWN — route by x to a button, a tab, or start a
     * window drag if it landed in empty space. Layout left → right:
     *   [v-menu | ...tabs... | new-tab | drag space | min | max | close] */
    if (in_strip && event->type == YETTY_YCORE_MOUSE_DOWN && bar->count > 0) {
        float winbtn_left = bar->width - TABBAR_WINBTN_AREA;
        /* '+' button hugs the right side of the last tab. */
        float tw = tab_width(bar);
        float step = tw + TABBAR_TAB_OVERLAP;
        if (step <= 0) {
            step = 1.0f;
        }
        float tabs_start = TABBAR_MENU_AREA;
        float tabs_end = tabs_start + (float)bar->count * step;
        float newtab_left = tabs_end;
        float newtab_right = newtab_left + TABBAR_NEWTAB_AREA;
        if (newtab_right > winbtn_left) {
            newtab_right = winbtn_left;
        }
        struct yetty_yplatform_window_manager *wm =
            bar->yetty_ctx ? bar->yetty_ctx->app_context.window_manager : NULL;
        ydebug("tabbar: strip MOUSE_DOWN at (%.1f, %.1f) bar=(%.0fx%.0f) winbtn_left=%.0f "
               "newtab=[%.0f..%.0f] wm=%p", event->mouse.x, event->mouse.y, bar->width,
               bar->height, winbtn_left, newtab_left, newtab_right, (void *)wm);

        if (event->mouse.x < TABBAR_MENU_AREA) {
            /* "v" button — fire the v-menu callback. The actual popup
             * lives on yui's ygui_engine; tabbar just reports the click
             * with the anchor (lower-left of the v-button) so the
             * subscriber can position the popup. */
            if (bar->v_menu_cb) {
                float anchor_x = 4.0f;
                float anchor_y = strip;
                bar->v_menu_cb(bar->v_menu_userdata, anchor_x, anchor_y);
                ydebug("tabbar: v-menu callback fired at (%.1f, %.1f)", anchor_x, anchor_y);
            } else {
                ydebug("tabbar: v-button click with no menu callback bound");
            }
        } else if (event->mouse.x >= winbtn_left) {
            /* Slot order: minimize, maximize, close (left to right). */
            float slot = (event->mouse.x - winbtn_left) / TABBAR_WINBTN_WIDTH;
            ydebug("tabbar: window-button slot=%.2f wm=%p", slot, (void *)wm);
            if (wm) {
                if (slot < 1.0f) {
                    ydebug("tabbar: iconify");
                    wm->ops->iconify(wm);
                } else if (slot < 2.0f) {
                    ydebug("tabbar: toggle_maximize");
                    wm->ops->toggle_maximize(wm);
                } else {
                    ydebug("tabbar: request_close");
                    wm->ops->request_close(wm);
                }
            }
        } else if (event->mouse.x >= newtab_left && event->mouse.x < newtab_right
                   && bar->config && bar->yetty_ctx) {
            struct yetty_ycore_void_result r = yetty_yui_tabbar_add_workspace_from_config(
                bar, bar->config, bar->yetty_ctx);
            if (YETTY_IS_ERR(r)) {
                yerror("tabbar: new-tab button: %s", r.error.msg);
                yetty_ycore_error_destroy(r.error);
            } else {
                ydebug("tabbar: new-tab button → workspace %zu", bar->count);
            }
        } else {
            /* Tabs occupy [TABBAR_MENU_AREA .. tabs_end). Past tabs_end +
             * newtab footprint and before winbtn_left is empty space —
             * grab handle for window drag. */
            if (event->mouse.x >= tabs_start && event->mouse.x < tabs_end) {
                size_t idx = (size_t)((event->mouse.x - tabs_start) / step);
                if (idx >= bar->count) {
                    idx = bar->count - 1;
                }
                tabbar_switch(bar, idx);
            } else if (wm) {
                /* Empty strip area — start a window drag. Anchor in
                 * window-relative coords; subsequent MOUSE_MOVE deltas
                 * go to the platform via wm->drag_by. */
                bar->dragging = 1;
                bar->drag_anchor_x = event->mouse.x;
                bar->drag_anchor_y = event->mouse.y;
                ydebug("tabbar: drag start at (%.1f, %.1f)", event->mouse.x, event->mouse.y);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Continue an in-progress drag. We don't gate on `in_strip` because
     * once the user has the window grabbed, the cursor will travel
     * across the whole screen; everything else gets swallowed for the
     * duration of the drag. */
    if (bar->dragging) {
        struct yetty_yplatform_window_manager *wm =
            bar->yetty_ctx ? bar->yetty_ctx->app_context.window_manager : NULL;
        if (event->type == YETTY_YCORE_MOUSE_MOVE ||
            event->type == YETTY_YCORE_MOUSE_DRAG) {
            int dx = (int)(event->mouse.x - bar->drag_anchor_x);
            int dy = (int)(event->mouse.y - bar->drag_anchor_y);
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
            ydebug("tabbar: drag end");
            bar->dragging = 0;
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
                ydebug("tabbar: resize start dirs=(%d,%d) at (%.1f, %.1f)",
                       bar->resize_dir_x, bar->resize_dir_y, mx, my);
                return YETTY_OK(yetty_ycore_int, 1);
            }
        }
        if (bar->resizing) {
            if (have_xy && (event->type == YETTY_YCORE_MOUSE_MOVE ||
                            event->type == YETTY_YCORE_MOUSE_DRAG)) {
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

void yetty_yui_tabbar_set_v_menu_callback(struct yetty_yui_tabbar *bar,
                                          yetty_yui_tabbar_v_menu_cb cb, void *userdata)
{
    if (!bar) {
        return;
    }
    bar->v_menu_cb = cb;
    bar->v_menu_userdata = userdata;
}
