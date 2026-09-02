/*
 * ygui2 table — fixed-width columns, header row + striped data rows,
 * painted as text runs over stripe boxes. Row data lives in fixed-size
 * arrays (no heap, no destructor).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/ysdf/funcs.gen.h>

#include "yetty/gen/impl/ygui2/widget.h"

YETTY_YRESULT_DECLARE(yetty_ygui2_table_ptr, struct yetty_ygui2_table *);
struct yetty_yclass_ptr_result yetty_ygui2_table_class_get(void);
struct yetty_ygui2_table_ptr_result yetty_ygui2_table_from(struct yetty_yclass_object *obj);

enum {
    YGUI2_TABLE_COLUMN_MAX = 8,
    YGUI2_TABLE_ROW_MAX = 64,
    YGUI2_TABLE_CELL_MAX = 32,
};

struct YETTY_ANNOTATE("class@ygui2:table") YETTY_ANNOTATE("parent@ygui2:widget") yetty_ygui2_table {
    uint32_t column_count;
    char headers[YGUI2_TABLE_COLUMN_MAX][YGUI2_TABLE_CELL_MAX];
    float column_widths[YGUI2_TABLE_COLUMN_MAX]; /* px; 0 = share the rest */
    uint32_t row_count;
    char cells[YGUI2_TABLE_ROW_MAX][YGUI2_TABLE_COLUMN_MAX][YGUI2_TABLE_CELL_MAX];
    float row_height;      /* px; 0 = 18 */
    float font_size;       /* 0 = 12 */
    uint32_t text_color;   /* 0 = off-white */
    uint32_t header_color; /* 0 = secondary */
    uint32_t stripe;       /* 0 = subtle row bg */
};

static void table_copy_cell(char *cell, const char *text)
{
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_TABLE_CELL_MAX) {
        length = YGUI2_TABLE_CELL_MAX - 1u;
    }
    if (length) {
        memcpy(cell, text, length);
    }
    cell[length] = '\0';
}

static struct yetty_ycore_void_result table_text(struct yetty_ydraw_drawable_list *list, float x,
                                                 float y, const char *text, float font_size,
                                                 uint32_t color)
{
    size_t length = strlen(text);
    if (length == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)text, .size = length};
    return yetty_ydraw_drawable_list_add_text(list, x, y, &text_buffer, font_size, color,
                                              /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result table_paint(struct yetty_yclass_object *obj,
                                                  struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_table_ptr_result data_res = yetty_ygui2_table_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 table paint: data");
    struct yetty_ygui2_table *table = data_res.value;
    if (table->column_count == 0) {
        return YETTY_OK_VOID();
    }
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 table paint: rect");
    float row_height = table->row_height > 0.0f ? table->row_height : 18.0f;
    float font_size = table->font_size > 0.0f ? table->font_size : 12.0f;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 table paint: theme");
    uint32_t text_color = table->text_color ? table->text_color : theme.text_primary;
    uint32_t header_color = table->header_color ? table->header_color : theme.text_secondary;
    uint32_t stripe = table->stripe ? table->stripe : theme.bg_lifted;

    /* Column x origins: fixed widths first, one 0-width column shares the
     * remainder equally with other 0-width columns. */
    float fixed_total = 0.0f;
    uint32_t flexible_count = 0;
    for (uint32_t column = 0; column < table->column_count; ++column) {
        if (table->column_widths[column] > 0.0f) {
            fixed_total += table->column_widths[column];
        } else {
            flexible_count++;
        }
    }
    float flexible_width = flexible_count ? (width - fixed_total) / (float)flexible_count : 0.0f;
    if (flexible_width < 0.0f) {
        flexible_width = 0.0f;
    }

    float text_baseline = font_size + (row_height - font_size) * 0.5f;
    /* Header row. */
    {
        float column_x = 0.0f;
        for (uint32_t column = 0; column < table->column_count; ++column) {
            struct yetty_ycore_void_result text_res = table_text(
                list, column_x, text_baseline, table->headers[column], font_size, header_color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 table paint: header");
            column_x +=
                table->column_widths[column] > 0.0f ? table->column_widths[column] : flexible_width;
        }
    }
    /* Data rows, clamped to whole rows that fit BELOW the header. A
     * widget shorter than two rows fits zero data rows — the negative
     * intermediate must never reach the unsigned conversion. */
    uint32_t fit_rows = 0;
    if (height >= 2.0f * row_height) {
        fit_rows = (uint32_t)((height - row_height) / row_height);
    }
    uint32_t paint_rows = table->row_count < fit_rows ? table->row_count : fit_rows;
    for (uint32_t row = 0; row < paint_rows; ++row) {
        float row_y = row_height * (float)(row + 1u);
        if (row % 2u == 1u) {
            struct yetty_ysdf_box stripe_geometry = {
                .center_x = width * 0.5f,
                .center_y = row_y + row_height * 0.5f,
                .half_width = width * 0.5f,
                .half_height = row_height * 0.5f,
                .corner_radius = 0.0f,
            };
            struct yetty_ycore_void_result stripe_res = yetty_ydraw_drawable_list_add_cmd_add_box(
                list, /*id=*/0, /*z_order=*/0, stripe, /*stroke=*/0u, /*stroke_width=*/0.0f,
                &stripe_geometry);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, stripe_res, "ygui2 table paint: stripe");
        }
        float column_x = 0.0f;
        for (uint32_t column = 0; column < table->column_count; ++column) {
            struct yetty_ycore_void_result text_res =
                table_text(list, column_x, row_y + text_baseline, table->cells[row][column],
                           font_size, text_color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 table paint: cell");
            column_x +=
                table->column_widths[column] > 0.0f ? table->column_widths[column] : flexible_width;
        }
    }
    return YETTY_OK_VOID();
}

/* columns: parallel arrays of header text + widths (0 = flexible). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_table_set_columns(struct yetty_yclass_object *obj,
                                                             const char *const *headers,
                                                             const float *widths, uint32_t count)
{
    struct yetty_ygui2_table_ptr_result data_res = yetty_ygui2_table_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 table_set_columns: data");
    struct yetty_ygui2_table *table = data_res.value;
    if (count > YGUI2_TABLE_COLUMN_MAX) {
        count = YGUI2_TABLE_COLUMN_MAX;
    }
    table->column_count = count;
    for (uint32_t column = 0; column < count; ++column) {
        table_copy_cell(table->headers[column], headers ? headers[column] : "");
        table->column_widths[column] = widths ? widths[column] : 0.0f;
    }
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_table_clear_rows(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_table_ptr_result data_res = yetty_ygui2_table_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 table_clear_rows: data");
    if (data_res.value->row_count == 0) {
        return YETTY_OK_VOID();
    }
    data_res.value->row_count = 0;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_table_add_row(struct yetty_yclass_object *obj,
                                                         const char *const *cells, uint32_t count)
{
    struct yetty_ygui2_table_ptr_result data_res = yetty_ygui2_table_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 table_add_row: data");
    struct yetty_ygui2_table *table = data_res.value;
    if (table->row_count >= YGUI2_TABLE_ROW_MAX) {
        return YETTY_OK_VOID(); /* silently full — dashboard tail rows drop */
    }
    uint32_t row = table->row_count++;
    for (uint32_t column = 0; column < table->column_count; ++column) {
        table_copy_cell(table->cells[row][column], (cells && column < count) ? cells[column] : "");
    }
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

#include "yetty/gen/impl/ygui2/widgets/table.c"
