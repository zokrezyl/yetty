/* GENERATED — do not edit. */
#include <yetty/api/yjupyter/client.h>

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
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object *obj,
                                                          const char *base_url, const char *token);
struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object *obj,
                                                                 const char *code, const char *tag);
struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object *obj,
                                                                 int timeout_ms);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(
    struct yetty_yclass_object *obj);
struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(
    struct yetty_yclass_object *obj, const char *parent_msg_id);
struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_open_fn)(
    struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yjupyter_client_execute_fn)(
    struct yetty_yclass_object *, const char *, const char *);
typedef struct yetty_yclass_object_ptr_result (*yetty_yjupyter_client_poll_fn)(
    struct yetty_yclass_object *, int);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_kernel_state_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_const_char_ptr_result (*yetty_yjupyter_client_tag_for_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_close_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yjupyter_client_destroy_fn)(
    struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_yjupyter_client_open(struct yetty_yclass_object *obj,
                                                          const char *base_url, const char *token)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_open);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_open: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_open: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_client_open: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_client_open: dispatch_lookup failed");
    return ((yetty_yjupyter_client_open_fn)dispatch_impl_r.value)(obj, base_url, token);
}

struct yetty_ycore_char_ptr_result yetty_yjupyter_client_execute(struct yetty_yclass_object *obj,
                                                                 const char *code, const char *tag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_execute);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_char_ptr,
                             "yetty_yjupyter_client_execute: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_char_ptr, "yetty_yjupyter_client_execute: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, object_class_r,
                        "yetty_yjupyter_client_execute: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_client_execute: dispatch_lookup failed");
    return ((yetty_yjupyter_client_execute_fn)dispatch_impl_r.value)(obj, code, tag);
}

struct yetty_yclass_object_ptr_result yetty_yjupyter_client_poll(struct yetty_yclass_object *obj,
                                                                 int timeout_ms)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_poll);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yjupyter_client_poll: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yjupyter_client_poll: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_class_r,
                        "yetty_yjupyter_client_poll: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dispatch_impl_r,
                        "yetty_yjupyter_client_poll: dispatch_lookup failed");
    return ((yetty_yjupyter_client_poll_fn)dispatch_impl_r.value)(obj, timeout_ms);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_kernel_state(
    struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_kernel_state);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_client_kernel_state: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr,
                         "yetty_yjupyter_client_kernel_state: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_client_kernel_state: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_client_kernel_state: dispatch_lookup failed");
    return ((yetty_yjupyter_client_kernel_state_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_const_char_ptr_result yetty_yjupyter_client_tag_for(
    struct yetty_yclass_object *obj, const char *parent_msg_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_tag_for);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_const_char_ptr,
                             "yetty_yjupyter_client_tag_for: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_yjupyter_client_tag_for: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, object_class_r,
                        "yetty_yjupyter_client_tag_for: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, dispatch_impl_r,
                        "yetty_yjupyter_client_tag_for: dispatch_lookup failed");
    return ((yetty_yjupyter_client_tag_for_fn)dispatch_impl_r.value)(obj, parent_msg_id);
}

struct yetty_ycore_void_result yetty_yjupyter_client_close(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_close);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_client_close: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_close: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_client_close: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_client_close: dispatch_lookup failed");
    return ((yetty_yjupyter_client_close_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yjupyter_client_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yjupyter", (yetty_yclass_method_id_t)yetty_yjupyter_client_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yjupyter_client_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yjupyter_client_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yjupyter_client_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yjupyter_client_destroy: dispatch_lookup failed");
    return ((yetty_yjupyter_client_destroy_fn)dispatch_impl_r.value)(obj);
}
