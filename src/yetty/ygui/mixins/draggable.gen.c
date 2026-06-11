/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ycore_int_result;
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj, float x,
                                                         float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj, float x,
                                                           float y, int button);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_ctx *,
                                                                       struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);

[[maybe_unused]]
static yetty_ygui_widget_on_press_fn yetty_ygui_draggable_yetty_ygui_widget_on_press_check =
    draggable_on_press;
[[maybe_unused]]
static yetty_ygui_widget_on_motion_fn yetty_ygui_draggable_yetty_ygui_widget_on_motion_check =
    draggable_on_motion;
[[maybe_unused]]
static yetty_ygui_widget_on_release_fn yetty_ygui_draggable_yetty_ygui_widget_on_release_check =
    draggable_on_release;

struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_draggable");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_draggable",
        .type = YETTY_YCLASS_TYPE_MIXIN,
        .data_size = sizeof(struct yetty_ygui_draggable),
        .data_align = _Alignof(struct yetty_ygui_draggable),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
         (yetty_yclass_impl_t)draggable_on_press},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion,
         (yetty_yclass_impl_t)draggable_on_motion},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release,
         (yetty_yclass_impl_t)draggable_on_release},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_draggable_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_draggable_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_draggable_ptr_result yetty_ygui_draggable_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_draggable_mixin_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_draggable_ptr, "yetty_ygui_draggable_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_draggable_ptr, "yetty_ygui_draggable_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ygui_draggable_ptr, (struct yetty_ygui_draggable *)slice_r.value);
}
