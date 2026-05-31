/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/primitive-widget.h"
#include "yetty/ygui/widgets/statusbar.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_statusbar_yetty_ygui_constructor_check = statusbar_constructor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_statusbar_yetty_ygui_destructor_check = statusbar_destructor;
[[maybe_unused]]
static yetty_ygui_widget_paint_fn yetty_ygui_statusbar_yetty_ygui_widget_paint_check = statusbar_paint;

struct yetty_yclass_ptr_result yetty_ygui_statusbar_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_statusbar");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_statusbar",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct statusbar_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)statusbar_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)statusbar_destructor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint, (yetty_yclass_impl_t)statusbar_paint},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_statusbar_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_statusbar_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
