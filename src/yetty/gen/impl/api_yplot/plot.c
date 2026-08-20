/* GENERATED — do not edit. */
#include "yetty/gen/impl/api_yplot/plot.h"
#include "yetty/gen/impl/ydrawlist2/drawable.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

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

YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_name_fn
    yetty_api_yplot_curve_yetty_api_yplot_set_name_curve_set_name_check = curve_set_name;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_color_fn
    yetty_api_yplot_curve_yetty_api_yplot_set_color_curve_set_color_check = curve_set_color;

struct yetty_yclass_ptr_result yetty_api_yplot_curve_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_api_yplot_curve");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_api_yplot_curve",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_api_yplot_curve),
        .data_align = _Alignof(struct yetty_api_yplot_curve),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_api_yplot", "set_name", (yetty_yclass_method_id_t)yetty_api_yplot_set_name,
         (yetty_yclass_impl_t)curve_set_name},
        {"yetty_api_yplot", "set_color", (yetty_yclass_method_id_t)yetty_api_yplot_set_color,
         (yetty_yclass_impl_t)curve_set_color},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_api_yplot_curve_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_api_yplot_curve_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_api_yplot_curve_ptr_result yetty_api_yplot_curve_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_curve_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_api_yplot_curve_ptr, "yetty_api_yplot_curve_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_api_yplot_curve_ptr, "yetty_api_yplot_curve_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_api_yplot_curve_ptr, (struct yetty_api_yplot_curve *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_to(struct yetty_api_yplot_curve *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_curve_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_api_yplot_curve_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_api_yplot_curve_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_body_fn
    yetty_api_yplot_function_yetty_api_yplot_set_body_function_set_body_check = function_set_body;

struct yetty_yclass_ptr_result yetty_api_yplot_function_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_api_yplot_function");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_api_yplot_function",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_api_yplot_function),
        .data_align = _Alignof(struct yetty_api_yplot_function),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_api_yplot", "set_body", (yetty_yclass_method_id_t)yetty_api_yplot_set_body,
         (yetty_yclass_impl_t)function_set_body},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_api_yplot_curve_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_api_yplot_function_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_api_yplot_function_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_api_yplot_function_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_api_yplot_function_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_api_yplot_function_ptr_result yetty_api_yplot_function_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_function_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_api_yplot_function_ptr,
                         "yetty_api_yplot_function_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_api_yplot_function_ptr, "yetty_api_yplot_function_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_api_yplot_function_ptr, (struct yetty_api_yplot_function *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_function_to(
    struct yetty_api_yplot_function *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_function_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_api_yplot_function_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_api_yplot_function_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_values_fn
    yetty_api_yplot_buffer_yetty_api_yplot_set_values_buffer_set_values_check = buffer_set_values;

struct yetty_yclass_ptr_result yetty_api_yplot_buffer_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_api_yplot_buffer");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_api_yplot_buffer",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_api_yplot_buffer),
        .data_align = _Alignof(struct yetty_api_yplot_buffer),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_api_yplot", "set_values", (yetty_yclass_method_id_t)yetty_api_yplot_set_values,
         (yetty_yclass_impl_t)buffer_set_values},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_api_yplot_curve_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_api_yplot_buffer_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_api_yplot_buffer_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_api_yplot_buffer_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_api_yplot_buffer_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_api_yplot_buffer_ptr_result yetty_api_yplot_buffer_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_buffer_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_api_yplot_buffer_ptr, "yetty_api_yplot_buffer_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_api_yplot_buffer_ptr, "yetty_api_yplot_buffer_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_api_yplot_buffer_ptr, (struct yetty_api_yplot_buffer *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_to(struct yetty_api_yplot_buffer *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_buffer_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_api_yplot_buffer_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_api_yplot_buffer_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct uint32_result yetty_api_yplot_buffer_size_get(struct yetty_yclass_object *obj)
{
    struct yetty_api_yplot_buffer_ptr_result data = yetty_api_yplot_buffer_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_api_yplot_buffer_size_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->size);
}

struct yetty_ycore_void_result yetty_api_yplot_buffer_size_set(struct yetty_yclass_object *obj,
                                                               uint32_t value)
{
    struct yetty_api_yplot_buffer_ptr_result data = yetty_api_yplot_buffer_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_buffer_size_set: data block", data);
    }
    data.value->size = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_expression_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_expression_plot_set_expression_check =
        plot_set_expression;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_add_function_fn
    yetty_api_yplot_plot_yetty_api_yplot_add_function_plot_add_function_check = plot_add_function;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_title_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_title_plot_set_title_check = plot_set_title;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_x_label_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_x_label_plot_set_x_label_check = plot_set_x_label;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_y_label_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_y_label_plot_set_y_label_check = plot_set_y_label;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_size_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_size_plot_set_size_check = plot_set_size;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_x_range_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_x_range_plot_set_x_range_check = plot_set_x_range;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_y_range_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_y_range_plot_set_y_range_check = plot_set_y_range;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_add_buffer_fn
    yetty_api_yplot_plot_yetty_api_yplot_add_buffer_plot_add_buffer_check = plot_add_buffer;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_view_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_view_plot_set_view_check = plot_set_view;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_nogrid_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_nogrid_plot_set_nogrid_check = plot_set_nogrid;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_noaxes_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_noaxes_plot_set_noaxes_check = plot_set_noaxes;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_set_nolabels_fn
    yetty_api_yplot_plot_yetty_api_yplot_set_nolabels_plot_set_nolabels_check = plot_set_nolabels;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_api_yplot_plot_yetty_ydrawlist2_pack_plot_pack_check =
    plot_pack;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_show_fn yetty_api_yplot_plot_yetty_api_yplot_show_plot_show_check =
    plot_show;
YETTY_MAYBE_UNUSED
static yetty_api_yplot_destroy_fn yetty_api_yplot_plot_yetty_api_yplot_destroy_plot_destroy_check =
    plot_destroy;

struct yetty_yclass_ptr_result yetty_api_yplot_plot_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_api_yplot_plot");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_api_yplot_plot",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_api_yplot_plot),
        .data_align = _Alignof(struct yetty_api_yplot_plot),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_api_yplot", "set_expression",
         (yetty_yclass_method_id_t)yetty_api_yplot_set_expression,
         (yetty_yclass_impl_t)plot_set_expression},
        {"yetty_api_yplot", "add_function", (yetty_yclass_method_id_t)yetty_api_yplot_add_function,
         (yetty_yclass_impl_t)plot_add_function},
        {"yetty_api_yplot", "set_title", (yetty_yclass_method_id_t)yetty_api_yplot_set_title,
         (yetty_yclass_impl_t)plot_set_title},
        {"yetty_api_yplot", "set_x_label", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_label,
         (yetty_yclass_impl_t)plot_set_x_label},
        {"yetty_api_yplot", "set_y_label", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_label,
         (yetty_yclass_impl_t)plot_set_y_label},
        {"yetty_api_yplot", "set_size", (yetty_yclass_method_id_t)yetty_api_yplot_set_size,
         (yetty_yclass_impl_t)plot_set_size},
        {"yetty_api_yplot", "set_x_range", (yetty_yclass_method_id_t)yetty_api_yplot_set_x_range,
         (yetty_yclass_impl_t)plot_set_x_range},
        {"yetty_api_yplot", "set_y_range", (yetty_yclass_method_id_t)yetty_api_yplot_set_y_range,
         (yetty_yclass_impl_t)plot_set_y_range},
        {"yetty_api_yplot", "add_buffer", (yetty_yclass_method_id_t)yetty_api_yplot_add_buffer,
         (yetty_yclass_impl_t)plot_add_buffer},
        {"yetty_api_yplot", "set_view", (yetty_yclass_method_id_t)yetty_api_yplot_set_view,
         (yetty_yclass_impl_t)plot_set_view},
        {"yetty_api_yplot", "set_nogrid", (yetty_yclass_method_id_t)yetty_api_yplot_set_nogrid,
         (yetty_yclass_impl_t)plot_set_nogrid},
        {"yetty_api_yplot", "set_noaxes", (yetty_yclass_method_id_t)yetty_api_yplot_set_noaxes,
         (yetty_yclass_impl_t)plot_set_noaxes},
        {"yetty_api_yplot", "set_nolabels", (yetty_yclass_method_id_t)yetty_api_yplot_set_nolabels,
         (yetty_yclass_impl_t)plot_set_nolabels},
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)plot_pack},
        {"yetty_api_yplot", "show", (yetty_yclass_method_id_t)yetty_api_yplot_show,
         (yetty_yclass_impl_t)plot_show},
        {"yetty_api_yplot", "destroy", (yetty_yclass_method_id_t)yetty_api_yplot_destroy,
         (yetty_yclass_impl_t)plot_destroy},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_api_yplot_plot_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_api_yplot_plot_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_api_yplot_plot_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_api_yplot_plot_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_api_yplot_plot_ptr_result yetty_api_yplot_plot_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_plot_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_api_yplot_plot_ptr, "yetty_api_yplot_plot_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_api_yplot_plot_ptr, "yetty_api_yplot_plot_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_api_yplot_plot_ptr, (struct yetty_api_yplot_plot *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_to(struct yetty_api_yplot_plot *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_plot_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_api_yplot_plot_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_api_yplot_plot_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_api_yplot_plot_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_api_yplot_plot_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->x);
}

struct yetty_ycore_void_result yetty_api_yplot_plot_x_set(struct yetty_yclass_object *obj,
                                                          float value)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_plot_x_set: data block", data);
    }
    data.value->x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_api_yplot_plot_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_api_yplot_plot_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->y);
}

struct yetty_ycore_void_result yetty_api_yplot_plot_y_set(struct yetty_yclass_object *obj,
                                                          float value)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_plot_y_set: data block", data);
    }
    data.value->y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_api_yplot_plot_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_api_yplot_plot_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->width);
}

struct yetty_ycore_void_result yetty_api_yplot_plot_width_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_plot_width_set: data block", data);
    }
    data.value->width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_api_yplot_plot_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_api_yplot_plot_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->height);
}

struct yetty_ycore_void_result yetty_api_yplot_plot_height_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_api_yplot_plot_ptr_result data = yetty_api_yplot_plot_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_api_yplot_plot_height_set: data block", data);
    }
    data.value->height = value;
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_api_yplot_curve");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_api_yplot_curve_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_api_yplot_curve_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_curve_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_api_yplot_function");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_api_yplot_function_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_api_yplot_function_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_function_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_api_yplot_buffer");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_api_yplot_buffer_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_api_yplot_buffer_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_buffer_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_api_yplot_plot");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_api_yplot_plot_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_api_yplot_plot_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_api_yplot_plot_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_api_yplot_curve_class_get(void);
struct yetty_yclass_ptr_result yetty_api_yplot_function_class_get(void);
struct yetty_yclass_ptr_result yetty_api_yplot_buffer_class_get(void);
struct yetty_yclass_ptr_result yetty_api_yplot_plot_class_get(void);
struct yetty_ycore_void_result yetty_api_yplot_register(void);

/* ---- api_yplot: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_api_yplot_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_api_yplot_curve") == 0) {
        return yetty_api_yplot_curve_class_get();
    }
    if (strcmp(name, "yetty_api_yplot_function") == 0) {
        return yetty_api_yplot_function_class_get();
    }
    if (strcmp(name, "yetty_api_yplot_buffer") == 0) {
        return yetty_api_yplot_buffer_class_get();
    }
    if (strcmp(name, "yetty_api_yplot_plot") == 0) {
        return yetty_api_yplot_plot_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- api_yplot: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_api_yplot_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_api_yplot_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_api_yplot_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
