/* GENERATED — do not edit. */
#include "yetty/yview/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_yview_configure_fn yetty_yview_view_yetty_yview_configure_check = view_configure;
[[maybe_unused]]
static yetty_yview_set_content_fn yetty_yview_view_yetty_yview_set_content_check = view_set_content;
[[maybe_unused]]
static yetty_yview_set_text_fn yetty_yview_view_yetty_yview_set_text_check = view_set_text;
[[maybe_unused]]
static yetty_yview_set_plot_fn yetty_yview_view_yetty_yview_set_plot_check = view_set_plot;
[[maybe_unused]]
static yetty_yview_set_content_size_fn yetty_yview_view_yetty_yview_set_content_size_check =
    view_set_content_size;
[[maybe_unused]]
static yetty_yview_scroll_to_fn yetty_yview_view_yetty_yview_scroll_to_check = view_scroll_to;
[[maybe_unused]]
static yetty_yview_scroll_by_fn yetty_yview_view_yetty_yview_scroll_by_check = view_scroll_by;
[[maybe_unused]]
static yetty_yview_set_rect_fn yetty_yview_view_yetty_yview_set_rect_check = view_set_rect;
[[maybe_unused]]
static yetty_yview_destroy_fn yetty_yview_view_yetty_yview_destroy_check = view_destroy;

struct yetty_yclass_ptr_result yetty_yview_view_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yview_view");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yview_view",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yview_view),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yview", "configure", (yetty_yclass_method_id_t)yetty_yview_configure,
         (yetty_yclass_impl_t)view_configure},
        {"yetty_yview", "set_content", (yetty_yclass_method_id_t)yetty_yview_set_content,
         (yetty_yclass_impl_t)view_set_content},
        {"yetty_yview", "set_text", (yetty_yclass_method_id_t)yetty_yview_set_text,
         (yetty_yclass_impl_t)view_set_text},
        {"yetty_yview", "set_plot", (yetty_yclass_method_id_t)yetty_yview_set_plot,
         (yetty_yclass_impl_t)view_set_plot},
        {"yetty_yview", "set_content_size", (yetty_yclass_method_id_t)yetty_yview_set_content_size,
         (yetty_yclass_impl_t)view_set_content_size},
        {"yetty_yview", "scroll_to", (yetty_yclass_method_id_t)yetty_yview_scroll_to,
         (yetty_yclass_impl_t)view_scroll_to},
        {"yetty_yview", "scroll_by", (yetty_yclass_method_id_t)yetty_yview_scroll_by,
         (yetty_yclass_impl_t)view_scroll_by},
        {"yetty_yview", "set_rect", (yetty_yclass_method_id_t)yetty_yview_set_rect,
         (yetty_yclass_impl_t)view_set_rect},
        {"yetty_yview", "destroy", (yetty_yclass_method_id_t)yetty_yview_destroy,
         (yetty_yclass_impl_t)view_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yview_view_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yview_view_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yview_view_ptr_result yetty_yview_view_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yview_view_ptr, "yetty_yview_view_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yview_view_ptr, "yetty_yview_view_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yview_view_ptr, (struct yetty_yview_view *)slice_r.value);
}
