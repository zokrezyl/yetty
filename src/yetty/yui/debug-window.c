#include <yetty/yui/debug-window.h>

#include <yetty/ygui/event.h>
#include <yetty/ygui/ygui.h>
#include <yetty/yfigure/wire.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

#include <stdio.h>
#include <stdlib.h>

/* Swallow a void-result from a best-effort setter: most ygui setters
 * only fail on OOM, and the debug overlay is non-critical chrome. */
static inline void yetty_ycore_error_destroy_safe(struct yetty_ycore_void_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
    }
}

/* Brand palette in ygui pack order (0xAABBGGRR). BRAND_BG_LIFTED for the
 * panel background, BRAND_ACCENT for the label text. */
#define DEBUG_WIN_BG 0xFF1F1A14u /* #141A1F BRAND_BG_LIFTED */
#define DEBUG_WIN_FG 0xFF92A86Bu /* #6BA892 BRAND_ACCENT     */

/* Anchored at the top-right of the pane with this much inset from the
 * pane edges. The window widget reserves room at the top for its built-in
 * title bar; the rest is the body vbox that stacks the three stat lines. */
#define DEBUG_WIN_W 280.0f
#define DEBUG_WIN_H 160.0f
#define DEBUG_WIN_INSET 8.0f
#define DEBUG_WIN_FONT_SIZE 16.0f

/* Resize grips — two thin splitter strips hugging the OUTSIDE of the
 * window's right and bottom edges. They must not overlap the window:
 * the framework's click-to-front raises a pressed floating window past
 * later siblings, so an overlapping grip would lose the hit-test after
 * the first click on the window body. */
#define DEBUG_WIN_GRIP_THICKNESS 6.0f
#define DEBUG_WIN_MIN_W 120.0f
#define DEBUG_WIN_MIN_H 60.0f

/* Unpack a 0xAABBGGRR colour into an rgba struct (label_set_color takes
 * the struct form). */
static struct yetty_ycore_rgba debug_rgba(uint32_t packed)
{
    return (struct yetty_ycore_rgba){
        .r = (uint8_t)(packed & 0xFFu),
        .g = (uint8_t)((packed >> 8) & 0xFFu),
        .b = (uint8_t)((packed >> 16) & 0xFFu),
        .a = (uint8_t)((packed >> 24) & 0xFFu),
    };
}

struct yetty_yui_debug_window {
    struct yetty_ygui_framework *engine; /* borrowed */
    struct yetty_yclass_object *window;
    /* Edge resize grips (splitter widgets in external-drive mode) —
     * right edge drives width, bottom edge drives height. Repositioned
     * against the window's current rect every frame by layout(). */
    struct yetty_yclass_object *grip_right;
    struct yetty_yclass_object *grip_bottom;
    /* Three separate labels — one per rolling window. ygui's label is
     * single-line, so multi-line text via "\n" doesn't render; stacking
     * them in the window's body vbox is the right primitive. */
    struct yetty_yclass_object *label_1s;
    struct yetty_yclass_object *label_10s;
    struct yetty_yclass_object *label_60s;
    yetty_ycore_object_id pane_id;
    /* Layout runs every frame, but we want the initial placement
     * (top-right of the pane) to stick exactly once, then leave the
     * geometry to the user (title drag + corner resize). Set on the
     * first layout that lands a real pane rect. */
    int placed;
    /* Fired by the title-bar close button; the owner clears the pane's
     * debug_open flag and the next reconcile destroys this window. */
    yetty_yui_debug_window_close_cb on_close;
    void *on_close_userdata;
};

/* Right-edge grip: the splitter reports the cursor's offset from the
 * bar's centre; apply it to the window width and let the per-frame grip
 * sync re-pin the bar to the new edge. */
static void debug_window_on_grip_right(struct yetty_yclass_object *widget, float delta,
                                       void *userdata)
{
    (void)widget;
    struct yetty_yui_debug_window *dw = userdata;
    if (!dw || !dw->window) {
        return;
    }
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(dw->window);
    float new_width = layout->width + delta;
    if (new_width < DEBUG_WIN_MIN_W) {
        new_width = DEBUG_WIN_MIN_W;
    }
    ydebug("debug_window: grip right pane_id=%llu delta=%.1f width %.1f -> %.1f",
           (unsigned long long)dw->pane_id, (double)delta, (double)layout->width,
           (double)new_width);
    yetty_ycore_error_destroy_safe(
        yetty_ygui_widget_set_size(dw->window, new_width, layout->height));
}

/* Bottom-edge grip — same model, applied to the height. */
static void debug_window_on_grip_bottom(struct yetty_yclass_object *widget, float delta,
                                        void *userdata)
{
    (void)widget;
    struct yetty_yui_debug_window *dw = userdata;
    if (!dw || !dw->window) {
        return;
    }
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(dw->window);
    float new_height = layout->height + delta;
    if (new_height < DEBUG_WIN_MIN_H) {
        new_height = DEBUG_WIN_MIN_H;
    }
    ydebug("debug_window: grip bottom pane_id=%llu delta=%.1f height %.1f -> %.1f",
           (unsigned long long)dw->pane_id, (double)delta, (double)layout->height,
           (double)new_height);
    yetty_ycore_error_destroy_safe(
        yetty_ygui_widget_set_size(dw->window, layout->width, new_height));
}

/* Apply visibility/position/size to a grip only on change — this runs
 * every frame and an unconditional setter would mark the engine dirty
 * each time, forcing a re-emit per frame even when idle. */
static void debug_window_pin_grip(struct yetty_yclass_object *grip, int visible, float x, float y,
                                  float w, float h)
{
    if (!grip) {
        return;
    }
    if (yetty_ygui_widget_is_visible(grip) != visible) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(grip, visible));
    }
    if (!visible) {
        return;
    }
    const struct yetty_ygui_layout *grip_layout = yetty_ygui_widget_layout_get(grip);
    int changed = 0;
    if (grip_layout->pos_x != x || grip_layout->pos_y != y) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(grip, x, y));
        changed = 1;
    }
    if (grip_layout->width != w || grip_layout->height != h) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(grip, w, h));
        changed = 1;
    }
    if (changed) {
        struct yetty_ycore_rectangle rect = yetty_ygui_widget_rect(grip);
        ydebug("debug_window: grip pinned pos=(%.1f,%.1f) size=(%.1f,%.1f) "
               "rect=(%.1f,%.1f)-(%.1f,%.1f)",
               (double)x, (double)y, (double)w, (double)h, (double)rect.min.x, (double)rect.min.y,
               (double)rect.max.x, (double)rect.max.y);
    }
}

/* Pin the grips to the window's current rect: the right grip runs down
 * the outside of the right edge, the bottom grip runs along the outside
 * of the bottom edge and covers the corner. Mirrors the window's
 * visibility so hidden windows don't leave stray hit-targets. */
static void debug_window_sync_grips(struct yetty_yui_debug_window *dw)
{
    if (!dw || !dw->window) {
        return;
    }
    const struct yetty_ygui_layout *layout = yetty_ygui_widget_layout_get(dw->window);
    int visible =
        yetty_ygui_widget_is_visible(dw->window) && layout->width > 0.0f && layout->height > 0.0f;
    const float thickness = DEBUG_WIN_GRIP_THICKNESS;
    debug_window_pin_grip(dw->grip_right, visible, layout->pos_x + layout->width, layout->pos_y,
                          thickness, layout->height);
    debug_window_pin_grip(dw->grip_bottom, visible, layout->pos_x, layout->pos_y + layout->height,
                          layout->width + thickness, thickness);
}

/* ygui CLOSE-event trampoline — forwards to the owner's close callback. */
static struct yetty_ycore_void_result debug_window_on_close_event(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *target,
    const struct yetty_ygui_event *event, void *userdata)
{
    (void)ctx;
    (void)target;
    (void)event;
    struct yetty_yui_debug_window *dw = userdata;
    if (!dw || !dw->on_close) {
        return YETTY_OK_VOID();
    }
    return dw->on_close(dw->on_close_userdata, dw->pane_id);
}

struct yetty_yui_debug_window_ptr_result yetty_yui_debug_window_create(
    struct yetty_ygui_framework *engine, yetty_ycore_object_id pane_id,
    yetty_yui_debug_window_close_cb on_close, void *on_close_userdata)
{
    if (!engine) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: NULL engine");
    }
    struct yetty_yclass_object *root = yetty_ygui_framework_root(engine);
    if (!root) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: engine has no root");
    }

    struct yetty_yui_debug_window *dw = calloc(1, sizeof(struct yetty_yui_debug_window));
    if (!dw) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: allocation failed");
    }
    dw->engine = engine;
    dw->pane_id = pane_id;
    dw->on_close = on_close;
    dw->on_close_userdata = on_close_userdata;

    struct yetty_yclass_object_ptr_result wr =
        yetty_ygui_widget_add(root, yetty_ygui_window_class_get().value);
    if (YETTY_IS_ERR(wr)) {
        free(dw);
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: window widget", wr);
    }
    dw->window = wr.value;
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_title(dw->window, "Debug"));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_bg_color(dw->window, DEBUG_WIN_BG));
    yetty_ycore_error_destroy_safe(
        yetty_ygui_widget_set_size(dw->window, DEBUG_WIN_W, DEBUG_WIN_H));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_floating(dw->window, 1));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->window, 1));
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_closable(dw->window, 1));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_subscribe(dw->window, YETTY_YGUI_EVENT_CLOSE,
                                                               debug_window_on_close_event, dw));

    /* Resize grips — splitter widgets in external-drive mode, parked
     * outside the window's right and bottom edges. Created hidden; the
     * first layout() places and reveals them with the window. */
    {
        struct yetty_yclass_object_ptr_result grip_r =
            yetty_ygui_widget_add(root, yetty_ygui_splitter_class_get().value);
        if (YETTY_IS_OK(grip_r)) {
            dw->grip_right = grip_r.value;
            yetty_ycore_error_destroy_safe(
                yetty_ygui_splitter_set_axis(dw->grip_right, 1 /* vertical bar */));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_splitter_on_change(dw->grip_right, debug_window_on_grip_right, dw));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->grip_right, 0));
        } else {
            yetty_ycore_error_destroy(grip_r.error);
        }
        struct yetty_yclass_object_ptr_result grip_b =
            yetty_ygui_widget_add(root, yetty_ygui_splitter_class_get().value);
        if (YETTY_IS_OK(grip_b)) {
            dw->grip_bottom = grip_b.value;
            yetty_ycore_error_destroy_safe(
                yetty_ygui_splitter_set_axis(dw->grip_bottom, 0 /* horizontal bar */));
            yetty_ycore_error_destroy_safe(
                yetty_ygui_splitter_on_change(dw->grip_bottom, debug_window_on_grip_bottom, dw));
            yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->grip_bottom, 0));
        } else {
            yetty_ycore_error_destroy(grip_b.error);
        }
    }

    /* Attach a placeholder popup menu so the title-bar hamburger has
     * something to toggle. Real actions land in later steps. The menu is
     * a child of the same root and floats absolutely when opened. */
    {
        struct yetty_yclass_object_ptr_result mr =
            yetty_ygui_widget_add(root, yetty_ygui_popup_menu_class_get().value);
        if (YETTY_IS_OK(mr)) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_popup_menu_add_item(mr.value, "(no actions yet)", NULL, NULL));
            yetty_ycore_error_destroy_safe(yetty_ygui_window_set_menu(dw->window, mr.value));
        } else {
            yetty_ycore_error_destroy(mr.error);
        }
    }

    struct yetty_yclass_object *body = yetty_ygui_window_body(dw->window);
    if (body) {
        struct yetty_yclass_object **slots[3] = {&dw->label_1s, &dw->label_10s, &dw->label_60s};
        for (int i = 0; i < 3; i++) {
            struct yetty_yclass_object_ptr_result lr =
                yetty_ygui_widget_add(body, yetty_ygui_label_class_get().value);
            if (YETTY_IS_OK(lr)) {
                yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(lr.value, "—"));
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_label_set_color(lr.value, debug_rgba(DEBUG_WIN_FG)));
                yetty_ycore_error_destroy_safe(
                    yetty_ygui_label_set_font_size(lr.value, DEBUG_WIN_FONT_SIZE));
                yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(lr.value, 0.0f, 24.0f));
                *slots[i] = lr.value;
            } else {
                yetty_ycore_error_destroy(lr.error);
            }
        }
    }

    ydebug("debug_window: created for pane_id=%llu", (unsigned long long)pane_id);
    return YETTY_OK(yetty_yui_debug_window_ptr, dw);
}

struct yetty_ycore_void_result yetty_yui_debug_window_destroy(struct yetty_yui_debug_window *dw)
{
    if (!dw) {
        return YETTY_OK_VOID();
    }
    if (dw->grip_right) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_destroy(dw->grip_right));
        dw->grip_right = NULL;
    }
    if (dw->grip_bottom) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_destroy(dw->grip_bottom));
        dw->grip_bottom = NULL;
    }
    if (dw->window) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_destroy(dw->window));
        dw->window = NULL;
    }
    dw->label_1s = NULL;
    dw->label_10s = NULL;
    dw->label_60s = NULL;
    free(dw);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_debug_window_layout(struct yetty_yui_debug_window *dw,
                                                             float pane_x, float pane_y,
                                                             float pane_w, float pane_h)
{
    if (!dw) {
        return YETTY_ERR(yetty_ycore_void, "debug_window_layout: NULL dw");
    }
    if (!dw->window) {
        return YETTY_OK_VOID();
    }

    /* Window geometry is applied exactly once — top-right corner of the
     * pane, clamped to the pane rect. From then on the user owns position
     * and size (title-bar drag / edge-grip resize); the per-frame
     * reconcile must not fight them. The grips ARE re-pinned every frame,
     * tracking whatever the window's current rect is. */
    if (dw->placed) {
        debug_window_sync_grips(dw);
        return YETTY_OK_VOID();
    }

    float w = DEBUG_WIN_W;
    float h = DEBUG_WIN_H;
    if (w > pane_w - 2.0f * DEBUG_WIN_INSET) {
        w = pane_w - 2.0f * DEBUG_WIN_INSET;
    }
    if (h > pane_h - 2.0f * DEBUG_WIN_INSET) {
        h = pane_h - 2.0f * DEBUG_WIN_INSET;
    }
    if (w < 1.0f || h < 1.0f) {
        /* Pane has no usable area yet — try again next frame. */
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->window, 0));
        debug_window_sync_grips(dw);
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->window, 1));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(dw->window, w, h));
    float x = pane_x + pane_w - w - DEBUG_WIN_INSET;
    float y = pane_y + DEBUG_WIN_INSET;
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(dw->window, x, y));
    dw->placed = 1;
    debug_window_sync_grips(dw);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_debug_window_set_stats(
    struct yetty_yui_debug_window *dw, const struct yetty_ywire_stats_snapshot *snap)
{
    if (!dw) {
        return YETTY_ERR(yetty_ycore_void, "debug_window_set_stats: NULL dw");
    }

    char line[96];
    if (snap) {
        if (dw->label_1s) {
            uint64_t avg = snap->count_1s ? snap->bytes_1s / snap->count_1s : 0;
            snprintf(line, sizeof(line), "1s:  %llu env  avg %llu B",
                     (unsigned long long)snap->count_1s, (unsigned long long)avg);
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(dw->label_1s, line));
        }
        if (dw->label_10s) {
            uint64_t avg = snap->count_10s ? snap->bytes_10s / snap->count_10s : 0;
            snprintf(line, sizeof(line), "10s: %llu env  avg %llu B",
                     (unsigned long long)snap->count_10s, (unsigned long long)avg);
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(dw->label_10s, line));
        }
        if (dw->label_60s) {
            uint64_t avg = snap->count_60s ? snap->bytes_60s / snap->count_60s : 0;
            snprintf(line, sizeof(line), "60s: %llu env  avg %llu B",
                     (unsigned long long)snap->count_60s, (unsigned long long)avg);
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(dw->label_60s, line));
        }
    } else {
        if (dw->label_1s) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_label_set_text(dw->label_1s, "(no terminal)"));
        }
        if (dw->label_10s) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(dw->label_10s, ""));
        }
        if (dw->label_60s) {
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(dw->label_60s, ""));
        }
    }
    return YETTY_OK_VOID();
}
