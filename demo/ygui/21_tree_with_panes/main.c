/*
 * Demo 21: Split layout — file tree on the left, content panel on the
 * right. Selecting any row in the tree updates the right pane with the
 * chosen item's name and a synthesized "details" block.
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
static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_tree = NULL;
static struct yetty_ygui_widget *g_detail_title = NULL;
static struct yetty_ygui_widget *g_detail_path = NULL;
static struct yetty_ygui_widget *g_detail_kind = NULL;

/* Synthetic item metadata so the details panel has something useful to
 * display. Keyed by the row widget's id, stored on the widget via the
 * userdata mechanism in the on_select callback. */
struct item_info {
    const char *full_path;
    const char *kind; /* "folder" / "file" */
};

static void update_detail(struct yetty_ygui_widget *row)
{
    const char *label = NULL;
    const char *kind = "—";
    if (yetty_ygui_widget_type(row) == YETTY_YGUI_WIDGET_TREE_NODE) {
        label = yetty_ygui_widget_tree_node_get_label(row);
        kind = "folder";
    } else {
        label = yetty_ygui_widget_label_get_text(row);
        kind = "file";
    }
    yetty_ygui_widget_label_set_text(g_detail_title, label ? label : "(none)");
    yetty_ygui_widget_label_set_text(g_detail_kind, kind);
    /* Fake a path by concatenating the id chain — good enough for a
     * demo. In a real app you'd attach a struct via on_select_userdata
     * or stash a pointer on the widget. */
    char buf[256];
    snprintf(buf, sizeof(buf), "id=%s", yetty_ygui_widget_id(row));
    yetty_ygui_widget_label_set_text(g_detail_path, buf);
}

static void on_select(struct yetty_ygui_widget *row, void *u)
{
    (void)u;
    update_detail(row);
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

static void on_resize(struct yetty_ygui_engine *e, float new_w, float new_h, float prev_w,
                      float prev_h, void *u)
{
    (void)e;
    (void)prev_w;
    (void)prev_h;
    (void)u;
    yetty_ygui_widget_set_size(g_outer, new_w, new_h);
}

static struct yetty_ygui_widget *make_node(const char *id, const char *label)
{
    return yetty_ygui_engine_tree_node(g_engine, id, label);
}

static struct yetty_ygui_widget *make_leaf(const char *id, const char *label)
{
    return yetty_ygui_engine_label(g_engine, id, 0, 0, label);
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
        yetty_ygui_engine_create("tree-panes", 0, 0, cols, rows);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    yetty_ygui_engine_set_canvas_mode(g_engine, YETTY_YGUI_CANVAS_FIT);

    /* Outer column fills the canvas. Header on top, two-pane row beneath. */
    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer,
                                 "padding: 12px; gap: 12px; align-items: stretch;");

    yetty_ygui_widget_add_child(
        g_outer, yetty_ygui_engine_label(g_engine, "title", 0, 0,
                                          "Tree + details — select a row, press q to quit"));

    /* Body row */
    struct yetty_ygui_widget *body = yetty_ygui_engine_hbox(g_engine, "body", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(body,
                                 "padding: 0; gap: 12px; flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(g_outer, body);

    /* Left pane: the tree, fixed-basis width. */
    g_tree = yetty_ygui_engine_list(g_engine, "tree", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(g_tree,
                                 "padding: 6px; gap: 2px; flex: 0 0 260px;");
    yetty_ygui_widget_list_on_select(g_tree, on_select, NULL);
    yetty_ygui_widget_add_child(body, g_tree);

    /* Right pane: details panel, grows to fill remaining width. */
    struct yetty_ygui_widget *detail = yetty_ygui_engine_vbox(g_engine, "detail", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(detail,
                                 "padding: 16px; gap: 8px; flex: 1 0 0; align-items: start;");
    yetty_ygui_widget_add_child(body, detail);

    yetty_ygui_widget_add_child(detail,
                                yetty_ygui_engine_label(g_engine, "h1", 0, 0, "Details"));
    g_detail_title = yetty_ygui_engine_label(g_engine, "d_title", 0, 0, "(no selection)");
    yetty_ygui_widget_add_child(detail, g_detail_title);
    g_detail_kind = yetty_ygui_engine_label(g_engine, "d_kind", 0, 0, "—");
    yetty_ygui_widget_add_child(detail, g_detail_kind);
    g_detail_path = yetty_ygui_engine_label(g_engine, "d_path", 0, 0, "id=—");
    yetty_ygui_widget_add_child(detail, g_detail_path);

    /* Build the tree. */
    struct yetty_ygui_widget *src = make_node("n_src", "src/");
    yetty_ygui_widget_add_child(g_tree, src);

    struct yetty_ygui_widget *yetty_ = make_node("n_yetty", "yetty/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(src), yetty_);

    struct yetty_ygui_widget *yg = make_node("n_ygui", "ygui/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yetty_), yg);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yg),
                                make_leaf("f_engine", "ygui_engine.c"));
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yg),
                                make_leaf("f_widgets", "ygui_widgets.c"));
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yg),
                                make_leaf("f_layout", "ygui_layout.c"));

    struct yetty_ygui_widget *ys = make_node("n_ysdf", "ysdf/");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(yetty_), ys);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(ys),
                                make_leaf("f_yaml", "primitives.yaml"));

    yetty_ygui_widget_add_child(g_tree, make_leaf("f_makefile", "Makefile"));
    yetty_ygui_widget_add_child(g_tree, make_leaf("f_readme", "README.md"));

    yetty_ygui_widget_tree_node_set_expanded(src, 1);
    yetty_ygui_widget_tree_node_set_expanded(yetty_, 1);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    {
        float cw = 0, ch = 0;
        yetty_ygui_engine_get_size(g_engine, &cw, &ch);
        if (cw > 0 && ch > 0) {
            yetty_ygui_widget_set_size(g_outer, cw, ch);
        }
    }

    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
