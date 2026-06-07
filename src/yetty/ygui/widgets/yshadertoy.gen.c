/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widget.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_yshadertoy_yetty_ygui_constructor_check = ctor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_yshadertoy_yetty_ygui_destructor_check = dtor;
[[maybe_unused]]
static yetty_ygui_widget_emit_container_fn
    yetty_ygui_yshadertoy_yetty_ygui_widget_emit_container_check = emit_container;
[[maybe_unused]]
static yetty_ygui_widget_emit_body_fn yetty_ygui_yshadertoy_yetty_ygui_widget_emit_body_check =
    emit_body;

struct yetty_yclass_ptr_result yetty_ygui_yshadertoy_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_yshadertoy");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_yshadertoy",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yshadertoy_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)ctor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
         (yetty_yclass_impl_t)dtor},
        {"yetty_ygui", "widget_emit_container",
         (yetty_yclass_method_id_t)yetty_ygui_widget_emit_container,
         (yetty_yclass_impl_t)emit_container},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body,
         (yetty_yclass_impl_t)emit_body},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_widget_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui_yshadertoy_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_yshadertoy_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_yshadertoy_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_yshadertoy_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_yshadertoy_data_ptr_result yetty_ygui_yshadertoy_data(
    struct yetty_ygui_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_yshadertoy_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yerror("yetty_ygui_yshadertoy_data: class accessor failed: %s", class_r.error.msg);
        return YETTY_ERR(yetty_ygui_yshadertoy_data_ptr,
                         "yetty_ygui_yshadertoy_data: class accessor failed", class_r);
    }
    struct yetty_ygui_void_ptr_result data_slice_r = yetty_ygui_data_get_result(obj, class_r.value);
    if (YETTY_IS_ERR(data_slice_r)) {
        return YETTY_ERR(yetty_ygui_yshadertoy_data_ptr, "yetty_ygui_yshadertoy_data",
                         data_slice_r);
    }
    return YETTY_OK(yetty_ygui_yshadertoy_data_ptr, (struct yshadertoy_data *)data_slice_r.value);
}
