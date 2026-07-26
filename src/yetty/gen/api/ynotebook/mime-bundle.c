/* GENERATED — do not edit. */
#include <yetty/api/ynotebook/mime-bundle.h>

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

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_size_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_from_json_text(struct yetty_yclass_object * obj, const char * data_json, const char * metadata_json);
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_to_json_text(struct yetty_yclass_object * obj);
struct yetty_ycore_size_result yetty_ynotebook_mime_bundle_count(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_mime_at(struct yetty_yclass_object * obj, size_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_kind_at(struct yetty_yclass_object * obj, size_t index);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_bytes_at(struct yetty_yclass_object * obj, size_t index, const uint8_t ** out_bytes, size_t * out_len);
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_json_at(struct yetty_yclass_object * obj, size_t index);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_mime_bundle_from_json_text_fn)(struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_mime_bundle_to_json_text_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_mime_bundle_count_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_mime_bundle_mime_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_mime_bundle_kind_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_mime_bundle_bytes_at_fn)(struct yetty_yclass_object *, size_t, const uint8_t **, size_t *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_mime_bundle_json_at_fn)(struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_mime_bundle_destroy_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_from_json_text(struct yetty_yclass_object * obj, const char * data_json, const char * metadata_json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_from_json_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_mime_bundle_from_json_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_mime_bundle_from_json_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_mime_bundle_from_json_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_mime_bundle_from_json_text: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_from_json_text_fn)dispatch_impl_r.value)(obj, data_json, metadata_json);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_to_json_text(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_to_json_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_mime_bundle_to_json_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_mime_bundle_to_json_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_mime_bundle_to_json_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_mime_bundle_to_json_text: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_to_json_text_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_size_result yetty_ynotebook_mime_bundle_count(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_count);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_mime_bundle_count: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_size, "yetty_ynotebook_mime_bundle_count: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, object_class_r, "yetty_ynotebook_mime_bundle_count: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, dispatch_impl_r, "yetty_ynotebook_mime_bundle_count: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_count_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_mime_at(struct yetty_yclass_object * obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_mime_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_mime_bundle_mime_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_mime_bundle_mime_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_mime_bundle_mime_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_mime_bundle_mime_at: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_mime_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_kind_at(struct yetty_yclass_object * obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_kind_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_mime_bundle_kind_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ynotebook_mime_bundle_kind_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_ynotebook_mime_bundle_kind_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_ynotebook_mime_bundle_kind_at: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_kind_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_bytes_at(struct yetty_yclass_object * obj, size_t index, const uint8_t ** out_bytes, size_t * out_len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_bytes_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_mime_bundle_bytes_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_mime_bundle_bytes_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_mime_bundle_bytes_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_mime_bundle_bytes_at: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_bytes_at_fn)dispatch_impl_r.value)(obj, index, out_bytes, out_len);
}

struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_json_at(struct yetty_yclass_object * obj, size_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_json_at);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_mime_bundle_json_at: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_ynotebook_mime_bundle_json_at: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_ynotebook_mime_bundle_json_at: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_ynotebook_mime_bundle_json_at: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_json_at_fn)dispatch_impl_r.value)(obj, index);
}

struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ynotebook", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_mime_bundle_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ynotebook_mime_bundle_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ynotebook_mime_bundle_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ynotebook_mime_bundle_destroy: dispatch_lookup failed");
    return ((yetty_ynotebook_mime_bundle_destroy_fn)dispatch_impl_r.value)(obj);
}

