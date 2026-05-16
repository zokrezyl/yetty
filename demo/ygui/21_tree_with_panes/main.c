/*
 * Demo 21: File browser with split panes — lazy CWD-rooted tree on the
 * left, details panel on the right. Selecting any row (folder or file)
 * shows its absolute path, kind, size, and modification time via stat().
 *
 * Press 'q' to quit.
 */

#define _DEFAULT_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <yetty/ygui/ygui.h>

static struct yetty_ygui_engine *g_engine = NULL;
static struct yetty_ygui_widget *g_outer = NULL;
static struct yetty_ygui_widget *g_tree = NULL;
static struct yetty_ygui_widget *g_detail_name = NULL;
static struct yetty_ygui_widget *g_detail_kind = NULL;
static struct yetty_ygui_widget *g_detail_path = NULL;
static struct yetty_ygui_widget *g_detail_size = NULL;
static struct yetty_ygui_widget *g_detail_mtime = NULL;
static unsigned long g_id_counter = 0;
static char g_root_path[4096];

/* Side-table mapping `widget pointer` → `absolute path`. The path lives
 * here so we can look up any selected widget — leaf labels included
 * (which don't carry path on themselves like folders do via on_toggle
 * userdata). Process-lifetime allocations; freed implicitly at exit. */
struct path_entry {
    struct yetty_ygui_widget *widget;
    char *abs_path;
    int   is_dir;
};

static struct path_entry *g_paths = NULL;
static int g_paths_count = 0;
static int g_paths_cap = 0;

static void register_path(struct yetty_ygui_widget *w, const char *abs_path, int is_dir)
{
    if (g_paths_count >= g_paths_cap) {
        g_paths_cap = g_paths_cap ? g_paths_cap * 2 : 64;
        g_paths = (struct path_entry *)realloc(g_paths, g_paths_cap * sizeof(*g_paths));
    }
    g_paths[g_paths_count].widget = w;
    g_paths[g_paths_count].abs_path = strdup(abs_path);
    g_paths[g_paths_count].is_dir = is_dir;
    g_paths_count++;
}

static const struct path_entry *lookup_path(const struct yetty_ygui_widget *w)
{
    for (int i = 0; i < g_paths_count; i++) {
        if (g_paths[i].widget == w) return &g_paths[i];
    }
    return NULL;
}

/* Per-folder lazy-load state stashed in tree_node on_toggle userdata. */
struct dir_entry {
    char *abs_path;
    int   loaded;
};

static struct dir_entry *make_dir_entry(const char *abs_path)
{
    struct dir_entry *de = (struct dir_entry *)calloc(1, sizeof(*de));
    if (!de) return NULL;
    de->abs_path = strdup(abs_path);
    return de;
}

static char *make_id(const char *prefix)
{
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
        return eb->is_dir - ea->is_dir;
    }
    return strcmp(ea->name, eb->name);
}

static struct fs_entry *scan_dir(const char *path, int *count)
{
    *count = 0;
    DIR *d = opendir(path);
    if (!d) return NULL;
    int cap = 32, n = 0;
    struct fs_entry *arr = (struct fs_entry *)calloc(cap, sizeof(*arr));
    if (!arr) {
        closedir(d);
        return NULL;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
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

static void format_size(off_t bytes, char *buf, size_t buflen)
{
    if (bytes < 1024) {
        snprintf(buf, buflen, "%lld B", (long long)bytes);
    } else if (bytes < 1024L * 1024) {
        snprintf(buf, buflen, "%.1f KiB", (double)bytes / 1024.0);
    } else if (bytes < 1024L * 1024 * 1024) {
        snprintf(buf, buflen, "%.1f MiB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buf, buflen, "%.2f GiB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

static void update_detail_for(const struct yetty_ygui_widget *w)
{
    const struct path_entry *pe = lookup_path(w);
    if (!pe) {
        yetty_ygui_widget_label_set_text(g_detail_name, "(not in tree)");
        return;
    }
    /* Name = basename. */
    const char *name = strrchr(pe->abs_path, '/');
    name = name ? name + 1 : pe->abs_path;
    yetty_ygui_widget_label_set_text(g_detail_name, name);
    yetty_ygui_widget_label_set_text(g_detail_kind, pe->is_dir ? "folder" : "file");
    yetty_ygui_widget_label_set_text(g_detail_path, pe->abs_path);

    struct stat st;
    if (lstat(pe->abs_path, &st) == 0) {
        char sz[64];
        if (pe->is_dir) {
            snprintf(sz, sizeof(sz), "—");
        } else {
            format_size(st.st_size, sz, sizeof(sz));
        }
        yetty_ygui_widget_label_set_text(g_detail_size, sz);

        char tbuf[64];
        struct tm tm_buf;
        localtime_r(&st.st_mtime, &tm_buf);
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M", &tm_buf);
        yetty_ygui_widget_label_set_text(g_detail_mtime, tbuf);
    } else {
        yetty_ygui_widget_label_set_text(g_detail_size, "(stat failed)");
        yetty_ygui_widget_label_set_text(g_detail_mtime, "—");
    }
}

static void on_select(struct yetty_ygui_widget *row, void *userdata)
{
    (void)userdata;
    update_detail_for(row);
}

static void on_folder_toggle(struct yetty_ygui_widget *node, int expanded, void *userdata);

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

    /* Forward selection events from the inner list to the same handler.
     * Each tree_node's children list defaults to no select callback. */
    yetty_ygui_widget_list_on_select(kids, on_select, NULL);

    for (int i = 0; i < n; i++) {
        if (items[i].is_dir) {
            char *id = make_id("d");
            struct yetty_ygui_widget *sub =
                yetty_ygui_engine_tree_node(g_engine, id, items[i].name);
            free(id);
            struct dir_entry *sub_entry = make_dir_entry(items[i].full);
            yetty_ygui_widget_tree_node_on_toggle(sub, on_folder_toggle, sub_entry);
            register_path(sub, items[i].full, 1);
            yetty_ygui_widget_add_child(kids, sub);
        } else {
            char *id = make_id("f");
            struct yetty_ygui_widget *leaf =
                yetty_ygui_engine_label(g_engine, id, 0, 0, items[i].name);
            free(id);
            register_path(leaf, items[i].full, 0);
            yetty_ygui_widget_add_child(kids, leaf);
        }
    }
    free_fs_entries(items, n);
}

static void on_folder_toggle(struct yetty_ygui_widget *node, int expanded, void *userdata)
{
    if (!expanded) return;
    populate_node(node, (struct dir_entry *)userdata);
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

static struct yetty_ygui_widget *make_field(const char *id, const char *label_text,
                                             struct yetty_ygui_widget **value_out)
{
    char hbox_id[128], lbl_id[128], val_id[128];
    snprintf(hbox_id, sizeof(hbox_id), "fld_%s", id);
    snprintf(lbl_id, sizeof(lbl_id), "lbl_%s", id);
    snprintf(val_id, sizeof(val_id), "val_%s", id);

    struct yetty_ygui_widget *row = yetty_ygui_engine_hbox(g_engine, hbox_id, 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(row, "padding: 0; gap: 12px; align-items: start;");
    yetty_ygui_widget_add_child(row, yetty_ygui_engine_label(g_engine, lbl_id, 0, 0, label_text));
    *value_out = yetty_ygui_engine_label(g_engine, val_id, 0, 0, "—");
    yetty_ygui_widget_add_child(row, *value_out);
    return row;
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
        yetty_ygui_engine_create("file-panes", 0, 0, cols, rows);
    if (YETTY_IS_ERR(eng_r)) {
        yetty_ycore_error_destroy(eng_r.error);
        yetty_ygui_shutdown();
        return 1;
    }
    g_engine = eng_r.value;
    yetty_ygui_engine_set_canvas_mode(g_engine, YETTY_YGUI_CANVAS_FIT);

    /* Larger theme so rows / chevrons are readable. */
    struct yetty_ygui_theme *theme = yetty_ygui_theme_create_default();
    yetty_ygui_theme_set_font_size(theme, 28.0f);
    yetty_ygui_theme_set_row_height(theme, 44.0f);
    yetty_ygui_theme_set_padding(theme, 4.0f, 8.0f, 16.0f);
    yetty_ygui_engine_set_theme(g_engine, theme);

    g_outer = yetty_ygui_engine_vbox(g_engine, "outer", 0, 0, 100, 100);
    yetty_ygui_widget_apply_css(g_outer,
                                 "padding: 12px; gap: 12px; align-items: stretch;");

    char title_buf[4200];
    snprintf(title_buf, sizeof(title_buf), "%s   (q to quit)", g_root_path);
    yetty_ygui_widget_add_child(g_outer,
                                 yetty_ygui_engine_label(g_engine, "title", 0, 0, title_buf));

    struct yetty_ygui_widget *body = yetty_ygui_engine_hbox(g_engine, "body", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(body, "padding: 0; gap: 12px; flex: 1 0 0; align-items: stretch;");
    yetty_ygui_widget_add_child(g_outer, body);

    /* Left pane: tree, fixed-basis 320px. */
    g_tree = yetty_ygui_engine_list(g_engine, "tree", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(g_tree, "padding: 6px; gap: 2px; flex: 0 0 320px;");
    yetty_ygui_widget_list_on_select(g_tree, on_select, NULL);
    yetty_ygui_widget_add_child(body, g_tree);

    /* Right pane: details. */
    struct yetty_ygui_widget *detail = yetty_ygui_engine_vbox(g_engine, "detail", 0, 0, 0, 0);
    yetty_ygui_widget_apply_css(detail,
                                 "padding: 16px; gap: 8px; flex: 1 0 0; align-items: start;");
    yetty_ygui_widget_add_child(body, detail);

    yetty_ygui_widget_add_child(detail,
                                 yetty_ygui_engine_label(g_engine, "h1", 0, 0, "Details"));
    yetty_ygui_widget_add_child(detail, make_field("name",  "Name:",  &g_detail_name));
    yetty_ygui_widget_add_child(detail, make_field("kind",  "Kind:",  &g_detail_kind));
    yetty_ygui_widget_add_child(detail, make_field("path",  "Path:",  &g_detail_path));
    yetty_ygui_widget_add_child(detail, make_field("size",  "Size:",  &g_detail_size));
    yetty_ygui_widget_add_child(detail, make_field("mtime", "Mtime:", &g_detail_mtime));

    /* Seed the root list. */
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
            register_path(sub, items[i].full, 1);
            yetty_ygui_widget_add_child(g_tree, sub);
        } else {
            char *id = make_id("f");
            struct yetty_ygui_widget *leaf =
                yetty_ygui_engine_label(g_engine, id, 0, 0, items[i].name);
            free(id);
            register_path(leaf, items[i].full, 0);
            yetty_ygui_widget_add_child(g_tree, leaf);
        }
    }
    free_fs_entries(items, n);

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
