/* GENERATED — do not edit. */
#include <yetty/api/yai/engine.h>

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

struct yai_app;
struct yetty_ycore_void_result;
struct yyjson_val;
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_object *obj,
                                               struct yai_app *app);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_object *obj,
                                                           struct yai_app *app, const char *text);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_object *obj,
                                                      struct yai_app *app,
                                                      struct yyjson_val *event);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object *obj,
                                                   struct yai_app *app);
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object *obj,
                                                       struct yai_app *app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object *obj,
                                                      struct yai_app *app);
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_object *obj,
                                                     struct yai_app *app, char *out,
                                                     size_t out_size);
struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_object *obj,
                                                      struct yai_app *app, const char *key,
                                                      const char *value);
struct yetty_ycore_void_result yetty_yai_resolve_permission(struct yetty_yclass_object *obj,
                                                            struct yai_app *app, int allowed);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_object *,
                                                             struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(
    struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_object *,
                                                                    struct yai_app *,
                                                                    struct yyjson_val *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_object *,
                                                                 struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_object *,
                                                                     struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_object *,
                                                                    struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_object *,
                                                                       struct yai_app *, char *,
                                                                       size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_object *,
                                                                   struct yai_app *, char *,
                                                                   size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_apply_config_fn)(struct yetty_yclass_object *,
                                                                    struct yai_app *, const char *,
                                                                    const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_resolve_permission_fn)(
    struct yetty_yclass_object *, struct yai_app *, int);

struct yetty_ycore_void_result yetty_yai_resolve_permission(struct yetty_yclass_object *obj,
                                                            struct yai_app *app, int allowed)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_resolve_permission);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yai_resolve_permission: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_resolve_permission: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_resolve_permission: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_resolve_permission: dispatch_lookup failed");
    return ((yetty_yai_resolve_permission_fn)dispatch_impl_r.value)(obj, app, allowed);
}

struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_object *obj,
                                                      struct yai_app *app, struct yyjson_val *event)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_handle_event);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_handle_event: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_handle_event: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_handle_event: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_handle_event: dispatch_lookup failed");
    return ((yetty_yai_handle_event_fn)dispatch_impl_r.value)(obj, app, event);
}

struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_object *obj,
                                                           struct yai_app *app, const char *text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_send_user_message);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yai_send_user_message: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_send_user_message: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_send_user_message: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_send_user_message: dispatch_lookup failed");
    return ((yetty_yai_send_user_message_fn)dispatch_impl_r.value)(obj, app, text);
}

struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object *obj,
                                                   struct yai_app *app)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_interrupt);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_interrupt: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_interrupt: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_interrupt: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_interrupt: dispatch_lookup failed");
    return ((yetty_yai_interrupt_fn)dispatch_impl_r.value)(obj, app);
}

struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_object *obj, struct yai_app *app)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yai", (yetty_yclass_method_id_t)yetty_yai_start);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_start: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_start: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yai_start: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_start: dispatch_lookup failed");
    return ((yetty_yai_start_fn)dispatch_impl_r.value)(obj, app);
}

struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_describe_config);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_describe_config: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_describe_config: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_describe_config: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_describe_config: dispatch_lookup failed");
    return ((yetty_yai_describe_config_fn)dispatch_impl_r.value)(obj, app, out, out_size);
}

struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_object *obj,
                                                     struct yai_app *app, char *out,
                                                     size_t out_size)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_config_knob);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_config_knob: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_config_knob: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_config_knob: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_config_knob: dispatch_lookup failed");
    return ((yetty_yai_config_knob_fn)dispatch_impl_r.value)(obj, app, out, out_size);
}

struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_object *obj,
                                                      struct yai_app *app, const char *key,
                                                      const char *value)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_apply_config);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_apply_config: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_apply_config: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_apply_config: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_apply_config: dispatch_lookup failed");
    return ((yetty_yai_apply_config_fn)dispatch_impl_r.value)(obj, app, key, value);
}

struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object *obj,
                                                       struct yai_app *app, int64_t exit_status)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_on_child_exit);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_on_child_exit: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_on_child_exit: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_on_child_exit: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_on_child_exit: dispatch_lookup failed");
    return ((yetty_yai_on_child_exit_fn)dispatch_impl_r.value)(obj, app, exit_status);
}

struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object *obj,
                                                      struct yai_app *app)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yai", (yetty_yclass_method_id_t)yetty_yai_on_child_eof);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yai_on_child_eof: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yai_on_child_eof: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yai_on_child_eof: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yai_on_child_eof: dispatch_lookup failed");
    return ((yetty_yai_on_child_eof_fn)dispatch_impl_r.value)(obj, app);
}
