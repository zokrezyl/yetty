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
#include <yetty/yevent/event.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yetty/yetty.h>
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

struct yetty_yui_tabbar_ptr_result yetty_yui_tabbar_create(void)
{
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
#define TABBAR_NEWTAB_AREA      40.0f   /* right-edge new-tab "+" button */
#define TABBAR_PLUS_THICKNESS   2.0f    /* arm thickness of the + glyph */

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
    /* Tabs share the strip with the new-tab "+" button and the three
     * window-control buttons; reserve both regions on the right. */
    float usable = bar->width - TABBAR_NEWTAB_AREA - TABBAR_WINBTN_AREA;
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

    /* Background fills are flush with the strip so the buttons read as
     * "part of the bar" rather than floating pills. Close is tinted red
     * subtly enough that hover/click states can layer brighter red on top
     * without losing legibility. */
    push_rect(rects, n,
              (struct yetty_yui_tabbar_rect){.x = min_x, .y = 0, .w = btn_w, .h = btn_h,
                                             .r = 0.18f, .g = 0.19f, .b = 0.20f, .a = 1.0f});
    push_rect(rects, n,
              (struct yetty_yui_tabbar_rect){.x = max_x, .y = 0, .w = btn_w, .h = btn_h,
                                             .r = 0.18f, .g = 0.19f, .b = 0.20f, .a = 1.0f});
    push_rect(rects, n,
              (struct yetty_yui_tabbar_rect){.x = close_x, .y = 0, .w = btn_w, .h = btn_h,
                                             .r = 0.35f, .g = 0.10f, .b = 0.11f, .a = 1.0f});

    /* Glyphs. All in a near-white so they pop against the dark/red fill.
     * Geometry constants are normalized to TABBAR_WINBTN_GLYPH_PX which is
     * sized to read cleanly at the strip's pixel height. */
    const float FG_R = 0.92f, FG_G = 0.93f, FG_B = 0.95f;
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
    struct yetty_yui_tabbar *bar, struct yetty_ypaint_core_target *render_target)
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
     *   + 3 window-button bgs + 1 (min) + 4 (max outline) + 2 (close X) = 13
     * Total: 14 + N. Stack-allocate up to 64, heap above. */
    size_t rect_count = 14 + bar->count;
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

    /* Dark-mode palette tuned to read as a single coherent UI element:
     *   strip    — medium dark, sits between the (darker) terminal content
     *              below and the (slightly lighter) inactive tabs above it.
     *   active   — matches the terminal background, so the active tab
     *              visually "merges" into the content area below.
     *   inactive — a step LIGHTER than the strip so unselected tabs read
     *              as raised pieces against the bar, with clear contrast. */
    const float STRIP_R = 0.18f, STRIP_G = 0.19f, STRIP_B = 0.20f;
    const float ACTIVE_R = 0.04f, ACTIVE_G = 0.04f, ACTIVE_B = 0.05f;
    const float INACTIVE_R = 0.30f, INACTIVE_G = 0.31f, INACTIVE_B = 0.33f;

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

    float x = 0.0f;
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
     * sharp rects forming the plus glyph. All in the same draw call so
     * there's no per-glyph shader/atlas cost. */
    {
        float btn = strip - top_inset - 4.0f;
        if (btn < 16.0f) btn = 16.0f;
        if (btn > 22.0f) btn = 22.0f;
        float bx = bar->width - TABBAR_WINBTN_AREA - TABBAR_NEWTAB_AREA
                   + (TABBAR_NEWTAB_AREA - btn) * 0.5f;
        float by = top_inset + (tab_h - btn) * 0.5f;
        float radius = btn * 0.5f; /* perfect circle */

        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){
                      .x = bx, .y = by, .w = btn, .h = btn,
                      .r = 0.26f, .g = 0.27f, .b = 0.29f, .a = 1.0f,
                      .radius_tl = radius, .radius_tr = radius,
                      .radius_br = radius, .radius_bl = radius,
                  });

        float arm_len = btn * 0.5f;
        float arm_th = TABBAR_PLUS_THICKNESS;
        float cx = bx + btn * 0.5f;
        float cy = by + btn * 0.5f;
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){.x = cx - arm_len * 0.5f,
                                                 .y = cy - arm_th * 0.5f,
                                                 .w = arm_len, .h = arm_th,
                                                 .r = 0.85f, .g = 0.86f, .b = 0.88f, .a = 1.0f});
        push_rect(rects, &n,
                  (struct yetty_yui_tabbar_rect){.x = cx - arm_th * 0.5f,
                                                 .y = cy - arm_len * 0.5f,
                                                 .w = arm_th, .h = arm_len,
                                                 .r = 0.85f, .g = 0.86f, .b = 0.88f, .a = 1.0f});
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

    bar->workspaces[bar->count++] = ws;
    bar->active = bar->count - 1;

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
    if (idx < bar->count && idx != bar->active) {
        bar->active = idx;
        ydebug("tabbar: switched to workspace %zu/%zu", idx + 1, bar->count);
        tabbar_request_render(bar);
    }
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
                if (shift) {
                    bar->active = (bar->active + bar->count - 1) % bar->count;
                } else {
                    bar->active = (bar->active + 1) % bar->count;
                }
                ydebug("tabbar: cycle → %zu/%zu", bar->active + 1, bar->count);
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
    if (event_y(event, &y) && y < strip) {
        if (event->type == YETTY_YCORE_MOUSE_DOWN && bar->count > 0) {
            /* Hit-test mirrors the render-time layout exactly. Right edge,
             * from far right going left:
             *   [window-buttons | new-tab | ...tabs...]
             *
             *   - clicks in the window-button area → swallow (visuals only
             *     today; minimize/maximize/close will be wired through the
             *     output pipe in a follow-up).
             *   - clicks in the new-tab area      → add workspace
             *   - everything left of that         → switch to tab at x */
            float winbtn_left = bar->width - TABBAR_WINBTN_AREA;
            float newtab_left = winbtn_left - TABBAR_NEWTAB_AREA;
            if (event->mouse.x >= winbtn_left) {
                /* Reserved for min/max/close — no-op until the output
                 * pipe lands. */
            } else if (event->mouse.x >= newtab_left && bar->config && bar->yetty_ctx) {
                struct yetty_ycore_void_result r = yetty_yui_tabbar_add_workspace_from_config(
                    bar, bar->config, bar->yetty_ctx);
                if (YETTY_IS_ERR(r)) {
                    yerror("tabbar: new-tab button: %s", r.error.msg);
                    yetty_ycore_error_destroy(r.error);
                } else {
                    ydebug("tabbar: new-tab button → workspace %zu", bar->count);
                }
            } else {
                float tw = tab_width(bar);
                float step = tw + TABBAR_TAB_OVERLAP;
                if (step <= 0) {
                    step = 1.0f;
                }
                size_t idx = (size_t)(event->mouse.x / step);
                if (idx >= bar->count) {
                    idx = bar->count - 1;
                }
                tabbar_switch(bar, idx);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* Anything else — including keys without a shortcut, resize broadcasts,
     * zoom apply — goes to the active workspace. The workspace's tile tree
     * is now anchored at y = strip (see workspace_set_origin call in resize),
     * so screen-space mouse coordinates land in the same frame the tile tree
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
