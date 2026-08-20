/* GENERATED — do not edit. */
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

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_size_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_from_json_text(
    struct yetty_yclass_object *obj, const char *data_json, const char *metadata_json);
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_to_json_text(
    struct yetty_yclass_object *obj);
struct yetty_ycore_size_result yetty_ynotebook_mime_bundle_count(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_mime_at(
    struct yetty_yclass_object *obj, size_t index);
struct yetty_ycore_const_char_ptr_result yetty_ynotebook_mime_bundle_kind_at(
    struct yetty_yclass_object *obj, size_t index);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_bytes_at(struct yetty_yclass_object *obj,
                                                                    size_t index,
                                                                    const uint8_t **out_bytes,
                                                                    size_t *out_len);
struct yetty_ycore_char_ptr_result yetty_ynotebook_mime_bundle_json_at(
    struct yetty_yclass_object *obj, size_t index);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_mime_bundle_from_json_text_fn)(
    struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_mime_bundle_to_json_text_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_size_result (*yetty_ynotebook_mime_bundle_count_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_mime_bundle_mime_at_fn)(
    struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_ynotebook_mime_bundle_kind_at_fn)(
    struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_mime_bundle_bytes_at_fn)(
    struct yetty_yclass_object *, size_t, const uint8_t **, size_t *);
typedef struct yetty_ycore_char_ptr_result (*yetty_ynotebook_mime_bundle_json_at_fn)(
    struct yetty_yclass_object *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ynotebook_mime_bundle_destroy_fn)(
    struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_from_json_text_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_from_json_text_mime_bundle_from_json_text_check =
        mime_bundle_from_json_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_to_json_text_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_to_json_text_mime_bundle_to_json_text_check =
        mime_bundle_to_json_text;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_count_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_count_mime_bundle_count_check =
        mime_bundle_count;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_mime_at_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_mime_at_mime_bundle_mime_at_check =
        mime_bundle_mime_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_kind_at_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_kind_at_mime_bundle_kind_at_check =
        mime_bundle_kind_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_bytes_at_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_bytes_at_mime_bundle_bytes_at_check =
        mime_bundle_bytes_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_json_at_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_json_at_mime_bundle_json_at_check =
        mime_bundle_json_at;
YETTY_MAYBE_UNUSED
static yetty_ynotebook_mime_bundle_destroy_fn
    yetty_ynotebook_mime_bundle_yetty_ynotebook_mime_bundle_destroy_mime_bundle_destroy_check =
        mime_bundle_destroy;

struct yetty_yclass_ptr_result yetty_ynotebook_mime_bundle_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ynotebook_mime_bundle");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ynotebook_mime_bundle",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ynotebook_mime_bundle),
        .data_align = _Alignof(struct yetty_ynotebook_mime_bundle),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ynotebook", "mime_bundle_from_json_text",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_from_json_text,
         (yetty_yclass_impl_t)mime_bundle_from_json_text},
        {"yetty_ynotebook", "mime_bundle_to_json_text",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_to_json_text,
         (yetty_yclass_impl_t)mime_bundle_to_json_text},
        {"yetty_ynotebook", "mime_bundle_count",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_count,
         (yetty_yclass_impl_t)mime_bundle_count},
        {"yetty_ynotebook", "mime_bundle_mime_at",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_mime_at,
         (yetty_yclass_impl_t)mime_bundle_mime_at},
        {"yetty_ynotebook", "mime_bundle_kind_at",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_kind_at,
         (yetty_yclass_impl_t)mime_bundle_kind_at},
        {"yetty_ynotebook", "mime_bundle_bytes_at",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_bytes_at,
         (yetty_yclass_impl_t)mime_bundle_bytes_at},
        {"yetty_ynotebook", "mime_bundle_json_at",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_json_at,
         (yetty_yclass_impl_t)mime_bundle_json_at},
        {"yetty_ynotebook", "mime_bundle_destroy",
         (yetty_yclass_method_id_t)yetty_ynotebook_mime_bundle_destroy,
         (yetty_yclass_impl_t)mime_bundle_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ynotebook_mime_bundle_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ynotebook_mime_bundle_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ynotebook_mime_bundle_ptr_result yetty_ynotebook_mime_bundle_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_mime_bundle_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ynotebook_mime_bundle_ptr,
                         "yetty_ynotebook_mime_bundle_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ynotebook_mime_bundle_ptr,
                         "yetty_ynotebook_mime_bundle_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ynotebook_mime_bundle_ptr,
                    (struct yetty_ynotebook_mime_bundle *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_to(
    struct yetty_ynotebook_mime_bundle *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ynotebook_mime_bundle_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ynotebook_mime_bundle_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ynotebook_mime_bundle_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ynotebook_mime_bundle_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ynotebook_mime_bundle");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ynotebook_mime_bundle_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ynotebook_mime_bundle_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ynotebook_mime_bundle_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ynotebook_mime_bundle_class_get(void);
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_register(void);

/* ---- ynotebook_mime_bundle: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ynotebook_mime_bundle_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ynotebook_mime_bundle") == 0) {
        return yetty_ynotebook_mime_bundle_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ynotebook_mime_bundle: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ynotebook_mime_bundle_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ynotebook_mime_bundle_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_ynotebook_mime_bundle_register(void);
struct yetty_ycore_void_result yetty_ynotebook_notebook_register(void);
struct yetty_ycore_void_result yetty_ynotebook_register(void);

/* ---- ynotebook: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ynotebook_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ynotebook_mime_bundle_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ynotebook_register: submodule ynotebook_mime_bundle");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ynotebook_notebook_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ynotebook_register: submodule ynotebook_notebook");
    }
    registered = true;
    return YETTY_OK_VOID();
}
