/*
 * Demo 07: Label and Button — click counter with reset.
 * Ported from yetty-poc/demo/assets/ygui-c/python/02_label_and_button.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_counter_label = NULL;
static int g_clicks = 0;

static void on_increment(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_clicks++;
    char buf[32];
    snprintf(buf, sizeof(buf), "Clicks: %d", g_clicks);
    yetty_ygui_widget_label_set_text(g_counter_label, buf);
}

static void on_reset(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_clicks = 0;
    yetty_ygui_widget_label_set_text(g_counter_label, "Clicks: 0");
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (yetty_ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create_with_pixel_hint("counter", 2, 2, 400.0f, 250.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    yetty_ygui_engine_label(g_engine, "title", 50, 30, "Click Counter");
    g_counter_label = yetty_ygui_engine_label(g_engine, "counter", 50, 80, "Clicks: 0");

    struct yetty_ygui_widget* inc = yetty_ygui_engine_button(g_engine, "increment", 50, 130, 120, 40, "Add +1");
    yetty_ygui_widget_button_on_click(inc, on_increment, NULL);

    struct yetty_ygui_widget* rst = yetty_ygui_engine_button(g_engine, "reset", 190, 130, 120, 40, "Reset");
    yetty_ygui_widget_button_on_click(rst, on_reset, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
