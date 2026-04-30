/*
 * Demo 14: Todo List — dynamic widget creation/removal.
 * Ported from yetty-poc/demo/assets/ygui-c/python/09_todo_list.py.
 *
 * Each todo row owns three widgets (checkbox, label, delete button).
 * The delete button's userdata points back at the todo so the C callback
 * can locate and remove it from the list.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ygui.h"

#define MAX_TODOS 64
#define START_Y   100
#define ROW_DY    45

struct todo {
    int id;
    int completed;
    ygui_widget_t* checkbox;
    ygui_widget_t* label;
    ygui_widget_t* delete_btn;
};

static ygui_engine_t* g_engine = NULL;
static ygui_widget_t* g_stats = NULL;
static struct todo    g_todos[MAX_TODOS];
static int            g_todo_count = 0;
static int            g_next_id = 0;

static void update_stats(void) {
    int completed = 0;
    for (int i = 0; i < g_todo_count; i++) {
        if (g_todos[i].completed) completed++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d done", completed, g_todo_count);
    ygui_label_set_text(g_stats, buf);
}

static void reposition(void) {
    for (int i = 0; i < g_todo_count; i++) {
        float y = START_Y + i * ROW_DY;
        ygui_widget_set_position(g_todos[i].checkbox,   30,  y);
        ygui_widget_set_position(g_todos[i].label,      70,  y + 6);
        ygui_widget_set_position(g_todos[i].delete_btn, 400, y);
    }
}

static int find_index_by_id(int id) {
    for (int i = 0; i < g_todo_count; i++) {
        if (g_todos[i].id == id) return i;
    }
    return -1;
}

static void remove_todo(int id) {
    int i = find_index_by_id(id);
    if (i < 0) return;
    ygui_widget_remove(g_todos[i].checkbox);
    ygui_widget_remove(g_todos[i].label);
    ygui_widget_remove(g_todos[i].delete_btn);
    /* shift remaining todos */
    for (int j = i; j < g_todo_count - 1; j++) g_todos[j] = g_todos[j + 1];
    g_todo_count--;
    reposition();
    update_stats();
}

static void on_toggle(ygui_widget_t* w, int checked, void* userdata) {
    (void)w;
    int id = (int)(intptr_t)userdata;
    int i = find_index_by_id(id);
    if (i < 0) return;
    g_todos[i].completed = checked;
    update_stats();
}

static void on_delete(ygui_widget_t* w, void* userdata) {
    (void)w;
    remove_todo((int)(intptr_t)userdata);
}

static void add_todo(const char* text) {
    if (g_todo_count >= MAX_TODOS) return;
    int id = g_next_id++;
    float y = START_Y + g_todo_count * ROW_DY;

    char id_buf[32];
    snprintf(id_buf, sizeof(id_buf), "todo_cb_%d", id);
    ygui_widget_t* cb = ygui_checkbox(g_engine, id_buf, 30, y, 30, 30, "", 0);

    snprintf(id_buf, sizeof(id_buf), "todo_text_%d", id);
    ygui_widget_t* lbl = ygui_label(g_engine, id_buf, 70, y + 6, text);

    snprintf(id_buf, sizeof(id_buf), "todo_del_%d", id);
    ygui_widget_t* del = ygui_button(g_engine, id_buf, 400, y, 65, 30, "Delete");

    ygui_checkbox_on_change(cb, on_toggle, (void*)(intptr_t)id);
    ygui_button_on_click(del, on_delete, (void*)(intptr_t)id);

    g_todos[g_todo_count].id         = id;
    g_todos[g_todo_count].completed  = 0;
    g_todos[g_todo_count].checkbox   = cb;
    g_todos[g_todo_count].label      = lbl;
    g_todos[g_todo_count].delete_btn = del;
    g_todo_count++;
    update_stats();
}

static void on_add(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    char buf[32];
    snprintf(buf, sizeof(buf), "Task %d", g_next_id + 1);
    add_todo(buf);
}

static void on_clear_completed(ygui_widget_t* w, void* u) {
    (void)w; (void)u;
    /* Walk from the back so removals don't shift indices we haven't visited. */
    for (int i = g_todo_count - 1; i >= 0; i--) {
        if (g_todos[i].completed) remove_todo(g_todos[i].id);
    }
}

static void on_key(ygui_engine_t* e, uint32_t key, int mods, void* u) {
    (void)mods; (void)u;
    if (key == 'q' || key == 'Q') ygui_engine_stop(e);
}

int main(void) {
    (void)freopen("/dev/null", "w", stderr);

    if (ygui_init() != 0) return 1;
    { struct ygui_engine_ptr_result _eng_r = ygui_engine_create_with_pixel_hint("todo-app", 2, 2, 500.0f, 500.0f);
        if (YETTY_IS_ERR(_eng_r)) { yetty_ycore_error_destroy(_eng_r.error); return 1; }
        g_engine = _eng_r.value; }
    if (!g_engine) { ygui_shutdown(); return 1; }

    ygui_label(g_engine, "title", 30, 20, "Todo List");
    g_stats = ygui_label(g_engine, "stats", 320, 58, "0 items");

    ygui_widget_t* add = ygui_button(g_engine, "add_btn",   30,  50, 100, 35, "+ Add Task");
    ygui_widget_t* clr = ygui_button(g_engine, "clear_btn", 150, 50, 140, 35, "Clear Done");
    ygui_button_on_click(add, on_add, NULL);
    ygui_button_on_click(clr, on_clear_completed, NULL);

    add_todo("Buy groceries");
    add_todo("Write documentation");
    add_todo("Review pull request");

    ygui_engine_on_key(g_engine, on_key, NULL);
    ygui_engine_show(g_engine);
    ygui_engine_run(g_engine);

    ygui_engine_destroy(g_engine);
    ygui_shutdown();
    return 0;
}
