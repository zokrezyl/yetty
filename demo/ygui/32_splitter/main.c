/*
 * Demo 32: splitter.
 *
 * hbox { left | splitter | right }. Drag the splitter to resize the
 * two panels. Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_widget *g_outer = NULL;

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
        (struct yetty_ygui_engine_args){.name = "splitter-demo"});
    if (YETTY_IS_ERR(eng_r)) { yetty_ycore_error_destroy(eng_r.error); yetty_ygui_shutdown(); return 1; }
    struct yetty_ygui_engine *engine = eng_r.value;

    g_outer = yetty_ygui_engine_hbox(engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    float cw = 800, ch = 600;
    yetty_ygui_engine_get_size(engine, &cw, &ch);
    if (cw <= 0) cw = 800;
    if (ch <= 0) ch = 600;
    yetty_ygui_widget_set_size(g_outer, cw, ch);

    /* Two panels with authored widths so the splitter has something to
     * resize. Do NOT apply `flex: 1 0 0` here — that would zero the
     * basis and ignore authored_w. */
    struct yetty_ygui_widget *left = yetty_ygui_engine_panel(engine, "left", 0, 0, 280, ch);
    yetty_ygui_widget_set_bg_color(left, 0xFF1E262C);
    yetty_ygui_widget_apply_css(left, "align-self: stretch;");
    yetty_ygui_widget_add_child(g_outer, left);

    struct yetty_ygui_widget *split = yetty_ygui_engine_splitter(engine, "split", 0, 0, 6, ch);
    yetty_ygui_widget_apply_css(split, "align-self: stretch;");
    yetty_ygui_widget_add_child(g_outer, split);

    struct yetty_ygui_widget *right = yetty_ygui_engine_panel(engine, "right", 0, 0, cw - 286, ch);
    yetty_ygui_widget_set_bg_color(right, 0xFF141A1F);
    yetty_ygui_widget_apply_css(right, "align-self: stretch;");
    yetty_ygui_widget_add_child(g_outer, right);

    /* Decorative labels so the panes are visibly distinguishable. */
    struct yetty_ygui_widget *lbl_l = yetty_ygui_engine_label(engine, "lbl_l", 16, 16,
                                                              "left pane — drag splitter →");
    yetty_ygui_widget_add_child(left, lbl_l);
    struct yetty_ygui_widget *lbl_r = yetty_ygui_engine_label(engine, "lbl_r", 16, 16,
                                                              "right pane");
    yetty_ygui_widget_add_child(right, lbl_r);

    yetty_ygui_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_engine_on_key(engine, on_key, NULL);

    yetty_ygui_engine_run(engine);
    yetty_ygui_engine_destroy(engine);
    yetty_ygui_shutdown();
    return 0;
}
