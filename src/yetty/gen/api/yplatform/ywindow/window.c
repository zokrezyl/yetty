/* GENERATED — do not edit. */
#include <yetty/api/yplatform/ywindow/window.h>

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

struct yetty_yclass_void_ptr_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object * obj, int width, int height, const char * title);
struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object * obj);
struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(struct yetty_yclass_object * obj, void * instance);
struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object * obj, int * width, int * height);
struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(struct yetty_yclass_object * obj, int * width, int * height);
struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(struct yetty_yclass_object * obj, float * xscale, float * yscale);
struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object * obj, const char * title);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_open_fn)(struct yetty_yclass_object *, int, int, const char *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_yclass_void_ptr_result (*yetty_yplatform_window_create_surface_fn)(struct yetty_yclass_object *, void *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_size_fn)(struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_framebuffer_size_fn)(struct yetty_yclass_object *, int *, int *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_get_content_scale_fn)(struct yetty_yclass_object *, float *, float *);
typedef struct yetty_ycore_int_result (*yetty_yplatform_window_should_close_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_set_title_fn)(struct yetty_yclass_object *, const char *);

struct yetty_ycore_void_result yetty_yplatform_window_open(struct yetty_yclass_object * obj, int width, int height, const char * title)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_open);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_open: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_open: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_open: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_open: dispatch_lookup failed");
    return ((yetty_yplatform_window_open_fn)dispatch_impl_r.value)(obj, width, height, title);
}

struct yetty_ycore_void_result yetty_yplatform_window_get_size(struct yetty_yclass_object * obj, int * width, int * height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_get_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_get_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_get_size: dispatch_lookup failed");
    return ((yetty_yplatform_window_get_size_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_yplatform_window_get_framebuffer_size(struct yetty_yclass_object * obj, int * width, int * height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_get_framebuffer_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_framebuffer_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_framebuffer_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_get_framebuffer_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_get_framebuffer_size: dispatch_lookup failed");
    return ((yetty_yplatform_window_get_framebuffer_size_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_yplatform_window_get_content_scale(struct yetty_yclass_object * obj, float * xscale, float * yscale)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_get_content_scale);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_content_scale: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_get_content_scale: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_get_content_scale: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_get_content_scale: dispatch_lookup failed");
    return ((yetty_yplatform_window_get_content_scale_fn)dispatch_impl_r.value)(obj, xscale, yscale);
}

struct yetty_ycore_int_result yetty_yplatform_window_should_close(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_should_close);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_yplatform_window_should_close: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_yplatform_window_should_close: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_yplatform_window_should_close: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_yplatform_window_should_close: dispatch_lookup failed");
    return ((yetty_yplatform_window_should_close_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_yplatform_window_set_title(struct yetty_yclass_object * obj, const char * title)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_set_title);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_set_title: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_set_title: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_set_title: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_set_title: dispatch_lookup failed");
    return ((yetty_yplatform_window_set_title_fn)dispatch_impl_r.value)(obj, title);
}

struct yetty_ycore_void_result yetty_yplatform_window_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yplatform_window_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yplatform_window_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yplatform_window_destroy: dispatch_lookup failed");
    return ((yetty_yplatform_window_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_void_ptr_result yetty_yplatform_window_create_surface(struct yetty_yclass_object * obj, void * instance)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yplatform", (yetty_yclass_method_id_t)yetty_yplatform_window_create_surface);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_yclass_void_ptr, "yetty_yplatform_window_create_surface: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_yclass_void_ptr, "yetty_yplatform_window_create_surface: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, object_class_r, "yetty_yplatform_window_create_surface: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, dispatch_impl_r, "yetty_yplatform_window_create_surface: dispatch_lookup failed");
    return ((yetty_yplatform_window_create_surface_fn)dispatch_impl_r.value)(obj, instance);
}

