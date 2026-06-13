/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */
/* The folded-in public stubs, rpc skeletons + create() and the
 * registration hooks (formerly methods.gen.c / rpc.gen.c) need
 * these. All header-guarded, so re-including what the hand-written
 * .c already pulled in is harmless; the class's OWN header is
 * still never included (that would redefine its expose'd types). */
#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yui_event;
struct yetty_ycore_void_result yetty_yplatform_window_manager_configure(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, void *os_window,
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe);
struct yetty_ycore_void_result yetty_yplatform_window_manager_destroy(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_iconify(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_toggle_maximize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_request_close(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_drag_by(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int dx, int dy);
struct yetty_ycore_void_result yetty_yplatform_window_manager_resize_by(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int dx, int dy, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_move(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_resize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_manager_set_cursor(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int shape);
struct yetty_ycore_void_result yetty_yplatform_window_manager_handle_event(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_yui_event *event);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_configure_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, void *,
    struct yetty_ycore_xthread_event_pipe *, struct yetty_ycore_xthread_event_pipe *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_iconify_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_toggle_maximize_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_request_close_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_drag_by_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_resize_by_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_begin_interactive_move_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (
    *yetty_yplatform_window_manager_begin_interactive_resize_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_set_cursor_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_handle_event_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const struct yetty_yui_event *);

/* ===== class accessors ===== */

[[maybe_unused]]
static yetty_yplatform_window_manager_configure_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_configure_check =
        window_manager_configure;
[[maybe_unused]]
static yetty_yplatform_window_manager_destroy_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_destroy_check =
        window_manager_destroy;
[[maybe_unused]]
static yetty_yplatform_window_manager_iconify_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_iconify_check =
        window_manager_iconify;
[[maybe_unused]]
static yetty_yplatform_window_manager_toggle_maximize_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_toggle_maximize_check =
        window_manager_toggle_maximize;
[[maybe_unused]]
static yetty_yplatform_window_manager_request_close_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_request_close_check =
        window_manager_request_close;
[[maybe_unused]]
static yetty_yplatform_window_manager_drag_by_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_drag_by_check =
        window_manager_drag_by;
[[maybe_unused]]
static yetty_yplatform_window_manager_resize_by_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_resize_by_check =
        window_manager_resize_by;
[[maybe_unused]]
static yetty_yplatform_window_manager_begin_interactive_move_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_begin_interactive_move_check =
        window_manager_begin_interactive_move;
[[maybe_unused]]
static yetty_yplatform_window_manager_begin_interactive_resize_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_begin_interactive_resize_check =
        window_manager_begin_interactive_resize;
[[maybe_unused]]
static yetty_yplatform_window_manager_set_cursor_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_set_cursor_check =
        window_manager_set_cursor;
[[maybe_unused]]
static yetty_yplatform_window_manager_handle_event_fn
    yetty_yplatform_window_manager_yetty_yplatform_window_manager_handle_event_check =
        window_manager_handle_event;

struct yetty_yclass_ptr_result yetty_yplatform_window_manager_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yplatform_window_manager");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yplatform_window_manager",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yplatform_window_manager),
        .data_align = _Alignof(struct yetty_yplatform_window_manager),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yplatform", "window_manager_configure",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_configure,
         (yetty_yclass_impl_t)window_manager_configure},
        {"yetty_yplatform", "window_manager_destroy",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_destroy,
         (yetty_yclass_impl_t)window_manager_destroy},
        {"yetty_yplatform", "window_manager_iconify",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_iconify,
         (yetty_yclass_impl_t)window_manager_iconify},
        {"yetty_yplatform", "window_manager_toggle_maximize",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_toggle_maximize,
         (yetty_yclass_impl_t)window_manager_toggle_maximize},
        {"yetty_yplatform", "window_manager_request_close",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_request_close,
         (yetty_yclass_impl_t)window_manager_request_close},
        {"yetty_yplatform", "window_manager_drag_by",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_drag_by,
         (yetty_yclass_impl_t)window_manager_drag_by},
        {"yetty_yplatform", "window_manager_resize_by",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_resize_by,
         (yetty_yclass_impl_t)window_manager_resize_by},
        {"yetty_yplatform", "window_manager_begin_interactive_move",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_begin_interactive_move,
         (yetty_yclass_impl_t)window_manager_begin_interactive_move},
        {"yetty_yplatform", "window_manager_begin_interactive_resize",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_begin_interactive_resize,
         (yetty_yclass_impl_t)window_manager_begin_interactive_resize},
        {"yetty_yplatform", "window_manager_set_cursor",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_set_cursor,
         (yetty_yclass_impl_t)window_manager_set_cursor},
        {"yetty_yplatform", "window_manager_handle_event",
         (yetty_yclass_method_id_t)yetty_yplatform_window_manager_handle_event,
         (yetty_yclass_impl_t)window_manager_handle_event},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yplatform_window_manager_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yplatform_window_manager_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yplatform_window_manager_ptr_result yetty_yplatform_window_manager_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_manager_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yplatform_window_manager_ptr,
                         "yetty_yplatform_window_manager_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yplatform_window_manager_ptr,
                         "yetty_yplatform_window_manager_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yplatform_window_manager_ptr,
                    (struct yetty_yplatform_window_manager *)slice_r.value);
}

struct yetty_yclass_object *yetty_yplatform_window_manager_to(
    struct yetty_yplatform_window_manager *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_manager_class_get();
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

/* ===== public method stubs (was methods.gen.c) ===== */

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

/* ===== rpc skeletons + create (was rpc.gen.c) ===== */

struct yetty_yclass_object_ptr_result yetty_yplatform_window_manager_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yplatform_window_manager");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yplatform_window_manager_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_manager_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r = yetty_yclass_rpc_session_translate_class(
            ctx->session, "yetty_yplatform_window_manager");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                                    "yetty_yplatform_window_manager_create: translate_class "
                                    "(degraded — will lazy-resolve)",
                                    translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yplatform_window_manager";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_manager_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yplatform_window_manager_create: CREATE returned no/invalid handle");
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
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_manager_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

/* ---- yplatform/window_manager: class name -> accessor ---------------------- */
static struct yetty_yclass_ptr_result yetty_yplatform_window_manager_accessor_lookup(
    const char *name)
{
    if (strcmp(name, "yetty_yplatform_window_manager") == 0) {
        return yetty_yplatform_window_manager_class_get();
    }
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

struct yetty_ycore_void_result yetty_yplatform_window_manager_register_hooks(void)
{
    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yplatform_window_manager_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yplatform_window_manager_register_hooks: accessor");
    return YETTY_OK_VOID();
}

/* ===== module registration (was rpc.gen.c) ========================== */
struct yetty_ycore_void_result yetty_yplatform_window_manager_register_hooks(void);

struct yetty_ycore_void_result yetty_yplatform_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }
    {
        struct yetty_ycore_void_result hook_r = yetty_yplatform_window_manager_register_hooks();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, hook_r, "yetty_yplatform_register: window_manager");
    }
    registered = true;
    return YETTY_OK_VOID();
}
