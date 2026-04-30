/*
 * Demo 11: Progress Bar — start/pause/reset over a progress widget.
 * Ported from yetty-poc/demo/assets/ygui-c/python/06_progress_bar.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_progress = NULL;
static ygui_widget_t* g_percent_label = NULL;
static ygui_widget_t* g_start_btn = NULL;
static int g_running = 0;
static float g_current = 0.0f;

static void on_start(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    g_running = !g_running;
    ygui_button_set_label(g_start_btn, g_running ? "Pause" : "Resume");
}

static void on_reset(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    g_running = 0;
    g_current = 0;
    ygui_progress_set_value(g_progress, 0);
    ygui_label_set_text(g_percent_label, "0%");
    ygui_button_set_label(g_start_btn, "Start");
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("progress-demo", 2, 2, 500.0f, 200.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 40, 20, "Download Progress");
    g_progress = ygui_progress(g_engine, "download", 40, 60, 350, 30, 0.0f);
    g_percent_label = ygui_label(g_engine, "percent", 410, 65, "0%");

    g_start_btn = ygui_button(g_engine, "start", 40, 120, 100, 40, "Start");
    ygui_button_on_click(g_start_btn, on_start, NULL);

    ygui_widget_t* reset = ygui_button(g_engine, "reset", 160, 120, 100, 40, "Reset");
    ygui_button_on_click(reset, on_reset, NULL);

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
