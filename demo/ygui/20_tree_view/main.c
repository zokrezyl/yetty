/*
 * Demo 20: Tree view — composes `list` and `tree_node` to render a small
 * project file tree. Click the chevron to expand/collapse a folder; click
 * a row to select it. The currently selected row gets a tinted background
 * drawn by the parent list.
 *
 * Press 'q' to quit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_status = NULL;
static struct yetty_ygui_widget *g_root_list = NULL;

static void on_select(struct yetty_ygui_widget *row, void *u)
{
    (void)u;
    char buf[256];
    const char *label = NULL;
    if (yetty_ygui_widget_type(row) == YETTY_YGUI_WIDGET_TREE_NODE) {
        label = yetty_ygui_widget_tree_node_get_label(row);
    } else {
        /* Plain label-row used for leaves. */
        label = yetty_ygui_widget_label_get_text(row);
    }
    snprintf(buf, sizeof(buf), "Selected: %s", label ? label : "(?)");
    yetty_ygui_widget_label_set_text(g_status, buf);
}

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

static struct yetty_ygui_widget *make_node(const char *id, const char *label)
{
    struct yetty_ygui_widget *n = yetty_ygui_engine_tree_node(g_engine, id, label);
    return n;
}

static struct yetty_ygui_widget *make_leaf(const char *id, const char *label)
{
    /* A leaf row is just a label inside the list — it gets the selection
     * background for free since the list highlights its `selected` child. */
    return yetty_ygui_engine_label(g_engine, id, 0, 0, label);
}

static void on_resize(struct yetty_ygui_engine *e, float new_w, float new_h, float prev_w,
                      float prev_h, void *u)
{
    (void)e;
    (void)prev_w;
    (void)prev_h;
    (void)u;
    yetty_ygui_widget_set_size(g_root_list, new_w - 16, new_h - 70);
}

int main(void)
{
    (void)freopen("/dev/null", "w", stderr);
    if (yetty_ygui_init() != 0) {
        return 1;
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create("tree-view", 0, 0, cols, rows);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    yetty_ygui_engine_set_canvas_mode(g_engine, YETTY_YGUI_CANVAS_FIT);

    yetty_ygui_engine_label(g_engine, "title", 8, 6,
                            "Tree view — chevron toggles, click to select.  q to quit");
    g_status = yetty_ygui_engine_label(g_engine, "status", 8, 30,
                                        "Click any row to see its label.");

    /* Root list spans most of the canvas. */
    g_root_list = yetty_ygui_engine_list(g_engine, "root", 8, 60, 600, 400);
    yetty_ygui_widget_apply_css(g_root_list, "padding: 6px; gap: 2px;");
    yetty_ygui_widget_list_on_select(g_root_list, on_select, NULL);

    /* Build a small synthetic file tree:
     *   src/
     *     yetty/
     *       ygui/
     *         ygui_engine.c
     *         ygui_widgets.c
     *         ygui_layout.c
     *       ysdf/
     *         primitives.yaml
     *     tools/
     *   docs/
     *     README.md
     *   Makefile
     */
    struct yetty_ygui_widget *src = make_node("n_src", "src/");
    yetty_ygui_widget_add_child(g_root_list, src);

    struct yetty_ygui_widget *ye = make_node("n_yetty", "yetty/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(src), ye);

    struct yetty_ygui_widget *yg = make_node("n_ygui", "ygui/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(ye), yg);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yg),
                                make_leaf("f_engine", "ygui_engine.c"));
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yg),
                                make_leaf("f_widgets", "ygui_widgets.c"));
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yg),
                                make_leaf("f_layout", "ygui_layout.c"));

    struct yetty_ygui_widget *ys = make_node("n_ysdf", "ysdf/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(ye), ys);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(ys),
                                make_leaf("f_yaml", "primitives.yaml"));

    struct yetty_ygui_widget *tools = make_node("n_tools", "tools/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(src), tools);

    struct yetty_ygui_widget *docs = make_node("n_docs", "docs/");
    yetty_ygui_widget_add_child(g_root_list, docs);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(docs),
                                make_leaf("f_readme", "README.md"));

    yetty_ygui_widget_add_child(g_root_list, make_leaf("f_makefile", "Makefile"));

    /* Open the top two levels by default so the demo isn't a wall of
     * collapsed folders. */
    yetty_ygui_widget_tree_node_set_expanded(src, 1);
    yetty_ygui_widget_tree_node_set_expanded(ye, 1);
    yetty_ygui_widget_tree_node_set_expanded(docs, 1);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    {
        float cw = 0, ch = 0;
        yetty_ygui_engine_get_size(g_engine, &cw, &ch);
        if (cw > 0 && ch > 0) {
            yetty_ygui_widget_set_size(g_root_list, cw - 16, ch - 70);
        }
    }
    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
