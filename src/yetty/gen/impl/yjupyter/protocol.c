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

struct yetty_yclass_object_ptr_result;
struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yjupyter_message_build(
    struct yetty_yclass_object *obj, const char *msg_type, const char *channel,
    const char *session_id, const char *msg_id, const char *parent_msg_id,
    const char *content_json);
struct yetty_ycore_void_result yetty_yjupyter_message_from_wire(struct yetty_yclass_object *obj,
                                                                const char *json);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_to_wire(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_type(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_id(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_parent_msg_id(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_channel(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_session(
    struct yetty_yclass_object *obj);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_json(
    struct yetty_yclass_object *obj);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_string(
    struct yetty_yclass_object *obj, const char *key);
struct yetty_ycore_int_result yetty_yjupyter_message_content_int(struct yetty_yclass_object *obj,
                                                                 const char *key);
struct yetty_ycore_void_result yetty_yjupyter_message_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yjupyter_session_init(struct yetty_yclass_object *obj,
                                                           const char *session_id);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_id(struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_kernel_state(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_new_request(
    struct yetty_yclass_object *obj, const char *msg_type, const char *channel,
    const char *content_json, const char *tag);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_handle_wire(
    struct yetty_yclass_object *obj, const char *json);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_tag_for(
    struct yetty_yclass_object *obj, const char *parent_msg_id);
struct yetty_ycore_void_result yetty_yjupyter_session_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_message_build_fn)(
    struct yetty_yclass_object *, const char *, const char *, const char *, const char *,
    const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_message_from_wire_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_message_to_wire_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_msg_type_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_msg_id_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_parent_msg_id_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_channel_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_session_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_message_content_json_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_message_content_string_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_yjupyter_message_content_int_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_message_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_session_init_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_session_id_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_session_kernel_state_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_session_new_request_fn)(
    struct yetty_yclass_object *, const char *, const char *, const char *, const char *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_session_handle_wire_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_session_tag_for_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_session_destroy_fn)(
    struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_build_fn
    yetty_yjupyter_message_yetty_yjupyter_message_build_message_build_check = message_build;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_from_wire_fn
    yetty_yjupyter_message_yetty_yjupyter_message_from_wire_message_from_wire_check =
        message_from_wire;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_to_wire_fn
    yetty_yjupyter_message_yetty_yjupyter_message_to_wire_message_to_wire_check = message_to_wire;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_msg_type_fn
    yetty_yjupyter_message_yetty_yjupyter_message_msg_type_message_msg_type_check =
        message_msg_type;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_msg_id_fn
    yetty_yjupyter_message_yetty_yjupyter_message_msg_id_message_msg_id_check = message_msg_id;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_parent_msg_id_fn
    yetty_yjupyter_message_yetty_yjupyter_message_parent_msg_id_message_parent_msg_id_check =
        message_parent_msg_id;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_channel_fn
    yetty_yjupyter_message_yetty_yjupyter_message_channel_message_channel_check = message_channel;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_session_fn
    yetty_yjupyter_message_yetty_yjupyter_message_session_message_session_check = message_session;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_content_json_fn
    yetty_yjupyter_message_yetty_yjupyter_message_content_json_message_content_json_check =
        message_content_json;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_content_string_fn
    yetty_yjupyter_message_yetty_yjupyter_message_content_string_message_content_string_check =
        message_content_string;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_content_int_fn
    yetty_yjupyter_message_yetty_yjupyter_message_content_int_message_content_int_check =
        message_content_int;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_destroy_fn
    yetty_yjupyter_message_yetty_yjupyter_message_destroy_message_destroy_check = message_destroy;

struct yetty_yclass_ptr_result yetty_yjupyter_message_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yjupyter_message");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yjupyter_message",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yjupyter_message),
        .data_align = _Alignof(struct yetty_yjupyter_message),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yjupyter", "message_build", (yetty_yclass_method_id_t)yetty_yjupyter_message_build,
         (yetty_yclass_impl_t)message_build},
        {"yetty_yjupyter", "message_from_wire",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_from_wire,
         (yetty_yclass_impl_t)message_from_wire},
        {"yetty_yjupyter", "message_to_wire",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_to_wire,
         (yetty_yclass_impl_t)message_to_wire},
        {"yetty_yjupyter", "message_msg_type",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_type,
         (yetty_yclass_impl_t)message_msg_type},
        {"yetty_yjupyter", "message_msg_id",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_id,
         (yetty_yclass_impl_t)message_msg_id},
        {"yetty_yjupyter", "message_parent_msg_id",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_parent_msg_id,
         (yetty_yclass_impl_t)message_parent_msg_id},
        {"yetty_yjupyter", "message_channel",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_channel,
         (yetty_yclass_impl_t)message_channel},
        {"yetty_yjupyter", "message_session",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_session,
         (yetty_yclass_impl_t)message_session},
        {"yetty_yjupyter", "message_content_json",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_content_json,
         (yetty_yclass_impl_t)message_content_json},
        {"yetty_yjupyter", "message_content_string",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_content_string,
         (yetty_yclass_impl_t)message_content_string},
        {"yetty_yjupyter", "message_content_int",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_content_int,
         (yetty_yclass_impl_t)message_content_int},
        {"yetty_yjupyter", "message_destroy",
         (yetty_yclass_method_id_t)yetty_yjupyter_message_destroy,
         (yetty_yclass_impl_t)message_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yjupyter_message_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yjupyter_message_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yjupyter_message_ptr_result yetty_yjupyter_message_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_message_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yjupyter_message_ptr, "yetty_yjupyter_message_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yjupyter_message_ptr, "yetty_yjupyter_message_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yjupyter_message_ptr, (struct yetty_yjupyter_message *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_message_to(struct yetty_yjupyter_message *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_message_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yjupyter_message_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yjupyter_message_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_init_fn
    yetty_yjupyter_session_yetty_yjupyter_session_init_session_init_check = session_init;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_id_fn
    yetty_yjupyter_session_yetty_yjupyter_session_id_session_id_check = session_id;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_kernel_state_fn
    yetty_yjupyter_session_yetty_yjupyter_session_kernel_state_session_kernel_state_check =
        session_kernel_state;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_new_request_fn
    yetty_yjupyter_session_yetty_yjupyter_session_new_request_session_new_request_check =
        session_new_request;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_handle_wire_fn
    yetty_yjupyter_session_yetty_yjupyter_session_handle_wire_session_handle_wire_check =
        session_handle_wire;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_tag_for_fn
    yetty_yjupyter_session_yetty_yjupyter_session_tag_for_session_tag_for_check = session_tag_for;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_destroy_fn
    yetty_yjupyter_session_yetty_yjupyter_session_destroy_session_destroy_check = session_destroy;

struct yetty_yclass_ptr_result yetty_yjupyter_session_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yjupyter_session");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yjupyter_session",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yjupyter_session),
        .data_align = _Alignof(struct yetty_yjupyter_session),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yjupyter", "session_init", (yetty_yclass_method_id_t)yetty_yjupyter_session_init,
         (yetty_yclass_impl_t)session_init},
        {"yetty_yjupyter", "session_id", (yetty_yclass_method_id_t)yetty_yjupyter_session_id,
         (yetty_yclass_impl_t)session_id},
        {"yetty_yjupyter", "session_kernel_state",
         (yetty_yclass_method_id_t)yetty_yjupyter_session_kernel_state,
         (yetty_yclass_impl_t)session_kernel_state},
        {"yetty_yjupyter", "session_new_request",
         (yetty_yclass_method_id_t)yetty_yjupyter_session_new_request,
         (yetty_yclass_impl_t)session_new_request},
        {"yetty_yjupyter", "session_handle_wire",
         (yetty_yclass_method_id_t)yetty_yjupyter_session_handle_wire,
         (yetty_yclass_impl_t)session_handle_wire},
        {"yetty_yjupyter", "session_tag_for",
         (yetty_yclass_method_id_t)yetty_yjupyter_session_tag_for,
         (yetty_yclass_impl_t)session_tag_for},
        {"yetty_yjupyter", "session_destroy",
         (yetty_yclass_method_id_t)yetty_yjupyter_session_destroy,
         (yetty_yclass_impl_t)session_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yjupyter_session_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yjupyter_session_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yjupyter_session_ptr_result yetty_yjupyter_session_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_session_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yjupyter_session_ptr, "yetty_yjupyter_session_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yjupyter_session_ptr, "yetty_yjupyter_session_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yjupyter_session_ptr, (struct yetty_yjupyter_session *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_to(struct yetty_yjupyter_session *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_session_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_yjupyter_session_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_yjupyter_session_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_message_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_message_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yjupyter_message");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yjupyter_message_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yjupyter_message_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_message_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yjupyter_session");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yjupyter_session_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yjupyter_session_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_session_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_yjupyter_message_class_get(void);
struct yetty_yclass_ptr_result yetty_yjupyter_session_class_get(void);
struct yetty_ycore_void_result yetty_yjupyter_protocol_register(void);

/* ---- yjupyter_protocol: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yjupyter_protocol_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yjupyter_message") == 0) {
        return yetty_yjupyter_message_class_get();
    }
    if (strcmp(name, "yetty_yjupyter_session") == 0) {
        return yetty_yjupyter_session_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yjupyter_protocol: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yjupyter_protocol_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yjupyter_protocol_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yjupyter_protocol_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
