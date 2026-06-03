/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include "yetty/ymgui/figure.h"
#include "yetty/ymgui/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_ymgui_figure_yetty_yfigure_render_check = ymgui_figure_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_ymgui_figure_yetty_yfigure_destroy_check = ymgui_figure_destroy_slot;
[[maybe_unused]]
static yetty_yfigure_process_input_fn yetty_ymgui_figure_yetty_yfigure_process_input_check = ymgui_figure_process_input_slot;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_ymgui_figure_yetty_yfigure_process_bytes_check = ymgui_figure_process_bytes_slot;

struct yetty_yclass_ptr_result yetty_ymgui_figure_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ymgui_figure");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ymgui_figure",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ymgui_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render, (yetty_yclass_impl_t)ymgui_figure_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy, (yetty_yclass_impl_t)ymgui_figure_destroy_slot},
        {"yetty_yfigure", "process_input", (yetty_yclass_method_id_t)yetty_yfigure_process_input, (yetty_yclass_impl_t)ymgui_figure_process_input_slot},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes, (yetty_yclass_impl_t)ymgui_figure_process_bytes_slot},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymgui_figure_class_get: parent accessor failed", parent_class_r);
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ymgui_figure_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
