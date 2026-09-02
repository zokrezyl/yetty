/* GENERATED — do not edit. */
#include <yetty/api/ydrawlist2/list.h>

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
struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *drawable);
struct yetty_ycore_void_result yetty_ydrawlist2_begin_group(struct yetty_yclass_object *obj,
                                                            uint32_t group_id);
struct yetty_ycore_void_result yetty_ydrawlist2_end_group(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_delete_group(struct yetty_yclass_object *obj,
                                                             uint32_t group_id);
struct yetty_ycore_void_result yetty_ydrawlist2_update(struct yetty_yclass_object *obj, uint32_t id,
                                                       struct yetty_ycore_buffer payload);
struct yetty_ycore_void_result yetty_ydrawlist2_path(struct yetty_yclass_object *obj,
                                                     struct yetty_ycore_buffer prefix);
struct yetty_ycore_void_result yetty_ydrawlist2_reserve(struct yetty_yclass_object *obj,
                                                        uint32_t height_px);
struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_add_fn)(struct yetty_yclass_object *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_begin_group_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_end_group_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_delete_group_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_update_fn)(struct yetty_yclass_object *,
                                                                     uint32_t,
                                                                     struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_path_fn)(struct yetty_yclass_object *,
                                                                   struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_reserve_fn)(struct yetty_yclass_object *,
                                                                      uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_dcs_emit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_destroy_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *drawable)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_add);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_add: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_add: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_add: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_add: dispatch_lookup failed");
    return ((yetty_ydrawlist2_add_fn)dispatch_impl_r.value)(obj, drawable);
}

struct yetty_ycore_void_result yetty_ydrawlist2_begin_group(struct yetty_yclass_object *obj,
                                                            uint32_t group_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_begin_group);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ydrawlist2_begin_group: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_begin_group: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_begin_group: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_begin_group: dispatch_lookup failed");
    return ((yetty_ydrawlist2_begin_group_fn)dispatch_impl_r.value)(obj, group_id);
}

struct yetty_ycore_void_result yetty_ydrawlist2_end_group(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_end_group);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_end_group: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_end_group: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_end_group: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_end_group: dispatch_lookup failed");
    return ((yetty_ydrawlist2_end_group_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ydrawlist2_delete_group(struct yetty_yclass_object *obj,
                                                             uint32_t group_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_delete_group);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ydrawlist2_delete_group: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_delete_group: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_delete_group: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_delete_group: dispatch_lookup failed");
    return ((yetty_ydrawlist2_delete_group_fn)dispatch_impl_r.value)(obj, group_id);
}

struct yetty_ycore_void_result yetty_ydrawlist2_update(struct yetty_yclass_object *obj, uint32_t id,
                                                       struct yetty_ycore_buffer payload)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_update);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_update: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_update: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_update: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_update: dispatch_lookup failed");
    return ((yetty_ydrawlist2_update_fn)dispatch_impl_r.value)(obj, id, payload);
}

struct yetty_ycore_void_result yetty_ydrawlist2_path(struct yetty_yclass_object *obj,
                                                     struct yetty_ycore_buffer prefix)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_path);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_path: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_path: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_path: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_path: dispatch_lookup failed");
    return ((yetty_ydrawlist2_path_fn)dispatch_impl_r.value)(obj, prefix);
}

struct yetty_ycore_void_result yetty_ydrawlist2_reserve(struct yetty_yclass_object *obj,
                                                        uint32_t height_px)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_reserve);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_reserve: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_reserve: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_reserve: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_reserve: dispatch_lookup failed");
    return ((yetty_ydrawlist2_reserve_fn)dispatch_impl_r.value)(obj, height_px);
}

struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_dcs_emit);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_dcs_emit: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_dcs_emit: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_dcs_emit: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_dcs_emit: dispatch_lookup failed");
    return ((yetty_ydrawlist2_dcs_emit_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_destroy: dispatch_lookup failed");
    return ((yetty_ydrawlist2_destroy_fn)dispatch_impl_r.value)(obj);
}
