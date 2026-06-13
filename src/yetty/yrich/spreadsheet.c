/*
 * yrich:spreadsheet — grid document (+ its cell element kind).
 *
 * Two classes live in this TU:
 *   spreadsheet — parent@yrich:document; grid geometry + cell lookup
 *   cell        — parent@yrich:element; SDF box body + one text span
 *
 * The grid keeps a cumulative position cache for fast row/col → screen
 * coordinate conversion; sparse row/col size overrides hang off two small
 * linear arrays. Cell objects are owned by the document base's element
 * list; the lookup array here aliases them by address.
 *
 * The cell-value mutators are wire-marshallable slots (scalars + by-value
 * buffer), so a spreadsheet can be driven over RPC / FFI bindings.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/element.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>
#include <string.h>

#define CELL_GRID_DEFAULT_ROWS 100
#define CELL_GRID_DEFAULT_COLS 26
#define CELL_GRID_DEFAULT_HEIGHT 24.0f
#define CELL_GRID_DEFAULT_WIDTH 80.0f

/* Accessors / downcasts / factories defined in the appended
 * spreadsheet.gen.c. */
YETTY_YRESULT_DECLARE(yetty_yrich_spreadsheet_ptr, struct yetty_yrich_spreadsheet *);
YETTY_YRESULT_DECLARE(yetty_yrich_cell_ptr, struct yetty_yrich_cell *);
struct yetty_yclass_ptr_result yetty_yrich_spreadsheet_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_cell_class_get(void);
struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_from(
    struct yetty_yclass_object *obj);
struct yetty_yrich_cell_ptr_result yetty_yrich_cell_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_cell_create(struct yetty_yclass_ctx *ctx);

/* Exposed helpers defined below, used before their definitions. */
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_y(struct yetty_yclass_object *obj,
                                                              int32_t row);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_x(struct yetty_yclass_object *obj,
                                                              int32_t col);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_height(struct yetty_yclass_object *obj,
                                                                   int32_t row);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_width(struct yetty_yclass_object *obj,
                                                                  int32_t col);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_ensure_cell(
    struct yetty_yclass_object *obj, int32_t row, int32_t col);

/*===========================================================================
 * Cell
 *=========================================================================*/

struct [[clang::annotate("class@yrich:cell")]] [[clang::annotate("parent@yrich:element")]]
yetty_yrich_cell {
    struct yetty_yrich_cell_addr address;
    struct yetty_yrich_rect bounds;
    char *text; /* owned, NUL-terminated */
    size_t text_len;
    char *formula; /* owned, may be NULL */

    struct yetty_yrich_text_style style;
    uint32_t fill_color;
    struct yetty_yrich_border border;

    uint32_t halign;
    uint32_t valign;

    int editing;
    int32_t cursor_pos;
    int32_t sel_start;
    int32_t sel_end;
};

[[clang::annotate("override@yrich:cell:constructor")]]
static struct yetty_ycore_void_result cell_constructor(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ycore_void_result super_res = yetty_yrich_super_void(
        obj, yetty_yrich_cell_class_get().value, (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "cell_constructor: super");
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_constructor: data_get");
    struct yetty_yrich_cell *cell = data_res.value;
    cell->style = yetty_yrich_text_style_default();
    cell->style.font_size = 12.0f;
    cell->fill_color = YETTY_YRICH_COLOR_WHITE;
    cell->border.width = 1.0f;
    cell->border.color = YETTY_YRICH_RGBA(200, 200, 200, 255);
    cell->border.style = YETTY_YRICH_BORDER_SOLID;
    cell->halign = YETTY_YRICH_HALIGN_LEFT;
    cell->valign = YETTY_YRICH_VALIGN_MIDDLE;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:cell:element_destroy")]]
static struct yetty_ycore_void_result cell_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_destroy: data_get");
    free(data_res.value->text);
    free(data_res.value->formula);
    return yetty_yrich_super_void(obj, yetty_yrich_cell_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_element_destroy);
}

[[clang::annotate("override@yrich:cell:element_bounds")]]
static struct yetty_ycore_void_result cell_bounds(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  struct yetty_yrich_rect *out_bounds)
{
    (void)ctx;
    if (!out_bounds) {
        return YETTY_ERR(yetty_ycore_void, "cell_bounds: NULL out_bounds");
    }
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_bounds: data_get");
    *out_bounds = data_res.value->bounds;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:cell:element_is_editable")]]
static struct yetty_ycore_int_result cell_is_editable(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@yrich:cell:element_begin_edit")]]
static struct yetty_ycore_void_result cell_begin_edit(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_begin_edit: data_get");
    struct yetty_yrich_cell *cell = data_res.value;
    cell->editing = 1;
    cell->cursor_pos = (int32_t)cell->text_len;
    cell->sel_start = 0;
    cell->sel_end = (int32_t)cell->text_len;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:cell:element_end_edit")]]
static struct yetty_ycore_void_result cell_end_edit(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_end_edit: data_get");
    struct yetty_yrich_cell *cell = data_res.value;
    cell->editing = 0;
    cell->sel_start = cell->sel_end = cell->cursor_pos;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:cell:element_is_editing")]]
static struct yetty_ycore_int_result cell_is_editing(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "cell_is_editing: data_get");
    return YETTY_OK(yetty_ycore_int, data_res.value->editing);
}

[[clang::annotate("override@yrich:cell:element_render")]]
static struct yetty_ycore_void_result cell_render(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  struct yetty_ydraw_drawable_list *drawable_list,
                                                  uint32_t layer, int selected)
{
    (void)ctx;
    if (!drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "cell_render: NULL drawable list");
    }
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_render: data_get");
    struct yetty_yrich_cell *cell = data_res.value;

    uint32_t fill = cell->fill_color;
    if (selected && !cell->editing) {
        fill = YETTY_YRICH_RGBA(220, 235, 255, 255);
    }

    struct yetty_ysdf_box body = {
        .center_x = cell->bounds.x + cell->bounds.w * 0.5f,
        .center_y = cell->bounds.y + cell->bounds.h * 0.5f,
        .half_width = cell->bounds.w * 0.5f,
        .half_height = cell->bounds.h * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        drawable_list, 0, layer, fill, cell->border.color, cell->border.width, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "cell_render: body box add failed");

    if (cell->text_len > 0) {
        float text_x = cell->bounds.x + 4.0f;
        float text_y = cell->bounds.y + cell->bounds.h * 0.5f + cell->style.font_size / 3.0f;

        if (cell->halign == YETTY_YRICH_HALIGN_CENTER) {
            text_x = cell->bounds.x + cell->bounds.w * 0.5f;
        } else if (cell->halign == YETTY_YRICH_HALIGN_RIGHT) {
            text_x = cell->bounds.x + cell->bounds.w - 4.0f;
        }

        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)cell->text,
            .size = cell->text_len,
            .capacity = cell->text_len,
        };
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            drawable_list, text_x, text_y, &text, cell->style.font_size, cell->style.color,
            layer + 1, cell->style.font_id, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "cell_render: add_text failed");
    }

    if (cell->editing) {
        float cursor_x =
            cell->bounds.x + 4.0f + (float)cell->cursor_pos * cell->style.font_size * 0.6f;
        struct yetty_ysdf_segment cursor_segment = {
            .start_x = cursor_x,
            .start_y = cell->bounds.y + 2.0f,
            .end_x = cursor_x,
            .end_y = cell->bounds.y + cell->bounds.h - 2.0f,
        };
        struct yetty_ycore_void_result cursor_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
            drawable_list, 0, layer + 2, 0, YETTY_YRICH_COLOR_BLACK, 1.0f, &cursor_segment);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cursor_res, "cell_render: cursor segment add failed");
    }

    if (selected) {
        struct yetty_ysdf_box border = body;
        struct yetty_ycore_void_result border_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            drawable_list, 0, layer + 3, 0, YETTY_YRICH_RGBA(0, 100, 200, 255), 2.0f, &border);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, border_res,
                            "cell_render: selection border add failed");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:cell:element_insert_text")]]
static struct yetty_ycore_void_result cell_insert_text(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_ycore_buffer text)
{
    (void)ctx;
    if (!text.data || text.size == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_insert_text: data_get");
    struct yetty_yrich_cell *cell = data_res.value;

    int32_t pos = cell->cursor_pos;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos > cell->text_len) {
        pos = (int32_t)cell->text_len;
    }

    size_t new_len = cell->text_len + text.size;
    char *new_buf = realloc(cell->text, new_len + 1);
    if (!new_buf) {
        return YETTY_ERR(yetty_ycore_void, "cell_insert_text: text grow failed");
    }
    memmove(new_buf + pos + text.size, new_buf + pos, cell->text_len - (size_t)pos);
    memcpy(new_buf + pos, text.data, text.size);
    new_buf[new_len] = '\0';
    cell->text = new_buf;
    cell->text_len = new_len;
    cell->cursor_pos = pos + (int32_t)text.size;
    cell->sel_start = cell->sel_end = cell->cursor_pos;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:cell:element_delete_sel")]]
static struct yetty_ycore_void_result cell_delete_sel(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_delete_sel: data_get");
    struct yetty_yrich_cell *cell = data_res.value;
    if (cell->text_len == 0) {
        return YETTY_OK_VOID();
    }
    if (cell->cursor_pos > 0) {
        memmove(cell->text + cell->cursor_pos - 1, cell->text + cell->cursor_pos,
                cell->text_len - (size_t)cell->cursor_pos);
        cell->text_len--;
        cell->text[cell->text_len] = '\0';
        cell->cursor_pos--;
        cell->sel_start = cell->sel_end = cell->cursor_pos;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_cell_set_text(struct yetty_yclass_object *obj,
                                                         const char *text, size_t len)
{
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "cell_set_text: data_get");
    struct yetty_yrich_cell *cell = data_res.value;
    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "cell_set_text: text alloc failed");
    }
    if (len > 0) {
        memcpy(buf, text, len);
    }
    buf[len] = '\0';
    free(cell->text);
    cell->text = buf;
    cell->text_len = len;
    cell->cursor_pos = (int32_t)len;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
const char *yetty_yrich_cell_text(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_cell_ptr_result data_res = yetty_yrich_cell_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return "";
    }
    return data_res.value->text ? data_res.value->text : "";
}

/*===========================================================================
 * Spreadsheet
 *=========================================================================*/

struct yetty_yrich_cell_entry {
    struct yetty_yrich_cell_addr addr;
    struct yetty_yclass_object *cell_obj; /* aliases the element list entry */
};

/* Sparse row/col size overrides — private to this TU. */
struct yetty_yrich_row_size {
    int32_t row;
    float height;
};

struct yetty_yrich_col_size {
    int32_t col;
    float width;
};

struct [[clang::annotate("class@yrich:spreadsheet")]] [[clang::annotate("parent@yrich:document")]]
yetty_yrich_spreadsheet {
    int32_t row_count;
    int32_t col_count;
    float default_row_height;
    float default_col_width;

    struct yetty_yrich_row_size *row_overrides;
    size_t row_override_count;
    size_t row_override_capacity;

    struct yetty_yrich_col_size *col_overrides;
    size_t col_override_count;
    size_t col_override_capacity;

    /* Cumulative-position cache. */
    float *row_y_cache;
    size_t row_y_cache_count;
    float *col_x_cache;
    size_t col_x_cache_count;
    int cache_valid;

    /* Address → cell lookup (linear scan, fine for typical grids). */
    struct yetty_yrich_cell_entry *cells;
    size_t cell_count;
    size_t cell_capacity;
};

static void invalidate_cache(struct yetty_yrich_spreadsheet *sheet)
{
    sheet->cache_valid = 0;
}

static float row_height_of(const struct yetty_yrich_spreadsheet *sheet, int32_t row)
{
    for (size_t i = 0; i < sheet->row_override_count; i++) {
        if (sheet->row_overrides[i].row == row) {
            return sheet->row_overrides[i].height;
        }
    }
    return sheet->default_row_height;
}

static float col_width_of(const struct yetty_yrich_spreadsheet *sheet, int32_t col)
{
    for (size_t i = 0; i < sheet->col_override_count; i++) {
        if (sheet->col_overrides[i].col == col) {
            return sheet->col_overrides[i].width;
        }
    }
    return sheet->default_col_width;
}

/* Rebuild the cumulative position cache. Best-effort: on alloc failure the
 * cache stays invalid and the getters recompute on the next call. */
static void rebuild_cache(struct yetty_yrich_spreadsheet *sheet)
{
    if (sheet->cache_valid) {
        return;
    }

    size_t row_count = (size_t)sheet->row_count + 1;
    size_t col_count = (size_t)sheet->col_count + 1;

    float *new_rows = realloc(sheet->row_y_cache, row_count * sizeof(float));
    float *new_cols = realloc(sheet->col_x_cache, col_count * sizeof(float));
    if (!new_rows || !new_cols) {
        free(new_rows);
        free(new_cols);
        sheet->row_y_cache = NULL;
        sheet->row_y_cache_count = 0;
        sheet->col_x_cache = NULL;
        sheet->col_x_cache_count = 0;
        return;
    }
    sheet->row_y_cache = new_rows;
    sheet->row_y_cache_count = row_count;
    sheet->col_x_cache = new_cols;
    sheet->col_x_cache_count = col_count;

    float y = 0.0f;
    for (int32_t row = 0; row < sheet->row_count; row++) {
        sheet->row_y_cache[row] = y;
        y += row_height_of(sheet, row);
    }
    sheet->row_y_cache[sheet->row_count] = y;

    float x = 0.0f;
    for (int32_t col = 0; col < sheet->col_count; col++) {
        sheet->col_x_cache[col] = x;
        x += col_width_of(sheet, col);
    }
    sheet->col_x_cache[sheet->col_count] = x;

    sheet->cache_valid = 1;
}

[[clang::annotate("override@yrich:spreadsheet:constructor")]]
static struct yetty_ycore_void_result spreadsheet_constructor(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ycore_void_result super_res =
        yetty_yrich_super_void(obj, yetty_yrich_spreadsheet_class_get().value,
                               (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "spreadsheet_constructor: super");
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "spreadsheet_constructor: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    sheet->row_count = CELL_GRID_DEFAULT_ROWS;
    sheet->col_count = CELL_GRID_DEFAULT_COLS;
    sheet->default_row_height = CELL_GRID_DEFAULT_HEIGHT;
    sheet->default_col_width = CELL_GRID_DEFAULT_WIDTH;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:spreadsheet:document_destroy")]]
static struct yetty_ycore_void_result spreadsheet_destroy(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "spreadsheet_destroy: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    free(sheet->row_overrides);
    free(sheet->col_overrides);
    free(sheet->row_y_cache);
    free(sheet->col_x_cache);
    free(sheet->cells);
    /* The document base frees the cells (elements) and the object itself. */
    return yetty_yrich_super_void(obj, yetty_yrich_spreadsheet_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_document_destroy);
}

[[clang::annotate("override@yrich:spreadsheet:document_content_width")]]
static struct yetty_ycore_float_result spreadsheet_content_width(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "spreadsheet_content_width: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    rebuild_cache(sheet);
    if (!sheet->col_x_cache || sheet->col_x_cache_count == 0) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    return YETTY_OK(yetty_ycore_float, sheet->col_x_cache[sheet->col_x_cache_count - 1]);
}

[[clang::annotate("override@yrich:spreadsheet:document_content_height")]]
static struct yetty_ycore_float_result spreadsheet_content_height(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "spreadsheet_content_height: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    rebuild_cache(sheet);
    if (!sheet->row_y_cache || sheet->row_y_cache_count == 0) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    return YETTY_OK(yetty_ycore_float, sheet->row_y_cache[sheet->row_y_cache_count - 1]);
}

/*===========================================================================
 * Grid configuration slots — scalar args, wire-marshallable.
 *=========================================================================*/

[[clang::annotate("virtual@yrich:spreadsheet:spreadsheet_set_grid_size")]]
static struct yetty_ycore_void_result spreadsheet_set_grid_size(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                int32_t rows, int32_t cols)
{
    (void)ctx;
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "spreadsheet_set_grid_size: data_get");
    data_res.value->row_count = rows;
    data_res.value->col_count = cols;
    invalidate_cache(data_res.value);
    return yetty_yrich_document_mark_dirty(obj);
}

static struct yetty_ycore_void_result set_row_override(struct yetty_yrich_spreadsheet *sheet,
                                                       int32_t row, float height)
{
    for (size_t i = 0; i < sheet->row_override_count; i++) {
        if (sheet->row_overrides[i].row == row) {
            sheet->row_overrides[i].height = height;
            return YETTY_OK_VOID();
        }
    }
    if (sheet->row_override_count == sheet->row_override_capacity) {
        size_t new_cap = sheet->row_override_capacity ? sheet->row_override_capacity * 2 : 8;
        struct yetty_yrich_row_size *new_arr =
            realloc(sheet->row_overrides, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_ycore_void, "spreadsheet: row override array grow failed");
        }
        sheet->row_overrides = new_arr;
        sheet->row_override_capacity = new_cap;
    }
    sheet->row_overrides[sheet->row_override_count++] = (struct yetty_yrich_row_size){row, height};
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result set_col_override(struct yetty_yrich_spreadsheet *sheet,
                                                       int32_t col, float width)
{
    for (size_t i = 0; i < sheet->col_override_count; i++) {
        if (sheet->col_overrides[i].col == col) {
            sheet->col_overrides[i].width = width;
            return YETTY_OK_VOID();
        }
    }
    if (sheet->col_override_count == sheet->col_override_capacity) {
        size_t new_cap = sheet->col_override_capacity ? sheet->col_override_capacity * 2 : 8;
        struct yetty_yrich_col_size *new_arr =
            realloc(sheet->col_overrides, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_ycore_void, "spreadsheet: col override array grow failed");
        }
        sheet->col_overrides = new_arr;
        sheet->col_override_capacity = new_cap;
    }
    sheet->col_overrides[sheet->col_override_count++] = (struct yetty_yrich_col_size){col, width};
    return YETTY_OK_VOID();
}

static void recalculate_cell_bounds(struct yetty_yrich_spreadsheet *sheet);

[[clang::annotate("virtual@yrich:spreadsheet:spreadsheet_set_row_height")]]
static struct yetty_ycore_void_result spreadsheet_set_row_height(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj,
                                                                 int32_t row, float height)
{
    (void)ctx;
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "spreadsheet_set_row_height: data_get");
    struct yetty_ycore_void_result override_res = set_row_override(data_res.value, row, height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, override_res, "spreadsheet_set_row_height failed");
    invalidate_cache(data_res.value);
    recalculate_cell_bounds(data_res.value);
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("virtual@yrich:spreadsheet:spreadsheet_set_col_width")]]
static struct yetty_ycore_void_result spreadsheet_set_col_width(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                int32_t col, float width)
{
    (void)ctx;
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "spreadsheet_set_col_width: data_get");
    struct yetty_ycore_void_result override_res = set_col_override(data_res.value, col, width);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, override_res, "spreadsheet_set_col_width failed");
    invalidate_cache(data_res.value);
    recalculate_cell_bounds(data_res.value);
    return yetty_yrich_document_mark_dirty(obj);
}

/*===========================================================================
 * Geometry queries
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_height(struct yetty_yclass_object *obj,
                                                                   int32_t row)
{
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "spreadsheet_row_height: data_get");
    return YETTY_OK(yetty_ycore_float, row_height_of(data_res.value, row));
}

[[clang::annotate("expose")]]
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_width(struct yetty_yclass_object *obj,
                                                                  int32_t col)
{
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "spreadsheet_col_width: data_get");
    return YETTY_OK(yetty_ycore_float, col_width_of(data_res.value, col));
}

[[clang::annotate("expose")]]
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_y(struct yetty_yclass_object *obj,
                                                              int32_t row)
{
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "spreadsheet_row_y: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    rebuild_cache(sheet);
    if (!sheet->row_y_cache || sheet->row_y_cache_count == 0 || row < 0) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    if ((size_t)row >= sheet->row_y_cache_count) {
        return YETTY_OK(yetty_ycore_float, sheet->row_y_cache[sheet->row_y_cache_count - 1]);
    }
    return YETTY_OK(yetty_ycore_float, sheet->row_y_cache[row]);
}

[[clang::annotate("expose")]]
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_x(struct yetty_yclass_object *obj,
                                                              int32_t col)
{
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "spreadsheet_col_x: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    rebuild_cache(sheet);
    if (!sheet->col_x_cache || sheet->col_x_cache_count == 0 || col < 0) {
        return YETTY_OK(yetty_ycore_float, 0.0f);
    }
    if ((size_t)col >= sheet->col_x_cache_count) {
        return YETTY_OK(yetty_ycore_float, sheet->col_x_cache[sheet->col_x_cache_count - 1]);
    }
    return YETTY_OK(yetty_ycore_float, sheet->col_x_cache[col]);
}

static void recalculate_cell_bounds(struct yetty_yrich_spreadsheet *sheet)
{
    for (size_t i = 0; i < sheet->cell_count; i++) {
        struct yetty_yrich_cell_addr addr = sheet->cells[i].addr;
        struct yetty_yrich_cell_ptr_result cell_res =
            yetty_yrich_cell_from(sheet->cells[i].cell_obj);
        if (YETTY_IS_ERR(cell_res)) {
            yetty_ycore_error_destroy(cell_res.error);
            continue;
        }
        struct yetty_yrich_cell *cell = cell_res.value;
        rebuild_cache(sheet);
        cell->bounds.x =
            (sheet->col_x_cache && addr.col >= 0 && (size_t)addr.col < sheet->col_x_cache_count)
                ? sheet->col_x_cache[addr.col]
                : 0.0f;
        cell->bounds.y =
            (sheet->row_y_cache && addr.row >= 0 && (size_t)addr.row < sheet->row_y_cache_count)
                ? sheet->row_y_cache[addr.row]
                : 0.0f;
        cell->bounds.w = col_width_of(sheet, addr.col);
        cell->bounds.h = row_height_of(sheet, addr.row);
    }
}

/*===========================================================================
 * Cell access
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_yrich_spreadsheet_cell_at(struct yetty_yclass_object *obj,
                                                            int32_t row, int32_t col)
{
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    struct yetty_yrich_cell_addr addr = {row, col};
    for (size_t i = 0; i < sheet->cell_count; i++) {
        if (yetty_yrich_cell_addr_eq(sheet->cells[i].addr, addr)) {
            return sheet->cells[i].cell_obj;
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result register_cell_entry(struct yetty_yrich_spreadsheet *sheet,
                                                          struct yetty_yrich_cell_addr addr,
                                                          struct yetty_yclass_object *cell_obj)
{
    if (sheet->cell_count == sheet->cell_capacity) {
        size_t new_cap = sheet->cell_capacity ? sheet->cell_capacity * 2 : 32;
        struct yetty_yrich_cell_entry *new_arr = realloc(sheet->cells, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_ycore_void, "spreadsheet: cell lookup array grow failed");
        }
        sheet->cells = new_arr;
        sheet->cell_capacity = new_cap;
    }
    sheet->cells[sheet->cell_count++] = (struct yetty_yrich_cell_entry){addr, cell_obj};
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_ensure_cell(
    struct yetty_yclass_object *obj, int32_t row, int32_t col)
{
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "ensure_cell: data_get");
    struct yetty_yrich_spreadsheet *sheet = data_res.value;

    struct yetty_yclass_object *existing = yetty_yrich_spreadsheet_cell_at(obj, row, col);
    if (existing) {
        return YETTY_OK(yetty_yclass_object_ptr, existing);
    }

    struct yetty_yclass_object_ptr_result create_res = yetty_yrich_cell_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, create_res, "ensure_cell: create");
    struct yetty_yclass_object *cell_obj = create_res.value;
    struct yetty_yrich_cell_ptr_result cell_res = yetty_yrich_cell_from(cell_obj);
    if (YETTY_IS_ERR(cell_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(NULL, cell_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ensure_cell: data_get", cell_res);
    }
    struct yetty_yrich_cell *cell = cell_res.value;
    cell->address = (struct yetty_yrich_cell_addr){row, col};

    struct yetty_ycore_float_result x_res = yetty_yrich_spreadsheet_col_x(obj, col);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, x_res, "ensure_cell: col_x");
    struct yetty_ycore_float_result y_res = yetty_yrich_spreadsheet_row_y(obj, row);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, y_res, "ensure_cell: row_y");
    cell->bounds.x = x_res.value;
    cell->bounds.y = y_res.value;
    cell->bounds.w = col_width_of(sheet, col);
    cell->bounds.h = row_height_of(sheet, row);

    struct yetty_yrich_element_id_result id_res = yetty_yrich_document_next_id(obj);
    if (YETTY_IS_OK(id_res)) {
        struct yetty_ycore_void_result set_id_res =
            yetty_yrich_element_set_id(cell_obj, id_res.value);
        if (YETTY_IS_ERR(set_id_res)) {
            yetty_ycore_error_destroy(set_id_res.error);
        }
    } else {
        yetty_ycore_error_destroy(id_res.error);
    }

    struct yetty_ycore_void_result add_res = yetty_yrich_document_add_element(obj, cell_obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, add_res, "ensure_cell: add_element");

    struct yetty_ycore_void_result register_res =
        register_cell_entry(sheet, (struct yetty_yrich_cell_addr){row, col}, cell_obj);
    if (YETTY_IS_ERR(register_res)) {
        /* Without the lookup entry a later ensure_cell would create a
		 * duplicate element at the same address — unwind and fail. */
        struct yetty_yrich_element_id_result unwind_id_res = yetty_yrich_element_id_value(cell_obj);
        if (YETTY_IS_OK(unwind_id_res)) {
            struct yetty_ycore_void_result remove_res =
                yetty_yrich_document_remove_element(obj, unwind_id_res.value);
            if (YETTY_IS_ERR(remove_res)) {
                yetty_ycore_error_destroy(remove_res.error);
            }
        } else {
            yetty_ycore_error_destroy(unwind_id_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ensure_cell: lookup registration failed",
                         register_res);
    }
    return YETTY_OK(yetty_yclass_object_ptr, cell_obj);
}

/* Set a cell's text value — wire-marshallable slot (scalars + buffer). */
[[clang::annotate("virtual@yrich:spreadsheet:spreadsheet_set_cell_value")]]
static struct yetty_ycore_void_result spreadsheet_set_cell_value(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj,
                                                                 int32_t row, int32_t col,
                                                                 struct yetty_ycore_buffer value)
{
    (void)ctx;
    struct yetty_yclass_object_ptr_result cell_res =
        yetty_yrich_spreadsheet_ensure_cell(obj, row, col);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cell_res, "set_cell_value: ensure_cell failed");
    struct yetty_ycore_void_result text_res =
        yetty_yrich_cell_set_text(cell_res.value, (const char *)value.data, value.size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "set_cell_value: set_text failed");
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("expose")]]
const char *yetty_yrich_spreadsheet_cell_value(struct yetty_yclass_object *obj, int32_t row,
                                               int32_t col)
{
    struct yetty_yclass_object *cell_obj = yetty_yrich_spreadsheet_cell_at(obj, row, col);
    return cell_obj ? yetty_yrich_cell_text(cell_obj) : "";
}

[[clang::annotate("expose")]]
struct yetty_yrich_cell_addr yetty_yrich_spreadsheet_cell_addr_at(struct yetty_yclass_object *obj,
                                                                  float x, float y)
{
    struct yetty_yrich_cell_addr addr = {0, 0};
    struct yetty_yrich_spreadsheet_ptr_result data_res = yetty_yrich_spreadsheet_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return addr;
    }
    struct yetty_yrich_spreadsheet *sheet = data_res.value;
    rebuild_cache(sheet);
    if (!sheet->col_x_cache || !sheet->row_y_cache) {
        return addr;
    }

    for (int32_t col = 0; col < sheet->col_count; col++) {
        if (sheet->col_x_cache[col + 1] > x) {
            addr.col = col;
            break;
        }
        addr.col = col;
    }
    for (int32_t row = 0; row < sheet->row_count; row++) {
        if (sheet->row_y_cache[row + 1] > y) {
            addr.row = row;
            break;
        }
        addr.row = row;
    }
    return addr;
}

#ifdef YCLASS_CODEGEN
/* Header-destined: the shared yrich value types (cell address) the public
 * spreadsheet API above references. */
#include <yetty/yrich/yrich-types.h>
#endif

#include "spreadsheet.gen.c"
