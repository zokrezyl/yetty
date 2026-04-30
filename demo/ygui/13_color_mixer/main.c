/*
 * Demo 13: Color Mixer — RGBA sliders with preview panel and presets.
 * Ported from yetty-poc/demo/assets/ygui-c/python/08_color_mixer.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_preview = NULL;
static struct yetty_ygui_widget* g_hex_label = NULL;
static struct yetty_ygui_widget* g_r_value = NULL;
static struct yetty_ygui_widget* g_g_value = NULL;
static struct yetty_ygui_widget* g_b_value = NULL;
static struct yetty_ygui_widget* g_a_value = NULL;
static struct yetty_ygui_widget* g_r_slider = NULL;
static struct yetty_ygui_widget* g_g_slider = NULL;
static struct yetty_ygui_widget* g_b_slider = NULL;
static struct yetty_ygui_widget* g_a_slider = NULL;

static uint32_t to_abgr(int r, int g, int b, int a) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static void update_color(void) {
    int r = (int)yetty_ygui_widget_slider_get_value(g_r_slider);
    int g = (int)yetty_ygui_widget_slider_get_value(g_g_slider);
    int b = (int)yetty_ygui_widget_slider_get_value(g_b_slider);
    int a = (int)yetty_ygui_widget_slider_get_value(g_a_slider);

    yetty_ygui_widget_set_bg_color(g_preview, to_abgr(r, g, b, a));

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", r); yetty_ygui_widget_label_set_text(g_r_value, buf);
    snprintf(buf, sizeof(buf), "%d", g); yetty_ygui_widget_label_set_text(g_g_value, buf);
    snprintf(buf, sizeof(buf), "%d", b); yetty_ygui_widget_label_set_text(g_b_value, buf);
    snprintf(buf, sizeof(buf), "%d", a); yetty_ygui_widget_label_set_text(g_a_value, buf);

    snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    yetty_ygui_widget_label_set_text(g_hex_label, buf);
}

static void on_slider(struct yetty_ygui_widget* w, float value, void* u) {
    (void)w; (void)value; (void)u;
    update_color();
}

static void set_preset(int r, int g, int b) {
    yetty_ygui_widget_slider_set_value(g_r_slider, (float)r);
    yetty_ygui_widget_slider_set_value(g_g_slider, (float)g);
    yetty_ygui_widget_slider_set_value(g_b_slider, (float)b);
    update_color();
}

static void on_red(struct yetty_ygui_widget* w, void* u)   { (void)w; (void)u; set_preset(255, 0,   0);   }
static void on_green(struct yetty_ygui_widget* w, void* u) { (void)w; (void)u; set_preset(0,   255, 0);   }
static void on_blue(struct yetty_ygui_widget* w, void* u)  { (void)w; (void)u; set_preset(0,   0,   255); }
static void on_white(struct yetty_ygui_widget* w, void* u) { (void)w; (void)u; set_preset(255, 255, 255); }

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (yetty_ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create_with_pixel_hint("color-mixer", 2, 2, 480.0f, 380.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    yetty_ygui_engine_label(g_engine, "title", 30, 15, "RGB Color Mixer");

    g_preview = yetty_ygui_engine_panel(g_engine, "preview", 280, 55, 160, 140);
    yetty_ygui_widget_set_bg_color(g_preview, to_abgr(128, 128, 128, 255));

    g_hex_label = yetty_ygui_engine_label(g_engine, "hex_label", 280, 210, "#808080");

    yetty_ygui_engine_label(g_engine, "r_label", 30, 55, "Red");
    g_r_value = yetty_ygui_engine_label(g_engine, "r_value", 200, 55, "128");
    g_r_slider = yetty_ygui_engine_slider(g_engine, "red", 30, 80, 180, 25, 0, 255, 128);

    yetty_ygui_engine_label(g_engine, "g_label", 30, 115, "Green");
    g_g_value = yetty_ygui_engine_label(g_engine, "g_value", 200, 115, "128");
    g_g_slider = yetty_ygui_engine_slider(g_engine, "green", 30, 140, 180, 25, 0, 255, 128);

    yetty_ygui_engine_label(g_engine, "b_label", 30, 175, "Blue");
    g_b_value = yetty_ygui_engine_label(g_engine, "b_value", 200, 175, "128");
    g_b_slider = yetty_ygui_engine_slider(g_engine, "blue", 30, 200, 180, 25, 0, 255, 128);

    yetty_ygui_engine_label(g_engine, "a_label", 30, 235, "Alpha");
    g_a_value = yetty_ygui_engine_label(g_engine, "a_value", 200, 235, "255");
    g_a_slider = yetty_ygui_engine_slider(g_engine, "alpha", 30, 260, 180, 25, 0, 255, 255);

    yetty_ygui_widget_slider_on_change(g_r_slider, on_slider, NULL);
    yetty_ygui_widget_slider_on_change(g_g_slider, on_slider, NULL);
    yetty_ygui_widget_slider_on_change(g_b_slider, on_slider, NULL);
    yetty_ygui_widget_slider_on_change(g_a_slider, on_slider, NULL);

    yetty_ygui_engine_label(g_engine, "presets", 30, 305, "Presets:");
    struct yetty_ygui_widget* br = yetty_ygui_engine_button(g_engine, "preset_red",   100, 300, 60, 30, "Red");
    struct yetty_ygui_widget* bg = yetty_ygui_engine_button(g_engine, "preset_green", 170, 300, 60, 30, "Green");
    struct yetty_ygui_widget* bb = yetty_ygui_engine_button(g_engine, "preset_blue",  240, 300, 60, 30, "Blue");
    struct yetty_ygui_widget* bw = yetty_ygui_engine_button(g_engine, "preset_white", 310, 300, 60, 30, "White");
    yetty_ygui_widget_button_on_click(br, on_red,   NULL);
    yetty_ygui_widget_button_on_click(bg, on_green, NULL);
    yetty_ygui_widget_button_on_click(bb, on_blue,  NULL);
    yetty_ygui_widget_button_on_click(bw, on_white, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
