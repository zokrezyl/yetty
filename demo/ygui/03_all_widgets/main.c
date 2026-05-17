/*
 * Demo 03: All Widgets
 *
 * Showcases all available widget types in ygui-c.
 * Properly laid out to avoid overlaps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine* g_engine = NULL;
static struct yetty_ygui_widget* g_status = NULL;
static struct yetty_ygui_widget* g_progress = NULL;
static float g_progress_value = 0.0f;

/* Helper to convert RGB to ABGR uint32 */
static uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (255u << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static void set_status(const char* text) {
    yetty_ygui_widget_label_set_text(g_status, text);
    fprintf(stderr, "[STATUS] %s\n", text);
}

/* Button callbacks */
static void on_btn1(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    set_status("Button 1 clicked!");
}

static void on_btn2(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    set_status("Button 2 clicked!");
}

static void on_btn3(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    set_status("Button 3 clicked!");
}

/* Slider callbacks */
static void on_slider1(struct yetty_ygui_widget* w, float value, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Slider 1: %.1f", value);
    set_status(buf);
}

static void on_slider2(struct yetty_ygui_widget* w, float value, void* u) {
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Slider 2: %.1f", value);
    set_status(buf);
}

/* Checkbox callback */
static void on_checkbox(struct yetty_ygui_widget* w, int checked, void* u) {
    (void)u;
    const char* id = yetty_ygui_widget_id(w);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s: %s", id, checked ? "ON" : "OFF");
    set_status(buf);
}

/* Progress animation */
static void on_animate(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_progress_value += 0.1f;
    if (g_progress_value > 1.0f) g_progress_value = 0.0f;
    yetty_ygui_widget_progress_set_value(g_progress, g_progress_value);
    char buf[64];
    snprintf(buf, sizeof(buf), "Progress: %.0f%%", g_progress_value * 100);
    set_status(buf);
}

static void on_reset_progress(struct yetty_ygui_widget* w, void* u) {
    (void)w; (void)u;
    g_progress_value = 0.0f;
    yetty_ygui_widget_progress_set_value(g_progress, g_progress_value);
    set_status("Progress reset");
}

/* Quit */
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
    fprintf(stderr, "=== ALL WIDGETS DEMO ===\n");
    fprintf(stderr, "Tests: buttons, sliders, checkboxes, progress, dropdown\n\n");

    if (yetty_ygui_init() != 0) {
        fprintf(stderr, "Failed to init\n");
        return 1;
    }

    { struct ygui_engine_ptr_result _eng_r = yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "all-widgets"});
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) {
        fprintf(stderr, "Failed to create engine\n");
        yetty_ygui_shutdown();
        return 1;
    }

    /* Title */
    struct yetty_ygui_widget* title = yetty_ygui_engine_label(g_engine, "title", 20, 10, "All Widgets Demo");
    yetty_ygui_widget_label_set_font_size(title, 16.0f);

    /* ============ ROW 1: Buttons (y=40) ============ */
    yetty_ygui_engine_label(g_engine, "btn_lbl", 20, 45, "Buttons:");

    struct yetty_ygui_widget* btn1 = yetty_ygui_engine_button(g_engine, "btn1", 100, 40, 90, 30, "Button 1");
    yetty_ygui_widget_button_on_click(btn1, on_btn1, NULL);

    struct yetty_ygui_widget* btn2 = yetty_ygui_engine_button(g_engine, "btn2", 200, 40, 90, 30, "Button 2");
    yetty_ygui_widget_button_on_click(btn2, on_btn2, NULL);

    struct yetty_ygui_widget* btn3 = yetty_ygui_engine_button(g_engine, "btn3", 300, 40, 90, 30, "Button 3");
    yetty_ygui_widget_button_on_click(btn3, on_btn3, NULL);

    /* ============ ROW 2: Sliders (y=85) ============ */
    yetty_ygui_engine_label(g_engine, "sld_lbl", 20, 90, "Sliders:");

    struct yetty_ygui_widget* slider1 = yetty_ygui_engine_slider(g_engine, "slider1", 100, 85, 180, 25, 0.0f, 100.0f, 50.0f);
    yetty_ygui_widget_slider_on_change(slider1, on_slider1, NULL);

    struct yetty_ygui_widget* slider2 = yetty_ygui_engine_slider(g_engine, "slider2", 300, 85, 140, 25, -50.0f, 50.0f, 0.0f);
    yetty_ygui_widget_slider_on_change(slider2, on_slider2, NULL);

    /* ============ ROW 3: Checkboxes (y=125) ============ */
    yetty_ygui_engine_label(g_engine, "chk_lbl", 20, 130, "Checks:");

    struct yetty_ygui_widget* chk1 = yetty_ygui_engine_checkbox(g_engine, "chk_a", 100, 125, 100, 25, "Option A", 0);
    yetty_ygui_widget_checkbox_on_change(chk1, on_checkbox, NULL);

    struct yetty_ygui_widget* chk2 = yetty_ygui_engine_checkbox(g_engine, "chk_b", 210, 125, 100, 25, "Option B", 1);
    yetty_ygui_widget_checkbox_on_change(chk2, on_checkbox, NULL);

    struct yetty_ygui_widget* chk3 = yetty_ygui_engine_checkbox(g_engine, "chk_c", 320, 125, 100, 25, "Option C", 0);
    yetty_ygui_widget_checkbox_on_change(chk3, on_checkbox, NULL);

    /* ============ ROW 4: Progress + Controls (y=165) ============ */
    yetty_ygui_engine_label(g_engine, "prg_lbl", 20, 175, "Progress:");

    g_progress = yetty_ygui_engine_progress(g_engine, "progress", 100, 170, 200, 20, 0.0f);

    struct yetty_ygui_widget* btn_animate = yetty_ygui_engine_button(g_engine, "btn_anim", 320, 165, 70, 28, "+10%");
    yetty_ygui_widget_button_on_click(btn_animate, on_animate, NULL);

    struct yetty_ygui_widget* btn_reset = yetty_ygui_engine_button(g_engine, "btn_reset", 400, 165, 60, 28, "Reset");
    yetty_ygui_widget_button_on_click(btn_reset, on_reset_progress, NULL);

    /* ============ ROW 5: Dropdown (y=210) ============ */
    yetty_ygui_engine_label(g_engine, "dd_lbl", 20, 220, "Dropdown:");

    static const char* dd_options[] = {"Select...", "Option 1", "Option 2", "Option 3"};
    struct yetty_ygui_widget* dropdown = yetty_ygui_engine_dropdown(g_engine, "dropdown", 100, 210, 150, 30,
                                             dd_options, 4);
    (void)dropdown;

    /* ============ ROW 6: Panel with nested widgets (y=260) ============ */
    struct yetty_ygui_widget* panel = yetty_ygui_engine_panel(g_engine, "panel", 20, 260, 200, 90);
    yetty_ygui_widget_set_bg_color(panel, rgb(45, 50, 55));

    yetty_ygui_engine_label(g_engine, "panel_title", 30, 268, "Panel Section");

    struct yetty_ygui_widget* panel_slider = yetty_ygui_engine_slider(g_engine, "panel_sld", 30, 295, 160, 20, 0, 100, 30);
    yetty_ygui_widget_slider_on_change(panel_slider, on_slider1, NULL);

    struct yetty_ygui_widget* panel_chk = yetty_ygui_engine_checkbox(g_engine, "panel_chk", 30, 320, 100, 22, "Enable", 1);
    yetty_ygui_widget_checkbox_on_change(panel_chk, on_checkbox, NULL);

    /* ============ ROW 6 RIGHT: More buttons (y=260) ============ */
    yetty_ygui_engine_label(g_engine, "more_lbl", 240, 268, "More buttons:");

    struct yetty_ygui_widget* btn4 = yetty_ygui_engine_button(g_engine, "btn4", 240, 295, 80, 28, "Action A");
    yetty_ygui_widget_button_on_click(btn4, on_btn1, NULL);

    struct yetty_ygui_widget* btn5 = yetty_ygui_engine_button(g_engine, "btn5", 330, 295, 80, 28, "Action B");
    yetty_ygui_widget_button_on_click(btn5, on_btn2, NULL);

    struct yetty_ygui_widget* btn6 = yetty_ygui_engine_button(g_engine, "btn6", 420, 295, 80, 28, "Action C");
    yetty_ygui_widget_button_on_click(btn6, on_btn3, NULL);

    /* ============ Separator ============ */
    yetty_ygui_engine_separator(g_engine, "sep", 20, 355, 510, 2);

    /* ============ Status + Quit ============ */
    g_status = yetty_ygui_engine_label(g_engine, "status", 20, 365, "Interact with widgets above...");

    struct yetty_ygui_widget* quit = yetty_ygui_engine_button(g_engine, "quit", 450, 360, 70, 30, "Quit");
    yetty_ygui_widget_button_on_click(quit, on_quit, NULL);

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    fprintf(stderr, "Press 'q' to quit\n");

    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();

    return 0;
}
