/* GENERATED — do not edit. */
#include <yetty/api/yfigure/container.h>

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

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_rectangle;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_yfigure_figure;
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_object *obj,
                                                                uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_object *obj,
                                                               uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_create_child(struct yetty_yclass_object *obj,
                                                          uint32_t kind_token, uint32_t id,
                                                          struct yetty_ycore_rectangle rect,
                                                          struct yetty_ycore_buffer init);
struct yetty_ycore_void_result yetty_yfigure_delete_child(struct yetty_yclass_object *obj,
                                                          uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_set_child_rect(struct yetty_yclass_object *obj,
                                                            uint32_t id,
                                                            struct yetty_ycore_rectangle rect);
struct yetty_ycore_void_result yetty_yfigure_set_rect(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_rectangle rect);
struct yetty_ycore_void_result yetty_yfigure_set_child_z(struct yetty_yclass_object *obj,
                                                         uint32_t id, int32_t z);
struct yetty_ycore_void_result yetty_yfigure_set_child_hidden(struct yetty_yclass_object *obj,
                                                              uint32_t id, uint32_t hidden);
struct yetty_ycore_void_result yetty_yfigure_set_child_scroll(struct yetty_yclass_object *obj,
                                                              uint32_t id, float scroll_x,
                                                              float scroll_y);
struct yetty_ycore_void_result yetty_yfigure_set_child_content_size(struct yetty_yclass_object *obj,
                                                                    uint32_t id, float content_w,
                                                                    float content_h);
struct yetty_ycore_void_result yetty_yfigure_apply_child_body(struct yetty_yclass_object *obj,
                                                              uint32_t id,
                                                              struct yetty_ycore_buffer body);
struct yetty_ycore_void_result yetty_yfigure_clear_all(struct yetty_yclass_object *obj);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_object *obj,
                                                            int indent);
typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_object *,
                                                                  struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_constructor_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_add_child_fn)(struct yetty_yclass_object *,
                                                                     struct yetty_yfigure_figure *,
                                                                     uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_remove_child_by_id_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_raise_child_by_id_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_create_child_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, struct yetty_ycore_rectangle,
    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yfigure_delete_child_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_child_rect_fn)(
    struct yetty_yclass_object *, uint32_t, struct yetty_ycore_rectangle);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_rect_fn)(struct yetty_yclass_object *,
                                                                    struct yetty_ycore_rectangle);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_child_z_fn)(struct yetty_yclass_object *,
                                                                       uint32_t, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_child_hidden_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_child_scroll_fn)(
    struct yetty_yclass_object *, uint32_t, float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_child_content_size_fn)(
    struct yetty_yclass_object *, uint32_t, float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_apply_child_body_fn)(
    struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yfigure_clear_all_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(
    struct yetty_yclass_object *, int);

struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_add_child: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_add_child");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_add_child: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint64_t child_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            container_of((struct yetty_yclass_object *)child, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_add_child", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_add_child: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_add_child);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_add_child: method_slot_get failed", method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_add_child: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_add_child: dispatch_lookup failed");
        return ((yetty_yfigure_add_child_fn)dispatch_impl_r.value)(obj, child, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_object *obj,
                                                                uint32_t id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_remove_child_by_id: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_remove_child_by_id");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_remove_child_by_id: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yfigure_remove_child_by_id",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_remove_child_by_id: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_remove_child_by_id: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_remove_child_by_id: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_remove_child_by_id: dispatch_lookup failed");
        return ((yetty_yfigure_remove_child_by_id_fn)dispatch_impl_r.value)(obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_object *obj,
                                                               uint32_t id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_raise_child_by_id: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_raise_child_by_id");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_raise_child_by_id: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yfigure_raise_child_by_id",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_raise_child_by_id: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_raise_child_by_id: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_raise_child_by_id: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_raise_child_by_id: dispatch_lookup failed");
        return ((yetty_yfigure_raise_child_by_id_fn)dispatch_impl_r.value)(obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_create_child(struct yetty_yclass_object *obj,
                                                          uint32_t kind_token, uint32_t id,
                                                          struct yetty_ycore_rectangle rect,
                                                          struct yetty_ycore_buffer init)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_create_child: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_create_child");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_create_child: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t kind_token;
            uint32_t id;
            struct yetty_ycore_rectangle rect;
            uint32_t init_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            kind_token, id, rect, (uint32_t)init.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)init.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_create_child: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, init.data, init.size);
        body_offset += init.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_create_child", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_create_child: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_create_child);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_create_child: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_create_child: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_create_child: dispatch_lookup failed");
        return ((yetty_yfigure_create_child_fn)dispatch_impl_r.value)(obj, kind_token, id, rect,
                                                                      init);
    }
}

struct yetty_ycore_void_result yetty_yfigure_delete_child(struct yetty_yclass_object *obj,
                                                          uint32_t id)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_delete_child: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_delete_child");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_delete_child: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_delete_child", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_delete_child: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_delete_child);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_delete_child: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_delete_child: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_delete_child: dispatch_lookup failed");
        return ((yetty_yfigure_delete_child_fn)dispatch_impl_r.value)(obj, id);
    }
}

struct yetty_ycore_void_result yetty_yfigure_set_child_rect(struct yetty_yclass_object *obj,
                                                            uint32_t id,
                                                            struct yetty_ycore_rectangle rect)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_child_rect: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_set_child_rect");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_set_child_rect: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
            struct yetty_ycore_rectangle rect;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id, rect};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_set_child_rect", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_set_child_rect: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_child_rect);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_set_child_rect: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_set_child_rect: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_set_child_rect: dispatch_lookup failed");
        return ((yetty_yfigure_set_child_rect_fn)dispatch_impl_r.value)(obj, id, rect);
    }
}

struct yetty_ycore_void_result yetty_yfigure_set_rect(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_rectangle rect)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_rect: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_set_rect");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_set_rect: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            struct yetty_ycore_rectangle rect;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            rect};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_set_rect", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_set_rect: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_rect);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_rect: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_set_rect: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_set_rect: dispatch_lookup failed");
        return ((yetty_yfigure_set_rect_fn)dispatch_impl_r.value)(obj, rect);
    }
}

struct yetty_ycore_void_result yetty_yfigure_set_child_z(struct yetty_yclass_object *obj,
                                                         uint32_t id, int32_t z)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_child_z: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_set_child_z");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_set_child_z: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
            int32_t z;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id, z};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_set_child_z", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_set_child_z: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_child_z);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_set_child_z: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_set_child_z: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_set_child_z: dispatch_lookup failed");
        return ((yetty_yfigure_set_child_z_fn)dispatch_impl_r.value)(obj, id, z);
    }
}

struct yetty_ycore_void_result yetty_yfigure_set_child_hidden(struct yetty_yclass_object *obj,
                                                              uint32_t id, uint32_t hidden)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_child_hidden: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_set_child_hidden");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_set_child_hidden: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
            uint32_t hidden;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id, hidden};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yfigure_set_child_hidden",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_set_child_hidden: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_child_hidden);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_set_child_hidden: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_set_child_hidden: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_set_child_hidden: dispatch_lookup failed");
        return ((yetty_yfigure_set_child_hidden_fn)dispatch_impl_r.value)(obj, id, hidden);
    }
}

struct yetty_ycore_void_result yetty_yfigure_set_child_scroll(struct yetty_yclass_object *obj,
                                                              uint32_t id, float scroll_x,
                                                              float scroll_y)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_child_scroll: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_set_child_scroll");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_set_child_scroll: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
            float scroll_x;
            float scroll_y;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id, scroll_x, scroll_y};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_void(obj->session, remote_id, "yetty_yfigure_set_child_scroll",
                                       &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_set_child_scroll: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_child_scroll);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_set_child_scroll: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_set_child_scroll: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_set_child_scroll: dispatch_lookup failed");
        return ((yetty_yfigure_set_child_scroll_fn)dispatch_impl_r.value)(obj, id, scroll_x,
                                                                          scroll_y);
    }
}

struct yetty_ycore_void_result yetty_yfigure_set_child_content_size(struct yetty_yclass_object *obj,
                                                                    uint32_t id, float content_w,
                                                                    float content_h)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_set_child_content_size: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_set_child_content_size");
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void, remote_id_r,
            "yetty_yfigure_set_child_content_size: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
            float content_w;
            float content_h;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id, content_w, content_h};
#pragma pack(pop)
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_set_child_content_size", &wire_args,
            sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_set_child_content_size: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_set_child_content_size);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_set_child_content_size: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_set_child_content_size: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_set_child_content_size: dispatch_lookup failed");
        return ((yetty_yfigure_set_child_content_size_fn)dispatch_impl_r.value)(obj, id, content_w,
                                                                                content_h);
    }
}

struct yetty_ycore_void_result yetty_yfigure_apply_child_body(struct yetty_yclass_object *obj,
                                                              uint32_t id,
                                                              struct yetty_ycore_buffer body)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_apply_child_body: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_apply_child_body");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_apply_child_body: ensure_remote_id_by_name failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t id;
            uint32_t body_len;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            id, (uint32_t)body.size};
#pragma pack(pop)
        size_t body_total = sizeof(wire_args) + (size_t)body.size;
        uint8_t *body_buf = (uint8_t *)malloc(body_total ? body_total : 1);
        if (!body_buf) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_apply_child_body: body buf oom");
        }
        memcpy(body_buf, &wire_args, sizeof(wire_args));
        size_t body_offset = sizeof(wire_args);
        memcpy(body_buf + body_offset, body.data, body.size);
        body_offset += body.size;
        struct yetty_ycore_void_result rpc_call_r = yetty_yclass_rpc_call_void(
            obj->session, remote_id, "yetty_yfigure_apply_child_body", body_buf, body_total);
        free(body_buf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_apply_child_body: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_apply_child_body);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_apply_child_body: method_slot_get failed",
                                 method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_apply_child_body: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_apply_child_body: dispatch_lookup failed");
        return ((yetty_yfigure_apply_child_body_fn)dispatch_impl_r.value)(obj, id, body);
    }
}

struct yetty_ycore_void_result yetty_yfigure_clear_all(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_clear_all: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r = yetty_yclass_rpc_session_ensure_remote_id_by_name(
            obj->session, "yetty_yfigure_clear_all");
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yfigure_clear_all: ensure_remote_id_by_name failed");
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
            obj->session, remote_id, "yetty_yfigure_clear_all", &wire_args, sizeof(wire_args));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yfigure_clear_all: RPC call failed");
        return YETTY_OK_VOID();
    } else {
        static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
        if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
            struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
                "yetty_yfigure", (yetty_yclass_method_id_t)yetty_yfigure_clear_all);
            if (YETTY_IS_ERR(method_slot_r)) {
                return YETTY_ERR(yetty_ycore_void,
                                 "yetty_yfigure_clear_all: method_slot_get failed", method_slot_r);
            }
            method_slot = method_slot_r.value;
        }
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yfigure_clear_all: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yfigure_clear_all: dispatch_lookup failed");
        return ((yetty_yfigure_clear_all_fn)dispatch_impl_r.value)(obj);
    }
}
