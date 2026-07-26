/* GENERATED — do not edit. */
#include <yetty/api/ydummy/canvas.h>

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

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ydummy_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ydummy_set_shader(struct yetty_yclass_object * obj, struct yetty_ycore_buffer wgsl);
struct yetty_ycore_void_result yetty_ydummy_set_rect(struct yetty_yclass_object * obj, float min_x, float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_ydummy_set_time(struct yetty_yclass_object * obj, float seconds);
struct yetty_ycore_void_result yetty_ydummy_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ydummy_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydummy_set_shader_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ydummy_set_rect_fn)(struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ydummy_set_time_fn)(struct yetty_yclass_object *, float);
typedef struct yetty_ycore_void_result (*yetty_ydummy_destroy_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ydummy_set_shader(struct yetty_yclass_object * obj, struct yetty_ycore_buffer wgsl)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_shader: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ydummy_set_shader");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ydummy_set_shader: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t wgsl_len;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, (uint32_t)wgsl.size };
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)wgsl.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_shader: body buf oom");
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, wgsl.data, wgsl.size);
        body_offset += wgsl.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ydummy_set_shader", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ydummy_set_shader: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_ydummy",
                                             (yetty_yclass_method_id_t)yetty_ydummy_set_shader);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_shader: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ydummy_set_shader: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ydummy_set_shader: dispatch_lookup failed");
        return ((yetty_ydummy_set_shader_fn)dispatch_impl_r.value)(obj, wgsl);
    }
}

struct yetty_ycore_void_result yetty_ydummy_set_rect(struct yetty_yclass_object * obj, float min_x, float min_y, float max_x, float max_y)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_rect: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ydummy_set_rect");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ydummy_set_rect: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float min_x;
            float min_y;
            float max_x;
            float max_y;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, min_x, min_y, max_x, max_y };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ydummy_set_rect", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ydummy_set_rect: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_ydummy",
                                             (yetty_yclass_method_id_t)yetty_ydummy_set_rect);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_rect: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ydummy_set_rect: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ydummy_set_rect: dispatch_lookup failed");
        return ((yetty_ydummy_set_rect_fn)dispatch_impl_r.value)(obj, min_x, min_y, max_x, max_y);
    }
}

struct yetty_ycore_void_result yetty_ydummy_set_time(struct yetty_yclass_object * obj, float seconds)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_time: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ydummy_set_time");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ydummy_set_time: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float seconds;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, seconds };
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ydummy_set_time", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ydummy_set_time: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_ydummy",
                                             (yetty_yclass_method_id_t)yetty_ydummy_set_time);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_set_time: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ydummy_set_time: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ydummy_set_time: dispatch_lookup failed");
        return ((yetty_ydummy_set_time_fn)dispatch_impl_r.value)(obj, seconds);
    }
}

struct yetty_ycore_void_result yetty_ydummy_destroy(struct yetty_yclass_object * obj)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_destroy: NULL object");

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ydummy_destroy");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ydummy_destroy: ensure_remote_id_by_name failed");
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
            obj->session, remote_id, "yetty_ydummy_destroy", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ydummy_destroy: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r =
                yetty_yclass_method_slot_get("yetty_ydummy",
                                             (yetty_yclass_method_id_t)yetty_ydummy_destroy);
            if (YETTY_IS_ERR(method_slot_r))
                return YETTY_ERR(yetty_ycore_void, "yetty_ydummy_destroy: method_slot_get failed", method_slot_r);
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ydummy_destroy: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ydummy_destroy: dispatch_lookup failed");
        return ((yetty_ydummy_destroy_fn)dispatch_impl_r.value)(obj);
    }
}

