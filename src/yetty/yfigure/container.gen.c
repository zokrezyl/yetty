/* GENERATED — do not edit. */
#include "yetty/yfigure/container.h"
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yfigure_container_yetty_yfigure_destroy_check = container_destroy;
[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yfigure_container_yetty_yfigure_render_check = container_render;
[[maybe_unused]]
static yetty_yfigure_constructor_fn yetty_yfigure_container_yetty_yfigure_constructor_check = yetty_yfigure_container_constructor_impl;
[[maybe_unused]]
static yetty_yfigure_add_child_fn yetty_yfigure_container_yetty_yfigure_add_child_check = yetty_yfigure_container_add_child_impl;
[[maybe_unused]]
static yetty_yfigure_remove_child_by_id_fn yetty_yfigure_container_yetty_yfigure_remove_child_by_id_check = yetty_yfigure_container_remove_child_by_id_impl;
[[maybe_unused]]
static yetty_yfigure_raise_child_by_id_fn yetty_yfigure_container_yetty_yfigure_raise_child_by_id_check = yetty_yfigure_container_raise_child_by_id_impl;
[[maybe_unused]]
static yetty_yfigure_process_records_fn yetty_yfigure_container_yetty_yfigure_process_records_check = yetty_yfigure_container_process_records_impl;

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yfigure_container");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yfigure_container",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yfigure_container),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy, (yetty_yclass_impl_t)container_destroy},
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render, (yetty_yclass_impl_t)container_render},
        {"yetty_yfigure", "constructor", (yetty_yclass_method_id_t)yetty_yfigure_constructor, (yetty_yclass_impl_t)yetty_yfigure_container_constructor_impl},
        {"yetty_yfigure", "add_child", (yetty_yclass_method_id_t)yetty_yfigure_add_child, (yetty_yclass_impl_t)yetty_yfigure_container_add_child_impl},
        {"yetty_yfigure", "remove_child_by_id", (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id, (yetty_yclass_impl_t)yetty_yfigure_container_remove_child_by_id_impl},
        {"yetty_yfigure", "raise_child_by_id", (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id, (yetty_yclass_impl_t)yetty_yfigure_container_raise_child_by_id_impl},
        {"yetty_yfigure", "process_records", (yetty_yclass_method_id_t)yetty_yfigure_process_records, (yetty_yclass_impl_t)yetty_yfigure_container_process_records_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yfigure_container_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yfigure_container_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
