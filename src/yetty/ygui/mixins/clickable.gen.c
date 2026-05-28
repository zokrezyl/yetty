/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/mixins/clickable.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_widget_on_press_fn _yetty_ygui_clickable_yetty_ygui_widget_on_press_check = clickable_on_press;
__attribute__((unused))
static yetty_ygui_widget_on_release_fn _yetty_ygui_clickable_yetty_ygui_widget_on_release_check = clickable_on_release;

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_clickable");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_clickable",
        .type = YETTY_YCLASS_TYPE_MIXIN,
        .data_size = sizeof(struct clickable_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press, (yetty_yclass_impl_t)clickable_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release, (yetty_yclass_impl_t)clickable_on_release},
    };
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_clickable_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
