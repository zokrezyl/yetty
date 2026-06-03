/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include "yetty/ygrid/grid.h"
#include "yetty/ygrid/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_ygrid_grid_yetty_yfigure_render_check = ygrid_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_ygrid_grid_yetty_yfigure_destroy_check = ygrid_destroy_slot;
[[maybe_unused]]
static yetty_ygrid_add_record_fn yetty_ygrid_grid_yetty_ygrid_add_record_check = yetty_ygrid_grid_add_record_impl;
[[maybe_unused]]
static yetty_ygrid_clear_fn yetty_ygrid_grid_yetty_ygrid_clear_check = yetty_ygrid_grid_clear_impl;
[[maybe_unused]]
static yetty_ygrid_destroy_fn yetty_ygrid_grid_yetty_ygrid_destroy_check = yetty_ygrid_grid_destroy_impl;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_ygrid_grid_yetty_yfigure_process_bytes_check = yetty_ygrid_grid_process_bytes_impl;
[[maybe_unused]]
static yetty_yfigure_reset_content_fn yetty_ygrid_grid_yetty_yfigure_reset_content_check = yetty_ygrid_grid_reset_content_impl;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_ygrid_grid_yetty_yfigure_dump_state_check = yetty_ygrid_grid_dump_state_impl;

struct yetty_yclass_ptr_result yetty_ygrid_grid_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ygrid_grid");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygrid_grid",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygrid_grid),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render, (yetty_yclass_impl_t)ygrid_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy, (yetty_yclass_impl_t)ygrid_destroy_slot},
        {"yetty_ygrid", "add_record", (yetty_yclass_method_id_t)yetty_ygrid_add_record, (yetty_yclass_impl_t)yetty_ygrid_grid_add_record_impl},
        {"yetty_ygrid", "clear", (yetty_yclass_method_id_t)yetty_ygrid_clear, (yetty_yclass_impl_t)yetty_ygrid_grid_clear_impl},
        {"yetty_ygrid", "destroy", (yetty_yclass_method_id_t)yetty_ygrid_destroy, (yetty_yclass_impl_t)yetty_ygrid_grid_destroy_impl},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes, (yetty_yclass_impl_t)yetty_ygrid_grid_process_bytes_impl},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content, (yetty_yclass_impl_t)yetty_ygrid_grid_reset_content_impl},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state, (yetty_yclass_impl_t)yetty_ygrid_grid_dump_state_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygrid_grid_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygrid_grid_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
