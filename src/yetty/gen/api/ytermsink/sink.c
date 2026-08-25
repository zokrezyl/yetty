/* GENERATED — do not edit. */
#include <yetty/api/ytermsink/sink.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* malloc/free for buffer marshalling */
#include <string.h> /* memcpy/strlen */

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ytermsink_pty_write(struct yetty_yclass_object *obj,
                                                         const char *data, size_t len);
struct yetty_ycore_void_result yetty_ytermsink_request_render(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ytermsink_mouse_sub(struct yetty_yclass_object *obj,
                                                         int click_enabled, int move_enabled,
                                                         int key_enabled);
struct yetty_ycore_void_result yetty_ytermsink_clipboard_write(struct yetty_yclass_object *obj,
                                                               const char *text, size_t len,
                                                               int clipboard);
struct yetty_ycore_void_result yetty_ytermsink_sixel_write(struct yetty_yclass_object *obj,
                                                           const char *data, size_t len);
struct yetty_ycore_void_result yetty_ytermsink_set_title(struct yetty_yclass_object *obj,
                                                         const char *title, size_t len);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_pty_write_fn)(struct yetty_yclass_object *,
                                                                       const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_request_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_mouse_sub_fn)(struct yetty_yclass_object *,
                                                                       int, int, int);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_clipboard_write_fn)(
    struct yetty_yclass_object *, const char *, size_t, int);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_sixel_write_fn)(
    struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ytermsink_set_title_fn)(struct yetty_yclass_object *,
                                                                       const char *, size_t);

struct yetty_ycore_void_result yetty_ytermsink_pty_write(struct yetty_yclass_object *obj,
                                                         const char *data, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ytermsink", (yetty_yclass_method_id_t)yetty_ytermsink_pty_write);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_pty_write: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_pty_write: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ytermsink_pty_write: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ytermsink_pty_write: dispatch_lookup failed");
    return ((yetty_ytermsink_pty_write_fn)dispatch_impl_r.value)(obj, data, len);
}

struct yetty_ycore_void_result yetty_ytermsink_request_render(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ytermsink", (yetty_yclass_method_id_t)yetty_ytermsink_request_render);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ytermsink_request_render: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_request_render: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ytermsink_request_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ytermsink_request_render: dispatch_lookup failed");
    return ((yetty_ytermsink_request_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ytermsink_mouse_sub(struct yetty_yclass_object *obj,
                                                         int click_enabled, int move_enabled,
                                                         int key_enabled)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ytermsink", (yetty_yclass_method_id_t)yetty_ytermsink_mouse_sub);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_mouse_sub: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_mouse_sub: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ytermsink_mouse_sub: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ytermsink_mouse_sub: dispatch_lookup failed");
    return ((yetty_ytermsink_mouse_sub_fn)dispatch_impl_r.value)(obj, click_enabled, move_enabled,
                                                                 key_enabled);
}

struct yetty_ycore_void_result yetty_ytermsink_clipboard_write(struct yetty_yclass_object *obj,
                                                               const char *text, size_t len,
                                                               int clipboard)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ytermsink", (yetty_yclass_method_id_t)yetty_ytermsink_clipboard_write);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ytermsink_clipboard_write: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_clipboard_write: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ytermsink_clipboard_write: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ytermsink_clipboard_write: dispatch_lookup failed");
    return ((yetty_ytermsink_clipboard_write_fn)dispatch_impl_r.value)(obj, text, len, clipboard);
}

struct yetty_ycore_void_result yetty_ytermsink_sixel_write(struct yetty_yclass_object *obj,
                                                           const char *data, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ytermsink", (yetty_yclass_method_id_t)yetty_ytermsink_sixel_write);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ytermsink_sixel_write: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_sixel_write: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ytermsink_sixel_write: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ytermsink_sixel_write: dispatch_lookup failed");
    return ((yetty_ytermsink_sixel_write_fn)dispatch_impl_r.value)(obj, data, len);
}

struct yetty_ycore_void_result yetty_ytermsink_set_title(struct yetty_yclass_object *obj,
                                                         const char *title, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ytermsink", (yetty_yclass_method_id_t)yetty_ytermsink_set_title);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_set_title: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ytermsink_set_title: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ytermsink_set_title: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ytermsink_set_title: dispatch_lookup failed");
    return ((yetty_ytermsink_set_title_fn)dispatch_impl_r.value)(obj, title, len);
}
