/* GENERATED — do not edit. */
#include "yetty/yplatform/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

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
