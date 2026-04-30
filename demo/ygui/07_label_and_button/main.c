/*
 * Demo 07: Label and Button — click counter with reset.
 * Ported from yetty-poc/demo/assets/ygui-c/python/02_label_and_button.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_counter_label = NULL;
static int g_clicks = 0;

static void on_increment(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    g_clicks++;
    char buf[32];
    snprintf(buf, sizeof(buf), "Clicks: %d", g_clicks);
    ygui_label_set_text(g_counter_label, buf);
}

static void on_reset(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    g_clicks = 0;
    ygui_label_set_text(g_counter_label, "Clicks: 0");
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("counter", 2, 2, 400.0f, 250.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 50, 30, "Click Counter");
    g_counter_label = ygui_label(g_engine, "counter", 50, 80, "Clicks: 0");

    ygui_widget_t* inc = ygui_button(g_engine, "increment", 50, 130, 120, 40, "Add +1");
    ygui_button_on_click(inc, on_increment, NULL);

    ygui_widget_t* rst = ygui_button(g_engine, "reset", 190, 130, 120, 40, "Reset");
    ygui_button_on_click(rst, on_reset, NULL);

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
