/*
 * Demo 01: Button Click Test
 *
 * Simple demo to test button click detection.
 * Creates buttons at known positions and prints click coordinates.
 */

#include <stdio.h>
#include <stdlib.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_status_label = NULL;
static int g_click_count = 0;

static void on_button1_click(ygui_widget_t* widget, void* userdata) {
    (void)userdata;
    g_click_count++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Button 1 clicked! Count: %d", g_click_count);
    ygui_label_set_text(g_status_label, buf);
    fprintf(stderr, "[DEMO] Button 1 clicked (id=%s)\n", ygui_widget_id(widget));
}

static void on_button2_click(ygui_widget_t* widget, void* userdata) {
    (void)userdata;
    g_click_count++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Button 2 clicked! Count: %d", g_click_count);
    ygui_label_set_text(g_status_label, buf);
    fprintf(stderr, "[DEMO] Button 2 clicked (id=%s)\n", ygui_widget_id(widget));
}

static void on_button3_click(ygui_widget_t* widget, void* userdata) {
    (void)userdata;
    g_click_count++;
    char buf[64];
    snprintf(buf, sizeof(buf), "Button 3 clicked! Count: %d", g_click_count);
    ygui_label_set_text(g_status_label, buf);
    fprintf(stderr, "[DEMO] Button 3 clicked (id=%s)\n", ygui_widget_id(widget));
}

static void on_quit_click(ygui_widget_t* widget, void* userdata) {
    (void)widget;
    (void)userdata;
    fprintf(stderr, "[DEMO] Quit clicked\n");
    ygui_engine_stop(g_engine);
}

static void on_key(ygui_engine_t* engine, uint32_t key, int mods, void* userdata) {
    (void)mods;
    (void)userdata;
    if (key == 'q' || key == 'Q') {
        ygui_engine_stop(engine);
    }
}

int main(void) {
    /* yetty's -e binds the child's stderr to the PTY. Redirect it to /dev/null
     * so [DEMO] lines don't land as terminal text on top of the widgets. */
    (void)freopen("/dev/null", "w", stderr);
    fprintf(stderr, "[DEMO] Starting button test demo\n");

    if (ygui_init() != 0) {
        fprintf(stderr, "[DEMO] Failed to initialize ygui\n");
        return 1;
    }

    /* Create engine with pixel hint - this enables SCALE_ON mode */
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("btn-test", 1, 1, 500.0f, 300.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) {
        fprintf(stderr, "[DEMO] Failed to create engine\n");
        ygui_shutdown();
        return 1;
    }

    /* Title */
    ygui_label(g_engine, "title", 30, 20, "Button Click Test");

    /* Status label */
    g_status_label = ygui_label(g_engine, "status", 30, 250, "Click any button...");

    /* Button 1 - at x=30 */
    fprintf(stderr, "[DEMO] Creating button1 at x=30, y=70, w=120, h=40\n");
    ygui_widget_t* btn1 = ygui_button(g_engine, "button1", 30, 70, 120, 40, "Button 1");
    ygui_button_on_click(btn1, on_button1_click, NULL);

    /* Button 2 - at x=180 */
    fprintf(stderr, "[DEMO] Creating button2 at x=180, y=70, w=120, h=40\n");
    ygui_widget_t* btn2 = ygui_button(g_engine, "button2", 180, 70, 120, 40, "Button 2");
    ygui_button_on_click(btn2, on_button2_click, NULL);

    /* Button 3 - at x=330 */
    fprintf(stderr, "[DEMO] Creating button3 at x=330, y=70, w=120, h=40\n");
    ygui_widget_t* btn3 = ygui_button(g_engine, "button3", 330, 70, 120, 40, "Button 3");
    ygui_button_on_click(btn3, on_button3_click, NULL);

    /* Info labels showing expected hit areas */
    ygui_label(g_engine, "info1", 30, 130, "Btn1: x=[30,150)");
    ygui_label(g_engine, "info2", 30, 160, "Btn2: x=[180,300)");
    ygui_label(g_engine, "info3", 30, 190, "Btn3: x=[330,450)");

    /* Quit button */
    ygui_widget_t* quit_btn = ygui_button(g_engine, "quit", 380, 250, 80, 30, "Quit");
    ygui_button_on_click(quit_btn, on_quit_click, NULL);

    /* Keyboard handler */
    ygui_engine_on_key(g_engine, on_key, NULL);

    fprintf(stderr, "[DEMO] Showing card and running event loop\n");
    fprintf(stderr, "[DEMO] Press 'q' to quit\n");

    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    fprintf(stderr, "[DEMO] Cleaning up\n");
    ygui_engine_destroy(g_engine);
    ygui_shutdown();

    return 0;
}
