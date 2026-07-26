/* GENERATED — do not edit. */
#include <yetty/api/yrich/document.h>

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
struct yetty_ycore_void_result;
struct yetty_yrich_operation;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object * obj, struct yetty_yrich_operation * op, int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object * obj, uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object * obj, struct yetty_ycore_buffer text);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_undo_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_redo_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_up_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);

struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_destroy: dispatch_lookup failed");
    return ((yetty_yrich_document_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_width: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_content_width");
        YETTY_RETURN_IF_ERR(yetty_ycore_float, remote_id_r, "yetty_yrich_document_content_width: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_alloc(
            obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, rpc_call_r, "yetty_yrich_document_content_width: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_width: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_float_result remote_error =
                YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_width: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(float)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_width: truncated RPC payload");
        }
        float return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_float, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_content_width);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_width: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, object_class_r, "yetty_yrich_document_content_width: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, dispatch_impl_r, "yetty_yrich_document_content_width: dispatch_lookup failed");
        return ((yetty_yrich_document_content_width_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_float_result yetty_yrich_document_content_height(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_height: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_content_height");
        YETTY_RETURN_IF_ERR(yetty_ycore_float, remote_id_r, "yetty_yrich_document_content_height: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_alloc(
            obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, rpc_call_r, "yetty_yrich_document_content_height: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_height: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_float_result remote_error =
                YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_height: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(float)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_height: truncated RPC payload");
        }
        float return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_float, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_content_height);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_float, "yetty_yrich_document_content_height: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, object_class_r, "yetty_yrich_document_content_height: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_float, dispatch_impl_r, "yetty_yrich_document_content_height: dispatch_lookup failed");
        return ((yetty_yrich_document_content_height_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_render: dispatch_lookup failed");
    return ((yetty_yrich_document_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object * obj, struct yetty_yrich_operation * op, int local_flag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_document_apply_op);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_apply_op: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_apply_op: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_apply_op: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_apply_op: dispatch_lookup failed");
    return ((yetty_yrich_document_apply_op_fn)dispatch_impl_r.value)(obj, op, local_flag);
}

struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_undo: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_undo");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_undo: ensure_remote_id_by_name failed");
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
            obj->session, remote_id, "yetty_yrich_document_undo", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_undo: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_undo);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_undo: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_undo: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_undo: dispatch_lookup failed");
        return ((yetty_yrich_document_undo_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_redo: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_redo");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_redo: ensure_remote_id_by_name failed");
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
            obj->session, remote_id, "yetty_yrich_document_redo", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_redo: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_redo);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_redo: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_redo: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_redo: dispatch_lookup failed");
        return ((yetty_yrich_document_redo_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_down: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_on_mouse_down");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_on_mouse_down: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, x, y, button, mods };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_document_on_mouse_down", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_on_mouse_down: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_down);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_down: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_on_mouse_down: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_on_mouse_down: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_down_fn)dispatch_impl_r.value)(obj, x, y, button, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_up: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_on_mouse_up");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_on_mouse_up: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, x, y, button, mods };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_document_on_mouse_up", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_on_mouse_up: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_up);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_up: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_on_mouse_up: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_on_mouse_up: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_up_fn)dispatch_impl_r.value)(obj, x, y, button, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_drag: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_on_mouse_drag");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_on_mouse_drag: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, x, y, button, mods };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_document_on_mouse_drag", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_on_mouse_drag: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_drag);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_drag: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_on_mouse_drag: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_on_mouse_drag: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_drag_fn)dispatch_impl_r.value)(obj, x, y, button, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_double_click: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_on_mouse_double_click");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_on_mouse_double_click: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            uint32_t button;
            uint32_t mods;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, x, y, button, mods };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_document_on_mouse_double_click", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_on_mouse_double_click: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_double_click);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_mouse_double_click: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_on_mouse_double_click: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_on_mouse_double_click: dispatch_lookup failed");
        return ((yetty_yrich_document_on_mouse_double_click_fn)dispatch_impl_r.value)(obj, x, y, button, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object * obj, uint32_t key, uint32_t mods)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_key_down: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_on_key_down");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_on_key_down: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t key;
            uint32_t mods;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, key, mods };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_document_on_key_down", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_on_key_down: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_on_key_down);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_key_down: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_on_key_down: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_on_key_down: dispatch_lookup failed");
        return ((yetty_yrich_document_on_key_down_fn)dispatch_impl_r.value)(obj, key, mods);
    }
}

struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object * obj, struct yetty_ycore_buffer text)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_text_input: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_yrich_document_on_text_input");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_yrich_document_on_text_input: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t text_len;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, (uint32_t)text.size };
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)text.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_text_input: body buf oom");
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, text.data, text.size);
        body_offset += text.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yrich_document_on_text_input", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_yrich_document_on_text_input: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_yrich",
                                             (yetty_yclass_method_id_t)yetty_yrich_document_on_text_input);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_yrich_document_on_text_input: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yrich_document_on_text_input: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yrich_document_on_text_input: dispatch_lookup failed");
        return ((yetty_yrich_document_on_text_input_fn)dispatch_impl_r.value)(obj, text);
    }
}

