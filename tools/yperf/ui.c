/*
 * ui.c — yperf presentation layer.
 *
 * The flame graph is produced by the yflame class: configure(width, row height,
 * flags) → parse(folded) → render() yields a ydraw drawable list that is handed
 * to a ygui ydraw_embed widget. Below it, a sortable top-symbol table (self /
 * total sample counts) is populated from the profile model; an optional sample
 * timeline strip sits above the flame for timestamped (perf) captures. Layout is
 * absolute and recomputed in yperf_ui_relayout so it reflows on resize.
 *
 * The flame is expensive to reparse but cheap to re-emit, so two render paths
 * exist: yperf_ui_refresh rebuilds it (configure + parse), while
 * yperf_ui_render_flame only re-emits with the current focus / hover / highlight
 * — the path used for cross-highlight, search, hover, and mouse zoom.
 */
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yflame/flame.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ysdf/funcs.gen.h> /* yetty_ydraw_drawable_list_add_cmd_add_box */
#include <yetty/ysdf/types.gen.h>
#include <yetty/yterminal/client-input.h> /* mouse-kind enum */

/* ------------------------------------------------------------------ */
/* Palette (macros, not file-scope data)                               */
/* ------------------------------------------------------------------ */

#define YPERF_RGBA(r, g, b, a) ((struct yetty_ycore_rgba){(r), (g), (b), (a)})

#define YPERF_COL_BG_LIFTED YPERF_RGBA(20, 26, 31, 255)
#define YPERF_COL_BORDER YPERF_RGBA(54, 74, 71, 255)
#define YPERF_COL_TEXT YPERF_RGBA(224, 229, 228, 255)
#define YPERF_COL_TEXT_SECONDARY YPERF_RGBA(159, 167, 168, 255)
#define YPERF_COL_TEXT_MUTED YPERF_RGBA(85, 97, 98, 255)
#define YPERF_COL_ACCENT YPERF_RGBA(107, 168, 146, 255)
#define YPERF_COL_ACCENT_BRIGHT YPERF_RGBA(116, 197, 165, 255)

/* BRAND_ACCENT #6BA892 as an ABGR fill for the ydraw timeline bars. */
#define YPERF_BAR_FILL 0xFF92A86Bu

/* ------------------------------------------------------------------ */
/* Widget handle table                                                 */
/* ------------------------------------------------------------------ */

struct yperf_ui {
    struct yetty_yclass_object *title;
    struct yetty_yclass_object *source;
    struct yetty_yclass_object *totals;
    struct yetty_yclass_object *status;

    struct yetty_yclass_object *timeline_panel;
    struct yetty_yclass_object *timeline_embed;
    float timeline_w, timeline_h;

    struct yetty_yclass_object *flame_panel;
    struct yetty_yclass_object *flame_embed; /* ydraw_embed canvas */
    float flame_x, flame_y;                  /* absolute origin of the embed */
    float flame_w, flame_h;

    struct yetty_yclass_object *table_panel;
    struct yetty_yclass_object *table;
    int table_rows;

    int32_t hover_id; /* flame node under the mouse, or -1 */
    char detail[256]; /* hover detail string for the status line */
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

static struct yetty_yclass_object *ui_add(struct yperf_app *app,
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
    absorb(yetty_ygui_panel_set_bg(panel, YPERF_COL_BG_LIFTED));
    absorb(yetty_ygui_panel_set_border(panel, YPERF_COL_BORDER, 1.0f));
}

/* ------------------------------------------------------------------ */
/* Flame highlight state                                               */
/* ------------------------------------------------------------------ */

/* The substring to highlight in the flame: an active search query, else the
 * selected symbol (so the table row and its flame frames stay in sync). */
static size_t current_highlight(struct yperf_app *app, char *out, size_t out_size)
{
    if (app->search_len > 0) {
        size_t n = app->search_len < out_size - 1 ? app->search_len : out_size - 1;
        memcpy(out, app->search, n);
        out[n] = '\0';
        return n;
    }
    if (app->profile && app->profile->n_symbols > 0 && app->selected >= 0 &&
        (size_t)app->selected < app->profile->n_symbols) {
        const char *name = app->profile->symbols[app->selected].name;
        size_t n = strlen(name);
        if (n > out_size - 1) {
            n = out_size - 1;
        }
        memcpy(out, name, n);
        out[n] = '\0';
        return n;
    }
    out[0] = '\0';
    return 0;
}

static void apply_highlight(struct yperf_app *app)
{
    char highlight[256];
    size_t n = current_highlight(app, highlight, sizeof(highlight));
    absorb(yetty_yflame_highlight_name(app->flame, n ? highlight : NULL, n));
}

/* ------------------------------------------------------------------ */
/* Flame graph (rendered by yflame into the ydraw_embed canvas)        */
/* ------------------------------------------------------------------ */

/* Full rebuild: reconfigure geometry, reparse the (possibly filtered) folded
 * text, re-apply the diff baseline (parse clears it) and highlight, emit. */
static void build_flame(struct yperf_app *app)
{
    struct yperf_ui *ui = app->ui;
    if (!ui->flame_embed || !app->flame || !app->profile) {
        return;
    }
    float width = ui->flame_w;
    float height = ui->flame_h;
    if (width < 2.0f || height < 2.0f) {
        return;
    }

    /* Fit the whole stack depth into the pane: derive the row height from the
     * deepest stack, clamped to a legible range. */
    uint32_t depth = app->profile->max_depth ? app->profile->max_depth : 1;
    float frame_height = (height - 24.0f) / (float)(depth + 1);
    if (frame_height < 6.0f) {
        frame_height = 6.0f;
    }
    if (frame_height > 26.0f) {
        frame_height = 26.0f;
    }

    uint32_t flags = YETTY_YFLAME_FLAG_LABELS;
    if (app->icicle) {
        flags |= YETTY_YFLAME_FLAG_ICICLE;
    }
    absorb(yetty_yflame_configure(app->flame, width, frame_height, 0.0f, flags));
    absorb(yetty_yflame_parse(app->flame, app->profile->folded, app->profile->folded_len));
    if (app->baseline_folded) {
        absorb(
            yetty_yflame_set_baseline(app->flame, app->baseline_folded, app->baseline_folded_len));
    }
    apply_highlight(app);

    struct yetty_ydraw_drawable_list_result list_res = yetty_yflame_render(app->flame);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return;
    }
    /* set_buffer takes ownership and frees the previously held list. */
    absorb(yetty_ygui_ydraw_embed_set_buffer(ui->flame_embed, list_res.value));
}

void yperf_ui_render_flame(struct yperf_app *app)
{
    if (!app || !app->ui || !app->ui->flame_embed || !app->flame) {
        return;
    }
    apply_highlight(app);
    struct yetty_ydraw_drawable_list_result list_res = yetty_yflame_render(app->flame);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return;
    }
    absorb(yetty_ygui_ydraw_embed_set_buffer(app->ui->flame_embed, list_res.value));
}

/* ------------------------------------------------------------------ */
/* Sample timeline strip (perf captures only)                          */
/* ------------------------------------------------------------------ */

static void build_timeline(struct yperf_app *app)
{
    struct yperf_ui *ui = app->ui;
    if (!ui->timeline_embed || !app->profile || app->profile->timeline_n == 0) {
        return;
    }
    float width = ui->timeline_w;
    float height = ui->timeline_h;
    if (width < 4.0f || height < 4.0f) {
        return;
    }
    struct yperf_profile *profile = app->profile;

    struct yetty_ydraw_drawable_list_config config = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = width,
        .scene_max_y = height,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&config);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return;
    }
    struct yetty_ydraw_drawable_list *buf = list_res.value;

    size_t n = profile->timeline_n;
    float slot = width / (float)n;
    float bar_width = slot > 2.0f ? slot - 1.0f : slot;
    double peak = profile->timeline_peak ? (double)profile->timeline_peak : 1.0;
    uint32_t z = 0;
    for (size_t i = 0; i < n; i++) {
        float frac = (float)((double)profile->timeline[i] / peak);
        float bar_height = frac * (height - 2.0f);
        if (profile->timeline[i] > 0 && bar_height < 1.5f) {
            bar_height = 1.5f;
        }
        if (bar_height <= 0.0f) {
            continue;
        }
        struct yetty_ysdf_box geom = {
            .center_x = (float)i * slot + slot * 0.5f,
            .center_y = height - bar_height * 0.5f,
            .half_width = bar_width * 0.5f,
            .half_height = bar_height * 0.5f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result box = yetty_ydraw_drawable_list_add_cmd_add_box(
            buf, /*id=*/0, /*z_order=*/z++, YPERF_BAR_FILL, /*stroke=*/0u, /*stroke_width=*/0.0f,
            &geom);
        if (YETTY_IS_ERR(box)) {
            yetty_ycore_error_destroy(box.error);
            yetty_ydraw_drawable_list_destroy(buf);
            return;
        }
    }
    absorb(yetty_ygui_ydraw_embed_set_buffer(ui->timeline_embed, buf));
}

/* ------------------------------------------------------------------ */
/* Hover detail                                                        */
/* ------------------------------------------------------------------ */

static void set_hover_detail(struct yperf_app *app, int32_t id)
{
    struct yperf_ui *ui = app->ui;
    ui->detail[0] = '\0';
    if (id < 0) {
        return;
    }
    struct yetty_ycore_const_char_ptr_result name_res = yetty_yflame_node_name(app->flame, id);
    struct yetty_ycore_uint64_result value_res = yetty_yflame_node_value(app->flame, id);
    struct yetty_ycore_uint64_result root_res = yetty_yflame_root_value(app->flame);
    const char *name = YETTY_IS_OK(name_res) ? name_res.value : "";
    if (YETTY_IS_ERR(name_res)) {
        yetty_ycore_error_destroy(name_res.error);
    }
    uint64_t value = YETTY_IS_OK(value_res) ? value_res.value : 0;
    if (YETTY_IS_ERR(value_res)) {
        yetty_ycore_error_destroy(value_res.error);
    }
    uint64_t root = YETTY_IS_OK(root_res) ? root_res.value : 0;
    if (YETTY_IS_ERR(root_res)) {
        yetty_ycore_error_destroy(root_res.error);
    }
    char count[24];
    yperf_fmt_count(value, count, sizeof(count));
    double pct = root ? 100.0 * (double)value / (double)root : 0.0;
    snprintf(ui->detail, sizeof(ui->detail), "%s  \xc2\xb7  %s samples  \xc2\xb7  %.1f%%", name,
             count, pct);
}

/* ------------------------------------------------------------------ */
/* Mouse routing to the flame                                          */
/* ------------------------------------------------------------------ */

static int32_t flame_hit(struct yperf_app *app, float flame_x, float flame_y)
{
    struct yetty_ycore_int_result hit = yetty_yflame_hit_test(app->flame, flame_x, flame_y);
    if (YETTY_IS_ERR(hit)) {
        yetty_ycore_error_destroy(hit.error);
        return -1;
    }
    return (int32_t)hit.value;
}

void yperf_ui_flame_mouse(struct yperf_app *app, uint32_t kind, float pane_x, float pane_y,
                          int button, int pressed, float wheel_dy)
{
    if (!app || !app->ui || !app->flame) {
        return;
    }
    struct yperf_ui *ui = app->ui;
    float flame_x = pane_x - ui->flame_x;
    float flame_y = pane_y - ui->flame_y;
    int inside =
        (flame_x >= 0.0f && flame_x < ui->flame_w && flame_y >= 0.0f && flame_y < ui->flame_h);

    if (kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
        int32_t id = inside ? flame_hit(app, flame_x, flame_y) : -1;
        int32_t highlight = (id >= 0) ? id : -1; /* nav-button sentinels are not frames */
        if (highlight != ui->hover_id) {
            ui->hover_id = highlight;
            absorb(yetty_yflame_set_highlight(app->flame, highlight));
            set_hover_detail(app, highlight);
            yperf_ui_render_flame(app);
            yperf_ui_refresh_table(app); /* refreshes the status line with the detail */
        }
        return;
    }
    if (!inside) {
        return;
    }
    int32_t id = flame_hit(app, flame_x, flame_y);
    if (kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && pressed) {
        if (button == 0) {
            if (id == YETTY_YFLAME_HIT_UP) {
                absorb(yetty_yflame_focus_parent(app->flame));
            } else if (id == YETTY_YFLAME_HIT_ROOT) {
                absorb(yetty_yflame_reset(app->flame));
            } else if (id >= 0) {
                absorb(yetty_yflame_focus(app->flame, id));
            } else {
                return;
            }
        } else {
            absorb(yetty_yflame_focus_parent(app->flame));
        }
        yperf_ui_render_flame(app);
    } else if (kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
        if (wheel_dy > 0.0f && id >= 0) {
            absorb(yetty_yflame_focus(app->flame, id));
        } else if (wheel_dy < 0.0f) {
            absorb(yetty_yflame_focus_parent(app->flame));
        } else {
            return;
        }
        yperf_ui_render_flame(app);
    }
}

/* ------------------------------------------------------------------ */
/* Symbol table                                                        */
/* ------------------------------------------------------------------ */

static void refresh_table(struct yperf_app *app)
{
    struct yperf_ui *ui = app->ui;
    struct yperf_profile *profile = app->profile;
    absorb(yetty_ygui_table_clear_rows(ui->table));
    if (!profile) {
        return;
    }
    int total = (int)profile->n_symbols;
    int rows = total < ui->table_rows ? total : ui->table_rows;
    if (rows <= 0) {
        return;
    }
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
    double denom = profile->total_samples ? (double)profile->total_samples : 1.0;
    for (int i = 0; i < rows; i++) {
        int idx = start + i;
        if (idx >= total) {
            break;
        }
        const struct yperf_symbol *sym = &profile->symbols[idx];
        char name[300], self[16], self_pct[12], total_s[16], total_pct[12];
        const char *marker = (idx == app->selected) ? "> " : "  ";
        snprintf(name, sizeof(name), "%s%s", marker, sym->name);
        yperf_fmt_count(sym->self, self, sizeof(self));
        yperf_fmt_count(sym->total, total_s, sizeof(total_s));
        snprintf(self_pct, sizeof(self_pct), "%.1f%%", 100.0 * (double)sym->self / denom);
        snprintf(total_pct, sizeof(total_pct), "%.1f%%", 100.0 * (double)sym->total / denom);
        const char *cells[] = {name, self, self_pct, total_s, total_pct};
        absorb(yetty_ygui_table_add_row(ui->table, cells, 5));
    }
}

static void refresh_header(struct yperf_app *app)
{
    struct yperf_ui *ui = app->ui;
    char buf[1152];
    const char *orient = app->icicle ? "icicle" : "flame";

    if (app->baseline_folded) {
        snprintf(buf, sizeof(buf), "%s   [%s] [diff]", app->source, orient);
    } else {
        snprintf(buf, sizeof(buf), "%s   [%s]", app->source, orient);
    }
    ui_set_text(ui->source, buf);

    uint64_t stacks = app->profile ? app->profile->stack_count : 0;
    uint64_t samples = app->profile ? app->profile->total_samples : 0;
    size_t symbols = app->profile ? app->profile->n_symbols : 0;
    char sample_str[24];
    yperf_fmt_count(samples, sample_str, sizeof(sample_str));
    if (app->filter[0]) {
        snprintf(buf, sizeof(buf),
                 "%llu stacks  \xc2\xb7  %s samples  \xc2\xb7  %zu symbols   filter: %s",
                 (unsigned long long)stacks, sample_str, symbols, app->filter);
    } else {
        snprintf(buf, sizeof(buf), "%llu stacks  \xc2\xb7  %s samples  \xc2\xb7  %zu symbols",
                 (unsigned long long)stacks, sample_str, symbols);
    }
    ui_set_text(ui->totals, buf);

    if (app->search_active) {
        /* trailing full block \xe2\x96\x88 as a caret */
        snprintf(buf, sizeof(buf), "search: %s\xe2\x96\x88", app->search);
    } else if (ui->hover_id >= 0 && ui->detail[0]) {
        snprintf(buf, sizeof(buf), "%s", ui->detail);
    } else {
        snprintf(buf, sizeof(buf),
                 "[j/k] move  [/] search  [enter] zoom  [f] filter  [F] clear  [s] sort:%s  "
                 "[i] %s  [r] reset  [q] quit",
                 yperf_sort_mode_name(app->sort_mode), orient);
    }
    ui_set_text(ui->status, buf);
}

/* ------------------------------------------------------------------ */
/* Build / free                                                        */
/* ------------------------------------------------------------------ */

struct yetty_ycore_void_result yperf_ui_build(struct yperf_app *app)
{
    if (!app || !app->root_widget) {
        return YETTY_ERR(yetty_ycore_void, "yperf_ui_build: no root");
    }
    struct yperf_ui *ui = calloc(1, sizeof(*ui));
    if (!ui) {
        return YETTY_ERR(yetty_ycore_void, "yperf_ui_build: calloc");
    }
    ui->hover_id = -1;
    app->ui = ui;

    ui->title = ui_add(app, yetty_ygui_label_class_get());
    ui_set_text(ui->title, "yperf");
    ui_set_color(ui->title, YPERF_COL_ACCENT_BRIGHT);
    ui_set_font(ui->title, 16.0f);

    ui->source = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->source, YPERF_COL_TEXT);
    ui_set_font(ui->source, 12.0f);

    ui->totals = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->totals, YPERF_COL_TEXT_SECONDARY);
    ui_set_font(ui->totals, 12.0f);

    ui->status = ui_add(app, yetty_ygui_label_class_get());
    ui_set_color(ui->status, YPERF_COL_TEXT_MUTED);
    ui_set_font(ui->status, 11.0f);

    ui->timeline_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->timeline_panel);
    ui->timeline_embed = ui_add(app, yetty_ygui_ydraw_embed_class_get());

    ui->flame_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->flame_panel);
    ui->flame_embed = ui_add(app, yetty_ygui_ydraw_embed_class_get());

    ui->table_panel = ui_add(app, yetty_ygui_panel_class_get());
    panel_style(ui->table_panel);
    ui->table = ui_add(app, yetty_ygui_table_class_get());
    if (ui->table) {
        static const char *const columns[] = {"SYMBOL", "SELF", "SELF%", "TOTAL", "TOTAL%"};
        absorb(yetty_ygui_table_set_columns(ui->table, 5, columns));
    }

    yperf_ui_relayout(app);
    return YETTY_OK_VOID();
}

void yperf_ui_free(struct yperf_app *app)
{
    if (app && app->ui) {
        free(app->ui);
        app->ui = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

#define YPERF_MARGIN 10.0f
#define YPERF_PAD 8.0f
#define YPERF_GAP 8.0f
#define YPERF_LINE 18.0f
#define YPERF_STATUS 16.0f
#define YPERF_TIMELINE_H 46.0f

void yperf_ui_relayout(struct yperf_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    struct yperf_ui *ui = app->ui;

    float vw = 0.0f, vh = 0.0f;
    yetty_ygui_framework_viewport(app->engine, &vw, &vh);
    if (vw < 480.0f) {
        vw = 1280.0f;
    }
    if (vh < 320.0f) {
        vh = 800.0f;
    }

    float x = YPERF_MARGIN;
    float y = YPERF_MARGIN;
    float full_w = vw - 2.0f * YPERF_MARGIN;

    ui_place(ui->title, x, y, 70.0f, YPERF_LINE);
    ui_place(ui->source, x + 74.0f, y + 1.0f, full_w - 74.0f, YPERF_LINE);
    y += YPERF_LINE + 2.0f;
    ui_place(ui->totals, x, y, full_w, YPERF_LINE);
    y += YPERF_LINE + YPERF_GAP;

    float body_bottom = vh - YPERF_MARGIN - YPERF_STATUS - YPERF_GAP;
    float body_h = body_bottom - y;
    if (body_h < 160.0f) {
        body_h = 160.0f;
    }

    int has_timeline = (app->profile && app->profile->timeline_n > 0);
    if (has_timeline) {
        ui_place(ui->timeline_panel, x, y, full_w, YPERF_TIMELINE_H);
        float tw = full_w - 2.0f * YPERF_PAD;
        float th = YPERF_TIMELINE_H - 2.0f * YPERF_PAD;
        ui_place(ui->timeline_embed, x + YPERF_PAD, y + YPERF_PAD, tw, th);
        ui->timeline_w = tw;
        ui->timeline_h = th;
        y += YPERF_TIMELINE_H + YPERF_GAP;
        body_h -= YPERF_TIMELINE_H + YPERF_GAP;
    } else {
        /* Park the unused strip off-screen so it never paints. */
        ui_place(ui->timeline_panel, -4.0f, -4.0f, 0.0f, 0.0f);
        ui_place(ui->timeline_embed, -4.0f, -4.0f, 0.0f, 0.0f);
        ui->timeline_w = 0.0f;
        ui->timeline_h = 0.0f;
    }

    float flame_h = body_h * 0.55f;
    float table_h = body_h - flame_h - YPERF_GAP;

    ui_place(ui->flame_panel, x, y, full_w, flame_h);
    float inner_w = full_w - 2.0f * YPERF_PAD;
    float inner_h = flame_h - 2.0f * YPERF_PAD;
    ui_place(ui->flame_embed, x + YPERF_PAD, y + YPERF_PAD, inner_w, inner_h);
    ui->flame_x = x + YPERF_PAD;
    ui->flame_y = y + YPERF_PAD;
    ui->flame_w = inner_w;
    ui->flame_h = inner_h;

    float table_y = y + flame_h + YPERF_GAP;
    ui_place(ui->table_panel, x, table_y, full_w, table_h);
    ui_place(ui->table, x + YPERF_PAD, table_y + YPERF_PAD, full_w - 2.0f * YPERF_PAD,
             table_h - 2.0f * YPERF_PAD);

    int rows = (int)((table_h - 2.0f * YPERF_PAD - 22.0f) / 20.0f);
    if (rows < 3) {
        rows = 3;
    }
    ui->table_rows = rows;

    ui_place(ui->status, x, vh - YPERF_MARGIN - YPERF_STATUS, full_w, YPERF_STATUS);
}

/* ------------------------------------------------------------------ */
/* Refresh                                                             */
/* ------------------------------------------------------------------ */

void yperf_ui_refresh_table(struct yperf_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    refresh_header(app);
    refresh_table(app);
}

void yperf_ui_refresh(struct yperf_app *app)
{
    if (!app || !app->ui) {
        return;
    }
    refresh_header(app);
    refresh_table(app);
    build_flame(app);
    build_timeline(app);
}
