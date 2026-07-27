/* GENERATED — do not edit. */
#include <yetty/api/yrich/spreadsheet.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* malloc/free for buffer marshalling */
#include <string.h> /* memcpy/strlen */

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

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_grid_size(
    struct yetty_yclass_object *obj, int32_t rows, int32_t cols)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_spreadsheet_set_grid_size: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yrich_spreadsheet_set_grid_size");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void, remote_id_r,
            "yetty_yrich_spreadsheet_set_grid_size: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            int32_t rows;
            int32_t cols;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            rows, cols};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_spreadsheet_set_grid_size", &wire_args,
            sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_spreadsheet_set_grid_size: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_grid_size);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yrich_spreadsheet_set_grid_size: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_spreadsheet_set_grid_size: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_spreadsheet_set_grid_size: dispatch_lookup failed");
        return ((yetty_yrich_spreadsheet_set_grid_size_fn)dispatch_impl_r.value)(obj, rows, cols);
    }
}

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_row_height(
    struct yetty_yclass_object *obj, int32_t row, float height)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_spreadsheet_set_row_height: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yrich_spreadsheet_set_row_height");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void, remote_id_r,
            "yetty_yrich_spreadsheet_set_row_height: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            int32_t row;
            float height;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            row, height};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_spreadsheet_set_row_height", &wire_args,
            sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_spreadsheet_set_row_height: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_row_height);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yrich_spreadsheet_set_row_height: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_spreadsheet_set_row_height: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_spreadsheet_set_row_height: dispatch_lookup failed");
        return ((yetty_yrich_spreadsheet_set_row_height_fn)dispatch_impl_r.value)(obj, row, height);
    }
}

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_col_width(
    struct yetty_yclass_object *obj, int32_t col, float width)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_spreadsheet_set_col_width: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yrich_spreadsheet_set_col_width");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void, remote_id_r,
            "yetty_yrich_spreadsheet_set_col_width: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            int32_t col;
            float width;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            col, width};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_spreadsheet_set_col_width", &wire_args,
            sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_spreadsheet_set_col_width: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_col_width);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yrich_spreadsheet_set_col_width: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_spreadsheet_set_col_width: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_spreadsheet_set_col_width: dispatch_lookup failed");
        return ((yetty_yrich_spreadsheet_set_col_width_fn)dispatch_impl_r.value)(obj, col, width);
    }
}

struct yetty_ycore_void_result yetty_yrich_spreadsheet_set_cell_value(
    struct yetty_yclass_object *obj, int32_t row, int32_t col, struct yetty_ycore_buffer value)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_spreadsheet_set_cell_value: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yrich_spreadsheet_set_cell_value");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void, remote_id_r,
            "yetty_yrich_spreadsheet_set_cell_value: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            int32_t row;
            int32_t col;
            uint32_t value_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            row, col, (uint32_t)value.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)value.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_spreadsheet_set_cell_value: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, value.data, value.size);
        body_offset += value.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_spreadsheet_set_cell_value", body_buf,
            body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_spreadsheet_set_cell_value: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_spreadsheet_set_cell_value);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yrich_spreadsheet_set_cell_value: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_spreadsheet_set_cell_value: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_spreadsheet_set_cell_value: dispatch_lookup failed");
        return ((yetty_yrich_spreadsheet_set_cell_value_fn)dispatch_impl_r.value)(obj, row, col,
                                                                                  value);
    }
}
