/* GENERATED — do not edit. */
#include "yetty/gen/impl/yplatform/yclipboard/clipboard.h"
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

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yplatform_clipboard_set_text(struct yetty_yclass_object *obj,
                                                                  const char *text, size_t len);
struct yetty_ycore_void_result yetty_yplatform_clipboard_request_paste(
    struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_set_text_fn)(
    struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_request_paste_fn)(
    struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yplatform_clipboard_set_text_fn
    yetty_yplatform_webasm_clipboard_yetty_yplatform_clipboard_set_text_webasm_clipboard_set_text_check =
        webasm_clipboard_set_text;
YETTY_MAYBE_UNUSED
static yetty_yplatform_clipboard_request_paste_fn
    yetty_yplatform_webasm_clipboard_yetty_yplatform_clipboard_request_paste_webasm_clipboard_request_paste_check =
        webasm_clipboard_request_paste;

struct yetty_yclass_ptr_result yetty_yplatform_webasm_clipboard_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yplatform_webasm_clipboard");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yplatform_webasm_clipboard",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yplatform_webasm_clipboard),
        .data_align = _Alignof(struct yetty_yplatform_webasm_clipboard),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yplatform", "clipboard_set_text",
         (yetty_yclass_method_id_t)yetty_yplatform_clipboard_set_text,
         (yetty_yclass_impl_t)webasm_clipboard_set_text},
        {"yetty_yplatform", "clipboard_request_paste",
         (yetty_yclass_method_id_t)yetty_yplatform_clipboard_request_paste,
         (yetty_yclass_impl_t)webasm_clipboard_request_paste},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yplatform_clipboard_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yplatform_webasm_clipboard_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yplatform_webasm_clipboard_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yplatform_webasm_clipboard_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yplatform_webasm_clipboard_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yplatform_webasm_clipboard_ptr_result yetty_yplatform_webasm_clipboard_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_webasm_clipboard_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yplatform_webasm_clipboard_ptr,
                         "yetty_yplatform_webasm_clipboard_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yplatform_webasm_clipboard_ptr,
                         "yetty_yplatform_webasm_clipboard_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yplatform_webasm_clipboard_ptr,
                    (struct yetty_yplatform_webasm_clipboard *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_clipboard_to(
    struct yetty_yplatform_webasm_clipboard *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_webasm_clipboard_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yplatform_webasm_clipboard_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yplatform_webasm_clipboard_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_clipboard_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_webasm_clipboard_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yplatform_webasm_clipboard");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_webasm_clipboard_create: remote create unsupported for a "
                         "split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yplatform_webasm_clipboard_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_webasm_clipboard_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}
