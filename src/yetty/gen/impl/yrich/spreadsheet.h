/* GENERATED — do not edit. */
/* Public interface for regular class(es) `cell, spreadsheet` (module: yrich).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YRICH_SPREADSHEET_H
#define YETTY_YCLASSGEN_YRICH_SPREADSHEET_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_yrich_cell_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_spreadsheet_class_get(void);

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
struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_to(
    struct yetty_yrich_spreadsheet *data);

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_grid_size(
    struct yetty_yclass_object *obj, int32_t rows, int32_t cols);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_row_height(
    struct yetty_yclass_object *obj, int32_t row, float height);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_col_width(
    struct yetty_yclass_object *obj, int32_t col, float width);
/* Set a cell's text value — wire-marshallable slot (scalars + buffer). */
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(
    struct yetty_yclass_object *obj, int32_t row, int32_t col, struct yetty_ycore_buffer value);

typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_grid_size_fn)(
    struct yetty_yclass_object *, int32_t, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_row_height_fn)(
    struct yetty_yclass_object *, int32_t, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_col_width_fn)(
    struct yetty_yclass_object *, int32_t, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_cell_value_fn)(
    struct yetty_yclass_object *, int32_t, int32_t, struct yetty_ycore_buffer);

struct yetty_yclass_object_ptr_result yetty_yrich_cell_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_register(void);

struct yetty_ycore_void_result yetty_yrich_cell_set_text(struct yetty_yclass_object *obj,
                                                         const char *text, size_t len);
struct yetty_ycore_const_char_ptr_result yetty_yrich_cell_text(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_height(struct yetty_yclass_object *obj,
                                                                   int32_t row);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_width(struct yetty_yclass_object *obj,
                                                                  int32_t col);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_row_y(struct yetty_yclass_object *obj,
                                                              int32_t row);
struct yetty_ycore_float_result yetty_yrich_spreadsheet_col_x(struct yetty_yclass_object *obj,
                                                              int32_t col);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_cell_at(
    struct yetty_yclass_object *obj, int32_t row, int32_t col);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_ensure_cell(
    struct yetty_yclass_object *obj, int32_t row, int32_t col);
struct yetty_ycore_const_char_ptr_result yetty_yrich_spreadsheet_cell_value(
    struct yetty_yclass_object *obj, int32_t row, int32_t col);
struct yetty_yrich_cell_addr_result yetty_yrich_spreadsheet_cell_addr_at(
    struct yetty_yclass_object *obj, float x, float y);

#ifdef __cplusplus
}
#endif

#endif
