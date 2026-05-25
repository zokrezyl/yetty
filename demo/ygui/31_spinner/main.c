/*
 * Demo 31: spinner (numeric input with ± buttons).
 *
 * Two spinners: one integer (1..100, step 1) and one float
 * (0.0..10.0, step 0.25, two decimals). Wheel and Up/Down keys also
 * step. Status label shows current value of the focused widget.
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui-old/ygui.h>

static struct yetty_ygui_old_widget *g_outer = NULL;
static struct yetty_ygui_old_widget *g_status = NULL;

static void on_int_change(struct yetty_ygui_old_widget *w, float value, void *u)
{
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Integer: %d", (int)value);
    yetty_ygui_old_widget_label_set_text(g_status, buf);
}

static void on_float_change(struct yetty_ygui_old_widget *w, float value, void *u)
{
    (void)w; (void)u;
    char buf[64];
    snprintf(buf, sizeof(buf), "Float:   %.2f", (double)value);
    yetty_ygui_old_widget_label_set_text(g_status, buf);
}

static void on_key(struct yetty_ygui_old_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_old_engine_stop(e);
}

static void on_resize(struct yetty_ygui_old_engine *e, float nw, float nh, float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    yetty_ygui_old_widget_set_size(g_outer, nw, nh);
}

int main(void)
{
    if (yetty_ygui_old_init() != 0) return 1;
    struct ygui_engine_ptr_result eng_r = yetty_ygui_old_engine_create(
        (struct yetty_ygui_old_engine_args){.name = "spinner-demo"});
    if (YETTY_IS_ERR(eng_r)) { yetty_ycore_error_destroy(eng_r.error); yetty_ygui_old_shutdown(); return 1; }
    struct yetty_ygui_old_engine *engine = eng_r.value;

    g_outer = yetty_ygui_old_engine_vbox(engine, "outer", 0, 0, 100, 100);
    yetty_ygui_old_widget_apply_css(g_outer, "padding: 16; gap: 16; align-items: stretch;");

    float cw = 800, ch = 600;
    struct pixel_size_result sr = yetty_ygui_old_engine_get_size(engine);
    if (YETTY_IS_OK(sr)) {
        if (sr.value.width  > 0) cw = sr.value.width;
        if (sr.value.height > 0) ch = sr.value.height;
    } else {
        yetty_ycore_error_destroy(sr.error);
    }
    yetty_ygui_old_widget_set_size(g_outer, cw, ch);

    struct yetty_ygui_old_widget *title = yetty_ygui_old_engine_label(engine, "title", 0, 0,
                                                              "Spinner — click ± / wheel / Up Down");
    yetty_ygui_old_widget_add_child(g_outer, title);

    struct yetty_ygui_old_widget *spin_int =
        yetty_ygui_old_engine_spinner(engine, "spin_int", 0, 0, 180, 32, 1, 100, 1, 42);
    yetty_ygui_old_widget_spinner_on_change(spin_int, on_int_change, NULL);
    yetty_ygui_old_widget_add_child(g_outer, spin_int);

    struct yetty_ygui_old_widget *spin_float =
        yetty_ygui_old_engine_spinner(engine, "spin_float", 0, 0, 180, 32, 0.0f, 10.0f, 0.25f, 2.5f);
    yetty_ygui_old_widget_spinner_set_precision(spin_float, 2);
    yetty_ygui_old_widget_spinner_on_change(spin_float, on_float_change, NULL);
    yetty_ygui_old_widget_add_child(g_outer, spin_float);

    g_status = yetty_ygui_old_engine_label(engine, "status", 0, 0, "Ready");
    yetty_ygui_old_widget_add_child(g_outer, g_status);

    yetty_ygui_old_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_old_engine_on_key(engine, on_key, NULL);

    yetty_ygui_old_engine_run(engine);
    yetty_ygui_old_engine_destroy(engine);
    yetty_ygui_old_shutdown();
    return 0;
}
