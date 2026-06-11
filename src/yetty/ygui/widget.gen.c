/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_widget_yetty_ygui_constructor_check =
    widget_default_constructor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_widget_yetty_ygui_destructor_check =
    widget_default_destructor;
[[maybe_unused]]
static yetty_ygui_widget_on_press_fn yetty_ygui_widget_yetty_ygui_widget_on_press_check =
    widget_default_on_press;
[[maybe_unused]]
static yetty_ygui_widget_on_release_fn yetty_ygui_widget_yetty_ygui_widget_on_release_check =
    widget_default_on_release;
[[maybe_unused]]
static yetty_ygui_widget_on_motion_fn yetty_ygui_widget_yetty_ygui_widget_on_motion_check =
    widget_default_on_motion;
[[maybe_unused]]
static yetty_ygui_widget_on_scroll_fn yetty_ygui_widget_yetty_ygui_widget_on_scroll_check =
    widget_default_on_scroll;
[[maybe_unused]]
static yetty_ygui_widget_paint_fn yetty_ygui_widget_yetty_ygui_widget_paint_check =
    widget_default_paint;
[[maybe_unused]]
static yetty_ygui_widget_emit_container_fn
    yetty_ygui_widget_yetty_ygui_widget_emit_container_check = widget_default_emit_container;
[[maybe_unused]]
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

struct yetty_ygui_widget_data_ptr_result yetty_ygui_widget_data(struct yetty_ygui_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yerror("yetty_ygui_widget_data: class accessor failed: %s", class_r.error.msg);
        return YETTY_ERR(yetty_ygui_widget_data_ptr,
                         "yetty_ygui_widget_data: class accessor failed", class_r);
    }
    struct yetty_ygui_void_ptr_result data_slice_r = yetty_ygui_data_get_result(obj, class_r.value);
    if (YETTY_IS_ERR(data_slice_r)) {
        return YETTY_ERR(yetty_ygui_widget_data_ptr, "yetty_ygui_widget_data", data_slice_r);
    }
    return YETTY_OK(yetty_ygui_widget_data_ptr, (struct yetty_ygui_widget *)data_slice_r.value);
}
