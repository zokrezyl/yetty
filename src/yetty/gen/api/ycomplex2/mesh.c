/* GENERATED — do not edit. */
#include <yetty/api/ycomplex2/mesh.h>

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
struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *obj,
                                                       const char *path);
typedef struct yetty_ycore_void_result (*yetty_ycomplex2_set_glb_fn)(struct yetty_yclass_object *,
                                                                     const char *);

struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *obj,
                                                       const char *path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycomplex2", (yetty_yclass_method_id_t)yetty_ycomplex2_set_glb);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_set_glb: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_set_glb: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ycomplex2_set_glb: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ycomplex2_set_glb: dispatch_lookup failed");
    return ((yetty_ycomplex2_set_glb_fn)dispatch_impl_r.value)(obj, path);
}
