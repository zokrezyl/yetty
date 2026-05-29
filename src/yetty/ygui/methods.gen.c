/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include <yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx * _yc_ctx, struct yetty_yclass_object * _yc_obj, float x, float y, int button)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!_yc_obj) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: NULL object");

    struct yetty_yclass_ctx *rpc_ctx = _yc_ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r, "yetty_ygui_widget_on_press: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
        struct __attribute__((packed)) {
            uint64_t _yc_obj_handle;
            float x;
            float y;
            int button;
        } wire_args = { container_of((struct yetty_yclass_object *)_yc_obj, struct yetty_yclass_proxy, header)->handle, x, y, button };
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r, "yetty_ygui_widget_on_press: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: remote impl returned error");
        if (response_len != sizeof(resp_buf)) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_press: truncated RPC payload");
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(_yc_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ygui_widget_on_press: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ygui_widget_on_press: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_press_fn)dispatch_impl_r.value)(_yc_ctx, _yc_obj, x, y, button);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx * _yc_ctx, struct yetty_yclass_object * _yc_obj, float x, float y, int button)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!_yc_obj) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: NULL object");

    struct yetty_yclass_ctx *rpc_ctx = _yc_ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r, "yetty_ygui_widget_on_release: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
        struct __attribute__((packed)) {
            uint64_t _yc_obj_handle;
            float x;
            float y;
            int button;
        } wire_args = { container_of((struct yetty_yclass_object *)_yc_obj, struct yetty_yclass_proxy, header)->handle, x, y, button };
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r, "yetty_ygui_widget_on_release: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: remote impl returned error");
        if (response_len != sizeof(resp_buf)) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_release: truncated RPC payload");
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(_yc_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ygui_widget_on_release: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ygui_widget_on_release: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_release_fn)dispatch_impl_r.value)(_yc_ctx, _yc_obj, x, y, button);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_ctx * _yc_ctx, struct yetty_yclass_object * _yc_obj, struct yetty_ygui_emit_ctx * ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!_yc_obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_body: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(_yc_obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ygui_widget_emit_body: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ygui_widget_emit_body: dispatch_lookup failed");
    return ((yetty_ygui_widget_emit_body_fn)dispatch_impl_r.value)(_yc_ctx, _yc_obj, ctx);
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_constructor);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: NULL object");

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ygui_constructor: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygui_constructor: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ygui_constructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ygui_constructor: dispatch_lookup failed");
        return ((yetty_ygui_constructor_fn)dispatch_impl_r.value)(ctx, obj);
    }
}

struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_destructor);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: NULL object");

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r, "yetty_ygui_destructor: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle };
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r, "yetty_ygui_destructor: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_destructor: remote impl returned error");
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ygui_destructor: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ygui_destructor: dispatch_lookup failed");
        return ((yetty_ygui_destructor_fn)dispatch_impl_r.value)(ctx, obj);
    }
}

struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float x, float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: NULL object");

    struct yetty_yclass_ctx *rpc_ctx = ctx;
    if (rpc_ctx && rpc_ctx->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(rpc_ctx->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, remote_id_r, "yetty_ygui_widget_on_motion: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
        struct __attribute__((packed)) {
            uint64_t obj_handle;
            float x;
            float y;
        } wire_args = { container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)->handle, x, y };
        uint8_t resp_buf[1 + sizeof(int)];
        struct yetty_ycore_size_result rpc_call_r = yetty_yclass_rpc_call(
            rpc_ctx->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args, sizeof(wire_args),
            resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rpc_call_r, "yetty_ygui_widget_on_motion: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: short RPC response");
        if (resp_buf[0] != 0) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: remote impl returned error");
        if (response_len != sizeof(resp_buf)) return YETTY_ERR(yetty_ycore_int, "yetty_ygui_widget_on_motion: truncated RPC payload");
        int return_value;
        memcpy(&return_value, resp_buf + 1, sizeof(return_value));
        return YETTY_OK(yetty_ycore_int, return_value);
    } else {
        struct yetty_yclass_ptr_result object_class_r =
            yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ygui_widget_on_motion: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ygui_widget_on_motion: dispatch_lookup failed");
        return ((yetty_ygui_widget_on_motion_fn)dispatch_impl_r.value)(ctx, obj, x, y);
    }
}

struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_paint);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_paint: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ygui_widget_paint: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ygui_widget_paint: dispatch_lookup failed");
    return ((yetty_ygui_widget_paint_fn)dispatch_impl_r.value)(ctx, obj, emit_ctx);
}

struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ygui", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ygui_widget_emit_container: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ygui_widget_emit_container: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ygui_widget_emit_container: dispatch_lookup failed");
    return ((yetty_ygui_widget_emit_container_fn)dispatch_impl_r.value)(ctx, obj, emit_ctx);
}

