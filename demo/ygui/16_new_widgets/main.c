/*
 * Demo 16: New Widgets — showcase for popup, collapsing-header, tooltip,
 * selectable, choicebox, vscrollbar, hscrollbar.
 *
 * These widgets were ported from the C++ yetty-poc/src/yetty/ygui to C in
 * src/yetty/ygui — this demo exercises each one.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_status = NULL;
static ygui_widget_t* g_popup = NULL;
static ygui_widget_t* g_tooltip = NULL;

static void on_open_popup(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    ygui_popup_set_open(g_popup, !ygui_popup_is_open(g_popup));
    ygui_label_set_text(g_status,
                        ygui_popup_is_open(g_popup) ? "Popup opened" : "Popup closed");
}

static void on_show_tooltip(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    int visible = ygui_widget_is_visible(g_tooltip);
    ygui_widget_set_visible(g_tooltip, !visible);
    ygui_label_set_text(g_status,
                        !visible ? "Tooltip shown" : "Tooltip hidden");
}

static void on_quit(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    ygui_engine_stop(g_engine);
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("new-widgets", 1, 1, 700.0f, 520.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 20, 10, "New Widgets Showcase");

    /* ---- Selectable list ---- */
    ygui_label(g_engine, "sel_lbl", 20, 50, "Selectable items:");
    ygui_selectable(g_engine, "sel1", 20, 75,  200, 26, "Apple");
    ygui_selectable(g_engine, "sel2", 20, 105, 200, 26, "Banana");
    ygui_selectable(g_engine, "sel3", 20, 135, 200, 26, "Cherry");

    /* ---- ChoiceBox (radio group) ---- */
    ygui_label(g_engine, "choice_lbl", 250, 50, "Choice:");
    static const char* choices[] = { "Small", "Medium", "Large", "Huge" };
    ygui_widget_t* cb = ygui_choicebox(g_engine, "size", 250, 75, 160, 24 * 4,
                                       choices, 4);
    ygui_choicebox_set_selected(cb, 1);

    /* ---- Vertical scrollbar ---- */
    ygui_label(g_engine, "vsb_lbl", 440, 50, "VScrollbar:");
    ygui_widget_t* vsb = ygui_vscrollbar(g_engine, "vsb", 440, 75, 18, 180);
    ygui_scrollbar_set_value(vsb, 0.25f);

    /* ---- Horizontal scrollbar ---- */
    ygui_label(g_engine, "hsb_lbl", 480, 50, "HScrollbar:");
    ygui_widget_t* hsb = ygui_hscrollbar(g_engine, "hsb", 480, 80, 200, 18);
    ygui_scrollbar_set_value(hsb, 0.5f);

    /* ---- Tooltip (initially visible, toggled by button) ---- */
    g_tooltip = ygui_tooltip(g_engine, "tip", 480, 120, 200, 28,
                             "This is a tooltip.");

    /* ---- CollapsingHeader with two child labels ---- */
    ygui_widget_t* coll = ygui_collapsing_header(g_engine, "coll",
                                                 20, 200, 320, 28,
                                                 "Advanced options");
    ygui_collapsing_header_set_open(coll, 1);
    ygui_widget_add_child(coll, ygui_label(g_engine, "coll_a", 0, 0, "  Option A"));
    ygui_widget_add_child(coll, ygui_label(g_engine, "coll_b", 0, 0, "  Option B"));

    /* ---- Buttons that drive the popup / tooltip ---- */
    ygui_widget_t* open = ygui_button(g_engine, "open_popup",  20, 320, 160, 32,
                                      "Toggle popup");
    ygui_button_on_click(open, on_open_popup, NULL);
    ygui_widget_t* show = ygui_button(g_engine, "show_tooltip", 200, 320, 160, 32,
                                      "Toggle tooltip");
    ygui_button_on_click(show, on_show_tooltip, NULL);

    /* ---- Popup (closed initially, modal, with one child button) ---- */
    g_popup = ygui_popup(g_engine, "popup", 200, 380, 320, 120, "Hello popup");
    ygui_popup_set_modal(g_popup, 1);
    ygui_widget_t* close_btn = ygui_button(g_engine, "popup_close", 220, 440, 80, 28,
                                           "Close");
    ygui_widget_add_child(g_popup, close_btn);
    ygui_button_on_click(close_btn, on_open_popup, NULL);

    /* ---- Status + quit ---- */
    g_status = ygui_label(g_engine, "status", 20, 480, "Press 'q' to quit");
    ygui_widget_t* quit = ygui_button(g_engine, "quit", 600, 475, 70, 30, "Quit");
    ygui_button_on_click(quit, on_quit, NULL);

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
