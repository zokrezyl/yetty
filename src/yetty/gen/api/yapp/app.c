/* GENERATED — do not edit. */
#include <yetty/api/yapp/app.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object * app, struct yetty_yclass_object * platform);
struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object * app, struct yetty_yclass_object * platform);
struct yetty_ycore_void_result yetty_yapp_quit(struct yetty_yclass_object * app);
typedef struct yetty_ycore_void_result (*yetty_yapp_init_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yapp_run_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yapp_quit_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_yapp_init(struct yetty_yclass_object * app, struct yetty_yclass_object * platform)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yapp", (yetty_yclass_method_id_t)yetty_yapp_init);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yapp_init: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!app) return YETTY_ERR(yetty_ycore_void, "yetty_yapp_init: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yapp_init: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yapp_init: dispatch_lookup failed");
    return ((yetty_yapp_init_fn)dispatch_impl_r.value)(app, platform);
}

struct yetty_ycore_void_result yetty_yapp_run(struct yetty_yclass_object * app, struct yetty_yclass_object * platform)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yapp", (yetty_yclass_method_id_t)yetty_yapp_run);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yapp_run: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!app) return YETTY_ERR(yetty_ycore_void, "yetty_yapp_run: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yapp_run: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yapp_run: dispatch_lookup failed");
    return ((yetty_yapp_run_fn)dispatch_impl_r.value)(app, platform);
}

struct yetty_ycore_void_result yetty_yapp_quit(struct yetty_yclass_object * app)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yapp", (yetty_yclass_method_id_t)yetty_yapp_quit);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yapp_quit: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!app) return YETTY_ERR(yetty_ycore_void, "yetty_yapp_quit: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yapp_quit: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yapp_quit: dispatch_lookup failed");
    return ((yetty_yapp_quit_fn)dispatch_impl_r.value)(app);
}

