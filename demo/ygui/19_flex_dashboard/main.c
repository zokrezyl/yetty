/*
 * Demo 19: Flex dashboard — full-terminal nested layout, interactive.
 *
 * Layout fills the entire terminal:
 *
 *   ┌── outer column ────────────────────────────────────────────────┐
 *   │  ┌─ toolbar (justify SPACE_BETWEEN) ──────────────────────────┐│
 *   │  │ [New] [Open] [Save]                              [Profile] ││
 *   │  └────────────────────────────────────────────────────────────┘│
 *   │  ┌─ body row (fills remaining height, gap=10) ────────────────┐│
 *   │  │  ┌─ sidebar (fixed flex_basis) ┐  ┌─ content (flex_grow) ┐ ││
 *   │  │  │ Dashboard / Inbox / Settings│  │ headline / progress  │ ││
 *   │  │  └─────────────────────────────┘  └──────────────────────┘ ││
 *   │  └────────────────────────────────────────────────────────────┘│
 *   │  ┌─ status row (justify SPACE_BETWEEN) ───────────────────────┐│
 *   │  │  Last action: ...                                    [Quit]││
 *   │  └────────────────────────────────────────────────────────────┘│
 *   └────────────────────────────────────────────────────────────────┘
 *
 * Keys:
 *   →  /  ←     grow / shrink the sidebar (flex_basis ± 20px)
 *   t           toggle toolbar between SPACE_BETWEEN and CENTER
 *   q           quit
 *
 * The sidebar key drives flex_basis on the sidebar widget; the body row
 * recomputes; the content panel's flex_grow=1 absorbs the change.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_toolbar = NULL;
static struct yetty_ygui_widget *g_sidebar = NULL;
static struct yetty_ygui_widget *g_status = NULL;
static float g_sidebar_basis = 180.0f;
static int g_toolbar_centered = 0;

static void on_action(struct yetty_ygui_widget *w, void *u)
{
    (void)u;
    char buf[128];
    snprintf(buf, sizeof(buf), "Last action: %s", yetty_ygui_widget_button_get_label(w));
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void on_quit(struct yetty_ygui_widget *w, void *u)
{
    (void)w;
    (void)u;
    yetty_ygui_engine_stop(g_engine);
}

static struct yetty_ygui_widget *make_action_button(const char *id, const char *label)
{
    struct yetty_ygui_widget *b = yetty_ygui_engine_button(g_engine, id, 0, 0, 96, 36, label);
    yetty_ygui_widget_button_on_click(b, on_action, NULL);
    return b;
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

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    /* libuv key codes for arrows on this engine: 0xE000+(L,R,U,D) is one
     * common convention; try a few likely mappings. */
    switch (key) {
    case 'q':
    case 'Q':
        yetty_ygui_engine_stop(e);
        return;
    case 'l':
    case 'L':
        g_sidebar_basis += 20.0f;
        if (g_sidebar_basis > 600.0f) {
            g_sidebar_basis = 600.0f;
        }
        yetty_ygui_widget_set_flex(g_sidebar, 0.0f, 0.0f, g_sidebar_basis);
        break;
    case 'h':
    case 'H':
        g_sidebar_basis -= 20.0f;
        if (g_sidebar_basis < 80.0f) {
            g_sidebar_basis = 80.0f;
        }
        yetty_ygui_widget_set_flex(g_sidebar, 0.0f, 0.0f, g_sidebar_basis);
        break;
    case 't':
    case 'T':
        g_toolbar_centered = !g_toolbar_centered;
        yetty_ygui_widget_set_justify_content(
            g_toolbar, g_toolbar_centered ? YETTY_YGUI_JUSTIFY_CENTER
                                          : YETTY_YGUI_JUSTIFY_SPACE_BETWEEN);
        break;
    default:
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
             "sidebar=%.0fpx  toolbar=%s   (h/l shrink/grow sidebar, t toolbar, q quit)",
             g_sidebar_basis, g_toolbar_centered ? "center" : "space-between");
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void query_terminal_cells(int *cols, int *rows)
{
    *cols = 80;
    *rows = 24;
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        if (ws.ws_col > 0) {
            *cols = ws.ws_col;
        }
        if (ws.ws_row > 0) {
            *rows = ws.ws_row;
        }
    }
}

int main(void)
{
    if (yetty_ygui_init() != 0) {
        return 1;
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "flex-dashboard"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    /* Outer column fills the canvas (resize callback re-applies). */
    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_set_padding(g_outer, 12, 12, 12, 12);
    yetty_ygui_widget_set_gap(g_outer, 12);
    yetty_ygui_widget_set_align_items(g_outer, YETTY_YGUI_ALIGN_STRETCH);

    /* Toolbar row */
    g_toolbar = yetty_ygui_engine_hbox(g_engine, "toolbar", 0, 0, 0, 48);
    yetty_ygui_widget_set_padding(g_toolbar, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(g_toolbar, 8);
    yetty_ygui_widget_set_justify_content(g_toolbar, YETTY_YGUI_JUSTIFY_SPACE_BETWEEN);
    yetty_ygui_widget_set_align_items(g_toolbar, YETTY_YGUI_ALIGN_CENTER);
    yetty_ygui_widget_add_child(g_outer, g_toolbar);

    struct yetty_ygui_widget *file_group = yetty_ygui_engine_hbox(g_engine, "file_group", 0, 0, 0, 0);
    yetty_ygui_widget_set_padding(file_group, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(file_group, 8);
    yetty_ygui_widget_add_child(g_toolbar, file_group);
    yetty_ygui_widget_add_child(file_group, make_action_button("new", "New"));
    yetty_ygui_widget_add_child(file_group, make_action_button("open", "Open"));
    yetty_ygui_widget_add_child(file_group, make_action_button("save", "Save"));
    yetty_ygui_widget_add_child(g_toolbar, make_action_button("profile", "Profile"));

    /* Body row fills remaining height. */
    struct yetty_ygui_widget *body = yetty_ygui_engine_hbox(g_engine, "body", 0, 0, 0, 0);
    yetty_ygui_widget_set_padding(body, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(body, 10);
    yetty_ygui_widget_set_align_items(body, YETTY_YGUI_ALIGN_STRETCH);
    yetty_ygui_widget_set_flex(body, 1.0f, 0.0f, 0.0f);
    yetty_ygui_widget_add_child(g_outer, body);

    /* Sidebar with adjustable flex_basis. */
    g_sidebar = yetty_ygui_engine_vbox(g_engine, "sidebar", 0, 0, 0, 0);
    yetty_ygui_widget_set_padding(g_sidebar, 8, 8, 8, 8);
    yetty_ygui_widget_set_gap(g_sidebar, 6);
    yetty_ygui_widget_set_align_items(g_sidebar, YETTY_YGUI_ALIGN_STRETCH);
    yetty_ygui_widget_set_flex(g_sidebar, 0.0f, 0.0f, g_sidebar_basis);
    yetty_ygui_widget_add_child(body, g_sidebar);
    yetty_ygui_widget_add_child(g_sidebar, make_action_button("nav_dashboard", "Dashboard"));
    yetty_ygui_widget_add_child(g_sidebar, make_action_button("nav_inbox", "Inbox"));
    yetty_ygui_widget_add_child(g_sidebar, make_action_button("nav_settings", "Settings"));

    /* Content fills the rest of the body row. */
    struct yetty_ygui_widget *content = yetty_ygui_engine_vbox(g_engine, "content", 0, 0, 0, 0);
    yetty_ygui_widget_set_padding(content, 12, 12, 12, 12);
    yetty_ygui_widget_set_gap(content, 8);
    yetty_ygui_widget_set_align_items(content, YETTY_YGUI_ALIGN_STRETCH);
    yetty_ygui_widget_set_flex(content, 1.0f, 0.0f, 0.0f);
    yetty_ygui_widget_add_child(body, content);
    yetty_ygui_widget_add_child(content,
                                yetty_ygui_engine_label(g_engine, "headline", 0, 0, "Welcome back."));
    yetty_ygui_widget_add_child(content, yetty_ygui_engine_label(
                                             g_engine, "tag", 0, 0,
                                             "h / l grow & shrink sidebar.  t toggle toolbar."));
    yetty_ygui_widget_add_child(content,
                                yetty_ygui_engine_progress(g_engine, "prog", 0, 0, 0, 18, 0.42f));

    /* Status row */
    struct yetty_ygui_widget *status_row =
        yetty_ygui_engine_hbox(g_engine, "status_row", 0, 0, 0, 40);
    yetty_ygui_widget_set_padding(status_row, 0, 0, 0, 0);
    yetty_ygui_widget_set_gap(status_row, 8);
    yetty_ygui_widget_set_justify_content(status_row, YETTY_YGUI_JUSTIFY_SPACE_BETWEEN);
    yetty_ygui_widget_set_align_items(status_row, YETTY_YGUI_ALIGN_CENTER);
    yetty_ygui_widget_add_child(g_outer, status_row);

    g_status = yetty_ygui_engine_label(g_engine, "status",  0, 0,
                                       "h / l shrink/grow sidebar, t toolbar, q quit");
    yetty_ygui_widget_add_child(status_row, g_status);
    struct yetty_ygui_widget *quit = yetty_ygui_engine_button(g_engine, "quit", 0, 0, 80, 36, "Quit");
    yetty_ygui_widget_button_on_click(quit, on_quit, NULL);
    yetty_ygui_widget_add_child(status_row, quit);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
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
