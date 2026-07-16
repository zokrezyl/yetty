/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

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

YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_from_json_text_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_from_json_text_check = mime_bundle_from_json_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_to_json_text_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_to_json_text_check = mime_bundle_to_json_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_count_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_count_check = mime_bundle_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_mime_at_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_mime_at_check = mime_bundle_mime_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_kind_at_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_kind_at_check = mime_bundle_kind_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_bytes_at_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_bytes_at_check = mime_bundle_bytes_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_json_at_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_json_at_check = mime_bundle_json_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_destroy_fn yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_destroy_check = mime_bundle_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_mime_bundle_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_ynotebook_mime_bundle");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_mime_bundle",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_mime_bundle),
        .data_align = _Alignof(struct yetty_ynotebook_mime_bundle),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "mime_bundle_from_json_text", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_from_json_text, (yetty_yclass_impl_t)mime_bundle_from_json_text},
        {"yetty_ynotebook", "mime_bundle_to_json_text", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_to_json_text, (yetty_yclass_impl_t)mime_bundle_to_json_text},
        {"yetty_ynotebook", "mime_bundle_count", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_count, (yetty_yclass_impl_t)mime_bundle_count},
        {"yetty_ynotebook", "mime_bundle_mime_at", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_mime_at, (yetty_yclass_impl_t)mime_bundle_mime_at},
        {"yetty_ynotebook", "mime_bundle_kind_at", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_kind_at, (yetty_yclass_impl_t)mime_bundle_kind_at},
        {"yetty_ynotebook", "mime_bundle_bytes_at", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_bytes_at, (yetty_yclass_impl_t)mime_bundle_bytes_at},
        {"yetty_ynotebook", "mime_bundle_json_at", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_json_at, (yetty_yclass_impl_t)mime_bundle_json_at},
        {"yetty_ynotebook", "mime_bundle_destroy", (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_destroy, (yetty_yclass_impl_t)mime_bundle_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_mime_bundle_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ynotebook_mime_bundle_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_mime_bundle_ptr_result yetty_ynotebook_mime_bundle_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_mime_bundle_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_ynotebook_mime_bundle_ptr, "yetty_ynotebook_mime_bundle_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_ynotebook_mime_bundle_ptr, "yetty_ynotebook_mime_bundle_from: object_data", slice_r);
    return YETTY_OK(yetty_ynotebook_mime_bundle_ptr, (struct yetty_ynotebook_mime_bundle *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_to(struct yetty_ynotebook_mime_bundle *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_mime_bundle_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ynotebook_mime_bundle_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ynotebook_mime_bundle_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


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

struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_mime_bundle");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_mime_bundle_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_mime_bundle_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ynotebook_mime_bundle");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_ynotebook_mime_bundle_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ynotebook_mime_bundle";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_mime_bundle_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_mime_bundle_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ynotebook_mime_bundle_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

