/* GENERATED — do not edit. */
#include "yetty/ygui/methods.gen.h"
#include "yetty/ygui/widgets/ydraw_embed.h"
#include "yetty/ygui/widgets/yrich_view.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_yrich_view_yetty_ygui_constructor_check = yrich_view_ctor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_yrich_view_yetty_ygui_destructor_check = yrich_view_dtor;
[[maybe_unused]]
static yetty_ygui_widget_emit_body_fn yetty_ygui_yrich_view_yetty_ygui_widget_emit_body_check = yrich_view_emit_body;
[[maybe_unused]]
static yetty_ygui_widget_on_press_fn yetty_ygui_yrich_view_yetty_ygui_widget_on_press_check = yrich_view_on_press;
[[maybe_unused]]
static yetty_ygui_widget_on_release_fn yetty_ygui_yrich_view_yetty_ygui_widget_on_release_check = yrich_view_on_release;
[[maybe_unused]]
static yetty_ygui_widget_on_motion_fn yetty_ygui_yrich_view_yetty_ygui_widget_on_motion_check = yrich_view_on_motion;

struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygui_yrich_view");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_yrich_view",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yrich_view_data),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor, (yetty_yclass_impl_t)yrich_view_ctor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor, (yetty_yclass_impl_t)yrich_view_dtor},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body, (yetty_yclass_impl_t)yrich_view_emit_body},
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press, (yetty_yclass_impl_t)yrich_view_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release, (yetty_yclass_impl_t)yrich_view_on_release},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion, (yetty_yclass_impl_t)yrich_view_on_motion},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_ydraw_embed_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_yrich_view_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_yrich_view_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_yrich_view_data_ptr_result yetty_ygui_yrich_view_data(struct yetty_ygui_object *obj)
{
    struct yetty_ygui_void_ptr_result data_slice_r =
        yetty_ygui_data_get_result(obj, yetty_ygui_yrich_view_class_get().value);
    if (YETTY_IS_ERR(data_slice_r))
        return YETTY_ERR(yetty_ygui_yrich_view_data_ptr, "yetty_ygui_yrich_view_data", data_slice_r);
    return YETTY_OK(yetty_ygui_yrich_view_data_ptr, (struct yrich_view_data *)data_slice_r.value);
}
