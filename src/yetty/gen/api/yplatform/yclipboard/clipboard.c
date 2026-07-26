/* GENERATED — do not edit. */
#include <yetty/api/yplatform/yclipboard/clipboard.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yplatform_clipboard_set_text(struct yetty_yclass_object * obj, const char * text, size_t len);
struct yetty_ycore_void_result yetty_yplatform_clipboard_request_paste(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_clipboard_drain(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_set_text_fn)(struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_request_paste_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_clipboard_drain_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_yplatform_clipboard_set_text(struct yetty_yclass_object * obj, const char * text, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_clipboard_set_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_clipboard_set_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_clipboard_set_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_clipboard_set_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_clipboard_set_text: dispatch_lookup failed");
    return ((yetty_yplatform_clipboard_set_text_fn)dispatch_impl_r.value)(obj, text, len);
}

struct yetty_ycore_void_result yetty_yplatform_clipboard_request_paste(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_clipboard_request_paste);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_clipboard_request_paste: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_clipboard_request_paste: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_clipboard_request_paste: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_clipboard_request_paste: dispatch_lookup failed");
    return ((yetty_yplatform_clipboard_request_paste_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yplatform_clipboard_drain(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_clipboard_drain);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_clipboard_drain: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_clipboard_drain: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_clipboard_drain: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_clipboard_drain: dispatch_lookup failed");
    return ((yetty_yplatform_clipboard_drain_fn)dispatch_impl_r.value)(obj);
}

