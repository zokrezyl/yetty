/*
 * Demo 16: New Widgets — showcase for popup, collapsing-header, tooltip,
 * selectable, choicebox, vscrollbar, hscrollbar.
 *
 * These widgets were ported from the C++ yetty-poc/src/yetty/ygui to C in
 * src/yetty/ygui — this demo exercises each one.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_status = NULL;
static struct yetty_ygui_widget* g_popup = NULL;
static struct yetty_ygui_widget* g_tooltip = NULL;

static void on_open_popup(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    yetty_ygui_widget_popup_set_open(g_popup, !yetty_ygui_widget_popup_is_open(g_popup));
    yetty_ygui_widget_label_set_text(g_status,
                        yetty_ygui_widget_popup_is_open(g_popup) ? "Popup opened" : "Popup closed");
}

static void on_show_tooltip(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    int visible = yetty_ygui_widget_is_visible(g_tooltip);
    yetty_ygui_widget_set_visible(g_tooltip, !visible);
    yetty_ygui_widget_label_set_text(g_status,
                        !visible ? "Tooltip shown" : "Tooltip hidden");
}

static void on_quit(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    yetty_ygui_engine_stop(g_engine);
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    if (yetty_ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create_with_pixel_hint("new-widgets", 1, 1, 700.0f, 520.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    yetty_ygui_engine_label(g_engine, "title", 20, 10, "New Widgets Showcase");

    /* ---- Selectable list ---- */
    yetty_ygui_engine_label(g_engine, "sel_lbl", 20, 50, "Selectable items:");
    yetty_ygui_engine_selectable(g_engine, "sel1", 20, 75,  200, 26, "Apple");
    yetty_ygui_engine_selectable(g_engine, "sel2", 20, 105, 200, 26, "Banana");
    yetty_ygui_engine_selectable(g_engine, "sel3", 20, 135, 200, 26, "Cherry");

    /* ---- ChoiceBox (radio group) ---- */
    yetty_ygui_engine_label(g_engine, "choice_lbl", 250, 50, "Choice:");
    static const char* choices[] = { "Small", "Medium", "Large", "Huge" };
    struct yetty_ygui_widget* cb = yetty_ygui_engine_choicebox(g_engine, "size", 250, 75, 160, 24 * 4,
                                       choices, 4);
    yetty_ygui_widget_choicebox_set_selected(cb, 1);

    /* ---- Vertical scrollbar ---- */
    yetty_ygui_engine_label(g_engine, "vsb_lbl", 440, 50, "VScrollbar:");
    struct yetty_ygui_widget* vsb = yetty_ygui_engine_vscrollbar(g_engine, "vsb", 440, 75, 18, 180);
    yetty_ygui_widget_scrollbar_set_value(vsb, 0.25f);

    /* ---- Horizontal scrollbar ---- */
    yetty_ygui_engine_label(g_engine, "hsb_lbl", 480, 50, "HScrollbar:");
    struct yetty_ygui_widget* hsb = yetty_ygui_engine_hscrollbar(g_engine, "hsb", 480, 80, 200, 18);
    yetty_ygui_widget_scrollbar_set_value(hsb, 0.5f);

    /* ---- Tooltip (initially visible, toggled by button) ---- */
    g_tooltip = yetty_ygui_engine_tooltip(g_engine, "tip", 480, 120, 200, 28,
                             "This is a tooltip.");

    /* ---- CollapsingHeader with two child labels ---- */
    struct yetty_ygui_widget* coll = yetty_ygui_engine_collapsing_header(g_engine, "coll",
                                                 20, 200, 320, 28,
                                                 "Advanced options");
    yetty_ygui_widget_collapsing_header_set_open(coll, 1);
    yetty_ygui_widget_add_child(coll, yetty_ygui_engine_label(g_engine, "coll_a", 0, 0, "  Option A"));
    yetty_ygui_widget_add_child(coll, yetty_ygui_engine_label(g_engine, "coll_b", 0, 0, "  Option B"));

    /* ---- Buttons that drive the popup / tooltip ---- */
    struct yetty_ygui_widget* open = yetty_ygui_engine_button(g_engine, "open_popup",  20, 320, 160, 32,
                                      "Toggle popup");
    yetty_ygui_widget_button_on_click(open, on_open_popup, NULL);
    struct yetty_ygui_widget* show = yetty_ygui_engine_button(g_engine, "show_tooltip", 200, 320, 160, 32,
                                      "Toggle tooltip");
    yetty_ygui_widget_button_on_click(show, on_show_tooltip, NULL);

    /* ---- Popup (closed initially, modal, with one child button) ---- */
    g_popup = yetty_ygui_engine_popup(g_engine, "popup", 200, 380, 320, 120, "Hello popup");
    yetty_ygui_widget_popup_set_modal(g_popup, 1);
    struct yetty_ygui_widget* close_btn = yetty_ygui_engine_button(g_engine, "popup_close", 220, 440, 80, 28,
                                           "Close");
    yetty_ygui_widget_add_child(g_popup, close_btn);
    yetty_ygui_widget_button_on_click(close_btn, on_open_popup, NULL);

    /* ---- Status + quit ---- */
    g_status = yetty_ygui_engine_label(g_engine, "status", 20, 480, "Press 'q' to quit");
    struct yetty_ygui_widget* quit = yetty_ygui_engine_button(g_engine, "quit", 600, 475, 70, 30, "Quit");
    yetty_ygui_widget_button_on_click(quit, on_quit, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
