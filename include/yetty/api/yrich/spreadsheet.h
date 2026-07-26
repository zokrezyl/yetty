/* GENERATED — do not edit. */
/* Object API for regular class(es) `cell, spreadsheet` (implementation module: yrich).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YRICH_SPREADSHEET_H
#define YETTY_YCLASSGEN_API_YRICH_SPREADSHEET_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_cell;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_CELL_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_CELL_PTR_RESULT
struct yetty_yrich_cell_ptr_result {
    int ok;
    union {
        struct yetty_yrich_cell *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_cell_ptr_result yetty_yrich_cell_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_cell_to(struct yetty_yrich_cell *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_spreadsheet;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SPREADSHEET_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_SPREADSHEET_PTR_RESULT
struct yetty_yrich_spreadsheet_ptr_result {
    int ok;
    union {
        struct yetty_yrich_spreadsheet *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_to(struct yetty_yrich_spreadsheet *data);

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_grid_size(struct yetty_yclass_object * obj, int32_t rows, int32_t cols);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_row_height(struct yetty_yclass_object * obj, int32_t row, float height);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_col_width(struct yetty_yclass_object * obj, int32_t col, float width);
/* Set a cell's text value — wire-marshallable slot (scalars + buffer). */
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(struct yetty_yclass_object * obj, int32_t row, int32_t col, struct yetty_ycore_buffer value);

struct yetty_yclass_object_ptr_result yetty_yrich_cell_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_yrich_cell_set_text(struct yetty_yclass_object *obj, const char *text, size_t len);
struct yetty_ycore_const_char_ptr_result yetty_yrich_cell_text(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_height(struct yetty_yclass_object *obj, int32_t row);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_width(struct yetty_yclass_object *obj, int32_t col);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_y(struct yetty_yclass_object *obj, int32_t row);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_x(struct yetty_yclass_object *obj, int32_t col);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_cell_at(struct yetty_yclass_object *obj, int32_t row, int32_t col);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_ensure_cell(struct yetty_yclass_object *obj, int32_t row, int32_t col);
struct yetty_ycore_const_char_ptr_result yetty_yrich_spreadsheet_cell_value(struct yetty_yclass_object *obj, int32_t row, int32_t col);
struct yetty_yrich_cell_addr_result yetty_yrich_spreadsheet_cell_addr_at(struct yetty_yclass_object *obj, float x, float y);

#ifdef __cplusplus
}
#endif

#endif
