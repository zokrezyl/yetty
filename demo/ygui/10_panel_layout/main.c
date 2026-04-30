/*
 * Demo 10: Panel Layout — settings panel with sliders, checkbox, buttons.
 * Ported from yetty-poc/demo/assets/ygui-c/python/05_panel_layout.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_status = NULL;
static ygui_widget_t* g_brightness = NULL;
static ygui_widget_t* g_contrast = NULL;

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static void on_brightness(ygui_widget_t* w, float value, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Brightness: %d", (int)value);
    ygui_label_set_text(g_status, buf);
}

static void on_contrast(ygui_widget_t* w, float value, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Contrast: %d", (int)value);
    ygui_label_set_text(g_status, buf);
}

static void on_apply(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    char buf[80];
    snprintf(buf, sizeof(buf), "Applied: B=%d, C=%d",
             (int)ygui_slider_get_value(g_brightness),
             (int)ygui_slider_get_value(g_contrast));
    ygui_label_set_text(g_status, buf);
}

static void on_cancel(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    ygui_label_set_text(g_status, "Cancelled");
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("settings", 2, 2, 420.0f, 360.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_widget_t* panel = ygui_panel(g_engine, "settings_panel", 20, 20, 380, 320);
    ygui_widget_set_bg_color(panel, rgba(45, 45, 45, 255));

    ygui_label(g_engine, "panel_title", 40, 40, "Settings Panel");

    ygui_label(g_engine, "brightness_label", 40, 80, "Brightness");
    g_brightness = ygui_slider(g_engine, "brightness", 40, 110, 200, 25, 0, 100, 75);
    ygui_slider_on_change(g_brightness, on_brightness, NULL);

    ygui_label(g_engine, "contrast_label", 40, 150, "Contrast");
    g_contrast = ygui_slider(g_engine, "contrast", 40, 180, 200, 25, 0, 100, 50);
    ygui_slider_on_change(g_contrast, on_contrast, NULL);

    ygui_checkbox(g_engine, "auto_save", 40, 220, 180, 30, "Auto-save", 1);

    ygui_widget_t* apply = ygui_button(g_engine, "apply", 40, 260, 90, 35, "Apply");
    ygui_button_on_click(apply, on_apply, NULL);

    ygui_widget_t* cancel = ygui_button(g_engine, "cancel", 150, 260, 90, 35, "Cancel");
    ygui_button_on_click(cancel, on_cancel, NULL);

    g_status = ygui_label(g_engine, "status", 40, 290, "Ready");

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
