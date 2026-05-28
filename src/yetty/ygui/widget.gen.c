/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widget.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_constructor_fn _yetty_ygui_widget_yetty_ygui_constructor_check = widget_default_constructor;
__attribute__((unused))
static yetty_ygui_destructor_fn _yetty_ygui_widget_yetty_ygui_destructor_check = widget_default_destructor;
__attribute__((unused))
static yetty_ygui_widget_on_press_fn _yetty_ygui_widget_yetty_ygui_widget_on_press_check = widget_default_on_press;
__attribute__((unused))
static yetty_ygui_widget_on_release_fn _yetty_ygui_widget_yetty_ygui_widget_on_release_check = widget_default_on_release;
__attribute__((unused))
static yetty_ygui_widget_on_motion_fn _yetty_ygui_widget_yetty_ygui_widget_on_motion_check = widget_default_on_motion;
__attribute__((unused))
static yetty_ygui_widget_paint_fn _yetty_ygui_widget_yetty_ygui_widget_paint_check = widget_default_paint;
__attribute__((unused))
static yetty_ygui_widget_emit_container_fn _yetty_ygui_widget_yetty_ygui_widget_emit_container_check = widget_default_emit_container;
__attribute__((unused))
static yetty_ygui_widget_emit_body_fn _yetty_ygui_widget_yetty_ygui_widget_emit_body_check = widget_default_emit_body;

struct yetty_yclass_ptr_result yetty_ygui_widget_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_widget");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_widget",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_widget_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)widget_default_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)widget_default_destructor},
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press, (yetty_yclass_impl_t)widget_default_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release, (yetty_yclass_impl_t)widget_default_on_release},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion, (yetty_yclass_impl_t)widget_default_on_motion},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint, (yetty_yclass_impl_t)widget_default_paint},
        {"yetty_ygui", "widget_emit_container", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container, (yetty_yclass_impl_t)widget_default_emit_container},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body, (yetty_yclass_impl_t)widget_default_emit_body},
    };
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_widget_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
