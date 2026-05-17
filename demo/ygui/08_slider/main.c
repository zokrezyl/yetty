/*
 * Demo 08: Slider — single slider with live value display.
 * Ported from yetty-poc/demo/assets/ygui-c/python/03_slider.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_value_label = NULL;

static void on_change(struct yetty_ygui_widget* w, float value, void* u) {
    (void)w; (void)u;
    char buf[32];
    snprintf(buf, sizeof(buf), "Volume: %d%%", (int)value);
    yetty_ygui_widget_label_set_text(g_value_label, buf);
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    if (yetty_ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "slider-demo"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    yetty_ygui_engine_label(g_engine, "title", 50, 30, "Volume Control");
    g_value_label = yetty_ygui_engine_label(g_engine, "value", 50, 70, "Volume: 50%");

    struct yetty_ygui_widget* sl = yetty_ygui_engine_slider(g_engine, "volume", 50, 110, 300, 30, 0, 100, 50);
    yetty_ygui_widget_slider_on_change(sl, on_change, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
