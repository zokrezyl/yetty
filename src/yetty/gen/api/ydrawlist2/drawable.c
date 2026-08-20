/* GENERATED — do not edit. */
#include <yetty/api/ydrawlist2/drawable.h>

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
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_ydrawlist2_pack(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *obj,
                                                         const char *name);
struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *obj,
                                                         const char *body);
struct yetty_ycore_void_result yetty_ydrawlist2_set_color(struct yetty_yclass_object *obj,
                                                          const char *color);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_pack_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_name_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_body_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_color_fn)(
    struct yetty_yclass_object *, const char *);

struct yetty_ycore_void_result yetty_ydrawlist2_pack(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_pack: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_pack: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_pack: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_pack: dispatch_lookup failed");
    return ((yetty_ydrawlist2_pack_fn)dispatch_impl_r.value)(obj, list);
}

struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *obj,
                                                         const char *name)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_name);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_name: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_name: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_set_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_set_name: dispatch_lookup failed");
    return ((yetty_ydrawlist2_set_name_fn)dispatch_impl_r.value)(obj, name);
}

struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *obj,
                                                         const char *body)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_body);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_body: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_body: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_set_body: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_set_body: dispatch_lookup failed");
    return ((yetty_ydrawlist2_set_body_fn)dispatch_impl_r.value)(obj, body);
}

struct yetty_ycore_void_result yetty_ydrawlist2_set_color(struct yetty_yclass_object *obj,
                                                          const char *color)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_color);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_color: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_color: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_set_color: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_set_color: dispatch_lookup failed");
    return ((yetty_ydrawlist2_set_color_fn)dispatch_impl_r.value)(obj, color);
}
