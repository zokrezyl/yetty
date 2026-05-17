/*
 * Demo 04: Edge Test
 *
 * Specifically tests click detection at button edges.
 * Use this to diagnose the "hit area shifted left" bug.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;

/* Exact button positions - use round numbers for easy testing */
#define BTN1_X 50
#define BTN1_Y 80
#define BTN1_W 100
#define BTN1_H 40

#define BTN2_X 200
#define BTN2_Y 80
#define BTN2_W 100
#define BTN2_H 40

#define BTN3_X 350
#define BTN3_Y 80
#define BTN3_W 100
#define BTN3_H 40

static void on_btn1(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    fprintf(stderr, "\n*** BTN1 CLICKED ***\n");
    fprintf(stderr, "    Expected area: x=[%d,%d) y=[%d,%d)\n",
            BTN1_X, BTN1_X + BTN1_W, BTN1_Y, BTN1_Y + BTN1_H);
}

static void on_btn2(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    fprintf(stderr, "\n*** BTN2 CLICKED ***\n");
    fprintf(stderr, "    Expected area: x=[%d,%d) y=[%d,%d)\n",
            BTN2_X, BTN2_X + BTN2_W, BTN2_Y, BTN2_Y + BTN2_H);
}

static void on_btn3(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    fprintf(stderr, "\n*** BTN3 CLICKED ***\n");
    fprintf(stderr, "    Expected area: x=[%d,%d) y=[%d,%d)\n",
            BTN3_X, BTN3_X + BTN3_W, BTN3_Y, BTN3_Y + BTN3_H);
}

static void on_quit(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    yetty_ygui_engine_stop(g_engine);
}

static void on_key(struct yetty_ygui_engine* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_engine_stop(e);
    }
}

int main(void) {
    fprintf(stderr, "╔══════════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║                    EDGE TEST DEMO                            ║\n");
    fprintf(stderr, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(stderr, "║ Tests click detection at exact button boundaries.            ║\n");
    fprintf(stderr, "║                                                              ║\n");
    fprintf(stderr, "║ Button positions (in reference coords 500x250):              ║\n");
    fprintf(stderr, "║   BTN1: x=[%3d,%3d) y=[%3d,%3d)                            ║\n",
            BTN1_X, BTN1_X + BTN1_W, BTN1_Y, BTN1_Y + BTN1_H);
    fprintf(stderr, "║   BTN2: x=[%3d,%3d) y=[%3d,%3d)                            ║\n",
            BTN2_X, BTN2_X + BTN2_W, BTN2_Y, BTN2_Y + BTN2_H);
    fprintf(stderr, "║   BTN3: x=[%3d,%3d) y=[%3d,%3d)                            ║\n",
            BTN3_X, BTN3_X + BTN3_W, BTN3_Y, BTN3_Y + BTN3_H);
    fprintf(stderr, "║                                                              ║\n");
    fprintf(stderr, "║ Watch [GRID] output to see actual coordinates!               ║\n");
    fprintf(stderr, "╚══════════════════════════════════════════════════════════════╝\n\n");

    if (yetty_ygui_init() != 0) {
        fprintf(stderr, "Failed to init\n");
        return 1;
    }

    /* Use specific size for predictable scaling */
    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "edge-test"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) {
        fprintf(stderr, "Failed to create engine\n");
        yetty_ygui_shutdown();
        return 1;
    }

    /* Title */
    yetty_ygui_engine_label(g_engine, "title", 20, 15, "Edge Test - Click button boundaries");

    /* Info labels */
    char info1[64], info2[64], info3[64];
    snprintf(info1, sizeof(info1), "BTN1: [%d,%d)", BTN1_X, BTN1_X + BTN1_W);
    snprintf(info2, sizeof(info2), "BTN2: [%d,%d)", BTN2_X, BTN2_X + BTN2_W);
    snprintf(info3, sizeof(info3), "BTN3: [%d,%d)", BTN3_X, BTN3_X + BTN3_W);

    yetty_ygui_engine_label(g_engine, "info1", BTN1_X, 130, info1);
    yetty_ygui_engine_label(g_engine, "info2", BTN2_X, 130, info2);
    yetty_ygui_engine_label(g_engine, "info3", BTN3_X, 130, info3);

    /* Test buttons at exact positions */
    struct yetty_ygui_widget* btn1 = yetty_ygui_engine_button(g_engine, "btn1", BTN1_X, BTN1_Y, BTN1_W, BTN1_H, "BTN1");
    yetty_ygui_widget_button_on_click(btn1, on_btn1, NULL);

    struct yetty_ygui_widget* btn2 = yetty_ygui_engine_button(g_engine, "btn2", BTN2_X, BTN2_Y, BTN2_W, BTN2_H, "BTN2");
    yetty_ygui_widget_button_on_click(btn2, on_btn2, NULL);

    struct yetty_ygui_widget* btn3 = yetty_ygui_engine_button(g_engine, "btn3", BTN3_X, BTN3_Y, BTN3_W, BTN3_H, "BTN3");
    yetty_ygui_widget_button_on_click(btn3, on_btn3, NULL);

    /* Instructions */
    yetty_ygui_engine_label(g_engine, "instr1", 20, 160, "Click at x=49 (should MISS btn1)");
    yetty_ygui_engine_label(g_engine, "instr2", 20, 180, "Click at x=50 (should HIT btn1 left edge)");
    yetty_ygui_engine_label(g_engine, "instr3", 20, 200, "Click at x=149 (should HIT btn1 right edge)");
    yetty_ygui_engine_label(g_engine, "instr4", 20, 220, "Click at x=150 (should MISS btn1)");

    /* Quit */
    struct yetty_ygui_widget* quit = yetty_ygui_engine_button(g_engine, "quit", 400, 200, 80, 35, "Quit");
    yetty_ygui_widget_button_on_click(quit, on_quit, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    fprintf(stderr, "Press 'q' to quit\n\n");

    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();

    return 0;
}
