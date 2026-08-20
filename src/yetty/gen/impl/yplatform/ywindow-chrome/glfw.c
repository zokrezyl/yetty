/* GENERATED — do not edit. */
#include "yetty/gen/impl/yplatform/ywindow-chrome/window-chrome.h"
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
struct yetty_yui_event;
struct yetty_ycore_void_result yetty_yplatform_window_chrome_destroy(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_handle_event(
    struct yetty_yclass_object *obj, const struct yetty_yui_event *event);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_handle_event_fn)(
    struct yetty_yclass_object *, const struct yetty_yui_event *);

YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_destroy_fn
    yetty_yplatform_glfw_window_chrome_yetty_yplatform_window_chrome_destroy_glfw_window_chrome_destroy_check =
        glfw_window_chrome_destroy;
YETTY_MAYBE_UNUSED
static yetty_yplatform_window_chrome_handle_event_fn
    yetty_yplatform_glfw_window_chrome_yetty_yplatform_window_chrome_handle_event_glfw_window_chrome_handle_event_check =
        glfw_window_chrome_handle_event;

struct yetty_yclass_ptr_result yetty_yplatform_glfw_window_chrome_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yplatform_glfw_window_chrome");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yplatform_glfw_window_chrome",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yplatform_glfw_window_chrome),
        .data_align = _Alignof(struct yetty_yplatform_glfw_window_chrome),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yplatform", "window_chrome_destroy",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_destroy,
         (yetty_yclass_impl_t)glfw_window_chrome_destroy},
        {"yetty_yplatform", "window_chrome_handle_event",
         (yetty_yclass_method_id_t)yetty_yplatform_window_chrome_handle_event,
         (yetty_yclass_impl_t)glfw_window_chrome_handle_event},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yplatform_window_chrome_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yplatform_glfw_window_chrome_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yplatform_glfw_window_chrome_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yplatform_glfw_window_chrome_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yplatform_glfw_window_chrome_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yplatform_glfw_window_chrome_ptr_result yetty_yplatform_glfw_window_chrome_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_glfw_window_chrome_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yplatform_glfw_window_chrome_ptr,
                         "yetty_yplatform_glfw_window_chrome_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yplatform_glfw_window_chrome_ptr,
                         "yetty_yplatform_glfw_window_chrome_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yplatform_glfw_window_chrome_ptr,
                    (struct yetty_yplatform_glfw_window_chrome *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_to(
    struct yetty_yplatform_glfw_window_chrome *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_glfw_window_chrome_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yplatform_glfw_window_chrome_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yplatform_glfw_window_chrome_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yplatform_glfw_window_chrome_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yplatform_glfw_window_chrome");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_glfw_window_chrome_create: remote create unsupported for "
                         "a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r =
        yetty_yplatform_glfw_window_chrome_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yplatform_glfw_window_chrome_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}
