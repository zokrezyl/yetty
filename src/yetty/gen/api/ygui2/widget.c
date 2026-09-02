/* GENERATED — do not edit. */
#include <yetty/api/ygui2/widget.h>

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

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_ygui2_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_destructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_widget_paint(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_void_result yetty_ygui2_widget_paint_retained(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_void_result yetty_ygui2_widget_emit_geometry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_int_result yetty_ygui2_widget_on_press(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y, int button,
                                                          int mods);
struct yetty_ycore_int_result yetty_ygui2_widget_on_release(struct yetty_yclass_object *obj,
                                                            float local_x, float local_y,
                                                            int button, int mods);
struct yetty_ycore_int_result yetty_ygui2_widget_on_motion(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           uint32_t buttons_held);
struct yetty_ycore_int_result yetty_ygui2_widget_on_scroll(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           float wheel_dy);
struct yetty_ycore_int_result yetty_ygui2_widget_on_key(struct yetty_yclass_object *obj,
                                                        uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_ygui2_widget_cleanup(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ygui2_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_paint_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_paint_retained_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_emit_geometry_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_press_fn)(
    struct yetty_yclass_object *, float, float, int, int);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int, int);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_motion_fn)(
    struct yetty_yclass_object *, float, float, uint32_t);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_scroll_fn)(
    struct yetty_yclass_object *, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ygui2_widget_on_key_fn)(struct yetty_yclass_object *,
                                                                      uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ygui2_widget_cleanup_fn)(
    struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ygui2_destructor(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui2_destructor: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_destructor");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui2_destructor: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ygui2_destructor", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui2_destructor: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_destructor);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_ygui2_destructor: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui2_destructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui2_destructor: dispatch_lookup failed");
        return ((yetty_ygui2_destructor_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_ygui2_widget_paint(struct yetty_yclass_object *obj,
                                                        struct yetty_ydraw_drawable_list *list)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui2_widget_paint: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_paint");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui2_widget_paint: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t list_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            container_of((struct yetty_yclass_object *)list, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ygui2_widget_paint", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui2_widget_paint: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_paint);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_ygui2_widget_paint: method_slot_get failed", method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui2_widget_paint: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui2_widget_paint: dispatch_lookup failed");
        return ((yetty_ygui2_widget_paint_fn)dispatch_impl_r.value)(obj, list);
    }
}

struct yetty_ycore_void_result yetty_ygui2_widget_paint_retained(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui2_widget_paint_retained: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_paint_retained");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui2_widget_paint_retained: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t list_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            container_of((struct yetty_yclass_object *)list, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_ygui2_widget_paint_retained",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui2_widget_paint_retained: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_paint_retained);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_ygui2_widget_paint_retained: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui2_widget_paint_retained: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui2_widget_paint_retained: dispatch_lookup failed");
        return ((yetty_ygui2_widget_paint_retained_fn)dispatch_impl_r.value)(obj, list);
    }
}

struct yetty_ycore_void_result yetty_ygui2_widget_emit_geometry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *list)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui2_widget_emit_geometry: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_emit_geometry");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui2_widget_emit_geometry: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t list_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            container_of((struct yetty_yclass_object *)list, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_ygui2_widget_emit_geometry",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui2_widget_emit_geometry: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_emit_geometry);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_ygui2_widget_emit_geometry: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui2_widget_emit_geometry: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui2_widget_emit_geometry: dispatch_lookup failed");
        return ((yetty_ygui2_widget_emit_geometry_fn)dispatch_impl_r.value)(obj, list);
    }
}

struct yetty_ycore_int_result yetty_ygui2_widget_on_press(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y, int button,
                                                          int mods)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_press: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_on_press");
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui2_widget_on_press: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float local_x;
            float local_y;
            int button;
            int mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            local_x, local_y, button, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui2_widget_on_press: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_press: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui2_widget_on_press: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_press: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_on_press);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui2_widget_on_press: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui2_widget_on_press: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui2_widget_on_press: dispatch_lookup failed");
        return ((yetty_ygui2_widget_on_press_fn)dispatch_impl_r.value)(obj, local_x, local_y,
                                                                       button, mods);
    }
}

struct yetty_ycore_int_result yetty_ygui2_widget_on_release(struct yetty_yclass_object *obj,
                                                            float local_x, float local_y,
                                                            int button, int mods)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_release: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_on_release");
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui2_widget_on_release: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float local_x;
            float local_y;
            int button;
            int mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            local_x, local_y, button, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui2_widget_on_release: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_release: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui2_widget_on_release: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui2_widget_on_release: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_on_release);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui2_widget_on_release: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui2_widget_on_release: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui2_widget_on_release: dispatch_lookup failed");
        return ((yetty_ygui2_widget_on_release_fn)dispatch_impl_r.value)(obj, local_x, local_y,
                                                                         button, mods);
    }
}

struct yetty_ycore_int_result yetty_ygui2_widget_on_motion(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           uint32_t buttons_held)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_motion: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_on_motion");
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui2_widget_on_motion: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float local_x;
            float local_y;
            uint32_t buttons_held;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            local_x, local_y, buttons_held};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui2_widget_on_motion: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_motion: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui2_widget_on_motion: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui2_widget_on_motion: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_on_motion);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui2_widget_on_motion: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui2_widget_on_motion: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui2_widget_on_motion: dispatch_lookup failed");
        return ((yetty_ygui2_widget_on_motion_fn)dispatch_impl_r.value)(obj, local_x, local_y,
                                                                        buttons_held);
    }
}

struct yetty_ycore_int_result yetty_ygui2_widget_on_scroll(struct yetty_yclass_object *obj,
                                                           float local_x, float local_y,
                                                           float wheel_dy)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_scroll: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_on_scroll");
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui2_widget_on_scroll: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float local_x;
            float local_y;
            float wheel_dy;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            local_x, local_y, wheel_dy};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui2_widget_on_scroll: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_scroll: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui2_widget_on_scroll: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui2_widget_on_scroll: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_on_scroll);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui2_widget_on_scroll: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui2_widget_on_scroll: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui2_widget_on_scroll: dispatch_lookup failed");
        return ((yetty_ygui2_widget_on_scroll_fn)dispatch_impl_r.value)(obj, local_x, local_y,
                                                                        wheel_dy);
    }
}

struct yetty_ycore_int_result yetty_ygui2_widget_on_key(struct yetty_yclass_object *obj,
                                                        uint32_t key, uint32_t mods)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_key: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_on_key");
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui2_widget_on_key: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t key;
            uint32_t mods;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            key, mods};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui2_widget_on_key: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_key: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error =
                YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_key: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui2_widget_on_key: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_on_key);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_int,
                                 "yetty_ygui2_widget_on_key: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui2_widget_on_key: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui2_widget_on_key: dispatch_lookup failed");
        return ((yetty_ygui2_widget_on_key_fn)dispatch_impl_r.value)(obj, key, mods);
    }
}

struct yetty_ycore_void_result yetty_ygui2_widget_cleanup(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui2_widget_cleanup: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygui2_widget_cleanup");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui2_widget_cleanup: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ygui2_widget_cleanup", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui2_widget_cleanup: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygui2", (yetty_yclass_method_id_t)yetty_ygui2_widget_cleanup);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_ygui2_widget_cleanup: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui2_widget_cleanup: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui2_widget_cleanup: dispatch_lookup failed");
        return ((yetty_ygui2_widget_cleanup_fn)dispatch_impl_r.value)(obj);
    }
}
