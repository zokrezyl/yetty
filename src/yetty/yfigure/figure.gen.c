/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yfigure_figure_yetty_yfigure_render_check = yetty_yfigure_figure_default_render;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yfigure_figure_yetty_yfigure_destroy_check = yetty_yfigure_figure_default_destroy;
[[maybe_unused]]
static yetty_yfigure_process_input_fn yetty_yfigure_figure_yetty_yfigure_process_input_check = yetty_yfigure_figure_default_process_input;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_yfigure_figure_yetty_yfigure_process_bytes_check = yetty_yfigure_figure_default_process_bytes;
[[maybe_unused]]
static yetty_yfigure_reset_content_fn yetty_yfigure_figure_yetty_yfigure_reset_content_check = yetty_yfigure_figure_default_reset_content;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_yfigure_figure_yetty_yfigure_dump_state_check = yetty_yfigure_figure_default_dump_state;

struct yetty_yclass_ptr_result yetty_yfigure_figure_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yfigure_figure");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yfigure_figure",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yfigure_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render, (yetty_yclass_impl_t)yetty_yfigure_figure_default_render},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy, (yetty_yclass_impl_t)yetty_yfigure_figure_default_destroy},
        {"yetty_yfigure", "process_input", (yetty_yclass_method_id_t)yetty_yfigure_process_input, (yetty_yclass_impl_t)yetty_yfigure_figure_default_process_input},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes, (yetty_yclass_impl_t)yetty_yfigure_figure_default_process_bytes},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content, (yetty_yclass_impl_t)yetty_yfigure_figure_default_reset_content},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state, (yetty_yclass_impl_t)yetty_yfigure_figure_default_dump_state},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r))
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yfigure_figure_class_get: class_register failed", register_class_r);
    cls = register_class_r.value;
    return register_class_r;
}
