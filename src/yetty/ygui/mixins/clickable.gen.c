/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_widget_on_press_fn yetty_ygui_clickable_yetty_ygui_widget_on_press_check =
    clickable_on_press;
[[maybe_unused]]
static yetty_ygui_widget_on_release_fn yetty_ygui_clickable_yetty_ygui_widget_on_release_check =
    clickable_on_release;

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_clickable");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_clickable",
        .type = YETTY_YCLASS_TYPE_MIXIN,
        .data_size = sizeof(struct yetty_ygui_clickable),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
         (yetty_yclass_impl_t)clickable_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release,
         (yetty_yclass_impl_t)clickable_on_release},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_clickable_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_clickable_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_clickable_data_ptr_result yetty_ygui_clickable_data(struct yetty_ygui_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_clickable_mixin_get();
    if (YETTY_IS_ERR(class_r)) {
        yerror("yetty_ygui_clickable_data: class accessor failed: %s", class_r.error.msg);
        return YETTY_ERR(yetty_ygui_clickable_data_ptr,
                         "yetty_ygui_clickable_data: class accessor failed", class_r);
    }
    struct yetty_ygui_void_ptr_result data_slice_r = yetty_ygui_data_get_result(obj, class_r.value);
    if (YETTY_IS_ERR(data_slice_r)) {
        return YETTY_ERR(yetty_ygui_clickable_data_ptr, "yetty_ygui_clickable_data", data_slice_r);
    }
    return YETTY_OK(yetty_ygui_clickable_data_ptr,
                    (struct yetty_ygui_clickable *)data_slice_r.value);
}
