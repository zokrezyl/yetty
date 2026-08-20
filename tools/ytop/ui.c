/*
 * ui.c — ytop presentation layer. Builds a btop-style dashboard from ygui
 * widgets and refreshes them from struct ytop_app's snapshots each tick.
 *
 * Layout is absolute (computed from the viewport in ytop_ui_relayout) so the
 * boxes reflow on resize. Colors follow the yetty brand palette.
 */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ysdf/funcs.gen.h>

/* ------------------------------------------------------------------ */
/* Palette (macros, not file-scope data)                               */
/* ------------------------------------------------------------------ */

#define YTOP_RGBA(r, g, b, a) ((struct yetty_ycore_rgba){(r), (g), (b), (a)})

#define YTOP_COL_BG YTOP_RGBA(11, 16, 20, 255)
#define YTOP_COL_BG_LIFTED YTOP_RGBA(20, 26, 31, 255)
#define YTOP_COL_BORDER YTOP_RGBA(54, 74, 71, 255)
#define YTOP_COL_TEXT YTOP_RGBA(224, 229, 228, 255)
#define YTOP_COL_TEXT_SECONDARY YTOP_RGBA(159, 167, 168, 255)
#define YTOP_COL_TEXT_MUTED YTOP_RGBA(85, 97, 98, 255)
#define YTOP_COL_ACCENT YTOP_RGBA(107, 168, 146, 255)
#define YTOP_COL_ACCENT_BRIGHT YTOP_RGBA(116, 197, 165, 255)
/* Network down/up label colours — green for receive, amber for transmit. */
#define YTOP_COL_NET_RX YTOP_RGBA(116, 197, 165, 255)
#define YTOP_COL_NET_TX YTOP_RGBA(255, 180, 90, 255)

/* ------------------------------------------------------------------ */
/* Widget handle table                                                 */
/* ------------------------------------------------------------------ */

struct ytop_ui {
    struct yetty_yclass_object *title;
    struct yetty_yclass_object *sysinfo;

    /* CPU box */
    struct yetty_yclass_object *cpu_panel;
    struct yetty_yclass_object *cpu_title;
    struct yetty_yclass_object *cpu_model;
    struct yetty_yclass_object *cpu_stat;
    struct yetty_yclass_object *cpu_graph; /* inline sparkline (ydraw_embed) */
    float cpu_graph_w, cpu_graph_h;
    int n_cores;
    struct yetty_yclass_object *core_bars[YTOP_MAX_CORES];
    struct yetty_yclass_object *core_labels[YTOP_MAX_CORES];

    /* Memory box */
    struct yetty_yclass_object *mem_panel;
    struct yetty_yclass_object *mem_title;
    struct yetty_yclass_object *ram_label;
    struct yetty_yclass_object *ram_bar;
    struct yetty_yclass_object *swap_label;
    struct yetty_yclass_object *swap_bar;
    struct yetty_yclass_object *mem_graph;
    float mem_graph_w, mem_graph_h;

    /* Network box */
    struct yetty_yclass_object *net_panel;
    struct yetty_yclass_object *net_title;
    struct yetty_yclass_object *net_down_label;
    struct yetty_yclass_object *net_up_label;
    struct yetty_yclass_object *net_graph;
    float net_graph_w, net_graph_h;

    /* Disk box */
    struct yetty_yclass_object *disk_panel;
    struct yetty_yclass_object *disk_title;
    struct yetty_yclass_object *disk_io_label;
    int n_mounts;
    struct yetty_yclass_object *mount_bars[YTOP_MAX_MOUNTS];
    struct yetty_yclass_object *mount_labels[YTOP_MAX_MOUNTS];

    /* Process box */
    struct yetty_yclass_object *proc_panel;
    struct yetty_yclass_object *proc_title;
    struct yetty_yclass_object *table;
    int table_rows; /* how many rows fit, computed in relayout */
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

/* Progress accent colour is packed low→high as r,g,b,a (0xAABBGGRR). */
static uint32_t accent_u32(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint32_t)255 << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r;
}

/* Green under 50%, amber under 80%, red above — a usage heat colour. */
static uint32_t usage_accent(float pct)
{
    if (pct < 50.0f) {
        return accent_u32(107, 168, 146);
    }
    if (pct < 80.0f) {
        return accent_u32(255, 200, 90);
    }
    return accent_u32(240, 100, 90);
}

static struct yetty_yclass_object *ui_add(struct ytop_app *app,
                                          struct yetty_yclass_ptr_result cls_result)
{
    if (YETTY_IS_ERR(cls_result)) {
        yetty_ycore_error_destroy(cls_result.error);
        return NULL;
    }
    struct yetty_yclass_object_ptr_result result =
        yetty_ygui_widget_add(app->root, cls_result.value);
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

static void ui_set_visible(struct yetty_yclass_object *widget, int visible)
{
    if (widget) {
        absorb(yetty_ygui_widget_set_visible(widget, visible));
    }
}

static void ui_progress(struct yetty_yclass_object *bar, float value, uint32_t accent)
{
    if (!bar) {
        return;
    }
    absorb(yetty_ygui_progress_set_accent(bar, accent));
    absorb(yetty_ygui_progress_set_value(bar, value));
}

static void panel_style(struct yetty_yclass_object *panel)
{
    if (!panel) {
        return;
    }
    absorb(yetty_ygui_panel_set_bg(panel, YTOP_COL_BG_LIFTED));
    absorb(yetty_ygui_panel_set_border(panel, YTOP_COL_BORDER, 1.0f));
}

/* Build a bar-style sparkline from `samples` and hand it to a ydraw_embed
 * widget, which paints it INLINE into the chrome layer (translated to the
 * widget's rect) — so, unlike a yplot figure, it lands exactly where the box
 * is. Bars are drawn in a local (0,0)-(w,h) space, newest sample on the right,
 * height proportional to value/max, bottom-anchored. */
static void build_sparkline(struct yetty_yclass_object *embed, const float *samples, int count,
                            float max_value, float w, float h, uint32_t color)
{
    if (!embed || count <= 0 || w <= 1.0f || h <= 1.0f) {
        return;
    }
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = list_res.value;

    /* Full-area backdrop pins the scene bounds to (0,0)-(w,h) so ydraw_embed
     * always aligns the sparkline's top-left to the widget rect, regardless of
     * how tall the tallest bar is. */
    struct yetty_ysdf_rounded_box backdrop = {
        .center_x = w * 0.5f,
        .center_y = h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
    };
    absorb(yetty_ydraw_drawable_list_add_cmd_add_rounded_box(list, 0u, 0u, accent_u32(16, 22, 27),
                                                             0u, 0.0f, &backdrop));

    float denom = max_value > 0.0f ? max_value : 1.0f;
    float bar_w = w / (float)count;
    for (int i = 0; i < count; i++) {
        float frac = samples[i] / denom;
        if (frac < 0.0f) {
            frac = 0.0f;
        }
        if (frac > 1.0f) {
            frac = 1.0f;
        }
        float bar_h = h * frac;
        if (bar_h < 1.0f) {
            bar_h = 1.0f;
        }
        struct yetty_ysdf_rounded_box bar = {
            .center_x = (float)i * bar_w + bar_w * 0.5f,
            .center_y = h - bar_h * 0.5f,
            .half_width = bar_w * 0.42f,
            .half_height = bar_h * 0.5f,
        };
        absorb(
            yetty_ydraw_drawable_list_add_cmd_add_rounded_box(list, 0u, 0u, color, 0u, 0.0f, &bar));
    }
    /* set_buffer takes ownership of `list` (destroys the previous one). */
    absorb(yetty_ygui_ydraw_embed_set_buffer(embed, list));
}

/* ------------------------------------------------------------------ */
/* Formatting                                                          */
/* ------------------------------------------------------------------ */

static void fmt_bytes(uint64_t bytes, char *out, size_t n)
{
    double value = (double)bytes;
    if (value >= 1024.0 * 1024 * 1024 * 1024) {
        snprintf(out, n, "%.2f TiB", value / (1024.0 * 1024 * 1024 * 1024));
    } else if (value >= 1024.0 * 1024 * 1024) {
        snprintf(out, n, "%.2f GiB", value / (1024.0 * 1024 * 1024));
    } else if (value >= 1024.0 * 1024) {
        snprintf(out, n, "%.1f MiB", value / (1024.0 * 1024));
    } else if (value >= 1024.0) {
        snprintf(out, n, "%.1f KiB", value / 1024.0);
    } else {
        snprintf(out, n, "%.0f B", value);
    }
}

static void fmt_rate(double bytes_per_sec, char *out, size_t n)
{
    double value = bytes_per_sec;
    if (value >= 1024.0 * 1024 * 1024) {
        snprintf(out, n, "%.2f GiB/s", value / (1024.0 * 1024 * 1024));
    } else if (value >= 1024.0 * 1024) {
        snprintf(out, n, "%.1f MiB/s", value / (1024.0 * 1024));
    } else if (value >= 1024.0) {
        snprintf(out, n, "%.1f KiB/s", value / 1024.0);
    } else {
        snprintf(out, n, "%.0f B/s", value);
    }
}

static void fmt_uptime(uint64_t sec, char *out, size_t n)
{
    uint64_t days = sec / 86400;
    sec %= 86400;
    uint64_t hours = sec / 3600;
    sec %= 3600;
    uint64_t mins = sec / 60;
    if (days > 0) {
        snprintf(out, n, "%lud %02lu:%02lu", (unsigned long)days, (unsigned long)hours,
                 (unsigned long)mins);
    } else {
        snprintf(out, n, "%02lu:%02lu", (unsigned long)hours, (unsigned long)mins);
    }
}

static void fmt_cputime(uint64_t sec, char *out, size_t n)
{
    uint64_t hours = sec / 3600;
    uint64_t mins = (sec % 3600) / 60;
    uint64_t secs = sec % 60;
    snprintf(out, n, "%lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)mins,
             (unsigned long)secs);
}

/* ------------------------------------------------------------------ */
/* Process sorting                                                     */
/* ------------------------------------------------------------------ */

static int cmp_cpu(const void *a, const void *b)
{
    const struct ytop_proc_entry *pa = a, *pb = b;
    if (pb->cpu_pct != pa->cpu_pct) {
        return (pb->cpu_pct > pa->cpu_pct) ? 1 : -1;
    }
    return (pb->rss_bytes > pa->rss_bytes) ? 1 : (pb->rss_bytes < pa->rss_bytes ? -1 : 0);
}

static int cmp_mem(const void *a, const void *b)
{
    const struct ytop_proc_entry *pa = a, *pb = b;
    if (pb->rss_bytes != pa->rss_bytes) {
        return (pb->rss_bytes > pa->rss_bytes) ? 1 : -1;
    }
    return (pb->cpu_pct > pa->cpu_pct) ? 1 : (pb->cpu_pct < pa->cpu_pct ? -1 : 0);
}

static int cmp_pid(const void *a, const void *b)
{
    const struct ytop_proc_entry *pa = a, *pb = b;
    return pa->pid - pb->pid;
}

static int cmp_name(const void *a, const void *b)
{
    const struct ytop_proc_entry *pa = a, *pb = b;
    int c = strcmp(pa->comm, pb->comm);
    if (c != 0) {
        return c;
    }
    return pa->pid - pb->pid;
}

static const char *sort_mode_name(enum ytop_sort_mode mode)
{
    switch (mode) {
    case YTOP_SORT_CPU:
        return "%CPU";
    case YTOP_SORT_MEM:
        return "MEM";
    case YTOP_SORT_PID:
        return "PID";
    case YTOP_SORT_NAME:
        return "NAME";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

struct yetty_ycore_void_result ytop_ui_build(struct ytop_app *app)
{
    if (!app || !app->root) {
        return YETTY_ERR(yetty_ycore_void, "ytop_ui_build: no root");
    }
    struct ytop_ui *ui = calloc(1, sizeof(*ui));
    if (!ui) {
        return YETTY_ERR(yetty_ycore_void, "ytop_ui_build: calloc");
    }
    app->ui = ui;

    ui->n_cores = app->cpu.n_cores;
    if (ui->n_cores > YTOP_MAX_CORES) {
        ui->n_cores = YTOP_MAX_CORES;
    }
    ui->n_mounts = app->disk.n_mounts;
    if (ui->n_mounts > YTOP_MAX_MOUNTS) {
        ui->n_mounts = YTOP_MAX_MOUNTS;
    }

    /* Header. */
    ui->title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->title, "ytop");
    ui_set_color(ui->title, YTOP_COL_ACCENT_BRIGHT);
    ui_set_font(ui->title, 16.0f);
    ui->sysinfo = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->sysinfo, YTOP_COL_TEXT_SECONDARY);
    ui_set_font(ui->sysinfo, 12.0f);

    /* CPU box. */
    ui->cpu_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->cpu_panel);
    ui->cpu_title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->cpu_title, "CPU");
    ui_set_color(ui->cpu_title, YTOP_COL_ACCENT);
    ui_set_font(ui->cpu_title, 13.0f);
    ui->cpu_model = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->cpu_model, YTOP_COL_TEXT_MUTED);
    ui_set_font(ui->cpu_model, 11.0f);
    ui->cpu_stat = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->cpu_stat, YTOP_COL_TEXT);
    ui_set_font(ui->cpu_stat, 12.0f);
    ui->cpu_graph = ui_add(app, yetty_ygui_ydraw_embed_class_get());
    for (int i = 0; i < ui->n_cores; i++) {
        ui->core_bars[i] = ui_add(app, yetty_ygui_progress_class_get());
        ui->core_labels[i] = ui_add(app, yetty_ygui_label_class_get());
        ui_set_color(ui->core_labels[i], YTOP_COL_TEXT_SECONDARY);
        ui_set_font(ui->core_labels[i], 11.0f);
    }

    /* Memory box. */
    ui->mem_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->mem_panel);
    ui->mem_title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->mem_title, "MEMORY");
    ui_set_color(ui->mem_title, YTOP_COL_ACCENT);
    ui_set_font(ui->mem_title, 13.0f);
    ui->ram_label = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->ram_label, YTOP_COL_TEXT);
    ui_set_font(ui->ram_label, 12.0f);
    ui->ram_bar = ui_add(app, yetty_ygui_progress_class_get());
    ui->swap_label = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->swap_label, YTOP_COL_TEXT);
    ui_set_font(ui->swap_label, 12.0f);
    ui->swap_bar = ui_add(app, yetty_ygui_progress_class_get());
    ui->mem_graph = ui_add(app, yetty_ygui_ydraw_embed_class_get());

    /* Network box. */
    ui->net_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->net_panel);
    ui->net_title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->net_title, "NETWORK");
    ui_set_color(ui->net_title, YTOP_COL_ACCENT);
    ui_set_font(ui->net_title, 13.0f);
    ui->net_down_label = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->net_down_label, YTOP_COL_NET_RX);
    ui_set_font(ui->net_down_label, 12.0f);
    ui->net_up_label = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->net_up_label, YTOP_COL_NET_TX);
    ui_set_font(ui->net_up_label, 12.0f);
    ui->net_graph = ui_add(app, yetty_ygui_ydraw_embed_class_get());

    /* Disk box. */
    ui->disk_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->disk_panel);
    ui->disk_title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->disk_title, "DISK");
    ui_set_color(ui->disk_title, YTOP_COL_ACCENT);
    ui_set_font(ui->disk_title, 13.0f);
    ui->disk_io_label = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->disk_io_label, YTOP_COL_TEXT_SECONDARY);
    ui_set_font(ui->disk_io_label, 11.0f);
    for (int i = 0; i < ui->n_mounts; i++) {
        ui->mount_labels[i] = ui_add(app, yetty_ygui_label_class_get());
        ui_set_color(ui->mount_labels[i], YTOP_COL_TEXT);
        ui_set_font(ui->mount_labels[i], 11.0f);
        ui->mount_bars[i] = ui_add(app, yetty_ygui_progress_class_get());
    }

    /* Process box. */
    ui->proc_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->proc_panel);
    ui->proc_title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->proc_title, YTOP_COL_ACCENT);
    ui_set_font(ui->proc_title, 13.0f);
    ui->table = ui_add(app, yetty_ygui_table_class_get());
    if (ui->table) {
        static const char *const columns[] = {"PID",   "USER", "%CPU",  "%MEM",
                                              "STATE", "THR",  "TIME+", "COMMAND"};
        absorb(yetty_ygui_table_set_columns(ui->table, (int)(sizeof(columns) / sizeof(columns[0])),
                                            columns));
    }

    ytop_ui_relayout(app);
    return YETTY_OK_VOID();
}

void ytop_ui_free(struct ytop_app *app)
{
    if (app && app->ui) {
        free(app->ui);
        app->ui = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define YTOP_MARGIN 10.0f
#define YTOP_PAD 8.0f
#define YTOP_GAP 8.0f
#define YTOP_LINE 16.0f
#define YTOP_BAR 14.0f
#define YTOP_TITLE 18.0f

void ytop_ui_relayout(struct ytop_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    struct ytop_ui *ui = app->ui;

    float vw = 0.0f, vh = 0.0f;
    yetty_ygui_framework_viewport(app->engine, &vw, &vh);
    if (vw < 480.0f) {
        vw = 1280.0f;
    }
    if (vh < 320.0f) {
        vh = 800.0f;
    }

    float x = YTOP_MARGIN;
    float y = YTOP_MARGIN;
    float full_w = vw - 2.0f * YTOP_MARGIN;

    /* Header strip. */
    ui_place(ui->title, x, y, 200.0f, YTOP_TITLE);
    ui_place(ui->sysinfo, x + 200.0f, y + 2.0f, full_w - 200.0f, YTOP_TITLE);
    y += YTOP_TITLE + 6.0f;

    /* -------- CPU box -------- */
    /* The per-core bar grid spans the full box width. */
    float core_cell_w = 118.0f;
    float core_area_w = full_w - 2.0f * YTOP_PAD;
    int cores_per_row = (int)(core_area_w / core_cell_w);
    if (cores_per_row < 1) {
        cores_per_row = 1;
    }
    int core_rows = (ui->n_cores + cores_per_row - 1) / cores_per_row;
    if (core_rows < 1) {
        core_rows = 1;
    }
    float grid_h = (float)core_rows * (YTOP_BAR + 4.0f);
    float cpu_spark_h = 40.0f; /* full-width CPU history strip below the cores */
    float cpu_h =
        YTOP_PAD + YTOP_TITLE + 2.0f + YTOP_LINE + 4.0f + grid_h + 4.0f + cpu_spark_h + YTOP_PAD;

    ui_place(ui->cpu_panel, x, y, full_w, cpu_h);
    ui_place(ui->cpu_title, x + YTOP_PAD, y + YTOP_PAD, 44.0f, YTOP_TITLE);
    ui_place(ui->cpu_model, x + YTOP_PAD + 46.0f, y + YTOP_PAD + 2.0f, full_w * 0.5f, YTOP_TITLE);
    ui_place(ui->cpu_stat, x + YTOP_PAD, y + YTOP_PAD + YTOP_TITLE + 2.0f, full_w - 2.0f * YTOP_PAD,
             YTOP_LINE);

    /* Distribute the cells evenly across the full width; the bar takes the cell
     * minus room for the "N  ppp%" label. */
    float cell_w = core_area_w / (float)cores_per_row;
    float bar_w = cell_w - 52.0f;
    if (bar_w < 40.0f) {
        bar_w = 40.0f;
    }
    float grid_x = x + YTOP_PAD;
    float grid_y = y + YTOP_PAD + YTOP_TITLE + 2.0f + YTOP_LINE + 4.0f;
    for (int i = 0; i < ui->n_cores; i++) {
        int row = i / cores_per_row;
        int col = i % cores_per_row;
        float cx = grid_x + (float)col * cell_w;
        float cy = grid_y + (float)row * (YTOP_BAR + 4.0f);
        ui_place(ui->core_bars[i], cx, cy, bar_w, YTOP_BAR);
        ui_place(ui->core_labels[i], cx + bar_w + 4.0f, cy, cell_w - bar_w - 8.0f, YTOP_BAR);
    }

    /* CPU history strip spanning the full box width, below the core grid. */
    float cpu_spark_w = full_w - 2.0f * YTOP_PAD;
    ui_place(ui->cpu_graph, grid_x, grid_y + grid_h + 4.0f, cpu_spark_w, cpu_spark_h);
    ui->cpu_graph_w = cpu_spark_w;
    ui->cpu_graph_h = cpu_spark_h;
    y += cpu_h + YTOP_GAP;

    /* -------- Middle row: MEM | NET | DISK -------- */
    float col_w = (full_w - 2.0f * YTOP_GAP) / 3.0f;
    float mid_h = 172.0f;
    float mem_x = x;
    float net_x = x + col_w + YTOP_GAP;
    float disk_x = x + 2.0f * (col_w + YTOP_GAP);

    /* Memory. */
    ui_place(ui->mem_panel, mem_x, y, col_w, mid_h);
    ui_place(ui->mem_title, mem_x + YTOP_PAD, y + YTOP_PAD, col_w - 2.0f * YTOP_PAD, YTOP_TITLE);
    float my = y + YTOP_PAD + YTOP_TITLE + 4.0f;
    float inner_w = col_w - 2.0f * YTOP_PAD;
    ui_place(ui->ram_label, mem_x + YTOP_PAD, my, inner_w, YTOP_LINE);
    my += YTOP_LINE;
    ui_place(ui->ram_bar, mem_x + YTOP_PAD, my, inner_w, YTOP_BAR);
    my += YTOP_BAR + 4.0f;
    ui_place(ui->swap_label, mem_x + YTOP_PAD, my, inner_w, YTOP_LINE);
    my += YTOP_LINE;
    ui_place(ui->swap_bar, mem_x + YTOP_PAD, my, inner_w, YTOP_BAR);
    my += YTOP_BAR + 6.0f;
    float mem_spark_h = y + mid_h - my - YTOP_PAD;
    ui_place(ui->mem_graph, mem_x + YTOP_PAD, my, inner_w, mem_spark_h);
    ui->mem_graph_w = inner_w;
    ui->mem_graph_h = mem_spark_h;

    /* Network. */
    ui_place(ui->net_panel, net_x, y, col_w, mid_h);
    ui_place(ui->net_title, net_x + YTOP_PAD, y + YTOP_PAD, col_w - 2.0f * YTOP_PAD, YTOP_TITLE);
    float ny = y + YTOP_PAD + YTOP_TITLE + 4.0f;
    ui_place(ui->net_down_label, net_x + YTOP_PAD, ny, inner_w, YTOP_LINE);
    ny += YTOP_LINE + 2.0f;
    ui_place(ui->net_up_label, net_x + YTOP_PAD, ny, inner_w, YTOP_LINE);
    ny += YTOP_LINE + 6.0f;
    float net_spark_h = y + mid_h - ny - YTOP_PAD;
    ui_place(ui->net_graph, net_x + YTOP_PAD, ny, inner_w, net_spark_h);
    ui->net_graph_w = inner_w;
    ui->net_graph_h = net_spark_h;

    /* Disk. */
    ui_place(ui->disk_panel, disk_x, y, col_w, mid_h);
    ui_place(ui->disk_title, disk_x + YTOP_PAD, y + YTOP_PAD, col_w - 2.0f * YTOP_PAD, YTOP_TITLE);
    float dy = y + YTOP_PAD + YTOP_TITLE + 4.0f;
    ui_place(ui->disk_io_label, disk_x + YTOP_PAD, dy, inner_w, YTOP_LINE);
    dy += YTOP_LINE + 4.0f;
    float mount_row_h = YTOP_LINE + YTOP_BAR + 4.0f;
    for (int i = 0; i < ui->n_mounts; i++) {
        int fits = (dy + mount_row_h) <= (y + mid_h - YTOP_PAD);
        ui_set_visible(ui->mount_labels[i], fits);
        ui_set_visible(ui->mount_bars[i], fits);
        if (!fits) {
            continue;
        }
        ui_place(ui->mount_labels[i], disk_x + YTOP_PAD, dy, inner_w, YTOP_LINE);
        ui_place(ui->mount_bars[i], disk_x + YTOP_PAD, dy + YTOP_LINE, inner_w, YTOP_BAR);
        dy += mount_row_h;
    }
    y += mid_h + YTOP_GAP;

    /* -------- Process box (fills the rest) -------- */
    float proc_h = vh - y - YTOP_MARGIN;
    if (proc_h < 120.0f) {
        proc_h = 120.0f;
    }
    ui_place(ui->proc_panel, x, y, full_w, proc_h);
    ui_place(ui->proc_title, x + YTOP_PAD, y + YTOP_PAD, full_w - 2.0f * YTOP_PAD, YTOP_TITLE);
    float table_y = y + YTOP_PAD + YTOP_TITLE + 2.0f;
    float table_h = proc_h - (YTOP_PAD + YTOP_TITLE + 2.0f) - YTOP_PAD;
    ui_place(ui->table, x + YTOP_PAD, table_y, full_w - 2.0f * YTOP_PAD, table_h);

    /* One header row plus data rows at ~20px each. */
    int rows = (int)((table_h - 22.0f) / 20.0f);
    if (rows < 4) {
        rows = 4;
    }
    if (rows > YTOP_MAX_PROCS) {
        rows = YTOP_MAX_PROCS;
    }
    ui->table_rows = rows;
}

/* ------------------------------------------------------------------ */
/* Refresh                                                             */
/* ------------------------------------------------------------------ */

static void refresh_cpu(struct ytop_app *app)
{
    struct ytop_ui *ui = app->ui;
    char buf[256];

    if (app->cpu.model[0]) {
        ui_set_text(ui->cpu_model, app->cpu.model);
    }

    /* Peak core frequency for the header. */
    uint64_t max_freq = 0;
    for (int i = 0; i < app->cpu.n_cores; i++) {
        if (app->cpu.cores[i].freq_khz > max_freq) {
            max_freq = app->cpu.cores[i].freq_khz;
        }
    }
    char temp_buf[32];
    if (app->cpu.temp_celsius >= 0.0f) {
        snprintf(temp_buf, sizeof(temp_buf), "%.0f C", app->cpu.temp_celsius);
    } else {
        snprintf(temp_buf, sizeof(temp_buf), "n/a");
    }
    snprintf(buf, sizeof(buf), "%5.1f%%   load %.2f %.2f %.2f   %.2f GHz   %s", app->cpu.total_pct,
             app->cpu.load_avg[0], app->cpu.load_avg[1], app->cpu.load_avg[2],
             (double)max_freq / 1e6, temp_buf);
    ui_set_text(ui->cpu_stat, buf);

    int n = ui->n_cores < app->cpu.n_cores ? ui->n_cores : app->cpu.n_cores;
    for (int i = 0; i < n; i++) {
        float pct = app->cpu.cores[i].usage_pct;
        ui_progress(ui->core_bars[i], pct / 100.0f, usage_accent(pct));
        char label[32];
        snprintf(label, sizeof(label), "%d %3.0f%%", i, pct);
        ui_set_text(ui->core_labels[i], label);
    }

    float samples[YTOP_HISTORY_CAP];
    int count = ytop_history_linearize(&app->cpu_hist, samples, YTOP_HISTORY_CAP);
    build_sparkline(ui->cpu_graph, samples, count, 100.0f, ui->cpu_graph_w, ui->cpu_graph_h,
                    accent_u32(107, 168, 146));
}

static void refresh_mem(struct ytop_app *app)
{
    struct ytop_ui *ui = app->ui;
    char buf[128], used[32], total[32];

    float ram_frac =
        app->mem.ram_total ? (float)app->mem.ram_used / (float)app->mem.ram_total : 0.0f;
    fmt_bytes(app->mem.ram_used, used, sizeof(used));
    fmt_bytes(app->mem.ram_total, total, sizeof(total));
    snprintf(buf, sizeof(buf), "RAM  %s / %s  (%.0f%%)", used, total, ram_frac * 100.0f);
    ui_set_text(ui->ram_label, buf);
    ui_progress(ui->ram_bar, ram_frac, usage_accent(ram_frac * 100.0f));

    float swap_frac =
        app->mem.swap_total ? (float)app->mem.swap_used / (float)app->mem.swap_total : 0.0f;
    fmt_bytes(app->mem.swap_used, used, sizeof(used));
    fmt_bytes(app->mem.swap_total, total, sizeof(total));
    snprintf(buf, sizeof(buf), "swap %s / %s  (%.0f%%)", used, total, swap_frac * 100.0f);
    ui_set_text(ui->swap_label, buf);
    ui_progress(ui->swap_bar, swap_frac, usage_accent(swap_frac * 100.0f));

    float samples[YTOP_HISTORY_CAP];
    int count = ytop_history_linearize(&app->mem_hist, samples, YTOP_HISTORY_CAP);
    build_sparkline(ui->mem_graph, samples, count, 100.0f, ui->mem_graph_w, ui->mem_graph_h,
                    accent_u32(120, 150, 230));
}

static void refresh_net(struct ytop_app *app)
{
    struct ytop_ui *ui = app->ui;
    char buf[128], rate[32];

    fmt_rate(app->net.total_rx_rate, rate, sizeof(rate));
    snprintf(buf, sizeof(buf), "down  %s", rate);
    ui_set_text(ui->net_down_label, buf);
    fmt_rate(app->net.total_tx_rate, rate, sizeof(rate));
    snprintf(buf, sizeof(buf), "up    %s", rate);
    ui_set_text(ui->net_up_label, buf);

    /* Sparkline of receive throughput, auto-scaled to its own peak. */
    float samples[YTOP_HISTORY_CAP];
    int count = ytop_history_linearize(&app->net_rx_hist, samples, YTOP_HISTORY_CAP);
    float peak = 1.0f;
    for (int i = 0; i < count; i++) {
        if (samples[i] > peak) {
            peak = samples[i];
        }
    }
    build_sparkline(ui->net_graph, samples, count, peak, ui->net_graph_w, ui->net_graph_h,
                    accent_u32(116, 197, 165));
}

static void refresh_disk(struct ytop_app *app)
{
    struct ytop_ui *ui = app->ui;
    char buf[128], read_rate[32], write_rate[32];

    fmt_rate(app->disk.io_read_rate, read_rate, sizeof(read_rate));
    fmt_rate(app->disk.io_write_rate, write_rate, sizeof(write_rate));
    snprintf(buf, sizeof(buf), "read %s   write %s", read_rate, write_rate);
    ui_set_text(ui->disk_io_label, buf);

    int n = ui->n_mounts < app->disk.n_mounts ? ui->n_mounts : app->disk.n_mounts;
    for (int i = 0; i < n; i++) {
        const struct ytop_disk_mount *mount = &app->disk.mounts[i];
        char used[32], total[32];
        fmt_bytes(mount->used_bytes, used, sizeof(used));
        fmt_bytes(mount->total_bytes, total, sizeof(total));
        snprintf(buf, sizeof(buf), "%s  %.0f%%  %s/%s", mount->mount_point, mount->used_pct, used,
                 total);
        ui_set_text(ui->mount_labels[i], buf);
        ui_progress(ui->mount_bars[i], mount->used_pct / 100.0f, usage_accent(mount->used_pct));
    }
}

static void refresh_procs(struct ytop_app *app)
{
    struct ytop_ui *ui = app->ui;
    char buf[128];

    int (*cmp)(const void *, const void *) = cmp_cpu;
    switch (app->sort_mode) {
    case YTOP_SORT_CPU:
        cmp = cmp_cpu;
        break;
    case YTOP_SORT_MEM:
        cmp = cmp_mem;
        break;
    case YTOP_SORT_PID:
        cmp = cmp_pid;
        break;
    case YTOP_SORT_NAME:
        cmp = cmp_name;
        break;
    }
    if (app->n_procs > 0) {
        qsort(app->procs, (size_t)app->n_procs, sizeof(app->procs[0]), cmp);
    }

    snprintf(buf, sizeof(buf), "PROCESSES  %d total   sort:%s   [c]pu [m]em [p]id [n]ame  [q]uit",
             app->n_procs, sort_mode_name(app->sort_mode));
    ui_set_text(ui->proc_title, buf);

    absorb(yetty_ygui_table_clear_rows(ui->table));
    int rows = app->n_procs < ui->table_rows ? app->n_procs : ui->table_rows;
    for (int i = 0; i < rows; i++) {
        const struct ytop_proc_entry *entry = &app->procs[i];
        char pid_s[16], cpu_s[12], mem_s[12], state_s[4], thr_s[12], time_s[24];
        snprintf(pid_s, sizeof(pid_s), "%d", entry->pid);
        snprintf(cpu_s, sizeof(cpu_s), "%.1f", entry->cpu_pct);
        snprintf(mem_s, sizeof(mem_s), "%.1f", entry->mem_pct);
        state_s[0] = entry->state ? entry->state : '?';
        state_s[1] = '\0';
        snprintf(thr_s, sizeof(thr_s), "%d", entry->num_threads);
        fmt_cputime(entry->cpu_time_sec, time_s, sizeof(time_s));
        const char *command = entry->cmdline[0] ? entry->cmdline : entry->comm;
        const char *cells[] = {
            pid_s,   entry->user[0] ? entry->user : "?", cpu_s, mem_s, state_s, thr_s, time_s,
            command,
        };
        absorb(yetty_ygui_table_add_row(ui->table, cells, 8));
    }
}

void ytop_ui_refresh(struct ytop_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    char buf[192];
    char uptime[32];
    fmt_uptime(app->sys.uptime_sec, uptime, sizeof(uptime));
    snprintf(buf, sizeof(buf), "%s  |  %s  |  %s  |  up %s", app->sys.hostname,
             app->sys.os_name[0] ? app->sys.os_name : "Linux", app->sys.kernel, uptime);
    ui_set_text(app->ui->sysinfo, buf);

    refresh_cpu(app);
    refresh_mem(app);
    refresh_net(app);
    refresh_disk(app);
    refresh_procs(app);
}
