/* GENERATED — do not edit. */
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

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(
    struct yetty_yclass_object *obj, struct yetty_ygui_emit_ctx *emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_scroll_fn)(
    struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_container_fn)(
    struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_body_fn)(
    struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);

YETTY_MAYBE_UNUSED
static yetty_ygui_constructor_fn yetty_ygui_widget_yetty_ygui_constructor_check =
    widget_default_constructor;
YETTY_MAYBE_UNUSED
static yetty_ygui_destructor_fn yetty_ygui_widget_yetty_ygui_destructor_check =
    widget_default_destructor;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_press_fn yetty_ygui_widget_yetty_ygui_widget_on_press_check =
    widget_default_on_press;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_release_fn yetty_ygui_widget_yetty_ygui_widget_on_release_check =
    widget_default_on_release;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_motion_fn yetty_ygui_widget_yetty_ygui_widget_on_motion_check =
    widget_default_on_motion;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_scroll_fn yetty_ygui_widget_yetty_ygui_widget_on_scroll_check =
    widget_default_on_scroll;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_paint_fn yetty_ygui_widget_yetty_ygui_widget_paint_check =
    widget_default_paint;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_emit_container_fn
    yetty_ygui_widget_yetty_ygui_widget_emit_container_check = widget_default_emit_container;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_emit_body_fn yetty_ygui_widget_yetty_ygui_widget_emit_body_check =
    widget_default_emit_body;

struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_widget");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_widget",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_widget),
        .data_align = _Alignof(struct yetty_ygui_widget),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)widget_default_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
         (yetty_yclass_impl_t)widget_default_destructor},
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
         (yetty_yclass_impl_t)widget_default_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release,
         (yetty_yclass_impl_t)widget_default_on_release},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion,
         (yetty_yclass_impl_t)widget_default_on_motion},
        {"yetty_ygui", "widget_on_scroll", (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll,
         (yetty_yclass_impl_t)widget_default_on_scroll},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint,
         (yetty_yclass_impl_t)widget_default_paint},
        {"yetty_ygui", "widget_emit_container",
         (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container,
         (yetty_yclass_impl_t)widget_default_emit_container},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body,
         (yetty_yclass_impl_t)widget_default_emit_body},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_widget_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_widget_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_widget_ptr_result yetty_ygui_widget_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_widget_ptr, "yetty_ygui_widget_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_widget_ptr, "yetty_ygui_widget_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygui_widget_ptr, (struct yetty_ygui_widget *)slice_r.value);
}

struct yetty_yclass_object *yetty_ygui_widget_to(struct yetty_ygui_widget *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_widget_class_get();
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

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_press: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            int button;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_press: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui_widget_on_press: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_press: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_press: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_press_fn)dispatch_impl_r.value)(obj, x, y, button);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_release: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_release: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            int button;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, button};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_release: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui_widget_on_release: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ygui_widget_on_release: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_release: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_release: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_release_fn)dispatch_impl_r.value)(obj, x, y, button);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *obj, float x,
                                                          float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_motion: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_motion: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui_widget_on_motion: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_motion: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_motion: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_motion_fn)dispatch_impl_r.value)(obj, x, y);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_widget_emit_body: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_widget_emit_body: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_widget_emit_body: dispatch_lookup failed");
    return ((yetty_ygui_widget_emit_body_fn)dispatch_impl_r.value)(obj, emit_ctx);
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_constructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui_constructor: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_ygui_constructor: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error =
                YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui_constructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui_constructor: dispatch_lookup failed");
        return ((yetty_ygui_constructor_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_destructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_ygui_destructor: ensure_remote_id failed");
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
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygui_destructor: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_void_result remote_error =
                YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        free(resp_buf);
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_ygui_destructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_ygui_destructor: dispatch_lookup failed");
        return ((yetty_ygui_destructor_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r,
                            "yetty_ygui_widget_on_scroll: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float x;
            float y;
            float dx;
            float dy;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            x, y, dx, dy};
#pragma pack(pop)
        uint8_t *resp_buf = NULL;
        size_t response_len = 0;
        struct yetty_ycore_void_result rpc_call_r =
            yetty_yclass_rpc_call_alloc(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id,
                                        &wire_args, sizeof(wire_args), &resp_buf, &response_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r,
                            "yetty_ygui_widget_on_scroll: RPC call failed");
        if (response_len < 1) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: short RPC response");
        }
        if (resp_buf[0] != 0) {
            struct yetty_ycore_error *remote_chain =
                yetty_ycore_error_deserialize(resp_buf + 1, response_len - 1);
            free(resp_buf);
            struct yetty_ycore_int_result remote_error = YETTY_ERR(
                yetty_ycore_int, "yetty_ygui_widget_on_scroll: remote impl returned error");
            remote_error.error.cause = remote_chain;
            return remote_error;
        }
        if (response_len != 1 + sizeof(int)) {
            free(resp_buf);
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_scroll: truncated RPC payload");
        }
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        free(resp_buf);
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                            "yetty_ygui_widget_on_scroll: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                            "yetty_ygui_widget_on_scroll: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_scroll_fn)dispatch_impl_r.value)(obj, x, y, dx, dy);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_paint);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_widget_paint: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_widget_paint: dispatch_lookup failed");
    return ((yetty_ygui_widget_paint_fn)dispatch_impl_r.value)(obj, emit_ctx);
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_container(
    struct yetty_yclass_object *obj, struct yetty_ygui_emit_ctx *emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ygui_widget_emit_container: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ygui_widget_emit_container: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ygui_widget_emit_container: dispatch_lookup failed");
    return ((yetty_ygui_widget_emit_container_fn)dispatch_impl_r.value)(obj, emit_ctx);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ygui_widget_on_press_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_press_skel(const void *body, size_t body_len, void *resp,
                                       size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float x;
        float y;
        int button;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_press: handle_resolve",
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
    struct yetty_ycore_int_result call_r =
        yetty_ygui_widget_on_press((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x,
                                   wire_args.y, wire_args.button);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_press", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ygui_widget_on_release_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_release_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float x;
        float y;
        int button;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_release: handle_resolve",
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
    struct yetty_ycore_int_result call_r =
        yetty_ygui_widget_on_release((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x,
                                     wire_args.y, wire_args.button);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_release", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ygui_widget_on_motion_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_motion_skel(const void *body, size_t body_len, void *resp,
                                        size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float x;
        float y;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_motion: handle_resolve",
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
    struct yetty_ycore_int_result call_r = yetty_ygui_widget_on_motion(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x, wire_args.y);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_motion", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_ygui_constructor_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_constructor_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_constructor: handle_resolve",
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
    struct yetty_ycore_void_result call_r =
        yetty_ygui_constructor((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_constructor", call_r.error);
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
size_t yetty_ygui_destructor_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_destructor_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_destructor: handle_resolve",
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
    struct yetty_ycore_void_result call_r =
        yetty_ygui_destructor((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_destructor", call_r.error);
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
size_t yetty_ygui_widget_on_scroll_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_ygui_widget_on_scroll_skel(const void *body, size_t body_len, void *resp,
                                        size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float x;
        float y;
        float dx;
        float dy;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_scroll: handle_resolve",
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
    struct yetty_ycore_int_result call_r =
        yetty_ygui_widget_on_scroll((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.x,
                                    wire_args.y, wire_args.dx, wire_args.dy);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_ygui_widget_on_scroll", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ygui_widget_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_widget");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_widget_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        struct yetty_ycore_void_result ctor_r = yetty_ygui_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) {
                yetty_ycore_error_destroy(free_r.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_ygui_widget_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ygui_widget");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr, "yetty_ygui_widget_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ygui_widget";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_widget_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_widget_create: CREATE returned no/invalid handle");
    }

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ygui_widget_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
