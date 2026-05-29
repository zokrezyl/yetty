/* ygui-table.c — fixed-width columns + striped rows. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/table.h>
#include <stdlib.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_HEADER 0xFF1F1A14u
#define COLOR_ROW_A 0xFF14100Bu
#define COLOR_ROW_B 0xFF181410u
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_HEADER_TEXT 0xFF92A86Bu
#define ROW_H 22.0f

struct [[clang::annotate("class@ygui:table")]] [[clang::annotate("parent@ygui:primitive_widget")]]
table_data {
    char **headers;
    int n_cols;
    char ***rows;
    int n_rows;
    int rows_cap;
};

[[clang::annotate("override@ygui:table:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "table: super");
    struct table_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"));
    d->headers = NULL;
    d->n_cols = 0;
    d->rows = NULL;
    d->n_rows = 0;
    d->rows_cap = 0;
    return YETTY_OK_VOID();
}

static void free_row(char **row, int n_cols)
{
    for (int i = 0; i < n_cols; i++) {
        free(row[i]);
    }
    free(row);
}

[[clang::annotate("override@ygui:table:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct table_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"));
    for (int i = 0; i < d->n_cols; i++) {
        free(d->headers[i]);
    }
    free(d->headers);
    for (int i = 0; i < d->n_rows; i++) {
        free_row(d->rows[i], d->n_cols);
    }
    free(d->rows);
    return yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:table:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "table paint: NULL ctx");
    }
    struct table_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"));
    if (d->n_cols <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    float col_w = w / (float)d->n_cols;
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4),
                        "table: bg");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x, r.min.y, w, ROW_H, COLOR_HEADER, 0),
                        "table: header_bg");
    float fs = 13.0f;
    for (int c = 0; c < d->n_cols; c++) {
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->headers[c] ? d->headers[c] : "",
                                       r.min.x + c * col_w + 8, r.min.y + (ROW_H + fs) * 0.5f - 3,
                                       fs, COLOR_HEADER_TEXT),
                            "table: header");
    }
    /* Separator hairline below header. */
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x, r.min.y + ROW_H, w, 1, COLOR_BORDER, 0),
                        "table: hsep");
    for (int rr = 0; rr < d->n_rows; rr++) {
        float ry = r.min.y + (rr + 1) * ROW_H + 1;
        if (ry > r.max.y) {
            break;
        }
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void,
            yguix_box(ctx, r.min.x, ry, w, ROW_H, (rr % 2) ? COLOR_ROW_B : COLOR_ROW_A, 0),
            "table: row");
        for (int c = 0; c < d->n_cols; c++) {
            YETTY_RETURN_IF_ERR(yetty_ycore_void,
                                yguix_text(ctx, d->rows[rr][c] ? d->rows[rr][c] : "",
                                           r.min.x + c * col_w + 8, ry + (ROW_H + fs) * 0.5f - 3,
                                           fs, COLOR_TEXT),
                                "table: cell");
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_table_set_columns(struct yetty_ygui_object *obj,
                                                            int n_cols, const char *const *headers)
{
    if (!obj || n_cols <= 0) {
        return YETTY_ERR(yetty_ycore_void, "table_set_columns: bad");
    }
    struct table_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"));
    for (int i = 0; i < d->n_cols; i++) {
        free(d->headers[i]);
    }
    free(d->headers);
    d->headers = calloc((size_t)n_cols, sizeof(char *));
    if (!d->headers) {
        return YETTY_ERR(yetty_ycore_void, "table_set_columns: malloc");
    }
    for (int i = 0; i < n_cols; i++) {
        if (headers && headers[i]) {
            size_t n = strlen(headers[i]);
            d->headers[i] = malloc(n + 1);
            if (!d->headers[i]) {
                return YETTY_ERR(yetty_ycore_void, "table_set_columns: malloc h");
            }
            memcpy(d->headers[i], headers[i], n + 1);
        }
    }
    d->n_cols = n_cols;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_table_add_row(struct yetty_ygui_object *obj,
                                                        const char *const *cells, int n_cells)
{
    if (!obj || !cells) {
        return YETTY_ERR(yetty_ycore_void, "table_add_row: NULL");
    }
    struct table_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_table_class_get(), "yetty_ygui_table_class_get"));
    if (d->n_rows + 1 > d->rows_cap) {
        int c = d->rows_cap ? d->rows_cap * 2 : 8;
        while (c < d->n_rows + 1) {
            c *= 2;
        }
        char ***nr = realloc(d->rows, (size_t)c * sizeof(*nr));
        if (!nr) {
            return YETTY_ERR(yetty_ycore_void, "table_add_row: realloc");
        }
        d->rows = nr;
        d->rows_cap = c;
    }
    char **row = calloc((size_t)d->n_cols, sizeof(char *));
    if (!row) {
        return YETTY_ERR(yetty_ycore_void, "table_add_row: row malloc");
    }
    int copy = n_cells < d->n_cols ? n_cells : d->n_cols;
    for (int i = 0; i < copy; i++) {
        if (!cells[i]) {
            continue;
        }
        size_t n = strlen(cells[i]);
        row[i] = malloc(n + 1);
        if (!row[i]) {
            free_row(row, d->n_cols);
            return YETTY_ERR(yetty_ycore_void, "table_add_row: cell malloc");
        }
        memcpy(row[i], cells[i], n + 1);
    }
    d->rows[d->n_rows++] = row;
    return yetty_ygui_object_set_dirty(obj);
}

#include "table.gen.c"
