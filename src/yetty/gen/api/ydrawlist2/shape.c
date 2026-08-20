/* GENERATED — do not edit. */
#include <yetty/api/ydrawlist2/shape.h>

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
struct yetty_ycore_void_result yetty_ydrawlist2_set_fill(struct yetty_yclass_object *obj,
                                                         const char *color);
struct yetty_ycore_void_result yetty_ydrawlist2_set_stroke(struct yetty_yclass_object *obj,
                                                           const char *color);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_fill_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_stroke_fn)(
    struct yetty_yclass_object *, const char *);

struct yetty_ycore_void_result yetty_ydrawlist2_set_fill(struct yetty_yclass_object *obj,
                                                         const char *color)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_fill);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_fill: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_fill: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_set_fill: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_set_fill: dispatch_lookup failed");
    return ((yetty_ydrawlist2_set_fill_fn)dispatch_impl_r.value)(obj, color);
}

struct yetty_ycore_void_result yetty_ydrawlist2_set_stroke(struct yetty_yclass_object *obj,
                                                           const char *color)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ydrawlist2", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_stroke);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ydrawlist2_set_stroke: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_set_stroke: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ydrawlist2_set_stroke: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ydrawlist2_set_stroke: dispatch_lookup failed");
    return ((yetty_ydrawlist2_set_stroke_fn)dispatch_impl_r.value)(obj, color);
}
