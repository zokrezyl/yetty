/*
 * Demo 06: Hello Button
 *
 * Mini dashboard: button counter + slider + progress + checkbox + reset.
 * Ported from yetty-poc/demo/assets/ygui-c/python/01_hello_button.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_click_button = NULL;
static ygui_widget_t* g_status = NULL;
static ygui_widget_t* g_slider = NULL;
static ygui_widget_t* g_progress = NULL;
static ygui_widget_t* g_checkbox = NULL;
static int g_clicks = 0;

static void on_click(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    g_clicks++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Clicks: %d", g_clicks);
    ygui_button_set_label(g_click_button, buf);
    snprintf(buf, sizeof(buf), "Clicked! Total: %d", g_clicks);
    ygui_label_set_text(g_status, buf);
}

static void on_reset(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    g_clicks = 0;
    ygui_button_set_label(g_click_button, "Clicks: 0");
    ygui_slider_set_value(g_slider, 50);
    ygui_checkbox_set_checked(g_checkbox, 0);
    ygui_label_set_text(g_status, "Reset!");
}

static void on_quit(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    ygui_engine_stop(g_engine);
}

static void on_slider_change(ygui_widget_t* w, float value, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Volume: %d%%", (int)value);
    ygui_label_set_text(g_status, buf);
    ygui_progress_set_value(g_progress, value / 100.0f);
}

static void on_checkbox_change(ygui_widget_t* w, int checked, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Feature %s", checked ? "enabled" : "disabled");
    ygui_label_set_text(g_status, buf);
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;

    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("dashboard", 2, 2, 500.0f, 350.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 20, 15, "YGui Dashboard");

    g_click_button = ygui_button(g_engine, "btn_click", 20, 50, 150, 45, "Clicks: 0");
    ygui_button_on_click(g_click_button, on_click, NULL);

    ygui_widget_t* btn_reset = ygui_button(g_engine, "btn_reset", 190, 50, 100, 45, "Reset");
    ygui_button_on_click(btn_reset, on_reset, NULL);

    ygui_widget_t* btn_quit = ygui_button(g_engine, "btn_quit", 310, 50, 100, 45, "Quit");
    ygui_button_on_click(btn_quit, on_quit, NULL);

    ygui_label(g_engine, "slider_lbl", 20, 115, "Volume: 50%");
    g_slider = ygui_slider(g_engine, "slider", 20, 145, 300, 30, 0, 100, 50);
    ygui_slider_on_change(g_slider, on_slider_change, NULL);

    ygui_label(g_engine, "prog_lbl", 20, 195, "Progress:");
    g_progress = ygui_progress(g_engine, "progress", 20, 220, 300, 25, 0.5f);

    g_checkbox = ygui_checkbox(g_engine, "checkbox", 20, 265, 200, 30, "Enable feature", 0);
    ygui_checkbox_on_change(g_checkbox, on_checkbox_change, NULL);

    g_status = ygui_label(g_engine, "status", 20, 315, "Ready - Click widgets or 'q' to quit");

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
