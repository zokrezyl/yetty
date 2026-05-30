/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widgets/vbox.h"
#include "yetty/ygui/widgets/window.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_constructor_fn yetty_ygui_window_yetty_ygui_constructor_check = ctor;
__attribute__((unused))
static yetty_ygui_destructor_fn yetty_ygui_window_yetty_ygui_destructor_check = dtor;
__attribute__((unused))
static yetty_ygui_widget_paint_fn yetty_ygui_window_yetty_ygui_widget_paint_check = paint;
__attribute__((unused))
static yetty_ygui_widget_on_press_fn yetty_ygui_window_yetty_ygui_widget_on_press_check = on_press;
__attribute__((unused))
static yetty_ygui_widget_on_motion_fn yetty_ygui_window_yetty_ygui_widget_on_motion_check = on_motion;
__attribute__((unused))
static yetty_ygui_widget_on_release_fn yetty_ygui_window_yetty_ygui_widget_on_release_check =
    on_release;

struct yetty_yclass_ptr_result yetty_ygui_window_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_window");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_window",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct window_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)ctor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)dtor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint, (yetty_yclass_impl_t)paint},
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press, (yetty_yclass_impl_t)on_press},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion, (yetty_yclass_impl_t)on_motion},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release, (yetty_yclass_impl_t)on_release},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_vbox_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_window_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_window_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
