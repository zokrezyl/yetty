/*
 * Demo 06: Hello Button
 *
 * Mini dashboard: button counter + slider + progress + checkbox + reset.
 * Ported from yetty-poc/demo/assets/ygui-c/python/01_hello_button.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_click_button = NULL;
static struct yetty_ygui_widget* g_status = NULL;
static struct yetty_ygui_widget* g_slider = NULL;
static struct yetty_ygui_widget* g_progress = NULL;
static struct yetty_ygui_widget* g_checkbox = NULL;
static int g_clicks = 0;

static void on_click(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_clicks++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Clicks: %d", g_clicks);
    yetty_ygui_widget_button_set_label(g_click_button, buf);
    snprintf(buf, sizeof(buf), "Clicked! Total: %d", g_clicks);
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void on_reset(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_clicks = 0;
    yetty_ygui_widget_button_set_label(g_click_button, "Clicks: 0");
    yetty_ygui_widget_slider_set_value(g_slider, 50);
    yetty_ygui_widget_checkbox_set_checked(g_checkbox, 0);
    yetty_ygui_widget_label_set_text(g_status, "Reset!");
}

static void on_quit(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    yetty_ygui_engine_stop(g_engine);
}

static void on_slider_change(struct yetty_ygui_widget* w, float value, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Volume: %d%%", (int)value);
    yetty_ygui_widget_label_set_text(g_status, buf);
    yetty_ygui_widget_progress_set_value(g_progress, value / 100.0f);
}

static void on_checkbox_change(struct yetty_ygui_widget* w, int checked, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Feature %s", checked ? "enabled" : "disabled");
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (yetty_ygui_init() != 0) return 1;

    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create_with_pixel_hint("dashboard", 2, 2, 500.0f, 350.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    yetty_ygui_engine_label(g_engine, "title", 20, 15, "YGui Dashboard");

    g_click_button = yetty_ygui_engine_button(g_engine, "btn_click", 20, 50, 150, 45, "Clicks: 0");
    yetty_ygui_widget_button_on_click(g_click_button, on_click, NULL);

    struct yetty_ygui_widget* btn_reset = yetty_ygui_engine_button(g_engine, "btn_reset", 190, 50, 100, 45, "Reset");
    yetty_ygui_widget_button_on_click(btn_reset, on_reset, NULL);

    struct yetty_ygui_widget* btn_quit = yetty_ygui_engine_button(g_engine, "btn_quit", 310, 50, 100, 45, "Quit");
    yetty_ygui_widget_button_on_click(btn_quit, on_quit, NULL);

    yetty_ygui_engine_label(g_engine, "slider_lbl", 20, 115, "Volume: 50%");
    g_slider = yetty_ygui_engine_slider(g_engine, "slider", 20, 145, 300, 30, 0, 100, 50);
    yetty_ygui_widget_slider_on_change(g_slider, on_slider_change, NULL);

    yetty_ygui_engine_label(g_engine, "prog_lbl", 20, 195, "Progress:");
    g_progress = yetty_ygui_engine_progress(g_engine, "progress", 20, 220, 300, 25, 0.5f);

    g_checkbox = yetty_ygui_engine_checkbox(g_engine, "checkbox", 20, 265, 200, 30, "Enable feature", 0);
    yetty_ygui_widget_checkbox_on_change(g_checkbox, on_checkbox_change, NULL);

    g_status = yetty_ygui_engine_label(g_engine, "status", 20, 315, "Ready - Click widgets or 'q' to quit");

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
