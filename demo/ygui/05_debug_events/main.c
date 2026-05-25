/*
 * Demo 05: Debug Events
 *
 * Visual event monitor — prints click events into labels inside the card.
 * Ported from yetty-poc/demo/assets/ygui-c/python/00_debug_events.py.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui-old/ygui.h>

static struct yetty_ygui_old_engine* g_engine = NULL;
static struct yetty_ygui_old_widget* g_status1 = NULL;
static struct yetty_ygui_old_widget* g_status2 = NULL;
static struct yetty_ygui_old_widget* g_counter = NULL;
static int g_click_count = 0;

static void on_test_click(struct yetty_ygui_old_widget* w, void* u) {
    (void)w; (void)u;
    g_click_count++;
    char buf[64];
    yetty_ygui_old_widget_label_set_text(g_status1, "Last event: Button CLICK");
    yetty_ygui_old_widget_label_set_text(g_status2, "Widget: test_btn");
    snprintf(buf, sizeof(buf), "Click count: %d", g_click_count);
    yetty_ygui_old_widget_label_set_text(g_counter, buf);
}

static void on_quit(struct yetty_ygui_old_widget* w, void* u) {
    (void)w; (void)u;
    yetty_ygui_old_engine_stop(g_engine);
}

static void on_key(struct yetty_ygui_old_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_old_engine_stop(e);
}

int main(void) {
    if (yetty_ygui_old_init() != 0) return 1;

    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_old_engine_create((struct yetty_ygui_old_engine_args){.name = "debug-card"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { yetty_ygui_old_shutdown(); return 1; }

    yetty_ygui_old_engine_label(g_engine, "title",   20, 15, "Event Monitor");
    g_status1 = yetty_ygui_old_engine_label(g_engine, "status1", 20, 120, "Last event: None");
    g_status2 = yetty_ygui_old_engine_label(g_engine, "status2", 20, 150, "Widget: --");
    g_counter = yetty_ygui_old_engine_label(g_engine, "counter", 20, 200, "Click count: 0");
    yetty_ygui_old_engine_label(g_engine, "hint",    20, 260, "Press 'q' to quit");

    struct yetty_ygui_old_widget* btn = yetty_ygui_old_engine_button(g_engine, "test_btn", 20, 50, 200, 50, "Click Me!");
    yetty_ygui_old_widget_button_on_click(btn, on_test_click, NULL);

    struct yetty_ygui_old_widget* quit = yetty_ygui_old_engine_button(g_engine, "quit_btn", 240, 50, 100, 50, "Quit");
    yetty_ygui_old_widget_button_on_click(quit, on_quit, NULL);

    yetty_ygui_old_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_old_engine_run(g_engine);

    yetty_ygui_old_engine_destroy(g_engine);
    yetty_ygui_old_shutdown();
    return 0;
}
