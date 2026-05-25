/*
 * Demo 35: tabbar with two tabs; the first tab is an "Elements" page
 * with a collapsing_header section that starts ALREADY OPEN — every
 * child widget should be visible the moment the demo paints its first
 * frame. No clicks required.
 *
 * Why this exists: the ygreeter Elements/Inputs section reportedly
 * renders empty when opened. This demo reproduces the exact widget
 * shape (tab panel → scrollarea → collapsing_header → 10 mixed
 * widgets) but eliminates every variable except "section is open at
 * paint time" — if the section's children are missing here, the bug
 * is on the producer side of the wire. If they're visible here, the
 * ygreeter issue lives in a downstream interaction (input routing,
 * tab switching, deferred prim factory, …).
 *
 * Press 'q' to quit.
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

/* Mirror of build_elements_tab(...)'s Inputs section in tools/ygreeter/main.c
 * — same widget mix, same authored sizes. Wraps the section in a
 * scrollarea exactly like ygreeter does. The collapsing_header is
 * configured OPEN at construction time, so no click is needed to see
 * the children. */
static void build_elements_tab(struct yetty_ygui_engine *engine,
                                struct yetty_ygui_widget *tab_panel)
{
    /* Tab panel is a flex column already (tabbar's add_tab sets that up).
     * Drop the scrollarea straight in to mirror ygreeter's "el_root". */
    struct yetty_ygui_widget *root =
        yetty_ygui_engine_scrollarea(engine, "el_root", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(root,
                                "padding: 12px; gap: 4px; flex: 1 0 0; "
                                "align-self: stretch; align-items: stretch;");
    yetty_ygui_widget_add_child(tab_panel, root);

    /* The collapsing header itself — START OPEN so the first paint
     * already shows the body box + every child widget. */
    struct yetty_ygui_widget *sec = yetty_ygui_engine_collapsing_header(
        engine, "el_inputs", 0, 0, 600, 28, "Inputs");
    yetty_ygui_widget_collapsing_header_set_open(sec, 1);
    yetty_ygui_widget_apply_css(sec, "align-self: stretch;");
    yetty_ygui_widget_add_child(root, sec);

    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_button(engine, "el_btn", 24, 0, 160, 32, "Button"));
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_textinput(engine, "el_input", 24, 0, 320, 28, "type here…"));
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_slider(engine, "el_slider", 24, 0, 320, 28, 0.0f, 1.0f, 0.4f));
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_spinner(engine, "el_spin_i", 24, 0, 160, 30, 1.0f, 100.0f, 1.0f,
                                       42.0f));
    struct yetty_ygui_widget *spin_f = yetty_ygui_engine_spinner(
        engine, "el_spin_f", 24, 0, 160, 30, 0.0f, 10.0f, 0.25f, 2.5f);
    yetty_ygui_widget_spinner_set_precision(spin_f, 2);
    yetty_ygui_widget_add_child(sec, spin_f);
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_checkbox(engine, "el_check", 24, 0, 220, 24, "Enabled", 1));
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_toggle(engine, "el_toggle", 24, 0, 240, 26, "Notifications", 1));
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_progress(engine, "el_prog", 24, 0, 320, 16, 0.65f));
    struct yetty_ygui_widget *prog_indet =
        yetty_ygui_engine_progress(engine, "el_prog_indet", 24, 0, 320, 16, 0.0f);
    yetty_ygui_widget_progress_set_indeterminate(prog_indet, 1);
    yetty_ygui_widget_add_child(sec, prog_indet);
    yetty_ygui_widget_add_child(
        sec, yetty_ygui_engine_textarea(engine, "el_ta", 24, 0, 420, 120,
                                        "Multi-line text area.\nClick to focus, then type.\n"));
}

/* Tab #2 — a single label so the tabbar has a sibling to switch to. */
static void build_other_tab(struct yetty_ygui_engine *engine,
                             struct yetty_ygui_widget *tab_panel)
{
    struct yetty_ygui_widget *label = yetty_ygui_engine_label(
        engine, "other_label", 24, 24,
        "This is the second tab. Click 'Elements' to see the open section.");
    yetty_ygui_widget_apply_css(label, "align-self: flex-start;");
    yetty_ygui_widget_add_child(tab_panel, label);
}

int main(void)
{
    if (yetty_ygui_init() != 0) {
        return 1;
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "demo35-elements"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    struct yetty_ygui_engine *engine = eng_r.value;
    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 16.0f);
    yetty_ygui_engine_set_theme(engine, theme);

    g_outer = yetty_ygui_engine_vbox(engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer, "padding: 0; gap: 0; align-items: stretch;");

    struct yetty_ygui_widget *tabbar =
        yetty_ygui_engine_tabbar(engine, "tabs", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(tabbar, "flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(g_outer, tabbar);

    struct yetty_ygui_widget *elements_tab =
        yetty_ygui_widget_tabbar_add_tab(tabbar, "Elements");
    build_elements_tab(engine, elements_tab);

    struct yetty_ygui_widget *other_tab =
        yetty_ygui_widget_tabbar_add_tab(tabbar, "Other");
    build_other_tab(engine, other_tab);

    /* Start on Elements so the user sees the open section immediately. */
    yetty_ygui_widget_tabbar_set_active(tabbar, 0);

    yetty_ygui_engine_on_resize(engine, on_resize, NULL);
    yetty_ygui_engine_on_key(engine, on_key, NULL);
    {
        struct pixel_size_result sr = yetty_ygui_engine_get_size(engine);
        if (YETTY_IS_OK(sr) && sr.value.width > 0 && sr.value.height > 0) {
            yetty_ygui_widget_set_size(g_outer, sr.value.width, sr.value.height);
        } else if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
    }
    yetty_ygui_engine_run(engine);

    yetty_ygui_engine_destroy(engine);
    yetty_ygui_shutdown();
    return 0;
}
