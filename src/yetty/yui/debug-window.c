#include <yetty/yui/debug-window.h>

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
    struct yetty_ygui_object *window;
    /* Three separate labels — one per rolling window. ygui's label is
     * single-line, so multi-line text via "\n" doesn't render; stacking
     * them in the window's body vbox is the right primitive. */
    struct yetty_ygui_object *label_1s;
    struct yetty_ygui_object *label_10s;
    struct yetty_ygui_object *label_60s;
    yetty_ycore_object_id pane_id;
    /* Layout runs every frame, but we want the initial placement
     * (top-right of the pane) to stick exactly once, then leave x/y
     * alone. Set on the first layout that lands a real pane rect. */
    int placed;
};

struct yetty_yui_debug_window_ptr_result yetty_yui_debug_window_create(
    struct yetty_ygui_framework *engine, yetty_ycore_object_id pane_id)
{
    if (!engine) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: NULL engine");
    }
    struct yetty_ygui_object *root = yetty_ygui_framework_root(engine);
    if (!root) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: engine has no root");
    }

    struct yetty_yui_debug_window *dw = calloc(1, sizeof(struct yetty_yui_debug_window));
    if (!dw) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: allocation failed");
    }
    dw->engine = engine;
    dw->pane_id = pane_id;

    struct yetty_ygui_object_ptr_result wr =
        yetty_ygui_add(yetty_ygui_class_expect(yetty_ygui_window_class_get(),
                                               "yetty_ygui_window_class_get"),
                       root);
    if (YETTY_IS_ERR(wr)) {
        free(dw);
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: window widget", wr);
    }
    dw->window = wr.value;
    yetty_ycore_error_destroy_safe(yetty_ygui_window_set_title(dw->window, "Debug"));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_bg_color(dw->window, DEBUG_WIN_BG));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(dw->window, DEBUG_WIN_W, DEBUG_WIN_H));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_floating(dw->window, 1));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->window, 1));

    /* Attach a placeholder popup menu so the title-bar hamburger has
     * something to toggle. Real actions land in later steps. The menu is
     * a child of the same root and floats absolutely when opened. */
    {
        struct yetty_ygui_object_ptr_result mr =
            yetty_ygui_add(yetty_ygui_class_expect(yetty_ygui_popup_menu_class_get(),
                                                   "yetty_ygui_popup_menu_class_get"),
                           root);
        if (YETTY_IS_OK(mr)) {
            yetty_ycore_error_destroy_safe(
                yetty_ygui_popup_menu_add_item(mr.value, "(no actions yet)", NULL, NULL));
            yetty_ycore_error_destroy_safe(yetty_ygui_window_set_menu(dw->window, mr.value));
        } else {
            yetty_ycore_error_destroy(mr.error);
        }
    }

    struct yetty_ygui_object *body = yetty_ygui_window_body(dw->window);
    if (body) {
        struct yetty_ygui_object **slots[3] = {&dw->label_1s, &dw->label_10s, &dw->label_60s};
        for (int i = 0; i < 3; i++) {
            struct yetty_ygui_object_ptr_result lr = yetty_ygui_add(
                yetty_ygui_class_expect(yetty_ygui_label_class_get(), "yetty_ygui_label_class_get"),
                body);
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
    if (dw->window) {
        yetty_ycore_error_destroy_safe(yetty_ygui_del(dw->window));
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

    float w = DEBUG_WIN_W;
    float h = DEBUG_WIN_H;
    if (w > pane_w - 2.0f * DEBUG_WIN_INSET) {
        w = pane_w - 2.0f * DEBUG_WIN_INSET;
    }
    if (h > pane_h - 2.0f * DEBUG_WIN_INSET) {
        h = pane_h - 2.0f * DEBUG_WIN_INSET;
    }
    if (w < 1.0f || h < 1.0f) {
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->window, 0));
        return YETTY_OK_VOID();
    }
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_visible(dw->window, 1));
    yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_size(dw->window, w, h));

    /* Initial placement only — top-right corner of the pane. */
    if (!dw->placed) {
        float x = pane_x + pane_w - w - DEBUG_WIN_INSET;
        float y = pane_y + DEBUG_WIN_INSET;
        yetty_ycore_error_destroy_safe(yetty_ygui_widget_set_position(dw->window, x, y));
        dw->placed = 1;
    }
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
            yetty_ycore_error_destroy_safe(yetty_ygui_label_set_text(dw->label_1s, "(no terminal)"));
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
