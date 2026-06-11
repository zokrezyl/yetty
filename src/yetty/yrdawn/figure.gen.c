/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include "yetty/yfigure/methods.gen.h"
#include "yetty/yrdawn/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yrdawn_figure_yetty_yfigure_render_check =
    yrdawn_figure_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yrdawn_figure_yetty_yfigure_destroy_check =
    yrdawn_figure_destroy_slot;
[[maybe_unused]]
static yetty_yfigure_process_input_fn yetty_yrdawn_figure_yetty_yfigure_process_input_check =
    yrdawn_figure_process_input_slot;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_yrdawn_figure_yetty_yfigure_process_bytes_check =
    yrdawn_figure_process_bytes_slot;

struct yetty_yclass_ptr_result yetty_yrdawn_figure_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrdawn_figure");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrdawn_figure",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrdawn_figure),
        .data_align = _Alignof(struct yetty_yrdawn_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)yrdawn_figure_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)yrdawn_figure_destroy_slot},
        {"yetty_yfigure", "process_input", (yetty_yclass_method_id_t)yetty_yfigure_process_input,
         (yetty_yclass_impl_t)yrdawn_figure_process_input_slot},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)yrdawn_figure_process_bytes_slot},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrdawn_figure_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrdawn_figure_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrdawn_figure_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrdawn_figure_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrdawn_figure_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrdawn_figure_ptr, "yetty_yrdawn_figure_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrdawn_figure_ptr, "yetty_yrdawn_figure_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yrdawn_figure_ptr, (struct yetty_yrdawn_figure *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrdawn_figure_to(struct yetty_yrdawn_figure *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrdawn_figure_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}
