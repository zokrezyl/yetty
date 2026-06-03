/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/mixins/clickable.h"
#include "yetty/ygui/primitive-widget.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_textinput_yetty_ygui_constructor_check = textinput_constructor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_textinput_yetty_ygui_destructor_check = textinput_destructor;
[[maybe_unused]]
static yetty_ygui_widget_paint_fn yetty_ygui_textinput_yetty_ygui_widget_paint_check = textinput_paint;

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
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_textinput_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result mixin_class_r_0 = yetty_ygui_clickable_mixin_get();
    if (YETTY_IS_ERR(mixin_class_r_0))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_textinput_class_get: mixin0 accessor failed", mixin_class_r_0);
    const struct yetty_yclass *mixins[] = { mixin_class_r_0.value };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, mixins, 1);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_textinput_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_textinput_data_ptr_result yetty_ygui_textinput_data(struct yetty_ygui_object *obj)
{
    struct yetty_ygui_void_ptr_result data_slice_r =
        yetty_ygui_data_get_result(obj, yetty_ygui_textinput_class_get().value);
    if (YETTY_IS_ERR(data_slice_r))
        return YETTY_ERR(yetty_ygui_textinput_data_ptr, "yetty_ygui_textinput_data", data_slice_r);
    return YETTY_OK(yetty_ygui_textinput_data_ptr, (struct textinput_data *)data_slice_r.value);
}
