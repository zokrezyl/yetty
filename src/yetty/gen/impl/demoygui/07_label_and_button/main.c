/* GENERATED — do not edit. */
#include "yetty/gen/impl/yguiapp/app.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

YETTY_MAYBE_UNUSED
static yetty_yguiapp_build_fn yetty_demoygui_07_label_and_button_yetty_yguiapp_build_check = build;

struct yetty_yclass_ptr_result yetty_demoygui_07_label_and_button_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_demoygui_07_label_and_button");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_demoygui_07_label_and_button",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_demoygui_07_label_and_button),
        .data_align = _Alignof(struct yetty_demoygui_07_label_and_button),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yguiapp", "build", (yetty_yclass_method_id_t)yetty_yguiapp_build,
         (yetty_yclass_impl_t)build},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yguiapp_app_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_demoygui_07_label_and_button_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_demoygui_07_label_and_button_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_demoygui_07_label_and_button_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_demoygui_07_label_and_button_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_demoygui_07_label_and_button_ptr_result yetty_demoygui_07_label_and_button_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_demoygui_07_label_and_button_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_demoygui_07_label_and_button_ptr,
                         "yetty_demoygui_07_label_and_button_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_demoygui_07_label_and_button_ptr,
                         "yetty_demoygui_07_label_and_button_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_demoygui_07_label_and_button_ptr,
                    (struct yetty_demoygui_07_label_and_button *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_demoygui_07_label_and_button_to(
    struct yetty_demoygui_07_label_and_button *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_demoygui_07_label_and_button_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_demoygui_07_label_and_button_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_demoygui_07_label_and_button_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_demoygui_07_label_and_button_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_demoygui_07_label_and_button_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_demoygui_07_label_and_button");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_demoygui_07_label_and_button_create: remote create unsupported for "
                         "a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r =
        yetty_demoygui_07_label_and_button_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_demoygui_07_label_and_button_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}
