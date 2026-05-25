/*
 * Demo 30: radio group + radio buttons.
 *
 * Three radios in a group; selection prints to stderr via on_change.
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui-old/ygui.h>

static struct yetty_ygui_old_widget *g_outer = NULL;
static struct yetty_ygui_old_widget *g_status = NULL;

static void on_radio_change(struct yetty_ygui_old_widget *w, float value, void *u)
{
    (void)w; (void)u;
    int idx = (int)value;
    static const char *names[] = {"Apple", "Banana", "Cherry"};
    char buf[64];
    if (idx >= 0 && idx < 3) {
        snprintf(buf, sizeof(buf), "Selected: %s (index %d)", names[idx], idx);
    } else {
        snprintf(buf, sizeof(buf), "Cleared");
    }
    yetty_ygui_old_widget_label_set_text(g_status, buf);
    fprintf(stderr, "radio-demo: %s\n", buf);
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
        (struct yetty_ygui_old_engine_args){.name = "radio-demo"});
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

    yetty_ygui_old_engine_label(engine, "title", 0, 0, "Pick a fruit:");

    struct yetty_ygui_old_widget *group = yetty_ygui_old_engine_radio_group(engine, "fruit", 0, 0, 240, 0);
    yetty_ygui_old_widget_radio_group_add(group, "r_apple",  "Apple");
    yetty_ygui_old_widget_radio_group_add(group, "r_banana", "Banana");
    yetty_ygui_old_widget_radio_group_add(group, "r_cherry", "Cherry");
    yetty_ygui_old_widget_radio_group_set_selected_index(group, 0);
    yetty_ygui_old_widget_radio_group_on_change(group, on_radio_change, NULL);

    g_status = yetty_ygui_old_engine_label(engine, "status", 0, 0, "Selected: Apple (index 0)");

    yetty_ygui_old_widget_add_child(g_outer, yetty_ygui_old_engine_find(engine, "title"));
    yetty_ygui_old_widget_add_child(g_outer, group);
    yetty_ygui_old_widget_add_child(g_outer, g_status);

    yetty_ygui_old_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_old_engine_on_key(engine, on_key, NULL);

    yetty_ygui_old_engine_run(engine);
    yetty_ygui_old_engine_destroy(engine);
    yetty_ygui_old_shutdown();
    return 0;
}
