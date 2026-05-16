/*
 * Demo 23: tabbar + rich (YAML).
 *
 * Three tabs, each populated with a rich widget whose content is parsed
 * from an inline ydraw YAML string:
 *   - "Text"  — heading + body span (mixed font sizes / colors)
 *   - "Plot"  — sin(x) / cos(x) over [-pi, pi]
 *   - "Shapes" — a couple of SDF primitives, exercises the translator
 *
 * Press 'q' to quit; click a header pill to switch tabs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    if (key == 'q' || key == 'Q') {
        yetty_ygui_engine_stop(e);
    }
}

static void query_terminal_cells(int *cols, int *rows)
{
    *cols = 80;
    *rows = 24;
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) *cols = ws.ws_col;
        if (ws.ws_row > 0) *rows = ws.ws_row;
    }
}

static struct yetty_ygui_widget *g_outer = NULL;

static void on_resize(struct yetty_ygui_engine *e, float new_w, float new_h, float pw, float ph,
                      void *u)
{
    (void)e;
    (void)pw;
    (void)ph;
    (void)u;
    yetty_ygui_widget_set_size(g_outer, new_w, new_h);
}

/* Tab YAML payloads — authored in widget-local pixel coordinates. The
 * rich widget translates them to the widget's absolute position at
 * render time. */
static const char *YAML_TEXT =
    "body:\n"
    "  - text:\n"
    "      position: [16, 40]\n"
    "      content: \"Rich text inside a ygui tab\"\n"
    "      font-size: 28\n"
    "      color: \"#ffffff\"\n"
    "  - text:\n"
    "      position: [16, 90]\n"
    "      content: \"A ygui app paints the whole canvas. The RICH widget\"\n"
    "      font-size: 16\n"
    "      color: \"#cccccc\"\n"
    "  - text:\n"
    "      position: [16, 115]\n"
    "      content: \"holds a ydraw-core buffer; the engine translates its\"\n"
    "      font-size: 16\n"
    "      color: \"#cccccc\"\n"
    "  - text:\n"
    "      position: [16, 140]\n"
    "      content: \"primitives to the widget's resolved flex box.\"\n"
    "      font-size: 16\n"
    "      color: \"#cccccc\"\n";

static const char *YAML_PLOT =
    "body:\n"
    "  - text:\n"
    "      position: [16, 32]\n"
    "      content: \"yplot — sin(x) and cos(x)\"\n"
    "      font-size: 20\n"
    "      color: \"#ffffff\"\n"
    "  - yplot:\n"
    "      position: [16, 50]\n"
    "      size: [420, 240]\n"
    "      x_range: [-3.14159, 3.14159]\n"
    "      y_range: [-1.5, 1.5]\n"
    "      show_grid: true\n"
    "      show_axes: true\n"
    "      show_labels: true\n"
    "      functions:\n"
    "        - expr: \"sin(x)\"\n"
    "          color: \"#ff6b6b\"\n"
    "        - expr: \"cos(x)\"\n"
    "          color: \"#4ecdc4\"\n";

static const char *YAML_SHAPES =
    "body:\n"
    "  - text:\n"
    "      position: [16, 32]\n"
    "      content: \"SDF primitives\"\n"
    "      font-size: 20\n"
    "      color: \"#ffffff\"\n"
    "  - circle:\n"
    "      position: [80, 130]\n"
    "      radius: 40\n"
    "      fill: \"#3498db\"\n"
    "      stroke: \"#2980b9\"\n"
    "      stroke-width: 2\n"
    "  - box:\n"
    "      position: [200, 130]\n"
    "      size: [120, 60]\n"
    "      fill: \"#9b59b6\"\n"
    "      stroke: \"#8e44ad\"\n"
    "      stroke-width: 2\n"
    "      round: 8\n"
    "  - segment:\n"
    "      from: [40, 220]\n"
    "      to: [340, 220]\n"
    "      stroke: \"#7f8c8d\"\n"
    "      stroke-width: 3\n";

int main(void)
{
    if (yetty_ygui_init() != 0) {
        return 1;
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create("rich-tabbar", 0, 0, cols, rows);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    struct yetty_ygui_engine *engine = eng_r.value;
    yetty_ygui_engine_set_canvas_mode(engine, YETTY_YGUI_CANVAS_FIT);

    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 16.0f);
    yetty_ygui_engine_set_theme(engine, theme);

    g_outer = yetty_ygui_engine_vbox(engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer,
                                "padding: 0; gap: 0; align-items: stretch;");

    struct yetty_ygui_widget *tabbar =
        yetty_ygui_engine_tabbar(engine, "tabs", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(tabbar, "flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(g_outer, tabbar);

    struct yetty_ygui_widget *text_tab = yetty_ygui_widget_tabbar_add_tab(tabbar, "Text");
    struct yetty_ygui_widget *plot_tab = yetty_ygui_widget_tabbar_add_tab(tabbar, "Plot");
    struct yetty_ygui_widget *shape_tab = yetty_ygui_widget_tabbar_add_tab(tabbar, "Shapes");

    struct yetty_ygui_widget *r_text = yetty_ygui_engine_rich_from_yaml(
        engine, "r_text", 0, 0, 0, 0, YAML_TEXT, strlen(YAML_TEXT));
    yetty_ygui_widget_apply_css(r_text, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(text_tab, r_text);

    struct yetty_ygui_widget *r_plot = yetty_ygui_engine_rich_from_yaml(
        engine, "r_plot", 0, 0, 0, 0, YAML_PLOT, strlen(YAML_PLOT));
    yetty_ygui_widget_apply_css(r_plot, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(plot_tab, r_plot);

    struct yetty_ygui_widget *r_shape = yetty_ygui_engine_rich_from_yaml(
        engine, "r_shape", 0, 0, 0, 0, YAML_SHAPES, strlen(YAML_SHAPES));
    yetty_ygui_widget_apply_css(r_shape, "flex: 1 0 0; align-self: stretch;");
    yetty_ygui_widget_add_child(shape_tab, r_shape);

    yetty_ygui_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_engine_on_key(engine, on_key, NULL);
    yetty_ygui_engine_show(engine);
    {
        float cw = 0, ch = 0;
        yetty_ygui_engine_get_size(engine, &cw, &ch);
        if (cw > 0 && ch > 0) {
            yetty_ygui_widget_set_size(g_outer, cw, ch);
        }
    }
    yetty_ygui_engine_run(engine);

    yetty_ygui_engine_destroy(engine);
    yetty_ygui_shutdown();
    return 0;
}
