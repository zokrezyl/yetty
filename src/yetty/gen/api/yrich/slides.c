/* GENERATED — do not edit. */
#include <yetty/api/yrich/slides.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

struct yetty_ycore_float_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object * obj, struct yetty_yrich_rect * out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_render(struct yetty_yclass_object * obj, struct yetty_ydraw_drawable_list * drawable_list, uint32_t layer, int selected);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object * obj, struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_object * obj, int32_t index);
struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_set_current_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_next_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_prev_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_object * obj, int32_t index)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_set_current: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_slides_set_current");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_slides_set_current: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            int32_t index;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, index };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_slides_set_current", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_slides_set_current: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_slides_set_current);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_set_current: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_slides_set_current: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_slides_set_current: dispatch_lookup failed");
        return ((yetty_yrich_slides_set_current_fn)dispatch_impl_r.value)(obj, index);
    }
}

struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_next: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_slides_next");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_slides_next: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_slides_next", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_slides_next: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_slides_next);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_next: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_slides_next: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_slides_next: dispatch_lookup failed");
        return ((yetty_yrich_slides_next_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_prev: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_slides_prev");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_slides_prev: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_slides_prev", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_slides_prev: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_slides_prev);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_prev: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_slides_prev: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_slides_prev: dispatch_lookup failed");
        return ((yetty_yrich_slides_prev_fn)dispatch_impl_r.value)(obj);
    }
}

