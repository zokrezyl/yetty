/* GENERATED — do not edit. */
#include <yetty/api/yplot/plot.h>

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
struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object *obj,
                                                        const char *name);
struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object *obj,
                                                         const char *color);
struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object *obj,
                                                        const char *body);
struct yetty_ycore_void_result yetty_api_yplot_set_values(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_buffer samples);
struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object *obj,
                                                              const char *source);
struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *function);
struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object *obj,
                                                         const char *title);
struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object *obj,
                                                        float width, float height);
struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object *obj,
                                                           float min, float max);
struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object *obj,
                                                           float min, float max);
struct yetty_ycore_void_result yetty_api_yplot_add_buffer(struct yetty_yclass_object *obj,
                                                          struct yetty_yclass_object *buffer);
struct yetty_ycore_void_result yetty_api_yplot_set_view(struct yetty_yclass_object *obj,
                                                        float x_min, float x_max, float y_min,
                                                        float y_max);
struct yetty_ycore_void_result yetty_api_yplot_set_nogrid(struct yetty_yclass_object *obj,
                                                          uint32_t disabled);
struct yetty_ycore_void_result yetty_api_yplot_set_noaxes(struct yetty_yclass_object *obj,
                                                          uint32_t disabled);
struct yetty_ycore_void_result yetty_api_yplot_set_nolabels(struct yetty_yclass_object *obj,
                                                            uint32_t disabled);
struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_name_fn)(struct yetty_yclass_object *,
                                                                      const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_color_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_body_fn)(struct yetty_yclass_object *,
                                                                      const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_values_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_expression_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_add_function_fn)(
    struct yetty_yclass_object *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_title_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_x_label_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_y_label_fn)(
    struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_size_fn)(struct yetty_yclass_object *,
                                                                      float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_x_range_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_y_range_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_add_buffer_fn)(
    struct yetty_yclass_object *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_view_fn)(struct yetty_yclass_object *,
                                                                      float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_nogrid_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_noaxes_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_nolabels_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_show_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_destroy_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object *obj,
                                                        const char *name)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_name);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_name: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_name: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_name: dispatch_lookup failed");
    return ((yetty_api_yplot_set_name_fn)dispatch_impl_r.value)(obj, name);
}

struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object *obj,
                                                         const char *color)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_color);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_color: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_color: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_color: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_color: dispatch_lookup failed");
    return ((yetty_api_yplot_set_color_fn)dispatch_impl_r.value)(obj, color);
}

struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object *obj,
                                                        const char *body)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_body);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_body: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_body: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_body: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_body: dispatch_lookup failed");
    return ((yetty_api_yplot_set_body_fn)dispatch_impl_r.value)(obj, body);
}

struct yetty_ycore_void_result yetty_api_yplot_set_values(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_buffer samples)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_values);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_values: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_values: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_values: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_values: dispatch_lookup failed");
    return ((yetty_api_yplot_set_values_fn)dispatch_impl_r.value)(obj, samples);
}

struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object *obj,
                                                              const char *source)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_expression);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_set_expression: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_expression: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_expression: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_expression: dispatch_lookup failed");
    return ((yetty_api_yplot_set_expression_fn)dispatch_impl_r.value)(obj, source);
}

struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *function)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_add_function);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_add_function: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_add_function: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_add_function: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_add_function: dispatch_lookup failed");
    return ((yetty_api_yplot_add_function_fn)dispatch_impl_r.value)(obj, function);
}

struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object *obj,
                                                         const char *title)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_title);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_title: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_title: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_title: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_title: dispatch_lookup failed");
    return ((yetty_api_yplot_set_title_fn)dispatch_impl_r.value)(obj, title);
}

struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object *obj,
                                                           const char *label)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_label);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_set_x_label: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_x_label: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_x_label: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_x_label: dispatch_lookup failed");
    return ((yetty_api_yplot_set_x_label_fn)dispatch_impl_r.value)(obj, label);
}

struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object *obj,
                                                           const char *label)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_label);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_set_y_label: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_y_label: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_y_label: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_y_label: dispatch_lookup failed");
    return ((yetty_api_yplot_set_y_label_fn)dispatch_impl_r.value)(obj, label);
}

struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object *obj,
                                                        float width, float height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_size);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_size: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_size: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_size: dispatch_lookup failed");
    return ((yetty_api_yplot_set_size_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object *obj,
                                                           float min, float max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_range);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_set_x_range: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_x_range: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_x_range: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_x_range: dispatch_lookup failed");
    return ((yetty_api_yplot_set_x_range_fn)dispatch_impl_r.value)(obj, min, max);
}

struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object *obj,
                                                           float min, float max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_range);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_set_y_range: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_y_range: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_y_range: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_y_range: dispatch_lookup failed");
    return ((yetty_api_yplot_set_y_range_fn)dispatch_impl_r.value)(obj, min, max);
}

struct yetty_ycore_void_result yetty_api_yplot_add_buffer(struct yetty_yclass_object *obj,
                                                          struct yetty_yclass_object *buffer)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_add_buffer);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_add_buffer: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_add_buffer: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_add_buffer: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_add_buffer: dispatch_lookup failed");
    return ((yetty_api_yplot_add_buffer_fn)dispatch_impl_r.value)(obj, buffer);
}

struct yetty_ycore_void_result yetty_api_yplot_set_view(struct yetty_yclass_object *obj,
                                                        float x_min, float x_max, float y_min,
                                                        float y_max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_view);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_view: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_view: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_view: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_view: dispatch_lookup failed");
    return ((yetty_api_yplot_set_view_fn)dispatch_impl_r.value)(obj, x_min, x_max, y_min, y_max);
}

struct yetty_ycore_void_result yetty_api_yplot_set_nogrid(struct yetty_yclass_object *obj,
                                                          uint32_t disabled)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_nogrid);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_nogrid: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_nogrid: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_nogrid: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_nogrid: dispatch_lookup failed");
    return ((yetty_api_yplot_set_nogrid_fn)dispatch_impl_r.value)(obj, disabled);
}

struct yetty_ycore_void_result yetty_api_yplot_set_noaxes(struct yetty_yclass_object *obj,
                                                          uint32_t disabled)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_noaxes);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_noaxes: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_noaxes: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_noaxes: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_noaxes: dispatch_lookup failed");
    return ((yetty_api_yplot_set_noaxes_fn)dispatch_impl_r.value)(obj, disabled);
}

struct yetty_ycore_void_result yetty_api_yplot_set_nolabels(struct yetty_yclass_object *obj,
                                                            uint32_t disabled)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_nolabels);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_api_yplot_set_nolabels: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_nolabels: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_set_nolabels: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_set_nolabels: dispatch_lookup failed");
    return ((yetty_api_yplot_set_nolabels_fn)dispatch_impl_r.value)(obj, disabled);
}

struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_show);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_show: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_show: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_show: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_show: dispatch_lookup failed");
    return ((yetty_api_yplot_show_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_api_yplot_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_api_yplot_destroy: dispatch_lookup failed");
    return ((yetty_api_yplot_destroy_fn)dispatch_impl_r.value)(obj);
}
