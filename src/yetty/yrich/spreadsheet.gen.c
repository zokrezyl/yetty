/* GENERATED — do not edit. */
#include "yetty/yrich/document.h"
#include "yetty/yrich/element.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_float_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ydraw_drawable_list *drawable_list, uint32_t layer, int selected);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_grid_size(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int32_t rows, int32_t cols);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_row_height(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int32_t row, float height);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_col_width(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int32_t col, float width);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int32_t row, int32_t col,
    struct yetty_ycore_buffer value);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *,
    uint32_t, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_grid_size_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_row_height_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_col_width_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_cell_value_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t, int32_t,
    struct yetty_ycore_buffer);

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_cell_yetty_yrich_constructor_check = cell_constructor;
[[maybe_unused]]
static yetty_yrich_element_destroy_fn yetty_yrich_cell_yetty_yrich_element_destroy_check =
    cell_destroy;
[[maybe_unused]]
static yetty_yrich_element_bounds_fn yetty_yrich_cell_yetty_yrich_element_bounds_check =
    cell_bounds;
[[maybe_unused]]
static yetty_yrich_element_is_editable_fn yetty_yrich_cell_yetty_yrich_element_is_editable_check =
    cell_is_editable;
[[maybe_unused]]
static yetty_yrich_element_begin_edit_fn yetty_yrich_cell_yetty_yrich_element_begin_edit_check =
    cell_begin_edit;
[[maybe_unused]]
static yetty_yrich_element_end_edit_fn yetty_yrich_cell_yetty_yrich_element_end_edit_check =
    cell_end_edit;
[[maybe_unused]]
static yetty_yrich_element_is_editing_fn yetty_yrich_cell_yetty_yrich_element_is_editing_check =
    cell_is_editing;
[[maybe_unused]]
static yetty_yrich_element_render_fn yetty_yrich_cell_yetty_yrich_element_render_check =
    cell_render;
[[maybe_unused]]
static yetty_yrich_element_insert_text_fn yetty_yrich_cell_yetty_yrich_element_insert_text_check =
    cell_insert_text;
[[maybe_unused]]
static yetty_yrich_element_delete_sel_fn yetty_yrich_cell_yetty_yrich_element_delete_sel_check =
    cell_delete_sel;

struct yetty_yclass_ptr_result yetty_yrich_cell_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_cell");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_cell",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_cell),
        .data_align = _Alignof(struct yetty_yrich_cell),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)cell_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy,
         (yetty_yclass_impl_t)cell_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds,
         (yetty_yclass_impl_t)cell_bounds},
        {"yetty_yrich", "element_is_editable",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editable,
         (yetty_yclass_impl_t)cell_is_editable},
        {"yetty_yrich", "element_begin_edit",
         (yetty_yclass_method_id_t)yetty_yrich_element_begin_edit,
         (yetty_yclass_impl_t)cell_begin_edit},
        {"yetty_yrich", "element_end_edit", (yetty_yclass_method_id_t)yetty_yrich_element_end_edit,
         (yetty_yclass_impl_t)cell_end_edit},
        {"yetty_yrich", "element_is_editing",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editing,
         (yetty_yclass_impl_t)cell_is_editing},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render,
         (yetty_yclass_impl_t)cell_render},
        {"yetty_yrich", "element_insert_text",
         (yetty_yclass_method_id_t)yetty_yrich_element_insert_text,
         (yetty_yclass_impl_t)cell_insert_text},
        {"yetty_yrich", "element_delete_sel",
         (yetty_yclass_method_id_t)yetty_yrich_element_delete_sel,
         (yetty_yclass_impl_t)cell_delete_sel},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_cell_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_cell_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_cell_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_cell_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_cell_ptr_result yetty_yrich_cell_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_cell_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_cell_ptr, "yetty_yrich_cell_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_cell_ptr, "yetty_yrich_cell_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yrich_cell_ptr, (struct yetty_yrich_cell *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_cell_to(struct yetty_yrich_cell *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_cell_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_spreadsheet_yetty_yrich_constructor_check =
    spreadsheet_constructor;
[[maybe_unused]]
static yetty_yrich_document_destroy_fn yetty_yrich_spreadsheet_yetty_yrich_document_destroy_check =
    spreadsheet_destroy;
[[maybe_unused]]
static yetty_yrich_document_content_width_fn
    yetty_yrich_spreadsheet_yetty_yrich_document_content_width_check = spreadsheet_content_width;
[[maybe_unused]]
static yetty_yrich_document_content_height_fn
    yetty_yrich_spreadsheet_yetty_yrich_document_content_height_check = spreadsheet_content_height;
[[maybe_unused]]
static yetty_yrich_spreadsheet_set_grid_size_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_grid_size_check = spreadsheet_set_grid_size;
[[maybe_unused]]
static yetty_yrich_spreadsheet_set_row_height_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_row_height_check =
        spreadsheet_set_row_height;
[[maybe_unused]]
static yetty_yrich_spreadsheet_set_col_width_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_col_width_check = spreadsheet_set_col_width;
[[maybe_unused]]
static yetty_yrich_spreadsheet_set_cell_value_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_cell_value_check =
        spreadsheet_set_cell_value;

struct yetty_yclass_ptr_result yetty_yrich_spreadsheet_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_spreadsheet");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_spreadsheet",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_spreadsheet),
        .data_align = _Alignof(struct yetty_yrich_spreadsheet),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)spreadsheet_constructor},
        {"yetty_yrich", "document_destroy", (yetty_yclass_method_id_t)yetty_yrich_document_destroy,
         (yetty_yclass_impl_t)spreadsheet_destroy},
        {"yetty_yrich", "document_content_width",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_width,
         (yetty_yclass_impl_t)spreadsheet_content_width},
        {"yetty_yrich", "document_content_height",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_height,
         (yetty_yclass_impl_t)spreadsheet_content_height},
        {"yetty_yrich", "spreadsheet_set_grid_size",
         (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_grid_size,
         (yetty_yclass_impl_t)spreadsheet_set_grid_size},
        {"yetty_yrich", "spreadsheet_set_row_height",
         (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_row_height,
         (yetty_yclass_impl_t)spreadsheet_set_row_height},
        {"yetty_yrich", "spreadsheet_set_col_width",
         (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_col_width,
         (yetty_yclass_impl_t)spreadsheet_set_col_width},
        {"yetty_yrich", "spreadsheet_set_cell_value",
         (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_cell_value,
         (yetty_yclass_impl_t)spreadsheet_set_cell_value},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_spreadsheet_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yrich_spreadsheet_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_spreadsheet_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yrich_spreadsheet_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_spreadsheet_ptr_result yetty_yrich_spreadsheet_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_spreadsheet_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr,
                         "yetty_yrich_spreadsheet_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_spreadsheet_ptr, "yetty_yrich_spreadsheet_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yrich_spreadsheet_ptr, (struct yetty_yrich_spreadsheet *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_spreadsheet_to(struct yetty_yrich_spreadsheet *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_spreadsheet_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}
