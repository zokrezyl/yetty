/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widgets/tooltip.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_constructor_fn _yetty_ygui_tooltip_yetty_ygui_constructor_check = tooltip_constructor;
__attribute__((unused))
static yetty_ygui_destructor_fn _yetty_ygui_tooltip_yetty_ygui_destructor_check = tooltip_destructor;
__attribute__((unused))
static yetty_ygui_widget_paint_fn _yetty_ygui_tooltip_yetty_ygui_widget_paint_check = tooltip_paint;

struct yetty_yclass_ptr_result yetty_ygui_tooltip_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_tooltip");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_tooltip",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct tooltip_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)tooltip_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)tooltip_destructor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint, (yetty_yclass_impl_t)tooltip_paint},
    };
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_tooltip_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
