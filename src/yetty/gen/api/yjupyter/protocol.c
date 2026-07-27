/* GENERATED — do not edit. */
#include <yetty/api/yjupyter/protocol.h>

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

struct yetty_ycore_void_result yetty_yjupyter_message_build(
    struct yetty_yclass_object *obj, const char *msg_type, const char *channel,
    const char *session_id, const char *msg_id, const char *parent_msg_id, const char *content_json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_build);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_message_build: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_build: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_message_build: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_message_build: dispatch_lookup failed");
    return ((yetty_yjupyter_message_build_fn)dispatch_impl_r.value)(
        obj, msg_type, channel, session_id, msg_id, parent_msg_id, content_json);
}

struct yetty_ycore_void_result yetty_yjupyter_message_from_wire(struct yetty_yclass_object *obj,
                                                                const char *json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_from_wire);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_message_from_wire: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_from_wire: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_message_from_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_message_from_wire: dispatch_lookup failed");
    return ((yetty_yjupyter_message_from_wire_fn)dispatch_impl_r.value)(obj, json);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_message_to_wire(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_to_wire);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_yjupyter_message_to_wire: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_to_wire: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_yjupyter_message_to_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_to_wire: dispatch_lookup failed");
    return ((yetty_yjupyter_message_to_wire_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_type(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_type);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_message_msg_type: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_yjupyter_message_msg_type: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_message_msg_type: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_msg_type: dispatch_lookup failed");
    return ((yetty_yjupyter_message_msg_type_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_msg_id(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_msg_id);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_message_msg_id: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_msg_id: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_message_msg_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_msg_id: dispatch_lookup failed");
    return ((yetty_yjupyter_message_msg_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_parent_msg_id(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_parent_msg_id);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_message_parent_msg_id: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_yjupyter_message_parent_msg_id: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_message_parent_msg_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_parent_msg_id: dispatch_lookup failed");
    return ((yetty_yjupyter_message_parent_msg_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_channel(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_channel);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_message_channel: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_channel: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_message_channel: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_channel: dispatch_lookup failed");
    return ((yetty_yjupyter_message_channel_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_message_session(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_session);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_message_session: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_message_session: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_message_session: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_session: dispatch_lookup failed");
    return ((yetty_yjupyter_message_session_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_json(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_json);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_yjupyter_message_content_json: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_message_content_json: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_yjupyter_message_content_json: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_content_json: dispatch_lookup failed");
    return ((yetty_yjupyter_message_content_json_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_message_content_string(
    struct yetty_yclass_object *obj, const char *key)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_string);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_yjupyter_message_content_string: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr,
                         "yetty_yjupyter_message_content_string: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_yjupyter_message_content_string: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_message_content_string: dispatch_lookup failed");
    return ((yetty_yjupyter_message_content_string_fn)dispatch_impl_r.value)(obj, key);
}

struct yetty_ycore_int_result yetty_yjupyter_message_content_int(struct yetty_yclass_object *obj,
                                                                 const char *key)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_content_int);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_yjupyter_message_content_int: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yjupyter_message_content_int: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_yjupyter_message_content_int: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_yjupyter_message_content_int: dispatch_lookup failed");
    return ((yetty_yjupyter_message_content_int_fn)dispatch_impl_r.value)(obj, key);
}

struct yetty_ycore_void_result yetty_yjupyter_message_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_message_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_message_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_message_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_message_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_message_destroy: dispatch_lookup failed");
    return ((yetty_yjupyter_message_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yjupyter_session_init(struct yetty_yclass_object *obj,
                                                           const char *session_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_init);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_session_init: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_session_init: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_session_init: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_session_init: dispatch_lookup failed");
    return ((yetty_yjupyter_session_init_fn)dispatch_impl_r.value)(obj, session_id);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_id(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_id);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_session_id: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_id: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_session_id: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_session_id: dispatch_lookup failed");
    return ((yetty_yjupyter_session_id_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_kernel_state(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_kernel_state);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_session_kernel_state: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_yjupyter_session_kernel_state: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_session_kernel_state: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_session_kernel_state: dispatch_lookup failed");
    return ((yetty_yjupyter_session_kernel_state_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_new_request(
    struct yetty_yclass_object *obj, const char *msg_type, const char *channel,
    const char *content_json, const char *tag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_new_request);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yjupyter_session_new_request: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_session_new_request: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r,
                        "yetty_yjupyter_session_new_request: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r,
                        "yetty_yjupyter_session_new_request: dispatch_lookup failed");
    return ((yetty_yjupyter_session_new_request_fn)dispatch_impl_r.value)(obj, msg_type, channel,
                                                                          content_json, tag);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_session_handle_wire(
    struct yetty_yclass_object *obj, const char *json)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_handle_wire);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yjupyter_session_handle_wire: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yjupyter_session_handle_wire: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r,
                        "yetty_yjupyter_session_handle_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r,
                        "yetty_yjupyter_session_handle_wire: dispatch_lookup failed");
    return ((yetty_yjupyter_session_handle_wire_fn)dispatch_impl_r.value)(obj, json);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_session_tag_for(
    struct yetty_yclass_object *obj, const char *parent_msg_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_tag_for);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_session_tag_for: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_session_tag_for: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_session_tag_for: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_session_tag_for: dispatch_lookup failed");
    return ((yetty_yjupyter_session_tag_for_fn)dispatch_impl_r.value)(obj, parent_msg_id);
}

struct yetty_ycore_void_result yetty_yjupyter_session_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_session_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_session_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_session_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_session_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_session_destroy: dispatch_lookup failed");
    return ((yetty_yjupyter_session_destroy_fn)dispatch_impl_r.value)(obj);
}
