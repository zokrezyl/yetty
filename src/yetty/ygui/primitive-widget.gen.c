/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widget.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_widget_emit_body_fn
    yetty_ygui_primitive_widget_yetty_ygui_widget_emit_body_check = primitive_emit_body;

struct yetty_yclass_ptr_result yetty_ygui_primitive_widget_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_primitive_widget");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_primitive_widget",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_primitive_widget),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body,
         (yetty_yclass_impl_t)primitive_emit_body},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui_primitive_widget_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_primitive_widget_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_primitive_widget_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_primitive_widget_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_primitive_widget_ptr_result yetty_ygui_primitive_widget_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_primitive_widget_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_primitive_widget_ptr,
                         "yetty_ygui_primitive_widget_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_primitive_widget_ptr,
                         "yetty_ygui_primitive_widget_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygui_primitive_widget_ptr,
                    (struct yetty_ygui_primitive_widget *)slice_r.value);
}
