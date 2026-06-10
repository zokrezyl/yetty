/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include "yetty/yvterm/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yvterm_grid_yetty_yfigure_render_check =
    grid_figure_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yvterm_grid_yetty_yfigure_destroy_check =
    grid_figure_destroy_slot;

struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yvterm_grid");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yvterm_grid",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yvterm_grid),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)grid_figure_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)grid_figure_destroy_slot},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yvterm_grid_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yvterm_grid_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yvterm_grid_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yvterm_grid_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yvterm_grid_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yvterm_grid_ptr, "yetty_yvterm_grid_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yvterm_grid_ptr, "yetty_yvterm_grid_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yvterm_grid_ptr, (struct yetty_yvterm_grid *)slice_r.value);
}
