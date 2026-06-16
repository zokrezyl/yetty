/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yvterm_vterm_yetty_yfigure_render_check = vterm_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yvterm_vterm_yetty_yfigure_destroy_check = vterm_destroy_slot;

struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yvterm_vterm");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yvterm_vterm",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yvterm_vterm),
        .data_align = _Alignof(struct yetty_yvterm_vterm),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)vterm_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)vterm_destroy_slot},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yvterm_vterm_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yvterm_vterm_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yvterm_vterm_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yvterm_vterm_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yvterm_vterm_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yvterm_vterm_ptr, "yetty_yvterm_vterm_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yvterm_vterm_ptr, "yetty_yvterm_vterm_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yvterm_vterm_ptr, (struct yetty_yvterm_vterm *)slice_r.value);
}

struct yetty_yclass_object *yetty_yvterm_vterm_to(struct yetty_yvterm_vterm *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yvterm_vterm_class_get();
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
