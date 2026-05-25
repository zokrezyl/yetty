/*
 * Demo 02: Coordinate Debug
 *
 * Helps debug the click boundary bug by showing exact coordinates.
 * Creates a grid of buttons and tracks click positions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui-old/ygui.h>

static struct yetty_ygui_old_engine* g_engine = NULL;
static struct yetty_ygui_old_widget* g_coord_label = NULL;
static struct yetty_ygui_old_widget* g_hit_label = NULL;

/* Button positions for reference */
#define BTN_X 100
#define BTN_Y 100
#define BTN_W 150
#define BTN_H 50

static void on_test_click(struct yetty_ygui_old_widget* widget, void* userdata) {
    (void)userdata;
    fprintf(stderr, "[COORD_DEBUG] *** BUTTON CLICKED: %s ***\n", yetty_ygui_old_widget_id(widget));
    yetty_ygui_old_widget_label_set_text(g_hit_label, "HIT: Button clicked!");
}

static void on_quit_click(struct yetty_ygui_old_widget* widget, void* userdata) {
    (void)widget;
    (void)userdata;
    yetty_ygui_old_engine_stop(g_engine);
}

static void on_key(struct yetty_ygui_old_engine* engine, uint32_t key, int mods, void* userdata) {
    (void)mods;
    (void)userdata;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_old_engine_stop(engine);
    }
}

int main(void) {
    fprintf(stderr, "=== COORDINATE DEBUG DEMO ===\n");
    fprintf(stderr, "This demo helps debug the click boundary bug.\n");
    fprintf(stderr, "Expected button area: x=[%d,%d), y=[%d,%d)\n",
            BTN_X, BTN_X + BTN_W, BTN_Y, BTN_Y + BTN_H);
    fprintf(stderr, "Watch stderr for [GRID] debug output from ygui_grid.c\n");
    fprintf(stderr, "=====================================\n\n");

    if (yetty_ygui_old_init() != 0) {
        fprintf(stderr, "Failed to init ygui\n");
        return 1;
    }

    /* Create with pixel hint to trigger SCALE_ON mode */
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_old_engine_create((struct yetty_ygui_old_engine_args){.name = "coord-dbg"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) {
        fprintf(stderr, "Failed to create engine\n");
        yetty_ygui_old_shutdown();
        return 1;
    }

    /* Title */
    yetty_ygui_old_engine_label(g_engine, "title", 20, 15, "Coordinate Debug");

    /* Info showing expected button position */
    char info[128];
    snprintf(info, sizeof(info), "Button: x=%d y=%d w=%d h=%d", BTN_X, BTN_Y, BTN_W, BTN_H);
    yetty_ygui_old_engine_label(g_engine, "info", 20, 45, info);

    /* The test button */
    fprintf(stderr, "[DEMO] Creating test button at x=%d, y=%d, w=%d, h=%d\n",
            BTN_X, BTN_Y, BTN_W, BTN_H);
    struct yetty_ygui_old_widget* test_btn = yetty_ygui_old_engine_button(g_engine, "test_btn", BTN_X, BTN_Y, BTN_W, BTN_H, "TEST BUTTON");
    yetty_ygui_old_widget_button_on_click(test_btn, on_test_click, NULL);

    /* Labels to show coordinates */
    g_coord_label = yetty_ygui_old_engine_label(g_engine, "coords", 20, 170, "Click near button...");
    g_hit_label = yetty_ygui_old_engine_label(g_engine, "hit", 20, 195, "");

    /* Quit button */
    struct yetty_ygui_old_widget* quit_btn = yetty_ygui_old_engine_button(g_engine, "quit", 300, 200, 80, 35, "Quit");
    yetty_ygui_old_widget_button_on_click(quit_btn, on_quit_click, NULL);

    /* Keyboard */
    yetty_ygui_old_engine_on_key(g_engine, on_key, NULL);

    fprintf(stderr, "\n[DEMO] Starting - click around the button edges\n");
    fprintf(stderr, "[DEMO] Left edge at x=%d, Right edge at x=%d\n", BTN_X, BTN_X + BTN_W);
    fprintf(stderr, "[DEMO] Press 'q' to quit\n\n");

    yetty_ygui_old_engine_run(g_engine);

    yetty_ygui_old_engine_destroy(g_engine);
    yetty_ygui_old_shutdown();

    return 0;
}
