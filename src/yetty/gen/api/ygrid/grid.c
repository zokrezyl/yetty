/* GENERATED — do not edit. */
#include <yetty/api/ygrid/grid.h>

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

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ygrid_add_record_fn)(struct yetty_yclass_object *,
                                                                    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ygrid_clear_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygrid_destroy_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_ygrid_add_record");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygrid_add_record: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t record_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            (uint32_t)record.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)record.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, record.data, record.size);
        body_offset += record.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_ygrid_add_record", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygrid_add_record: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_add_record);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_add_record: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygrid_add_record: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygrid_add_record: dispatch_lookup failed");
        return ((yetty_ygrid_add_record_fn)dispatch_impl_r.value)(obj, record);
    }
}

struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ygrid_clear");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygrid_clear: ensure_remote_id_by_name failed");
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
            obj->session, remote_id, "yetty_ygrid_clear", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygrid_clear: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_clear);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_clear: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygrid_clear: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygrid_clear: dispatch_lookup failed");
        return ((yetty_ygrid_clear_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id_by_name(obj->session, "yetty_ygrid_destroy");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygrid_destroy: ensure_remote_id_by_name failed");
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
            obj->session, remote_id, "yetty_ygrid_destroy", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygrid_destroy: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_ygrid", (yetty_yclass_method_id_t)yetty_ygrid_destroy);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_ygrid_destroy: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygrid_destroy: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygrid_destroy: dispatch_lookup failed");
        return ((yetty_ygrid_destroy_fn)dispatch_impl_r.value)(obj);
    }
}
