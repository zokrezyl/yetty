/*
 * Demo 00: minimal tabbar + yplot inside the active tab.
 *
 * Test invariant: yplot must paint only INSIDE the active tab's content
 * area (below the tabbar header strip). If anything spills over the
 * header pills, the widget-local → absolute composition of the yplot
 * prim is broken at the widget level (ygui_yplot.c).
 *
 * Trace coverage:
 *   - ygui_yplot.c:yplot_build_buffer  → WIDGET-LOCAL bounds going INTO
 *     the cached prim.
 *   - ygui_yplot.c:yplot_render        → widget layout box.
 *   - ygui_rich.c:emit_buffer_translated → the (dx, dy) translation
 *     applied to the cached prim before it hits the wire.
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yplot.h>
#include <yetty/ytrace/ytrace.h>

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_outer  = NULL;

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') yetty_ygui_engine_stop(e);
}

static void on_resize(struct yetty_ygui_engine *e, float nw, float nh,
                      float pw, float ph, void *u)
{
    (void)e; (void)pw; (void)ph; (void)u;
    ydebug("demo on_resize: canvas=(%.1f x %.1f)", nw, nh);
    yetty_ygui_widget_set_size(g_outer, nw, nh);
}

int main(void)
{
    if (yetty_ygui_init() != 0) return 1;

    struct ygui_engine_ptr_result eng_r = yetty_ygui_engine_create(
        (struct yetty_ygui_engine_args){.name = "simple-tabbar"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;

    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 16.0f);
    yetty_ygui_engine_set_theme(g_engine, theme);

    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    struct yetty_ygui_widget *tabbar =
        yetty_ygui_engine_tabbar(g_engine, "tabs", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(tabbar, "flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(g_outer, tabbar);

    struct yetty_ygui_widget *tab_plot = yetty_ygui_widget_tabbar_add_tab(tabbar, "Plot");

    struct yetty_yplot_render_config cfg = {
        .x_min = -3.14159f, .x_max = 3.14159f,
        .y_min = -1.5f,     .y_max = 1.5f,
    };
    struct yetty_ygui_widget *plot = yetty_ygui_engine_yplot_from_source(
        g_engine, "p_sin", 0, 0, 0, 0, "sin(x)", 0, &cfg);
    yetty_ygui_widget_apply_css(plot, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(tab_plot, plot);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);

    float W = 800, H = 600;
    struct pixel_size_result sr = yetty_ygui_engine_get_size(g_engine);
    if (YETTY_IS_OK(sr)) {
        if (sr.value.width  > 1.0f) W = sr.value.width;
        if (sr.value.height > 1.0f) H = sr.value.height;
    } else {
        yetty_ycore_error_destroy(sr.error);
    }
    yetty_ygui_widget_set_size(g_outer, W, H);

    yetty_ygui_engine_run(g_engine);
    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
