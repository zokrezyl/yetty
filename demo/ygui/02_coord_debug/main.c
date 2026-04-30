/*
 * Demo 02: Coordinate Debug
 *
 * Helps debug the click boundary bug by showing exact coordinates.
 * Creates a grid of buttons and tracks click positions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ygui.h"

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_coord_label = NULL;
static ygui_widget_t* g_hit_label = NULL;

/* Button positions for reference */
#define BTN_X 100
#define BTN_Y 100
#define BTN_W 150
#define BTN_H 50

static void on_test_click(ygui_widget_t* widget, void* userdata) {
    (void)userdata;
    fprintf(stderr, "[COORD_DEBUG] *** BUTTON CLICKED: %s ***\n", ygui_widget_id(widget));
    ygui_label_set_text(g_hit_label, "HIT: Button clicked!");
}

static void on_quit_click(ygui_widget_t* widget, void* userdata) {
    (void)widget;
    (void)userdata;
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
    (void)freopen("/dev/null", "w", stderr);
    fprintf(stderr, "=== COORDINATE DEBUG DEMO ===\n");
    fprintf(stderr, "This demo helps debug the click boundary bug.\n");
    fprintf(stderr, "Expected button area: x=[%d,%d), y=[%d,%d)\n",
            BTN_X, BTN_X + BTN_W, BTN_Y, BTN_Y + BTN_H);
    fprintf(stderr, "Watch stderr for [GRID] debug output from ygui_grid.c\n");
    fprintf(stderr, "=====================================\n\n");

    if (ygui_init() != 0) {
        fprintf(stderr, "Failed to init ygui\n");
        return 1;
    }

    /* Create with pixel hint to trigger SCALE_ON mode */
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("coord-dbg", 1, 1, 400.0f, 250.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) {
        fprintf(stderr, "Failed to create engine\n");
        ygui_shutdown();
        return 1;
    }

    /* Title */
    ygui_label(g_engine, "title", 20, 15, "Coordinate Debug");

    /* Info showing expected button position */
    char info[128];
    snprintf(info, sizeof(info), "Button: x=%d y=%d w=%d h=%d", BTN_X, BTN_Y, BTN_W, BTN_H);
    ygui_label(g_engine, "info", 20, 45, info);

    /* The test button */
    fprintf(stderr, "[DEMO] Creating test button at x=%d, y=%d, w=%d, h=%d\n",
            BTN_X, BTN_Y, BTN_W, BTN_H);
    ygui_widget_t* test_btn = ygui_button(g_engine, "test_btn", BTN_X, BTN_Y, BTN_W, BTN_H, "TEST BUTTON");
    ygui_button_on_click(test_btn, on_test_click, NULL);

    /* Labels to show coordinates */
    g_coord_label = ygui_label(g_engine, "coords", 20, 170, "Click near button...");
    g_hit_label = ygui_label(g_engine, "hit", 20, 195, "");

    /* Quit button */
    ygui_widget_t* quit_btn = ygui_button(g_engine, "quit", 300, 200, 80, 35, "Quit");
    ygui_button_on_click(quit_btn, on_quit_click, NULL);

    /* Keyboard */
    ygui_engine_on_key(g_engine, on_key, NULL);

    fprintf(stderr, "\n[DEMO] Starting - click around the button edges\n");
    fprintf(stderr, "[DEMO] Left edge at x=%d, Right edge at x=%d\n", BTN_X, BTN_X + BTN_W);
    fprintf(stderr, "[DEMO] Press 'q' to quit\n\n");

    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();

    return 0;
}
