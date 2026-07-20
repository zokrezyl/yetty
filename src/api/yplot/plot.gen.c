/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object * obj, const char * source);
struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object * obj, struct yetty_yclass_object * function);
struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object * obj, const char * title);
struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object * obj, const char * label);
struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object * obj, const char * label);
struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object * obj, float width, float height);
struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object * obj, float min, float max);
struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object * obj, float min, float max);
struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object * obj, const char * body);
struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object * obj, const char * name);
struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object * obj, const char * color);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_expression_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_add_function_fn)(struct yetty_yclass_object *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_title_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_x_label_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_y_label_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_size_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_x_range_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_y_range_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_show_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_body_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_name_fn)(struct yetty_yclass_object *, const char *);
typedef struct yetty_ycore_void_result (*yetty_api_yplot_set_color_fn)(struct yetty_yclass_object *, const char *);

YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_expression_fn yetty_api_yplot_plot_yetty_api_yplot_set_expression_check = plot_set_expression;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_add_function_fn yetty_api_yplot_plot_yetty_api_yplot_add_function_check = plot_add_function;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_title_fn yetty_api_yplot_plot_yetty_api_yplot_set_title_check = plot_set_title;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_x_label_fn yetty_api_yplot_plot_yetty_api_yplot_set_x_label_check = plot_set_x_label;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_y_label_fn yetty_api_yplot_plot_yetty_api_yplot_set_y_label_check = plot_set_y_label;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_size_fn yetty_api_yplot_plot_yetty_api_yplot_set_size_check = plot_set_size;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_x_range_fn yetty_api_yplot_plot_yetty_api_yplot_set_x_range_check = plot_set_x_range;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_y_range_fn yetty_api_yplot_plot_yetty_api_yplot_set_y_range_check = plot_set_y_range;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_show_fn yetty_api_yplot_plot_yetty_api_yplot_show_check = plot_show;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_destroy_fn yetty_api_yplot_plot_yetty_api_yplot_destroy_check = plot_destroy;

struct yetty_yclass_ptr_result yetty_api_yplot_plot_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_api_yplot_plot");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_api_yplot_plot",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_api_yplot_plot),
        .data_align = _Alignof(struct yetty_api_yplot_plot),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_api_yplot", "set_expression", (yetty_yclass_method_id_t)yetty_api_yplot_set_expression, (yetty_yclass_impl_t)plot_set_expression},
        {"yetty_api_yplot", "add_function", (yetty_yclass_method_id_t)yetty_api_yplot_add_function, (yetty_yclass_impl_t)plot_add_function},
        {"yetty_api_yplot", "set_title", (yetty_yclass_method_id_t)yetty_api_yplot_set_title, (yetty_yclass_impl_t)plot_set_title},
        {"yetty_api_yplot", "set_x_label", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_label, (yetty_yclass_impl_t)plot_set_x_label},
        {"yetty_api_yplot", "set_y_label", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_label, (yetty_yclass_impl_t)plot_set_y_label},
        {"yetty_api_yplot", "set_size", (yetty_yclass_method_id_t)yetty_api_yplot_set_size, (yetty_yclass_impl_t)plot_set_size},
        {"yetty_api_yplot", "set_x_range", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_range, (yetty_yclass_impl_t)plot_set_x_range},
        {"yetty_api_yplot", "set_y_range", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_range, (yetty_yclass_impl_t)plot_set_y_range},
        {"yetty_api_yplot", "show", (yetty_yclass_method_id_t)yetty_api_yplot_show, (yetty_yclass_impl_t)plot_show},
        {"yetty_api_yplot", "destroy", (yetty_yclass_method_id_t)yetty_api_yplot_destroy, (yetty_yclass_impl_t)plot_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_api_yplot_plot_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_api_yplot_plot_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_api_yplot_plot_ptr_result yetty_api_yplot_plot_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_plot_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_api_yplot_plot_ptr, "yetty_api_yplot_plot_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_api_yplot_plot_ptr, "yetty_api_yplot_plot_from: object_data", slice_r);
    return YETTY_OK(yetty_api_yplot_plot_ptr, (struct yetty_api_yplot_plot *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_to(struct yetty_api_yplot_plot *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_plot_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_api_yplot_plot_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_api_yplot_plot_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_body_fn yetty_api_yplot_function_yetty_api_yplot_set_body_check = function_set_body;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_name_fn yetty_api_yplot_function_yetty_api_yplot_set_name_check = function_set_name;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_color_fn yetty_api_yplot_function_yetty_api_yplot_set_color_check = function_set_color;

struct yetty_yclass_ptr_result yetty_api_yplot_function_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_api_yplot_function");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_api_yplot_function",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_api_yplot_function),
        .data_align = _Alignof(struct yetty_api_yplot_function),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_api_yplot", "set_body", (yetty_yclass_method_id_t)yetty_api_yplot_set_body, (yetty_yclass_impl_t)function_set_body},
        {"yetty_api_yplot", "set_name", (yetty_yclass_method_id_t)yetty_api_yplot_set_name, (yetty_yclass_impl_t)function_set_name},
        {"yetty_api_yplot", "set_color", (yetty_yclass_method_id_t)yetty_api_yplot_set_color, (yetty_yclass_impl_t)function_set_color},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_api_yplot_function_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_api_yplot_function_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_api_yplot_function_ptr_result yetty_api_yplot_function_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_function_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_api_yplot_function_ptr, "yetty_api_yplot_function_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_api_yplot_function_ptr, "yetty_api_yplot_function_from: object_data", slice_r);
    return YETTY_OK(yetty_api_yplot_function_ptr, (struct yetty_api_yplot_function *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_function_to(struct yetty_api_yplot_function *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_function_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_api_yplot_function_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_api_yplot_function_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object * obj, const char * source)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_expression);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_expression: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_expression: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_expression: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_expression: dispatch_lookup failed");
    return ((yetty_api_yplot_set_expression_fn)dispatch_impl_r.value)(obj, source);
}

struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object * obj, struct yetty_yclass_object * function)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_add_function);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_add_function: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_add_function: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_add_function: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_add_function: dispatch_lookup failed");
    return ((yetty_api_yplot_add_function_fn)dispatch_impl_r.value)(obj, function);
}

struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object * obj, const char * title)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_title);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_title: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_title: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_title: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_title: dispatch_lookup failed");
    return ((yetty_api_yplot_set_title_fn)dispatch_impl_r.value)(obj, title);
}

struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object * obj, const char * label)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_label);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_x_label: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_x_label: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_x_label: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_x_label: dispatch_lookup failed");
    return ((yetty_api_yplot_set_x_label_fn)dispatch_impl_r.value)(obj, label);
}

struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object * obj, const char * label)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_label);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_y_label: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_y_label: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_y_label: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_y_label: dispatch_lookup failed");
    return ((yetty_api_yplot_set_y_label_fn)dispatch_impl_r.value)(obj, label);
}

struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object * obj, float width, float height)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_size: dispatch_lookup failed");
    return ((yetty_api_yplot_set_size_fn)dispatch_impl_r.value)(obj, width, height);
}

struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object * obj, float min, float max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_range);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_x_range: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_x_range: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_x_range: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_x_range: dispatch_lookup failed");
    return ((yetty_api_yplot_set_x_range_fn)dispatch_impl_r.value)(obj, min, max);
}

struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object * obj, float min, float max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_range);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_y_range: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_y_range: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_y_range: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_y_range: dispatch_lookup failed");
    return ((yetty_api_yplot_set_y_range_fn)dispatch_impl_r.value)(obj, min, max);
}

struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_show);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_show: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_show: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_show: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_show: dispatch_lookup failed");
    return ((yetty_api_yplot_show_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_destroy: dispatch_lookup failed");
    return ((yetty_api_yplot_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object * obj, const char * body)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_body);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_body: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_body: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_body: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_body: dispatch_lookup failed");
    return ((yetty_api_yplot_set_body_fn)dispatch_impl_r.value)(obj, body);
}

struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object * obj, const char * name)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_name);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_name: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_name: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_name: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_name: dispatch_lookup failed");
    return ((yetty_api_yplot_set_name_fn)dispatch_impl_r.value)(obj, name);
}

struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object * obj, const char * color)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_api_yplot", (yetty_yclass_method_id_t)yetty_api_yplot_set_color);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_color: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_set_color: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_api_yplot_set_color: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_api_yplot_set_color: dispatch_lookup failed");
    return ((yetty_api_yplot_set_color_fn)dispatch_impl_r.value)(obj, color);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_api_yplot_plot");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_api_yplot_plot_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_plot_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_api_yplot_plot");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_api_yplot_plot_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_api_yplot_plot";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_plot_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_plot_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_api_yplot_plot_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_api_yplot_function");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_api_yplot_function_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_function_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_api_yplot_function");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_api_yplot_function_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_api_yplot_function";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_function_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_function_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_api_yplot_function_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

