/* GENERATED — do not edit. */
#include "yetty/yfigure/container.h"
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

__attribute__((unused))
static yetty_yfigure_constructor_fn _yetty_yfigure_container_yetty_yfigure_constructor_check = yetty_yfigure_container_constructor_impl;
__attribute__((unused))
static yetty_yfigure_add_child_fn _yetty_yfigure_container_yetty_yfigure_add_child_check = yetty_yfigure_container_add_child_impl;
__attribute__((unused))
static yetty_yfigure_remove_child_by_id_fn _yetty_yfigure_container_yetty_yfigure_remove_child_by_id_check = yetty_yfigure_container_remove_child_by_id_impl;
__attribute__((unused))
static yetty_yfigure_raise_child_by_id_fn _yetty_yfigure_container_yetty_yfigure_raise_child_by_id_check = yetty_yfigure_container_raise_child_by_id_impl;
__attribute__((unused))
static yetty_yfigure_process_records_fn _yetty_yfigure_container_yetty_yfigure_process_records_check = yetty_yfigure_container_process_records_impl;

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
        {"yetty_yfigure", "constructor", (yetty_yclass_method_id_t)yetty_yfigure_constructor, (yetty_yclass_impl_t)yetty_yfigure_container_constructor_impl},
        {"yetty_yfigure", "add_child", (yetty_yclass_method_id_t)yetty_yfigure_add_child, (yetty_yclass_impl_t)yetty_yfigure_container_add_child_impl},
        {"yetty_yfigure", "remove_child_by_id", (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id, (yetty_yclass_impl_t)yetty_yfigure_container_remove_child_by_id_impl},
        {"yetty_yfigure", "raise_child_by_id", (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id, (yetty_yclass_impl_t)yetty_yfigure_container_raise_child_by_id_impl},
        {"yetty_yfigure", "process_records", (yetty_yclass_method_id_t)yetty_yfigure_process_records, (yetty_yclass_impl_t)yetty_yfigure_container_process_records_impl},
    };
    struct yetty_yclass_ptr_result _parent_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yfigure_container_class_get: parent accessor failed", _parent_r);
    struct yetty_yclass_ptr_result _r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              _parent_r.value, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yfigure_container_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
