/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>  /* NULL, size_t */

struct yai_app;
struct yetty_ycore_int_result;
struct yetty_ycore_int_result yetty_yai_feed_byte(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, int byte);
typedef struct yetty_ycore_int_result (*yetty_yai_feed_byte_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, int);

[[maybe_unused]]
static yetty_yai_feed_byte_fn yetty_yai_editor_yetty_yai_feed_byte_check = editor_feed_byte;

struct yetty_yclass_ptr_result yetty_yai_editor_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_editor");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_editor",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_editor),
        .data_align = _Alignof(struct yetty_yai_editor),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "feed_byte", (yetty_yclass_method_id_t)yetty_yai_feed_byte, (yetty_yclass_impl_t)editor_feed_byte},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_editor_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_editor_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_editor_ptr_result yetty_yai_editor_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_editor_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_editor_ptr, "yetty_yai_editor_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_editor_ptr, "yetty_yai_editor_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_editor_ptr, (struct yetty_yai_editor *)slice_r.value);
}

struct yetty_yclass_object *yetty_yai_editor_to(struct yetty_yai_editor *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yai_editor_class_get();
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
