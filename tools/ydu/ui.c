/*
 * ui.c — ydu presentation layer.
 *
 * Two panes: a squarified treemap of the current directory painted into a
 * ydraw_embed canvas (tiles sized by allocated disk usage, largest first), and
 * a synchronized sortable table of the same directory's entries. Layout is
 * absolute, recomputed from the viewport in ydu_ui_relayout so it reflows on
 * resize. Colours follow the yetty brand palette.
 */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

/* ------------------------------------------------------------------ */
/* Palette (macros, not file-scope data)                               */
/* ------------------------------------------------------------------ */

#define YDU_RGBA(r, g, b, a) ((struct yetty_ycore_rgba){(r), (g), (b), (a)})

#define YDU_COL_BG_LIFTED YDU_RGBA(20, 26, 31, 255)
#define YDU_COL_BORDER YDU_RGBA(54, 74, 71, 255)
#define YDU_COL_TEXT YDU_RGBA(224, 229, 228, 255)
#define YDU_COL_TEXT_SECONDARY YDU_RGBA(159, 167, 168, 255)
#define YDU_COL_TEXT_MUTED YDU_RGBA(85, 97, 98, 255)
#define YDU_COL_ACCENT_BRIGHT YDU_RGBA(116, 197, 165, 255)

#define YDU_MAX_TILES 200

/* ------------------------------------------------------------------ */
/* Widget handle table                                                 */
/* ------------------------------------------------------------------ */

struct ydu_ui {
    struct yetty_yclass_object *title;
    struct yetty_yclass_object *path;
    struct yetty_yclass_object *totals;
    struct yetty_yclass_object *status;

    struct yetty_yclass_object *map_panel;
    struct yetty_yclass_object *map; /* ydraw_embed treemap canvas */
    float map_w, map_h;

    struct yetty_yclass_object *table_panel;
    struct yetty_yclass_object *table;
    int table_rows;
};

/* One treemap rectangle in the canvas's local (0,0)-(map_w,map_h) space. */
struct ydu_tile {
    double area; /* scaled so the tiles' areas sum to map_w * map_h */
    float x, y, w, h;
    struct ydu_node *node; /* NULL for the folded "(others)" tile */
    uint64_t bytes;
};

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static void absorb(struct yetty_ycore_void_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
    }
}

/* ydraw colours are packed low→high as r,g,b,a (0xAABBGGRR). */
static uint32_t pack_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

static struct yetty_yclass_object *ui_add(struct ydu_app *app,
                                          struct yetty_yclass_ptr_result cls_result)
{
    if (YETTY_IS_ERR(cls_result)) {
        yetty_ycore_error_destroy(cls_result.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result result =
        yetty_ygui_widget_add(app->root_widget, cls_result.value);
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
        return NULL;
    }
    return result.value;
}

static void ui_place(struct yetty_yclass_object *widget, float x, float y, float w, float h)
{
    if (!widget) {
        return;
    }
    absorb(yetty_ygui_widget_set_position(widget, x, y));
    absorb(yetty_ygui_widget_set_size(widget, w, h));
}

static void ui_set_text(struct yetty_yclass_object *label, const char *text)
{
    if (label) {
        absorb(yetty_ygui_label_set_text(label, text));
    }
}

static void ui_set_color(struct yetty_yclass_object *label, struct yetty_ycore_rgba color)
{
    if (label) {
        absorb(yetty_ygui_label_set_color(label, color));
    }
}

static void ui_set_font(struct yetty_yclass_object *label, float size_px)
{
    if (label) {
        absorb(yetty_ygui_label_set_font_size(label, size_px));
    }
}

static void panel_style(struct yetty_yclass_object *panel)
{
    if (!panel) {
        return;
    }
    absorb(yetty_ygui_panel_set_bg(panel, YDU_COL_BG_LIFTED));
    absorb(yetty_ygui_panel_set_border(panel, YDU_COL_BORDER, 1.0f));
}

static void tile_text(struct yetty_ydraw_drawable_list *list, float x, float y, const char *text,
                      float size, uint32_t color)
{
    struct yetty_ycore_buffer buf = {
        .data = (uint8_t *)text,
        .size = strlen(text),
        .capacity = strlen(text),
    };
    absorb(yetty_ydraw_drawable_list_add_text(list, x, y, &buf, size, color, 0u, -1, 0.0f));
}

/* ------------------------------------------------------------------ */
/* Squarified treemap layout (Bruls, Huizing, van Wijk)                */
/* ------------------------------------------------------------------ */

static float row_worst(const struct ydu_tile *tiles, int start, int end, double sum, float side)
{
    if (end <= start || sum <= 0.0 || side <= 1e-6f) {
        return 1e30f;
    }
    double row_max = tiles[start].area;
    double row_min = tiles[start].area;
    for (int k = start + 1; k < end; k++) {
        if (tiles[k].area > row_max) {
            row_max = tiles[k].area;
        }
        if (tiles[k].area < row_min) {
            row_min = tiles[k].area;
        }
    }
    double side2 = (double)side * (double)side;
    double sum2 = sum * sum;
    double worst_high = (side2 * row_max) / sum2;
    double worst_low = sum2 / (side2 * row_min);
    return (float)(worst_high > worst_low ? worst_high : worst_low);
}

/* Areas in `tiles` must already be scaled so their sum equals w*h and be sorted
 * largest first. Fills each tile's x/y/w/h within the rect at (x,y,w,h). */
static void squarify(struct ydu_tile *tiles, int n, float x, float y, float w, float h)
{
    int i = 0;
    float rx = x, ry = y, rw = w, rh = h;
    while (i < n) {
        float side = rw < rh ? rw : rh;
        double sum = 0.0;
        float best = 1e30f;
        int j = i;
        while (j < n) {
            double next_sum = sum + tiles[j].area;
            float worst = row_worst(tiles, i, j + 1, next_sum, side);
            if (j == i || worst <= best) {
                best = worst;
                sum = next_sum;
                j++;
            } else {
                break;
            }
        }
        float thick = side > 1e-6f ? (float)(sum / side) : rh;
        if (rw <= rh) {
            if (thick > rh) {
                thick = rh;
            }
            float cx = rx;
            for (int k = i; k < j; k++) {
                float cw = (float)(tiles[k].area / sum) * rw;
                tiles[k].x = cx;
                tiles[k].y = ry;
                tiles[k].w = cw;
                tiles[k].h = thick;
                cx += cw;
            }
            ry += thick;
            rh -= thick;
        } else {
            if (thick > rw) {
                thick = rw;
            }
            float cy = ry;
            for (int k = i; k < j; k++) {
                float ch = (float)(tiles[k].area / sum) * rh;
                tiles[k].x = rx;
                tiles[k].y = cy;
                tiles[k].w = thick;
                tiles[k].h = ch;
                cy += ch;
            }
            rx += thick;
            rw -= thick;
        }
        i = j;
    }
}

static int tile_cmp_disk(const void *a, const void *b)
{
    const struct ydu_node *x = *(const struct ydu_node *const *)a;
    const struct ydu_node *y = *(const struct ydu_node *const *)b;
    if (x->disk_bytes != y->disk_bytes) {
        return y->disk_bytes > x->disk_bytes ? 1 : -1;
    }
    return strcmp(x->name, y->name);
}

static uint32_t tile_fill(const struct ydu_node *node, int idx, int selected)
{
    static const uint8_t palette[8][3] = {
        {107, 168, 146}, {116, 197, 165}, {90, 137, 121},  {120, 150, 230},
        {150, 130, 210}, {210, 160, 110}, {200, 120, 120}, {120, 170, 200},
    };
    if (selected) {
        return pack_rgba(116, 197, 165, 255);
    }
    const uint8_t *base = palette[idx & 7];
    /* Files read dimmer than directories. */
    float scale = (node && node->is_dir) ? 1.0f : 0.6f;
    return pack_rgba((uint8_t)(base[0] * scale), (uint8_t)(base[1] * scale),
                     (uint8_t)(base[2] * scale), 255);
}

static void build_treemap(struct ydu_app *app)
{
    struct ydu_ui *ui = app->ui;
    if (!ui->map || ui->map_w <= 2.0f || ui->map_h <= 2.0f) {
        return;
    }
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = list_res.value;
    float map_w = ui->map_w;
    float map_h = ui->map_h;

    struct yetty_ysdf_box backdrop = {
        .center_x = map_w * 0.5f,
        .center_y = map_h * 0.5f,
        .half_width = map_w * 0.5f,
        .half_height = map_h * 0.5f,
        .corner_radius = 0.0f,
    };
    absorb(yetty_ydraw_drawable_list_add_cmd_add_box(list, 0u, 0u, pack_rgba(16, 22, 27, 255), 0u,
                                                     0.0f, &backdrop));

    struct ydu_node *dir = app->cwd;
    if (!dir || dir->n_children == 0) {
        tile_text(list, 8.0f, 20.0f, (dir && !dir->is_dir) ? "(file)" : "(empty)", 13.0f,
                  pack_rgba(159, 167, 168, 255));
        absorb(yetty_ygui_ydraw_embed_set_buffer(ui->map, list));
        return;
    }

    size_t nc = dir->n_children;
    struct ydu_node **sorted = malloc(nc * sizeof(*sorted));
    if (!sorted) {
        absorb(yetty_ygui_ydraw_embed_set_buffer(ui->map, list));
        return;
    }
    memcpy(sorted, dir->children, nc * sizeof(*sorted));
    qsort(sorted, nc, sizeof(*sorted), tile_cmp_disk);

    struct ydu_node *sel_node = NULL;
    if (app->selected >= 0 && (size_t)app->selected < nc) {
        sel_node = dir->children[app->selected];
    }

    struct ydu_tile tiles[YDU_MAX_TILES];
    int n = 0;
    int take = (int)nc;
    int has_others = 0;
    if (take > YDU_MAX_TILES) {
        take = YDU_MAX_TILES - 1;
        has_others = 1;
    }
    for (int i = 0; i < take; i++) {
        uint64_t bytes = sorted[i]->disk_bytes;
        tiles[n].node = sorted[i];
        tiles[n].bytes = bytes;
        tiles[n].area = (double)(bytes == 0 ? 1 : bytes);
        n++;
    }
    if (has_others) {
        uint64_t rest = 0;
        for (size_t i = (size_t)take; i < nc; i++) {
            rest += sorted[i]->disk_bytes;
        }
        tiles[n].node = NULL;
        tiles[n].bytes = rest;
        tiles[n].area = (double)(rest == 0 ? 1 : rest);
        n++;
    }

    double area_sum = 0.0;
    for (int i = 0; i < n; i++) {
        area_sum += tiles[i].area;
    }
    if (area_sum <= 0.0) {
        area_sum = 1.0;
    }
    double scale = ((double)map_w * (double)map_h) / area_sum;
    for (int i = 0; i < n; i++) {
        tiles[i].area *= scale;
    }

    squarify(tiles, n, 0.0f, 0.0f, map_w, map_h);

    for (int i = 0; i < n; i++) {
        struct ydu_tile *tile = &tiles[i];
        if (tile->w < 1.0f || tile->h < 1.0f) {
            continue;
        }
        int is_sel = (tile->node && tile->node == sel_node);
        uint32_t fill = tile_fill(tile->node, i, is_sel);
        uint32_t stroke = is_sel ? pack_rgba(224, 229, 228, 255) : pack_rgba(11, 16, 20, 255);
        float stroke_w = is_sel ? 2.0f : 1.0f;

        struct yetty_ysdf_box box = {
            .center_x = tile->x + tile->w * 0.5f,
            .center_y = tile->y + tile->h * 0.5f,
            .half_width = tile->w * 0.5f - 0.5f,
            .half_height = tile->h * 0.5f - 0.5f,
            .corner_radius = 2.0f,
        };
        if (box.half_width < 0.5f) {
            box.half_width = tile->w * 0.5f;
        }
        if (box.half_height < 0.5f) {
            box.half_height = tile->h * 0.5f;
        }
        absorb(
            yetty_ydraw_drawable_list_add_cmd_add_box(list, 0u, 0u, fill, stroke, stroke_w, &box));

        if (tile->w >= 48.0f && tile->h >= 18.0f) {
            const char *name = tile->node ? tile->node->name : "(others)";
            char label[64];
            int max_chars = (int)(tile->w / 7.5f);
            if (max_chars < 3) {
                max_chars = 3;
            }
            if (max_chars > (int)sizeof(label) - 1) {
                max_chars = (int)sizeof(label) - 1;
            }
            snprintf(label, sizeof(label), "%.*s", max_chars, name);
            uint32_t text_col = is_sel ? pack_rgba(11, 16, 20, 255) : pack_rgba(224, 229, 228, 255);
            tile_text(list, tile->x + 4.0f, tile->y + 13.0f, label, 11.0f, text_col);
            if (tile->h >= 32.0f) {
                char size_str[32];
                ydu_fmt_bytes(tile->bytes, size_str, sizeof(size_str));
                uint32_t size_col =
                    is_sel ? pack_rgba(20, 28, 26, 255) : pack_rgba(159, 167, 168, 255);
                tile_text(list, tile->x + 4.0f, tile->y + 26.0f, size_str, 10.0f, size_col);
            }
        }
    }

    free(sorted);
    absorb(yetty_ygui_ydraw_embed_set_buffer(ui->map, list));
}

/* ------------------------------------------------------------------ */
/* Table                                                               */
/* ------------------------------------------------------------------ */

static void fmt_date(int64_t mtime, char *out, size_t out_size)
{
    time_t stamp = (time_t)mtime;
    struct tm broken;
    if (localtime_r(&stamp, &broken)) {
        strftime(out, out_size, "%Y-%m-%d %H:%M", &broken);
    } else {
        snprintf(out, out_size, "-");
    }
}

static void refresh_table(struct ydu_app *app)
{
    struct ydu_ui *ui = app->ui;
    struct ydu_node *dir = app->cwd;
    absorb(yetty_ygui_table_clear_rows(ui->table));
    if (!dir) {
        return;
    }
    int total = (int)dir->n_children;
    int rows = total < ui->table_rows ? total : ui->table_rows;
    if (rows <= 0) {
        return;
    }
    /* Scroll the window so the selected row stays visible. */
    int start = 0;
    if (app->selected >= rows) {
        start = app->selected - rows + 1;
    }
    if (start > total - rows) {
        start = total - rows;
    }
    if (start < 0) {
        start = 0;
    }
    for (int i = 0; i < rows; i++) {
        int idx = start + i;
        if (idx >= total) {
            break;
        }
        const struct ydu_node *node = dir->children[idx];
        char name[300], size[32], items[24], date[32];
        const char *marker = (idx == app->selected) ? "> " : "  ";
        snprintf(name, sizeof(name), "%s%s%s", marker, node->name, node->is_dir ? "/" : "");
        ydu_fmt_bytes(node->disk_bytes, size, sizeof(size));
        if (node->is_dir) {
            snprintf(items, sizeof(items), "%llu", (unsigned long long)node->item_count);
        } else {
            snprintf(items, sizeof(items), "-");
        }
        fmt_date(node->mtime, date, sizeof(date));
        const char *cells[] = {name, size, items, date};
        absorb(yetty_ygui_table_add_row(ui->table, cells, 4));
    }
}

/* ------------------------------------------------------------------ */
/* Build / free                                                        */
/* ------------------------------------------------------------------ */

struct yetty_ycore_void_result ydu_ui_build(struct ydu_app *app)
{
    if (!app || !app->root_widget) {
        return YETTY_ERR(yetty_ycore_void, "ydu_ui_build: no root");
    }
    struct ydu_ui *ui = calloc(1, sizeof(*ui));
    if (!ui) {
        return YETTY_ERR(yetty_ycore_void, "ydu_ui_build: calloc");
    }
    app->ui = ui;

    ui->title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->title, "ydu");
    ui_set_color(ui->title, YDU_COL_ACCENT_BRIGHT);
    ui_set_font(ui->title, 16.0f);

    ui->path = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->path, YDU_COL_TEXT);
    ui_set_font(ui->path, 12.0f);

    ui->totals = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->totals, YDU_COL_TEXT_SECONDARY);
    ui_set_font(ui->totals, 12.0f);

    ui->status = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->status, YDU_COL_TEXT_MUTED);
    ui_set_font(ui->status, 11.0f);

    ui->map_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->map_panel);
    ui->map = ui_add(app, yetty_ygui_ydraw_embed_class_get());

    ui->table_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->table_panel);
    ui->table = ui_add(app, yetty_ygui_table_class_get());
    if (ui->table) {
        static const char *const columns[] = {"NAME", "SIZE", "ITEMS", "MODIFIED"};
        absorb(yetty_ygui_table_set_columns(ui->table, 4, columns));
    }

    ydu_ui_relayout(app);
    return YETTY_OK_VOID();
}

void ydu_ui_free(struct ydu_app *app)
{
    if (app && app->ui) {
        free(app->ui);
        app->ui = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define YDU_MARGIN 10.0f
#define YDU_PAD 8.0f
#define YDU_GAP 8.0f
#define YDU_LINE 18.0f

void ydu_ui_relayout(struct ydu_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    struct ydu_ui *ui = app->ui;

    float vw = 0.0f, vh = 0.0f;
    yetty_ygui_framework_viewport(app->engine, &vw, &vh);
    if (vw < 480.0f) {
        vw = 1280.0f;
    }
    if (vh < 320.0f) {
        vh = 800.0f;
    }

    float x = YDU_MARGIN;
    float y = YDU_MARGIN;
    float full_w = vw - 2.0f * YDU_MARGIN;

    ui_place(ui->title, x, y, 60.0f, YDU_LINE);
    ui_place(ui->path, x + 64.0f, y + 1.0f, full_w - 64.0f, YDU_LINE);
    y += YDU_LINE + 2.0f;
    ui_place(ui->totals, x, y, full_w, YDU_LINE);
    y += YDU_LINE;
    ui_place(ui->status, x, y, full_w, 16.0f);
    y += 16.0f + YDU_GAP;

    float body_h = vh - y - YDU_MARGIN;
    if (body_h < 120.0f) {
        body_h = 120.0f;
    }
    float map_w = (full_w - YDU_GAP) * 0.60f;
    float table_w = full_w - YDU_GAP - map_w;

    ui_place(ui->map_panel, x, y, map_w, body_h);
    float inner_w = map_w - 2.0f * YDU_PAD;
    float inner_h = body_h - 2.0f * YDU_PAD;
    ui_place(ui->map, x + YDU_PAD, y + YDU_PAD, inner_w, inner_h);
    ui->map_w = inner_w;
    ui->map_h = inner_h;

    float tx = x + map_w + YDU_GAP;
    ui_place(ui->table_panel, tx, y, table_w, body_h);
    float table_h = body_h - 2.0f * YDU_PAD;
    ui_place(ui->table, tx + YDU_PAD, y + YDU_PAD, table_w - 2.0f * YDU_PAD, table_h);

    int rows = (int)((table_h - 22.0f) / 20.0f);
    if (rows < 4) {
        rows = 4;
    }
    ui->table_rows = rows;
}

/* ------------------------------------------------------------------ */
/* Refresh                                                             */
/* ------------------------------------------------------------------ */

void ydu_ui_refresh(struct ydu_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    struct ydu_ui *ui = app->ui;
    char buf[4224];

    char path[4096];
    ydu_node_path(app->cwd, path, sizeof(path));
    ui_set_text(ui->path, path);

    char root_size[32], here_size[32];
    ydu_fmt_bytes(app->tree ? app->tree->disk_bytes : 0, root_size, sizeof(root_size));
    ydu_fmt_bytes(app->cwd ? app->cwd->disk_bytes : 0, here_size, sizeof(here_size));
    snprintf(buf, sizeof(buf), "total %s  \xc2\xb7  here %s  \xc2\xb7  %d entries", root_size,
             here_size, app->cwd ? (int)app->cwd->n_children : 0);
    ui_set_text(ui->totals, buf);

    snprintf(buf, sizeof(buf),
             "[j/k] move  [enter/l] open  [u/h] up  [s] sort:%s  [r] rescan  [q] quit     "
             "scanned %llu dirs / %llu files / %llu skipped",
             ydu_sort_mode_name(app->sort_mode), (unsigned long long)app->stats.dirs,
             (unsigned long long)app->stats.files, (unsigned long long)app->stats.errors);
    ui_set_text(ui->status, buf);

    build_treemap(app);
    refresh_table(app);
}
