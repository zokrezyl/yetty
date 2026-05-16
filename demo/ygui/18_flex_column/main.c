/*
 * Demo 18: Flex column — full-terminal, interactive.
 *
 * A vertical flex container fills the canvas. Keys change flex parameters:
 *
 *   +     add a child (max 8)
 *   -     remove the last child
 *   g     toggle flex_grow on the middle child (vertical absorption)
 *   j     cycle justify_content (controls main axis = Y)
 *   a     cycle align_items (controls cross axis = X stretch/center/...)
 *   s     toggle align_self=CENTER on the middle child
 *   q     quit
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

#define MAX_CHILDREN 8
#define HEADER_H 70.0f

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_col = NULL;
static struct yetty_ygui_widget *g_status = NULL;
static struct yetty_ygui_widget *g_children[MAX_CHILDREN] = {0};
static int g_n_children = 0;
static int g_justify = YETTY_YGUI_JUSTIFY_START;
static int g_align = YETTY_YGUI_ALIGN_STRETCH;
static int g_grow_middle = 1;
static int g_self_center = 0;

static const char *justify_name(int j)
{
    switch (j) {
    case YETTY_YGUI_JUSTIFY_START: return "start";
    case YETTY_YGUI_JUSTIFY_CENTER: return "center";
    case YETTY_YGUI_JUSTIFY_END: return "end";
    case YETTY_YGUI_JUSTIFY_SPACE_BETWEEN: return "space-between";
    case YETTY_YGUI_JUSTIFY_SPACE_AROUND: return "space-around";
    case YETTY_YGUI_JUSTIFY_SPACE_EVENLY: return "space-evenly";
    }
    return "?";
}

static const char *align_name(int a)
{
    switch (a) {
    case YETTY_YGUI_ALIGN_START: return "start";
    case YETTY_YGUI_ALIGN_CENTER: return "center";
    case YETTY_YGUI_ALIGN_END: return "end";
    case YETTY_YGUI_ALIGN_STRETCH: return "stretch";
    }
    return "auto";
}

static void update_status(void)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "n=%d  justify=%s  align=%s  grow_middle=%s  self_center=%s   "
             "(+/- j a g s, q quit)",
             g_n_children, justify_name(g_justify), align_name(g_align),
             g_grow_middle ? "on" : "off", g_self_center ? "on" : "off");
    yetty_ygui_widget_label_set_text(g_status, buf);
}

static void apply_modifiers(void)
{
    int mid = g_n_children / 2;
    for (int i = 0; i < g_n_children; i++) {
        float grow = (g_grow_middle && i == mid) ? 1.0f : 0.0f;
        yetty_ygui_widget_set_flex(g_children[i], grow, 0.0f, 0.0f);
        ygui_align_t self = (g_self_center && i == mid) ? YETTY_YGUI_ALIGN_CENTER
                                                         : YETTY_YGUI_ALIGN_AUTO;
        yetty_ygui_widget_set_align_self(g_children[i], self);
    }
}

static void add_child(void)
{
    if (g_n_children >= MAX_CHILDREN) {
        return;
    }
    char id[16];
    char label[24];
    snprintf(id, sizeof(id), "btn%d", g_n_children);
    snprintf(label, sizeof(label), "Item %c", 'A' + g_n_children);
    struct yetty_ygui_widget *b = yetty_ygui_engine_button(g_engine, id, 0, 0, 200, 44, label);
    yetty_ygui_widget_add_child(g_col, b);
    g_children[g_n_children++] = b;
    apply_modifiers();
}

static void remove_last_child(void)
{
    if (g_n_children == 0) {
        return;
    }
    g_n_children--;
    yetty_ygui_widget_remove(g_children[g_n_children]);
    g_children[g_n_children] = NULL;
    apply_modifiers();
}

static void on_resize(struct yetty_ygui_engine *e, float new_w, float new_h, float prev_w,
                      float prev_h, void *u)
{
    (void)e;
    (void)prev_w;
    (void)prev_h;
    (void)u;
    yetty_ygui_widget_set_size(g_col, new_w, new_h - HEADER_H);
}

static void on_key(struct yetty_ygui_engine *e, uint32_t key, int mods, void *u)
{
    (void)mods;
    (void)u;
    int dirty = 1;
    switch (key) {
    case 'q':
    case 'Q':
        yetty_ygui_engine_stop(e);
        return;
    case '+':
    case '=':
        add_child();
        break;
    case '-':
    case '_':
        remove_last_child();
        break;
    case 'g':
    case 'G':
        g_grow_middle = !g_grow_middle;
        apply_modifiers();
        break;
    case 's':
    case 'S':
        g_self_center = !g_self_center;
        apply_modifiers();
        break;
    case 'j':
    case 'J':
        g_justify = (g_justify + 1) % 6;
        yetty_ygui_widget_set_justify_content(g_col, (ygui_justify_t)g_justify);
        break;
    case 'a':
    case 'A':
        g_align = g_align >= YETTY_YGUI_ALIGN_STRETCH ? YETTY_YGUI_ALIGN_START : g_align + 1;
        yetty_ygui_widget_set_align_items(g_col, (ygui_align_t)g_align);
        break;
    default:
        dirty = 0;
        break;
    }
    if (dirty) {
        update_status();
    }
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
        yetty_ygui_engine_create("flex-column", 0, 0, cols, rows);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    yetty_ygui_engine_set_canvas_mode(g_engine, YETTY_YGUI_CANVAS_FIT);

    yetty_ygui_engine_label(g_engine, "title", 16, 14,
                            "Flex column — keys: +/- j a g s, q to quit");
    g_status = yetty_ygui_engine_label(g_engine, "status", 16, 40, "");

    g_col = yetty_ygui_engine_vbox(g_engine, "col", 0, HEADER_H, 100, 100);
    yetty_ygui_widget_set_padding(g_col, 16, 16, 16, 16);
    yetty_ygui_widget_set_gap(g_col, 8);
    yetty_ygui_widget_set_align_items(g_col, (ygui_align_t)g_align);
    yetty_ygui_widget_set_justify_content(g_col, (ygui_justify_t)g_justify);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);

    add_child();
    add_child();
    add_child();
    add_child();
    update_status();

    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
    yetty_ygui_engine_show(g_engine);
    {
        float cw = 0, ch = 0;
        yetty_ygui_engine_get_size(g_engine, &cw, &ch);
        if (cw > 0 && ch > 0) {
            yetty_ygui_widget_set_size(g_col, cw, ch - HEADER_H);
        }
    }

    yetty_ygui_engine_run(g_engine);

    yetty_ygui_engine_destroy(g_engine);
    yetty_ygui_shutdown();
    return 0;
}
