/* GENERATED — do not edit. */
#include "yetty/yplatform/methods.gen.h"
#include "yetty/yplatform/window-manager.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h> /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result yetty_yplatform_window_manager_configure(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, void *os_window,
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_manager_configure);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_configure: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_manager_configure: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_configure: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_configure_fn)dispatch_impl_r.value)(
        ctx, obj, os_window, output_pipe, input_pipe);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_destroy(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_manager_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_manager_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_destroy: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_destroy_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_iconify(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_manager_iconify);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_iconify: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_manager_iconify: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_iconify: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_iconify: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_iconify_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_toggle_maximize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform",
            (yetty_yclass_method_id_t)yetty_yplatform_window_manager_toggle_maximize);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(
                yetty_ycore_void,
                "yetty_yplatform_window_manager_toggle_maximize: method_slot_get failed",
                method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yplatform_window_manager_toggle_maximize: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_toggle_maximize: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_toggle_maximize: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_toggle_maximize_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_request_close(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform",
            (yetty_yclass_method_id_t)yetty_yplatform_window_manager_request_close);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_request_close: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yplatform_window_manager_request_close: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_request_close: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_request_close: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_request_close_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_drag_by(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int dx, int dy)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_manager_drag_by);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_drag_by: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_manager_drag_by: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_drag_by: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_drag_by: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_drag_by_fn)dispatch_impl_r.value)(ctx, obj, dx, dy);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_resize_by(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int dx, int dy, int edge)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_manager_resize_by);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_resize_by: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_manager_resize_by: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_resize_by: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_resize_by: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_resize_by_fn)dispatch_impl_r.value)(ctx, obj, dx, dy,
                                                                                edge);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_move(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform",
            (yetty_yclass_method_id_t)yetty_yplatform_window_manager_begin_interactive_move);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(
                yetty_ycore_void,
                "yetty_yplatform_window_manager_begin_interactive_move: method_slot_get failed",
                method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yplatform_window_manager_begin_interactive_move: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(
        yetty_ycore_void, object_class_r,
        "yetty_yplatform_window_manager_begin_interactive_move: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(
        yetty_ycore_void, dispatch_impl_r,
        "yetty_yplatform_window_manager_begin_interactive_move: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_begin_interactive_move_fn)dispatch_impl_r.value)(ctx,
                                                                                             obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_resize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int edge)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform",
            (yetty_yclass_method_id_t)yetty_yplatform_window_manager_begin_interactive_resize);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(
                yetty_ycore_void,
                "yetty_yplatform_window_manager_begin_interactive_resize: method_slot_get failed",
                method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yplatform_window_manager_begin_interactive_resize: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(
        yetty_ycore_void, object_class_r,
        "yetty_yplatform_window_manager_begin_interactive_resize: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(
        yetty_ycore_void, dispatch_impl_r,
        "yetty_yplatform_window_manager_begin_interactive_resize: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_begin_interactive_resize_fn)dispatch_impl_r.value)(
        ctx, obj, edge);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_set_cursor(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int shape)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_manager_set_cursor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_set_cursor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yplatform_window_manager_set_cursor: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_set_cursor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_set_cursor: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_set_cursor_fn)dispatch_impl_r.value)(ctx, obj, shape);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_handle_event(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_yui_event *event)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yplatform",
            (yetty_yclass_method_id_t)yetty_yplatform_window_manager_handle_event);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yplatform_window_manager_handle_event: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_yplatform_window_manager_handle_event: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yplatform_window_manager_handle_event: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yplatform_window_manager_handle_event: dispatch_lookup failed");
    return ((yetty_yplatform_window_manager_handle_event_fn)dispatch_impl_r.value)(ctx, obj, event);
}
