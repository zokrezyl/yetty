/*
 * Demo 33: modal dialog.
 *
 * Click "Save" to open a Save? / Cancel / Save / Discard dialog.
 * The label below shows which button was clicked.
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_status = NULL;

static void on_dialog_button(struct yetty_ygui_widget *dlg, int idx, void *u)
{
    (void)dlg; (void)u;
    static const char *names[] = {"Cancel", "Save", "Discard"};
    char buf[64];
    snprintf(buf, sizeof(buf), "You clicked: %s (index %d)", names[idx], idx);
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static struct yetty_ygui_widget *g_dlg = NULL;

static void on_open_dialog(struct yetty_ygui_widget *btn, void *u)
{
    (void)btn; (void)u;
    yetty_ygui_widget_popup_set_open(g_dlg, 1);
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

static void on_resize(struct yetty_ygui_engine *e, float nw, float nh, float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    yetty_ygui_widget_set_size(g_outer, nw, nh);
}

int main(void)
{
    if (yetty_ygui_init() != 0) return 1;
    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "dialog-demo"});
    if (YETTY_IS_ERR(eng_r)) { yetty_ycore_error_destroy(eng_r.error); yetty_ygui_shutdown(); return 1; }
    struct yetty_ygui_engine *engine = eng_r.value;

    g_outer = yetty_ygui_engine_vbox(engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 16; gap: 16; align-items: stretch;");

    float cw = 800, ch = 600;
    struct pixel_size_result sr = yetty_ygui_engine_get_size(engine);
    if (YETTY_IS_OK(sr)) {
        if (sr.value.width  > 0) cw = sr.value.width;
        if (sr.value.height > 0) ch = sr.value.height;
    } else {
        yetty_ycore_error_destroy(sr.error);
    }
    yetty_ygui_widget_set_size(g_outer, cw, ch);

    struct yetty_ygui_widget *btn = yetty_ygui_engine_button(engine, "open", 0, 0, 140, 36,
                                                              "Save…");
    yetty_ygui_widget_button_on_click(btn, on_open_dialog, NULL);
    yetty_ygui_widget_add_child(g_outer, btn);

    g_status = yetty_ygui_engine_label(engine, "status", 0, 0,
                                       "Click \"Save…\" to open the dialog.");
    yetty_ygui_widget_add_child(g_outer, g_status);

    const char *btns[] = {"Cancel", "Save", "Discard"};
    struct yetty_ygui_dialog_args args = {
        .id = "save_q",
        .title = "Save changes?",
        .message = "Unsaved edits will be lost.",
        .buttons = btns,
        .button_count = 3,
        .on_button = on_dialog_button,
        .userdata = NULL,
        .modal = 1,
    };
    g_dlg = yetty_ygui_engine_dialog(engine, &args);

    yetty_ygui_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_engine_on_key(engine, on_key, NULL);

    yetty_ygui_engine_run(engine);
    yetty_ygui_engine_destroy(engine);
    yetty_ygui_shutdown();
    return 0;
}
