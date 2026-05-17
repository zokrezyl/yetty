/*
 * Demo 20: File-tree browser rooted at CWD.
 *
 * Builds a tree where each folder is lazy: its children are scanned the
 * first time the user expands it. Folders sort first, alphabetical
 * within each group; hidden entries (dot-files) are skipped. Each
 * tree_node carries a heap-allocated `dir_entry` via on_toggle's
 * userdata so the path travels with the node.
 *
 * Press 'q' to quit.
 */

#define _DEFAULT_SOURCE  /* DT_DIR / DT_REG when available */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_root_list = NULL;
static struct yetty_ygui_widget *g_status = NULL;
static unsigned long g_id_counter = 0;
static char g_root_path[4096];

/* Per-folder state attached to each tree_node via on_toggle's userdata.
 * Lifetime = process lifetime; we don't currently free these (the
 * engine outlives the dir_entries it points at, and process exit
 * reclaims them). For long-running apps this would need a registry. */
struct dir_entry {
    char *abs_path;
    int   loaded;
};

static struct dir_entry *make_dir_entry(const char *abs_path)
{
    struct dir_entry *de = (struct dir_entry *)calloc(1, sizeof(*de));
    if (!de) return NULL;
    de->abs_path = strdup(abs_path);
    de->loaded = 0;
    return de;
}

static char *make_id(const char *prefix)
{
    /* Caller frees. yetty_ygui copies the id internally so this string
     * doesn't need to outlive the constructor call. */
    char buf[64];
    snprintf(buf, sizeof(buf), "%s_%lu", prefix, ++g_id_counter);
    return strdup(buf);
}

struct fs_entry {
    char *name;
    char *full;
    int   is_dir;
};

static int fs_entry_cmp(const void *a, const void *b)
{
    const struct fs_entry *ea = (const struct fs_entry *)a;
    const struct fs_entry *eb = (const struct fs_entry *)b;
    if (ea->is_dir != eb->is_dir) {
        return eb->is_dir - ea->is_dir; /* dirs first */
    }
    return strcmp(ea->name, eb->name);
}

/* Read a directory into a sorted, allocated array. *count receives the
 * length; the caller must free each entry's strings and the array. On
 * I/O failure returns NULL and *count = 0. */
static struct fs_entry *scan_dir(const char *path, int *count)
{
    *count = 0;
    DIR *d = opendir(path);
    if (!d) {
        return NULL;
    }
    int cap = 32;
    int n = 0;
    struct fs_entry *arr = (struct fs_entry *)calloc(cap, sizeof(*arr));
    if (!arr) {
        closedir(d);
        return NULL;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') {
            continue; /* skip "." ".." and dotfiles */
        }
        if (n >= cap) {
            cap *= 2;
            struct fs_entry *bigger = (struct fs_entry *)realloc(arr, cap * sizeof(*arr));
            if (!bigger) break;
            arr = bigger;
        }
        char full[4096];
        snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        struct stat st;
        int is_dir = 0;
        if (lstat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        }
        arr[n].name = strdup(de->d_name);
        arr[n].full = strdup(full);
        arr[n].is_dir = is_dir;
        n++;
    }
    closedir(d);
    qsort(arr, n, sizeof(*arr), fs_entry_cmp);
    *count = n;
    return arr;
}

static void free_fs_entries(struct fs_entry *arr, int n)
{
    if (!arr) return;
    for (int i = 0; i < n; i++) {
        free(arr[i].name);
        free(arr[i].full);
    }
    free(arr);
}

/* Forward decls for the populate / toggle pair. */
static void populate_node(struct yetty_ygui_widget *node, struct dir_entry *entry);
static void on_folder_toggle(struct yetty_ygui_widget *node, int expanded, void *userdata);

static void on_select(struct yetty_ygui_widget *row, void *userdata)
{
    (void)userdata;
    char buf[256];
    const char *label = NULL;
    if (yetty_ygui_widget_type(row) == YETTY_YGUI_WIDGET_TREE_NODE) {
        label = yetty_ygui_widget_tree_node_get_label(row);
    } else {
        label = yetty_ygui_widget_label_get_text(row);
    }
    snprintf(buf, sizeof(buf), "Selected: %s", label ? label : "(?)");
    yetty_ygui_widget_label_set_text(g_status, buf);
}

/* Populate `node`'s children list from the directory it represents.
 * Idempotent — only runs the first time (entry->loaded gates it). */
static void populate_node(struct yetty_ygui_widget *node, struct dir_entry *entry)
{
    if (!entry || entry->loaded) return;
    entry->loaded = 1;

    int n = 0;
    struct fs_entry *items = scan_dir(entry->abs_path, &n);
    struct yetty_ygui_widget *kids = yetty_ygui_widget_tree_node_children(node);
    if (!kids) {
        free_fs_entries(items, n);
        return;
    }

    for (int i = 0; i < n; i++) {
        if (items[i].is_dir) {
            char *id = make_id("d");
            struct yetty_ygui_widget *sub = yetty_ygui_engine_tree_node(g_engine, id, items[i].name);
            free(id);
            struct dir_entry *sub_entry = make_dir_entry(items[i].full);
            yetty_ygui_widget_tree_node_on_toggle(sub, on_folder_toggle, sub_entry);
            yetty_ygui_widget_add_child(kids, sub);
        } else {
            char *id = make_id("f");
            yetty_ygui_widget_add_child(
                kids, yetty_ygui_engine_label(g_engine, id, 0, 0, items[i].name));
            free(id);
        }
    }
    free_fs_entries(items, n);
}

static void on_folder_toggle(struct yetty_ygui_widget *node, int expanded, void *userdata)
{
    struct dir_entry *entry = (struct dir_entry *)userdata;
    if (!expanded) return;
    populate_node(node, entry);
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
    yetty_ygui_widget_set_size(g_root_list, new_w - 16, new_h - 70);
}

int main(void)
{
    if (yetty_ygui_init() != 0) {
        return 1;
    }

    if (!getcwd(g_root_path, sizeof(g_root_path))) {
        snprintf(g_root_path, sizeof(g_root_path), ".");
    }

    int cols, rows;
    query_terminal_cells(&cols, &rows);

    struct ygui_engine_ptr_result eng_r =
        yetty_ygui_engine_create((struct yetty_ygui_engine_args){.name = "file-tree"});
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    /* Roughly double the default font and row sizes for readability — the
     * tree_node header height tracks theme->row_height, so this also
     * resizes the chevron and per-row spacing. We build a theme from
     * scratch and hand ownership over via set_theme. */
    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 28.0f);
    yetty_ygui_theme_set_row_height(theme, 44.0f);
    yetty_ygui_theme_set_padding(theme, 4.0f, 8.0f, 16.0f);
    yetty_ygui_engine_set_theme(g_engine, theme);

    char title_buf[4200];
    snprintf(title_buf, sizeof(title_buf), "%s   (q to quit)", g_root_path);
    yetty_ygui_engine_label(g_engine, "title", 8, 6, title_buf);
    g_status = yetty_ygui_engine_label(g_engine, "status", 8, 30, "Click any folder to expand");

    g_root_list = yetty_ygui_engine_list(g_engine, "root", 8, 60, 600, 400);
    yetty_ygui_widget_apply_css(g_root_list, "padding: 6px; gap: 2px;");
    yetty_ygui_widget_list_on_select(g_root_list, on_select, NULL);

    /* Seed the root list with the immediate contents of CWD (no toggle
     * needed — the user is already "inside" the root). Sub-folders get
     * lazy on_toggle handlers. */
    int n = 0;
    struct fs_entry *items = scan_dir(g_root_path, &n);
    for (int i = 0; i < n; i++) {
        if (items[i].is_dir) {
            char *id = make_id("d");
            struct yetty_ygui_widget *sub =
                yetty_ygui_engine_tree_node(g_engine, id, items[i].name);
            free(id);
            struct dir_entry *entry = make_dir_entry(items[i].full);
            yetty_ygui_widget_tree_node_on_toggle(sub, on_folder_toggle, entry);
            yetty_ygui_widget_add_child(g_root_list, sub);
        } else {
            char *id = make_id("f");
            yetty_ygui_widget_add_child(
                g_root_list, yetty_ygui_engine_label(g_engine, id, 0, 0, items[i].name));
            free(id);
        }
    }
    free_fs_entries(items, n);

    yetty_ygui_engine_on_resize(g_engine, on_resize, NULL);
    yetty_ygui_engine_on_key(g_engine, on_key, NULL);
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
