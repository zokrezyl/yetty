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

struct yetty_yclass_object_ptr_result;
struct yetty_ycore_char_ptr_result;
struct yetty_ycore_const_char_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yjupyter_message_build(struct yetty_yclass_object * obj, const char * msg_type, const char * channel, const char * session_id, const char * msg_id, const char * parent_msg_id, const char * content_json);
struct yetty_ycore_void_result yetty_yjupyter_message_from_wire(struct yetty_yclass_object * obj, const char * json);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_to_wire(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_type(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_id(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_parent_msg_id(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_channel(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_session(struct yetty_yclass_object * obj);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_json(struct yetty_yclass_object * obj);
struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_string(struct yetty_yclass_object * obj, const char * key);
struct yetty_ycore_int_result yetty_yjupyter_message_content_int(struct yetty_yclass_object * obj, const char * key);
struct yetty_ycore_void_result yetty_yjupyter_message_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yjupyter_session_init(struct yetty_yclass_object * obj, const char * session_id);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_id(struct yetty_yclass_object * obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_kernel_state(struct yetty_yclass_object * obj);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_new_request(struct yetty_yclass_object * obj, const char * msg_type, const char * channel, const char * content_json, const char * tag);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_handle_wire(struct yetty_yclass_object * obj, const char * json);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_tag_for(struct yetty_yclass_object * obj, const char * parent_msg_id);
struct yetty_ycore_void_result yetty_yjupyter_session_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_message_build_fn)(struct yetty_yclass_object *, const char *, const char *, const char *, const char *, const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_message_from_wire_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_message_to_wire_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_msg_type_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_msg_id_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_parent_msg_id_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_channel_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_message_session_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_message_content_json_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_message_content_string_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_int_result (*yetty_yjupyter_message_content_int_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_message_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_session_init_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_session_id_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_session_kernel_state_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_session_new_request_fn)(struct yetty_yclass_object *, const char *, const char *, const char *, const char *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_session_handle_wire_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_session_tag_for_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_session_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_build_fn yetty_yjupyter_message_yetty_yjupyter_message_build_check = message_build;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_from_wire_fn yetty_yjupyter_message_yetty_yjupyter_message_from_wire_check = message_from_wire;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_to_wire_fn yetty_yjupyter_message_yetty_yjupyter_message_to_wire_check = message_to_wire;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_msg_type_fn yetty_yjupyter_message_yetty_yjupyter_message_msg_type_check = message_msg_type;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_msg_id_fn yetty_yjupyter_message_yetty_yjupyter_message_msg_id_check = message_msg_id;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_parent_msg_id_fn yetty_yjupyter_message_yetty_yjupyter_message_parent_msg_id_check = message_parent_msg_id;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_channel_fn yetty_yjupyter_message_yetty_yjupyter_message_channel_check = message_channel;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_session_fn yetty_yjupyter_message_yetty_yjupyter_message_session_check = message_session;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_content_json_fn yetty_yjupyter_message_yetty_yjupyter_message_content_json_check = message_content_json;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_content_string_fn yetty_yjupyter_message_yetty_yjupyter_message_content_string_check = message_content_string;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_content_int_fn yetty_yjupyter_message_yetty_yjupyter_message_content_int_check = message_content_int;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_message_destroy_fn yetty_yjupyter_message_yetty_yjupyter_message_destroy_check = message_destroy;

struct yetty_yclass_ptr_result yetty_yjupyter_message_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yjupyter_message");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yjupyter_message",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yjupyter_message),
        .data_align = _Alignof(struct yetty_yjupyter_message),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yjupyter", "message_build", (yetty_yclass_method_id_t)yetty_yjupyter_message_build, (yetty_yclass_impl_t)message_build},
        {"yetty_yjupyter", "message_from_wire", (yetty_yclass_method_id_t)yetty_yjupyter_message_from_wire, (yetty_yclass_impl_t)message_from_wire},
        {"yetty_yjupyter", "message_to_wire", (yetty_yclass_method_id_t)yetty_yjupyter_message_to_wire, (yetty_yclass_impl_t)message_to_wire},
        {"yetty_yjupyter", "message_msg_type", (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_type, (yetty_yclass_impl_t)message_msg_type},
        {"yetty_yjupyter", "message_msg_id", (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_id, (yetty_yclass_impl_t)message_msg_id},
        {"yetty_yjupyter", "message_parent_msg_id", (yetty_yclass_method_id_t)yetty_yjupyter_message_parent_msg_id, (yetty_yclass_impl_t)message_parent_msg_id},
        {"yetty_yjupyter", "message_channel", (yetty_yclass_method_id_t)yetty_yjupyter_message_channel, (yetty_yclass_impl_t)message_channel},
        {"yetty_yjupyter", "message_session", (yetty_yclass_method_id_t)yetty_yjupyter_message_session, (yetty_yclass_impl_t)message_session},
        {"yetty_yjupyter", "message_content_json", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_json, (yetty_yclass_impl_t)message_content_json},
        {"yetty_yjupyter", "message_content_string", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_string, (yetty_yclass_impl_t)message_content_string},
        {"yetty_yjupyter", "message_content_int", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_int, (yetty_yclass_impl_t)message_content_int},
        {"yetty_yjupyter", "message_destroy", (yetty_yclass_method_id_t)yetty_yjupyter_message_destroy, (yetty_yclass_impl_t)message_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yjupyter_message_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yjupyter_message_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yjupyter_message_ptr_result yetty_yjupyter_message_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_message_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yjupyter_message_ptr, "yetty_yjupyter_message_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yjupyter_message_ptr, "yetty_yjupyter_message_from: object_data", slice_r);
    return YETTY_OK(yetty_yjupyter_message_ptr, (struct yetty_yjupyter_message *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_message_to(struct yetty_yjupyter_message *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_message_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yjupyter_message_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yjupyter_message_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_init_fn yetty_yjupyter_session_yetty_yjupyter_session_init_check = session_init;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_id_fn yetty_yjupyter_session_yetty_yjupyter_session_id_check = session_id;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_kernel_state_fn yetty_yjupyter_session_yetty_yjupyter_session_kernel_state_check = session_kernel_state;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_new_request_fn yetty_yjupyter_session_yetty_yjupyter_session_new_request_check = session_new_request;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_handle_wire_fn yetty_yjupyter_session_yetty_yjupyter_session_handle_wire_check = session_handle_wire;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_tag_for_fn yetty_yjupyter_session_yetty_yjupyter_session_tag_for_check = session_tag_for;
YETTY_MAYBE_UNUSED
static yetty_yjupyter_session_destroy_fn yetty_yjupyter_session_yetty_yjupyter_session_destroy_check = session_destroy;

struct yetty_yclass_ptr_result yetty_yjupyter_session_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yjupyter_session");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yjupyter_session",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yjupyter_session),
        .data_align = _Alignof(struct yetty_yjupyter_session),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yjupyter", "session_init", (yetty_yclass_method_id_t)yetty_yjupyter_session_init, (yetty_yclass_impl_t)session_init},
        {"yetty_yjupyter", "session_id", (yetty_yclass_method_id_t)yetty_yjupyter_session_id, (yetty_yclass_impl_t)session_id},
        {"yetty_yjupyter", "session_kernel_state", (yetty_yclass_method_id_t)yetty_yjupyter_session_kernel_state, (yetty_yclass_impl_t)session_kernel_state},
        {"yetty_yjupyter", "session_new_request", (yetty_yclass_method_id_t)yetty_yjupyter_session_new_request, (yetty_yclass_impl_t)session_new_request},
        {"yetty_yjupyter", "session_handle_wire", (yetty_yclass_method_id_t)yetty_yjupyter_session_handle_wire, (yetty_yclass_impl_t)session_handle_wire},
        {"yetty_yjupyter", "session_tag_for", (yetty_yclass_method_id_t)yetty_yjupyter_session_tag_for, (yetty_yclass_impl_t)session_tag_for},
        {"yetty_yjupyter", "session_destroy", (yetty_yclass_method_id_t)yetty_yjupyter_session_destroy, (yetty_yclass_impl_t)session_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yjupyter_session_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yjupyter_session_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yjupyter_session_ptr_result yetty_yjupyter_session_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_session_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yjupyter_session_ptr, "yetty_yjupyter_session_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yjupyter_session_ptr, "yetty_yjupyter_session_from: object_data", slice_r);
    return YETTY_OK(yetty_yjupyter_session_ptr, (struct yetty_yjupyter_session *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_to(struct yetty_yjupyter_session *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yjupyter_session_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yjupyter_session_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yjupyter_session_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_ycore_void_result yetty_yjupyter_message_build(struct yetty_yclass_object * obj, const char * msg_type, const char * channel, const char * session_id, const char * msg_id, const char * parent_msg_id, const char * content_json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_build);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_build: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_build: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_message_build: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_message_build: dispatch_lookup failed");
    return ((yetty_yjupyter_message_build_fn)dispatch_impl_r.value)(obj, msg_type, channel, session_id, msg_id, parent_msg_id, content_json);
}

struct yetty_ycore_void_result yetty_yjupyter_message_from_wire(struct yetty_yclass_object * obj, const char * json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_from_wire);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_from_wire: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_from_wire: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_message_from_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_message_from_wire: dispatch_lookup failed");
    return ((yetty_yjupyter_message_from_wire_fn)dispatch_impl_r.value)(obj, json);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_message_to_wire(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_to_wire);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_to_wire: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_to_wire: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_yjupyter_message_to_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_to_wire: dispatch_lookup failed");
    return ((yetty_yjupyter_message_to_wire_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_type(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_type);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_msg_type: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_msg_type: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_message_msg_type: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_msg_type: dispatch_lookup failed");
    return ((yetty_yjupyter_message_msg_type_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_id(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_id);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_msg_id: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_msg_id: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_message_msg_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_msg_id: dispatch_lookup failed");
    return ((yetty_yjupyter_message_msg_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_parent_msg_id(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_parent_msg_id);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_parent_msg_id: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_parent_msg_id: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_message_parent_msg_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_parent_msg_id: dispatch_lookup failed");
    return ((yetty_yjupyter_message_parent_msg_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_channel(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_channel);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_channel: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_channel: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_message_channel: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_channel: dispatch_lookup failed");
    return ((yetty_yjupyter_message_channel_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_session(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_session);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_session: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_session: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_message_session: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_session: dispatch_lookup failed");
    return ((yetty_yjupyter_message_session_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_json(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_json);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_content_json: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_content_json: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_yjupyter_message_content_json: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_content_json: dispatch_lookup failed");
    return ((yetty_yjupyter_message_content_json_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_string(struct yetty_yclass_object * obj, const char * key)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_string);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_content_string: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_content_string: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r, "yetty_yjupyter_message_content_string: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r, "yetty_yjupyter_message_content_string: dispatch_lookup failed");
    return ((yetty_yjupyter_message_content_string_fn)dispatch_impl_r.value)(obj, key);
}

struct yetty_ycore_int_result yetty_yjupyter_message_content_int(struct yetty_yclass_object * obj, const char * key)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_int);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_yjupyter_message_content_int: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_yjupyter_message_content_int: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_yjupyter_message_content_int: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_yjupyter_message_content_int: dispatch_lookup failed");
    return ((yetty_yjupyter_message_content_int_fn)dispatch_impl_r.value)(obj, key);
}

struct yetty_ycore_void_result yetty_yjupyter_message_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_message_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_message_destroy: dispatch_lookup failed");
    return ((yetty_yjupyter_message_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yjupyter_session_init(struct yetty_yclass_object * obj, const char * session_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_init);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_session_init: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_session_init: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_session_init: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_session_init: dispatch_lookup failed");
    return ((yetty_yjupyter_session_init_fn)dispatch_impl_r.value)(obj, session_id);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_id(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_id);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_id: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_id: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_session_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_session_id: dispatch_lookup failed");
    return ((yetty_yjupyter_session_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_kernel_state(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_kernel_state);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_kernel_state: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_kernel_state: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_session_kernel_state: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_session_kernel_state: dispatch_lookup failed");
    return ((yetty_yjupyter_session_kernel_state_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_new_request(struct yetty_yclass_object * obj, const char * msg_type, const char * channel, const char * content_json, const char * tag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_new_request);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_session_new_request: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_session_new_request: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_yjupyter_session_new_request: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_yjupyter_session_new_request: dispatch_lookup failed");
    return ((yetty_yjupyter_session_new_request_fn)dispatch_impl_r.value)(obj, msg_type, channel, content_json, tag);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_handle_wire(struct yetty_yclass_object * obj, const char * json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_handle_wire);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_session_handle_wire: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_session_handle_wire: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r, "yetty_yjupyter_session_handle_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r, "yetty_yjupyter_session_handle_wire: dispatch_lookup failed");
    return ((yetty_yjupyter_session_handle_wire_fn)dispatch_impl_r.value)(obj, json);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_tag_for(struct yetty_yclass_object * obj, const char * parent_msg_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_tag_for);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_tag_for: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_tag_for: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r, "yetty_yjupyter_session_tag_for: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r, "yetty_yjupyter_session_tag_for: dispatch_lookup failed");
    return ((yetty_yjupyter_session_tag_for_fn)dispatch_impl_r.value)(obj, parent_msg_id);
}

struct yetty_ycore_void_result yetty_yjupyter_session_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_session_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_session_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yjupyter_session_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yjupyter_session_destroy: dispatch_lookup failed");
    return ((yetty_yjupyter_session_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_message_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_message_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yjupyter_message");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yjupyter_message_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_message_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yjupyter_message");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yjupyter_message_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yjupyter_message";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_message_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_message_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_message_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yjupyter_session_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yjupyter_session");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yjupyter_session_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_session_create: class accessor failed", class_accessor_r);
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
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yjupyter_session");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yjupyter_session_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yjupyter_session";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_session_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_session_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_session_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

