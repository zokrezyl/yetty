/* GENERATED — do not edit. */
#include "yetty/gen/impl/yrich/document.h"
#include "yetty/gen/impl/yrich/element.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

struct yetty_ycore_float_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list,
    uint32_t layer, int selected);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_grid_size(
    struct yetty_yclass_object *obj, int32_t rows, int32_t cols);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_row_height(
    struct yetty_yclass_object *obj, int32_t row, float height);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_col_width(
    struct yetty_yclass_object *obj, int32_t col, float width);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(
    struct yetty_yclass_object *obj, int32_t row, int32_t col, struct yetty_ycore_buffer value);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(
    struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_grid_size_fn)(
    struct yetty_yclass_object *, int32_t, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_row_height_fn)(
    struct yetty_yclass_object *, int32_t, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_col_width_fn)(
    struct yetty_yclass_object *, int32_t, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_spreadsheet_set_cell_value_fn)(
    struct yetty_yclass_object *, int32_t, int32_t, struct yetty_ycore_buffer);

YETTY_MAYBE_UNUSED
static yetty_yrich_constructor_fn yetty_yrich_cell_yetty_yrich_constructor_cell_constructor_check =
    cell_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_destroy_fn
    yetty_yrich_cell_yetty_yrich_element_destroy_cell_destroy_check = cell_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_bounds_fn yetty_yrich_cell_yetty_yrich_element_bounds_cell_bounds_check =
    cell_bounds;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_is_editable_fn
    yetty_yrich_cell_yetty_yrich_element_is_editable_cell_is_editable_check = cell_is_editable;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_begin_edit_fn
    yetty_yrich_cell_yetty_yrich_element_begin_edit_cell_begin_edit_check = cell_begin_edit;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_end_edit_fn
    yetty_yrich_cell_yetty_yrich_element_end_edit_cell_end_edit_check = cell_end_edit;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_is_editing_fn
    yetty_yrich_cell_yetty_yrich_element_is_editing_cell_is_editing_check = cell_is_editing;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_render_fn yetty_yrich_cell_yetty_yrich_element_render_cell_render_check =
    cell_render;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_insert_text_fn
    yetty_yrich_cell_yetty_yrich_element_insert_text_cell_insert_text_check = cell_insert_text;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_delete_sel_fn
    yetty_yrich_cell_yetty_yrich_element_delete_sel_cell_delete_sel_check = cell_delete_sel;

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

struct yetty_yclass_object_ptr_result yetty_yrich_cell_to(struct yetty_yrich_cell *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_cell_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yrich_cell_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yrich_cell_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_yrich_constructor_fn
    yetty_yrich_spreadsheet_yetty_yrich_constructor_spreadsheet_constructor_check =
        spreadsheet_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_destroy_fn
    yetty_yrich_spreadsheet_yetty_yrich_document_destroy_spreadsheet_destroy_check =
        spreadsheet_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_width_fn
    yetty_yrich_spreadsheet_yetty_yrich_document_content_width_spreadsheet_content_width_check =
        spreadsheet_content_width;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_height_fn
    yetty_yrich_spreadsheet_yetty_yrich_document_content_height_spreadsheet_content_height_check =
        spreadsheet_content_height;
YETTY_MAYBE_UNUSED
static yetty_yrich_spreadsheet_set_grid_size_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_grid_size_spreadsheet_set_grid_size_check =
        spreadsheet_set_grid_size;
YETTY_MAYBE_UNUSED
static yetty_yrich_spreadsheet_set_row_height_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_row_height_spreadsheet_set_row_height_check =
        spreadsheet_set_row_height;
YETTY_MAYBE_UNUSED
static yetty_yrich_spreadsheet_set_col_width_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_col_width_spreadsheet_set_col_width_check =
        spreadsheet_set_col_width;
YETTY_MAYBE_UNUSED
static yetty_yrich_spreadsheet_set_cell_value_fn
    yetty_yrich_spreadsheet_yetty_yrich_spreadsheet_set_cell_value_spreadsheet_set_cell_value_check =
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

struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_to(
    struct yetty_yrich_spreadsheet *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_spreadsheet_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yrich_spreadsheet_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yrich_spreadsheet_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_spreadsheet_set_grid_size_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_spreadsheet_set_grid_size_skel(const void *body, size_t body_len, void *resp,
                                                  size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        int32_t rows;
        int32_t cols;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yrich_spreadsheet_set_grid_size: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_spreadsheet_set_grid_size(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.rows, wire_args.cols);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_spreadsheet_set_grid_size",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_spreadsheet_set_row_height_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_spreadsheet_set_row_height_skel(const void *body, size_t body_len, void *resp,
                                                   size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        int32_t row;
        float height;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yrich_spreadsheet_set_row_height: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_spreadsheet_set_row_height(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.row, wire_args.height);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_spreadsheet_set_row_height",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_spreadsheet_set_col_width_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_spreadsheet_set_col_width_skel(const void *body, size_t body_len, void *resp,
                                                  size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        int32_t col;
        float width;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yrich_spreadsheet_set_col_width: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_spreadsheet_set_col_width(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.col, wire_args.width);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_spreadsheet_set_col_width",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_spreadsheet_set_cell_value_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_spreadsheet_set_cell_value_skel(const void *body, size_t body_len, void *resp,
                                                   size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        int32_t row;
        int32_t col;
        uint32_t value_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.value_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer value_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.value_len,
        .capacity = (size_t)wire_args.value_len,
    };
    body_offset += (size_t)wire_args.value_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yrich_spreadsheet_set_cell_value: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_spreadsheet_set_cell_value(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.row, wire_args.col, value_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_spreadsheet_set_cell_value",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_cell_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_cell_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_cell");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yrich_cell_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_cell_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_cell_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ycore_void_result ctor_r = yetty_yrich_constructor(alloc_r.value);
    if (YETTY_IS_ERR(ctor_r)) {
        struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
        if (YETTY_IS_ERR(free_r)) {
            yetty_ycore_error_destroy(free_r.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_cell_create: constructor failed",
                         ctor_r);
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_spreadsheet_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_spreadsheet");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yrich_spreadsheet_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_spreadsheet_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_spreadsheet_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ycore_void_result ctor_r = yetty_yrich_constructor(alloc_r.value);
    if (YETTY_IS_ERR(ctor_r)) {
        struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
        if (YETTY_IS_ERR(free_r)) {
            yetty_ycore_error_destroy(free_r.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_spreadsheet_create: constructor failed", ctor_r);
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yrich_cell_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_spreadsheet_class_get(void);
size_t yetty_yrich_spreadsheet_set_grid_size_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_row_height_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_col_width_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_spreadsheet_set_cell_value_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yrich_spreadsheet_register(void);

/* ---- yrich_spreadsheet: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yrich_spreadsheet_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yrich_cell") == 0) {
        return yetty_yrich_cell_class_get();
    }
    if (strcmp(name, "yetty_yrich_spreadsheet") == 0) {
        return yetty_yrich_spreadsheet_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yrich_spreadsheet: slot -> skel, name-keyed static data --------------- */

struct yetty_yrich_spreadsheet_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_yrich_spreadsheet_skel_row yetty_yrich_spreadsheet_skel_rows[] = {
    {"yetty_yrich_spreadsheet_set_grid_size", yetty_yrich_spreadsheet_set_grid_size_skel},
    {"yetty_yrich_spreadsheet_set_row_height", yetty_yrich_spreadsheet_set_row_height_skel},
    {"yetty_yrich_spreadsheet_set_col_width", yetty_yrich_spreadsheet_set_col_width_skel},
    {"yetty_yrich_spreadsheet_set_cell_value", yetty_yrich_spreadsheet_set_cell_value_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yrich_spreadsheet_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0; i < sizeof(yetty_yrich_spreadsheet_skel_rows) /
                               sizeof(yetty_yrich_spreadsheet_skel_rows[0]);
         ++i) {
        if (strcmp(yetty_yrich_spreadsheet_skel_rows[i].name, name) == 0) {
            return yetty_yrich_spreadsheet_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yrich_spreadsheet: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yrich_spreadsheet_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yrich_spreadsheet_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yrich_spreadsheet_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yrich_spreadsheet_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yrich_spreadsheet_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
