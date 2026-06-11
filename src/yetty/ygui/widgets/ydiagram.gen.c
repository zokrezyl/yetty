/* GENERATED — do not edit. */
#include "yetty/ygui/widgets/ydraw_embed.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_ygui_constructor_fn yetty_ygui_ydiagram_yetty_ygui_constructor_check =
    ydiagram_constructor;
[[maybe_unused]]
static yetty_ygui_destructor_fn yetty_ygui_ydiagram_yetty_ygui_destructor_check =
    ydiagram_destructor;

struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_ydiagram");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_ydiagram",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_ydiagram),
        .data_align = _Alignof(struct yetty_ygui_ydiagram),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)ydiagram_constructor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
         (yetty_yclass_impl_t)ydiagram_destructor},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_ydraw_embed_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui_ydiagram_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_ydiagram_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_ydiagram_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_ydiagram_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_ydiagram_ptr_result yetty_ygui_ydiagram_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_ydiagram_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_ydiagram_ptr, "yetty_ygui_ydiagram_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_ydiagram_ptr, "yetty_ygui_ydiagram_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygui_ydiagram_ptr, (struct yetty_ygui_ydiagram *)slice_r.value);
}

struct yetty_yclass_object *yetty_ygui_ydiagram_to(struct yetty_ygui_ydiagram *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_ydiagram_class_get();
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
