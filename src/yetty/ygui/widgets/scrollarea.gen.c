/* GENERATED — do not edit. */
#include "yetty/ygui/mixins/draggable.h"
#include "yetty/ygui/widgets/vbox.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;
struct yetty_ycore_int_result yetty_ygui_widget_on_scroll(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj, float x,
                                                          float y, float dx, float dy);
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_ygui_emit_ctx *emit_ctx);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_scroll_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_paint_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_ygui_emit_ctx *);

[[maybe_unused]]
static yetty_ygui_widget_on_scroll_fn yetty_ygui_scrollarea_yetty_ygui_widget_on_scroll_check =
    on_scroll;
[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_scrollarea_yetty_ygui_constructor_check = ctor;
[[maybe_unused]]
static yetty_ygui_widget_paint_fn yetty_ygui_scrollarea_yetty_ygui_widget_paint_check = paint;

struct yetty_yclass_ptr_result yetty_ygui_scrollarea_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_scrollarea");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_scrollarea",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_scrollarea),
        .data_align = _Alignof(struct yetty_ygui_scrollarea),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "widget_on_scroll", (yetty_yclass_method_id_t)yetty_ygui_widget_on_scroll,
         (yetty_yclass_impl_t)on_scroll},
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)ctor},
        {"yetty_ygui", "widget_paint", (yetty_yclass_method_id_t)yetty_ygui_widget_paint,
         (yetty_yclass_impl_t)paint},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_vbox_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui_scrollarea_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_scrollarea_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result mixin_class_r_0 = yetty_ygui_draggable_mixin_get();
    if (YETTY_IS_ERR(mixin_class_r_0)) {
        yerror("yetty_ygui_scrollarea_class_get: mixin0 accessor failed: %s",
               mixin_class_r_0.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_scrollarea_class_get: mixin0 accessor failed",
                         mixin_class_r_0);
    }
    const struct yetty_yclass *mixins[] = {mixin_class_r_0.value};
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, mixins, 1);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_scrollarea_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_scrollarea_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_scrollarea_ptr_result yetty_ygui_scrollarea_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_scrollarea_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_scrollarea_ptr, "yetty_ygui_scrollarea_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_scrollarea_ptr, "yetty_ygui_scrollarea_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ygui_scrollarea_ptr, (struct yetty_ygui_scrollarea *)slice_r.value);
}

struct yetty_yclass_object *yetty_ygui_scrollarea_to(struct yetty_ygui_scrollarea *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_scrollarea_class_get();
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
