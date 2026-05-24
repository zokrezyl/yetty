#include <yetty/yui/debug-window.h>

#include <yetty/ygui/ygui.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/wire-statemachine.h>

#include <stdio.h>
#include <stdlib.h>

/* Brand palette in ygui pack order (0xAABBGGRR — see demo/ygui rgba()
 * helper). BRAND_BG_LIFTED for the panel background, BRAND_ACCENT for
 * the label text. Keep these in sync with the rules in
 * 08-branding.md if the role assignment changes. */
#define DEBUG_WIN_BG 0xFF1F1A14u     /* #141A1F BRAND_BG_LIFTED */
#define DEBUG_WIN_FG 0xFF92A86Bu     /* #6BA892 BRAND_ACCENT     */
#define DEBUG_WIN_BORDER 0xFF474A36u /* #364A47 BRAND_BORDER     */

/* Anchored at the top-right of the pane with this much inset from the
 * pane edges. Window form (engine_window) reserves room at the top
 * for its built-in title bar; the rest is the body vbox that stacks
 * the three stat lines. */
#define DEBUG_WIN_W 280.0f
#define DEBUG_WIN_H 160.0f
#define DEBUG_WIN_INSET 8.0f
#define DEBUG_WIN_FONT_SIZE 16.0f

struct yetty_yui_debug_window {
    struct yetty_ygui_engine *engine; /* borrowed */
    struct yetty_ygui_widget *window;
    /* Three separate labels — one per rolling window. ygui's label is
     * single-line, so multi-line text via "\n" doesn't render; stacking
     * them in the window's body vbox is the right primitive. */
    struct yetty_ygui_widget *label_1s;
    struct yetty_ygui_widget *label_10s;
    struct yetty_ygui_widget *label_60s;
    yetty_ycore_object_id pane_id;
    /* Layout runs every frame, but the window widget has its own drag
     * handling — we want the initial placement (top-right of the pane)
     * to stick exactly once, then leave x/y alone so the user can drag
     * it around. Set on the first layout that lands a real (non-empty)
     * pane rect. */
    int placed;
};

struct yetty_yui_debug_window_ptr_result yetty_yui_debug_window_create(
    struct yetty_ygui_engine *engine, yetty_ycore_object_id pane_id)
{
    if (!engine) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: NULL engine");
    }

    struct yetty_yui_debug_window *dw = calloc(1, sizeof(struct yetty_yui_debug_window));
    if (!dw) {
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: allocation failed");
    }
    dw->engine = engine;
    dw->pane_id = pane_id;

    char id_buf[64];
    snprintf(id_buf, sizeof(id_buf), "yui_debug_window_%llu", (unsigned long long)pane_id);
    dw->window = yetty_ygui_engine_window(engine, id_buf, 0.0f, 0.0f, DEBUG_WIN_W, DEBUG_WIN_H,
                                          "Debug");
    if (!dw->window) {
        free(dw);
        return YETTY_ERR(yetty_yui_debug_window_ptr, "debug_window_create: window widget");
    }
    yetty_ygui_widget_set_bg_color(dw->window, DEBUG_WIN_BG);
    yetty_ygui_widget_set_visible(dw->window, 1);

    /* Attach a popup menu so the title-bar hamburger toggles the menu
     * instead of calling engine_close_preserve — which would shut the
     * entire ygui engine down for the whole app (see ygui_window.c
     * window_on_press, no-menu fall-through). The menu is intentionally
     * a placeholder; real actions land in later steps. */
    {
        char menu_id[80];
        snprintf(menu_id, sizeof(menu_id), "yui_debug_window_%llu/menu",
                 (unsigned long long)pane_id);
        struct yetty_ygui_widget *menu =
            yetty_ygui_engine_popup_menu(engine, menu_id, 0.0f, 0.0f, 180.0f);
        if (menu) {
            yetty_ygui_widget_popup_menu_add_item(menu, "(no actions yet)", NULL, NULL);
            yetty_ygui_widget_window_set_menu(dw->window, menu);
        }
    }

    struct yetty_ygui_widget *body = yetty_ygui_widget_window_body(dw->window);
    if (body) {
        struct {
            const char *suffix;
            struct yetty_ygui_widget **slot;
        } rows[3] = {
            {"label_1s", &dw->label_1s},
            {"label_10s", &dw->label_10s},
            {"label_60s", &dw->label_60s},
        };
        for (int i = 0; i < 3; i++) {
            char lbl_id[96];
            snprintf(lbl_id, sizeof(lbl_id), "yui_debug_window_%llu/%s",
                     (unsigned long long)pane_id, rows[i].suffix);
            struct yetty_ygui_widget *lbl =
                yetty_ygui_engine_label(engine, lbl_id, 0.0f, 0.0f, "—");
            if (lbl) {
                yetty_ygui_widget_set_fg_color(lbl, DEBUG_WIN_FG);
                yetty_ygui_widget_label_set_font_size(lbl, DEBUG_WIN_FONT_SIZE);
                yetty_ygui_widget_add_child(body, lbl);
            }
            *rows[i].slot = lbl;
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
        yetty_ygui_widget_remove(dw->window);
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
        yetty_ygui_widget_set_visible(dw->window, 0);
        return YETTY_OK_VOID();
    }
    yetty_ygui_widget_set_visible(dw->window, 1);
    yetty_ygui_widget_set_size(dw->window, w, h);

    /* Initial placement only — top-right corner of the pane. After
     * that, leave x/y alone so the window's built-in title-bar drag
     * (see ygui_window.c on_press/on_drag) can move it freely. */
    if (!dw->placed) {
        float x = pane_x + pane_w - w - DEBUG_WIN_INSET;
        float y = pane_y + DEBUG_WIN_INSET;
        yetty_ygui_widget_set_position(dw->window, x, y);
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
            yetty_ygui_widget_label_set_text(dw->label_1s, line);
        }
        if (dw->label_10s) {
            uint64_t avg = snap->count_10s ? snap->bytes_10s / snap->count_10s : 0;
            snprintf(line, sizeof(line), "10s: %llu env  avg %llu B",
                     (unsigned long long)snap->count_10s, (unsigned long long)avg);
            yetty_ygui_widget_label_set_text(dw->label_10s, line);
        }
        if (dw->label_60s) {
            uint64_t avg = snap->count_60s ? snap->bytes_60s / snap->count_60s : 0;
            snprintf(line, sizeof(line), "60s: %llu env  avg %llu B",
                     (unsigned long long)snap->count_60s, (unsigned long long)avg);
            yetty_ygui_widget_label_set_text(dw->label_60s, line);
        }
    } else {
        if (dw->label_1s) {
            yetty_ygui_widget_label_set_text(dw->label_1s, "(no terminal)");
        }
        if (dw->label_10s) {
            yetty_ygui_widget_label_set_text(dw->label_10s, "");
        }
        if (dw->label_60s) {
            yetty_ygui_widget_label_set_text(dw->label_60s, "");
        }
    }
    return YETTY_OK_VOID();
}
