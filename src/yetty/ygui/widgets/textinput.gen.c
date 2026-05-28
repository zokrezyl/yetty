/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/mixins/clickable.h"
#include "yetty/ygui/primitive-widget.h"
#include "yetty/ygui/widgets/textinput.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_ygui_constructor_fn _yetty_ygui_textinput_yetty_ygui_constructor_check = textinput_constructor;
__attribute__((unused))
static yetty_ygui_destructor_fn _yetty_ygui_textinput_yetty_ygui_destructor_check = textinput_destructor;
__attribute__((unused))
static yetty_ygui_widget_paint_fn _yetty_ygui_textinput_yetty_ygui_widget_paint_check = textinput_paint;

struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_textinput");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_textinput",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct textinput_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)textinput_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)textinput_destructor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint, (yetty_yclass_impl_t)textinput_paint},
    };
    struct yetty_yclass_ptr_result _parent_r = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_textinput_class_get: parent accessor failed", _parent_r);
    struct yetty_yclass_ptr_result _mixin0_r = yetty_ygui_clickable_mixin_get();
    if (YETTY_IS_ERR(_mixin0_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_textinput_class_get: mixin0 accessor failed", _mixin0_r);
    const struct yetty_yclass *mixins[] = { _mixin0_r.value };
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              _parent_r.value, mixins, 1);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_textinput_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
