/*
 * Demo 08: Slider — single slider with live value display.
 * Ported from yetty-poc/demo/assets/ygui-c/python/03_slider.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_value_label = NULL;

static void on_change(ygui_widget_t* w, float value, void* u) {
    (void)w; (void)u;
    char buf[32];
    snprintf(buf, sizeof(buf), "Volume: %d%%", (int)value);
    ygui_label_set_text(g_value_label, buf);
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("slider-demo", 2, 2, 400.0f, 200.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 50, 30, "Volume Control");
    g_value_label = ygui_label(g_engine, "value", 50, 70, "Volume: 50%");

    ygui_widget_t* sl = ygui_slider(g_engine, "volume", 50, 110, 300, 30, 0, 100, 50);
    ygui_slider_on_change(sl, on_change, NULL);

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
