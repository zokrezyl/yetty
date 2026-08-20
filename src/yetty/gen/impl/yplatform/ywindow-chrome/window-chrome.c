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

struct yetty_ycore_void_result;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yui_event;
struct yetty_ycore_void_result yetty_yplatform_window_chrome_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *output_pipe);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_destroy(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_iconify(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_toggle_maximize(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_request_close(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_drag_by(
    struct yetty_yclass_object *obj, int dx, int dy);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_resize_by(
    struct yetty_yclass_object *obj, int dx, int dy, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_move(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_resize(
    struct yetty_yclass_object *obj, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_set_cursor(
    struct yetty_yclass_object *obj, int shape);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_handle_event(
    struct yetty_yclass_object *obj, const struct yetty_yui_event *event);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_configure_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_xthread_event_pipe *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_iconify_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_toggle_maximize_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_request_close_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_drag_by_fn)(
    struct yetty_yclass_object *, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_resize_by_fn)(
    struct yetty_yclass_object *, int, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_begin_interactive_move_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_begin_interactive_resize_fn)(
    struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_set_cursor_fn)(
    struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_handle_event_fn)(
    struct yetty_yclass_object *, const struct yetty_yui_event *);

YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_configure_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_configure_window_chrome_configure_check =
        window_chrome_configure;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_destroy_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_destroy_window_chrome_destroy_check =
        window_chrome_destroy;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_iconify_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_iconify_window_chrome_iconify_check =
        window_chrome_iconify;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_toggle_maximize_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_toggle_maximize_window_chrome_toggle_maximize_check =
        window_chrome_toggle_maximize;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_request_close_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_request_close_window_chrome_request_close_check =
        window_chrome_request_close;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_drag_by_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_drag_by_window_chrome_drag_by_check =
        window_chrome_drag_by;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_resize_by_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_resize_by_window_chrome_resize_by_check =
        window_chrome_resize_by;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_begin_interactive_move_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_begin_interactive_move_window_chrome_begin_interactive_move_check =
        window_chrome_begin_interactive_move;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_begin_interactive_resize_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_begin_interactive_resize_window_chrome_begin_interactive_resize_check =
        window_chrome_begin_interactive_resize;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_set_cursor_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_set_cursor_window_chrome_set_cursor_check =
        window_chrome_set_cursor;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_handle_event_fn
    yetty_yplatform_window_chrome_yetty_yplatform_window_chrome_handle_event_window_chrome_handle_event_check =
        window_chrome_handle_event;

struct yetty_yclass_ptr_result yetty_yplatform_window_chrome_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yplatform_window_chrome");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yplatform_window_chrome",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yplatform_window_chrome),
        .data_align = _Alignof(struct yetty_yplatform_window_chrome),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yplatform", "window_chrome_configure",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_configure,
         (yetty_yclass_impl_t)window_chrome_configure},
        {"yetty_yplatform", "window_chrome_destroy",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_destroy,
         (yetty_yclass_impl_t)window_chrome_destroy},
        {"yetty_yplatform", "window_chrome_iconify",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_iconify,
         (yetty_yclass_impl_t)window_chrome_iconify},
        {"yetty_yplatform", "window_chrome_toggle_maximize",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_toggle_maximize,
         (yetty_yclass_impl_t)window_chrome_toggle_maximize},
        {"yetty_yplatform", "window_chrome_request_close",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_request_close,
         (yetty_yclass_impl_t)window_chrome_request_close},
        {"yetty_yplatform", "window_chrome_drag_by",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_drag_by,
         (yetty_yclass_impl_t)window_chrome_drag_by},
        {"yetty_yplatform", "window_chrome_resize_by",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_resize_by,
         (yetty_yclass_impl_t)window_chrome_resize_by},
        {"yetty_yplatform", "window_chrome_begin_interactive_move",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_begin_interactive_move,
         (yetty_yclass_impl_t)window_chrome_begin_interactive_move},
        {"yetty_yplatform", "window_chrome_begin_interactive_resize",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_begin_interactive_resize,
         (yetty_yclass_impl_t)window_chrome_begin_interactive_resize},
        {"yetty_yplatform", "window_chrome_set_cursor",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_set_cursor,
         (yetty_yclass_impl_t)window_chrome_set_cursor},
        {"yetty_yplatform", "window_chrome_handle_event",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_handle_event,
         (yetty_yclass_impl_t)window_chrome_handle_event},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yplatform_window_chrome_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yplatform_window_chrome_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yplatform_window_chrome_ptr_result yetty_yplatform_window_chrome_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_chrome_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yplatform_window_chrome_ptr,
                         "yetty_yplatform_window_chrome_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yplatform_window_chrome_ptr,
                         "yetty_yplatform_window_chrome_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yplatform_window_chrome_ptr,
                    (struct yetty_yplatform_window_chrome *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_to(
    struct yetty_yplatform_window_chrome *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_chrome_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yplatform_window_chrome_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yplatform_window_chrome_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yplatform_window_chrome");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_chrome_create: remote create unsupported for a "
                         "split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yplatform_window_chrome_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_window_chrome_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}
