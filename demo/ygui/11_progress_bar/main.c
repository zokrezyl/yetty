/*
 * Demo 11: Progress Bar — start/pause/reset over a progress widget.
 * Ported from yetty-poc/demo/assets/ygui-c/python/06_progress_bar.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_progress = NULL;
static struct yetty_ygui_widget* g_percent_label = NULL;
static struct yetty_ygui_widget* g_start_btn = NULL;
static int g_running = 0;
static float g_current = 0.0f;

static void on_start(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_running = !g_running;
    yetty_ygui_widget_button_set_label(g_start_btn, g_running ? "Pause" : "Resume");
}

static void on_reset(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_running = 0;
    g_current = 0;
    yetty_ygui_widget_progress_set_value(g_progress, 0);
    yetty_ygui_widget_label_set_text(g_percent_label, "0%");
    yetty_ygui_widget_button_set_label(g_start_btn, "Start");
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

int main(void) {
    if (yetty_ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "progress-demo"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_shutdown(); return 1; }

    yetty_ygui_engine_label(g_engine, "title", 40, 20, "Download Progress");
    g_progress = yetty_ygui_engine_progress(g_engine, "download", 40, 60, 350, 30, 0.0f);
    g_percent_label = yetty_ygui_engine_label(g_engine, "percent", 410, 65, "0%");

    g_start_btn = yetty_ygui_engine_button(g_engine, "start", 40, 120, 100, 40, "Start");
    yetty_ygui_widget_button_on_click(g_start_btn, on_start, NULL);

    struct yetty_ygui_widget* reset = yetty_ygui_engine_button(g_engine, "reset", 160, 120, 100, 40, "Reset");
    yetty_ygui_widget_button_on_click(reset, on_reset, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
