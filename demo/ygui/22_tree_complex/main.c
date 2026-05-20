/*
 * Demo 22: Tree with complex child widgets — a "settings" panel where
 * tree_node leaves contain real controls (sliders, checkboxes, dropdowns,
 * labels) instead of plain rows. Demonstrates the composition story:
 * tree_node children are arbitrary widgets, including hboxes that mix
 * label + control.
 *
 * Press 'q' to quit.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_tree = NULL;
static struct yetty_ygui_widget *g_status = NULL;

static void status(const char *fmt, ...)
{
    va_list ap;
    char buf[256];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void on_volume(struct yetty_ygui_widget *w, float v, void *u)
{
    (void)w; (void)u;
    status("Volume: %d%%", (int)v);
}

static void on_brightness(struct yetty_ygui_widget *w, float v, void *u)
{
    (void)w; (void)u;
    status("Brightness: %d%%", (int)v);
}

static void on_mute(struct yetty_ygui_widget *w, int checked, void *u)
{
    (void)w; (void)u;
    status("Mute: %s", checked ? "on" : "off");
}

static void on_dark(struct yetty_ygui_widget *w, int checked, void *u)
{
    (void)w; (void)u;
    status("Dark theme: %s", checked ? "on" : "off");
}

static void on_reset(struct yetty_ygui_widget *w, void *u)
{
    (void)w; (void)u;
    status("Reset clicked");
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods; (void)u;
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
    (void)e; (void)prev_w; (void)prev_h; (void)u;
    yetty_ygui_widget_set_size(g_outer, new_w, new_h);
}

/* Build a "field row" — an hbox with a label on the left and any control
 * on the right. Used as a leaf inside a tree_node so each child of an
 * expanded section is a fully-functional setting. */
static struct yetty_ygui_widget *
make_field_row(const char *id, const char *label_text, struct yetty_ygui_widget *control)
{
    char hbox_id[128], lbl_id[128];
    snprintf(hbox_id, sizeof(hbox_id), "row_%s", id);
    snprintf(lbl_id, sizeof(lbl_id), "lbl_%s", id);

    struct yetty_ygui_widget *row = yetty_ygui_engine_hbox(g_engine, hbox_id, 0, 0, 0, 32);
    yetty_ygui_widget_apply_css(row,
                                 "padding: 4px 8px; gap: 12px;"
                                 "justify-content: space-between; align-items: center;");
    struct yetty_ygui_widget *lbl = yetty_ygui_engine_label(g_engine, lbl_id, 0, 0, label_text);
    yetty_ygui_widget_add_child(row, lbl);
    yetty_ygui_widget_add_child(row, control);
    return row;
}

int main(void)
{
    if (yetty_ygui_init() != 0) {
        return 1;
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "tree-complex"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer,
                                 "padding: 12px; gap: 12px; align-items: stretch;");

    yetty_ygui_widget_add_child(
        g_outer,
        yetty_ygui_engine_label(g_engine, "title", 0, 0,
                                 "Settings tree — sliders, checkboxes, "
                                 "buttons live inside tree_node children"));

    g_status = yetty_ygui_engine_label(g_engine, "status", 0, 0, "Interact with any control");
    yetty_ygui_widget_add_child(g_outer, g_status);

    /* The settings tree fills the remaining height. */
    g_tree = yetty_ygui_engine_list(g_engine, "tree", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(g_tree,
                                 "padding: 6px; gap: 4px; flex: 1 0 0;");
    yetty_ygui_widget_add_child(g_outer, g_tree);

    /* Audio section */
    struct yetty_ygui_widget *audio = yetty_ygui_engine_tree_node(g_engine, "audio", "Audio");
    yetty_ygui_widget_add_child(g_tree, audio);

    struct yetty_ygui_widget *volume = yetty_ygui_engine_slider(
        g_engine, "vol", 0, 0, 180, 22, 0, 100, 75);
    yetty_ygui_widget_slider_on_change(volume, on_volume, NULL);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(audio),
                                 make_field_row("volume", "Volume", volume));

    struct yetty_ygui_widget *mute = yetty_ygui_engine_checkbox(
        g_engine, "mute", 0, 0, 24, 22, "", 0);
    yetty_ygui_widget_checkbox_on_change(mute, on_mute, NULL);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(audio),
                                 make_field_row("mute", "Mute", mute));

    /* Display section */
    struct yetty_ygui_widget *display = yetty_ygui_engine_tree_node(g_engine, "display", "Display");
    yetty_ygui_widget_add_child(g_tree, display);

    struct yetty_ygui_widget *brightness = yetty_ygui_engine_slider(
        g_engine, "bri", 0, 0, 180, 22, 0, 100, 60);
    yetty_ygui_widget_slider_on_change(brightness, on_brightness, NULL);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(display),
                                 make_field_row("brightness", "Brightness", brightness));

    struct yetty_ygui_widget *dark = yetty_ygui_engine_checkbox(
        g_engine, "dark", 0, 0, 24, 22, "", 1);
    yetty_ygui_widget_checkbox_on_change(dark, on_dark, NULL);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(display),
                                 make_field_row("dark", "Dark theme", dark));

    /* Nested: an "Advanced" subsection inside Display with another control. */
    struct yetty_ygui_widget *advanced = yetty_ygui_engine_tree_node(
        g_engine, "adv", "Advanced");
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(display), advanced);
    struct yetty_ygui_widget *reset = yetty_ygui_engine_button(
        g_engine, "reset", 0, 0, 100, 28, "Reset");
    yetty_ygui_widget_button_on_click(reset, on_reset, NULL);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(advanced),
                                 make_field_row("reset", "Defaults", reset));

    /* About section: leaf labels only. */
    struct yetty_ygui_widget *about = yetty_ygui_engine_tree_node(g_engine, "about", "About");
    yetty_ygui_widget_add_child(g_tree, about);
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(about),
                                 yetty_ygui_engine_label(g_engine, "ver", 0, 0,
                                                          "ygui v0.2.0"));
    yetty_ygui_widget_add_child(yetty_ygui_widget_tree_node_children(about),
                                 yetty_ygui_engine_label(g_engine, "lic", 0, 0,
                                                          "MIT-licensed"));

    /* Open the obvious sections by default so the controls are visible. */
    yetty_ygui_widget_tree_node_set_expanded(audio, 1);
    yetty_ygui_widget_tree_node_set_expanded(display, 1);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    {
        struct pixel_size_result sr = yetty_ygui_engine_get_size(g_engine);
        if (YETTY_IS_OK(sr) && sr.value.width > 0 && sr.value.height > 0) {
            yetty_ygui_widget_set_size(g_outer, sr.value.width, sr.value.height);
        } else if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
    }

    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
