/* GENERATED — do not edit. */
#include "yetty/ychrome/methods.gen.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

[[maybe_unused]]
static yetty_ychrome_configure_fn yetty_ychrome_chrome_yetty_ychrome_configure_check =
    chrome_configure;
[[maybe_unused]]
static yetty_ychrome_set_size_fn yetty_ychrome_chrome_yetty_ychrome_set_size_check =
    chrome_set_size;
[[maybe_unused]]
static yetty_ychrome_destroy_fn yetty_ychrome_chrome_yetty_ychrome_destroy_check = chrome_destroy;
[[maybe_unused]]
static yetty_ychrome_edge_cursor_at_fn yetty_ychrome_chrome_yetty_ychrome_edge_cursor_at_check =
    chrome_edge_cursor_at;
[[maybe_unused]]
static yetty_ychrome_render_fn yetty_ychrome_chrome_yetty_ychrome_render_check = chrome_render;
[[maybe_unused]]
static yetty_ychrome_handle_event_fn yetty_ychrome_chrome_yetty_ychrome_handle_event_check =
    chrome_handle_event;

struct yetty_yclass_ptr_result yetty_ychrome_chrome_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ychrome_chrome");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ychrome_chrome",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ychrome_chrome),
        .data_align = _Alignof(struct yetty_ychrome_chrome),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ychrome", "configure", (yetty_yclass_method_id_t)yetty_ychrome_configure,
         (yetty_yclass_impl_t)chrome_configure},
        {"yetty_ychrome", "set_size", (yetty_yclass_method_id_t)yetty_ychrome_set_size,
         (yetty_yclass_impl_t)chrome_set_size},
        {"yetty_ychrome", "destroy", (yetty_yclass_method_id_t)yetty_ychrome_destroy,
         (yetty_yclass_impl_t)chrome_destroy},
        {"yetty_ychrome", "edge_cursor_at", (yetty_yclass_method_id_t)yetty_ychrome_edge_cursor_at,
         (yetty_yclass_impl_t)chrome_edge_cursor_at},
        {"yetty_ychrome", "render", (yetty_yclass_method_id_t)yetty_ychrome_render,
         (yetty_yclass_impl_t)chrome_render},
        {"yetty_ychrome", "handle_event", (yetty_yclass_method_id_t)yetty_ychrome_handle_event,
         (yetty_yclass_impl_t)chrome_handle_event},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ychrome_chrome_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ychrome_chrome_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ychrome_chrome_ptr_result yetty_ychrome_chrome_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ychrome_chrome_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ychrome_chrome_ptr, "yetty_ychrome_chrome_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ychrome_chrome_ptr, "yetty_ychrome_chrome_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ychrome_chrome_ptr, (struct yetty_ychrome_chrome *)slice_r.value);
}

struct yetty_yclass_object *yetty_ychrome_chrome_to(struct yetty_ychrome_chrome *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ychrome_chrome_class_get();
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
