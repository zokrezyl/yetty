/* GENERATED — do not edit. */
#include "yetty/gen/impl/ydrawlist2/drawable.h"
#include "yetty/gen/impl/ydrawlist2/shape.h"
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

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_circle_yetty_ydrawlist2_pack_ysdf2_circle_pack_check =
    ysdf2_circle_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_circle_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_circle");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_circle",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_circle),
        .data_align = _Alignof(struct yetty_ysdf2_circle),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_circle_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_circle_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_circle_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_circle_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_circle_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_circle_ptr_result yetty_ysdf2_circle_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_circle_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_circle_ptr, "yetty_ysdf2_circle_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_circle_ptr, "yetty_ysdf2_circle_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_circle_ptr, (struct yetty_ysdf2_circle *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_to(struct yetty_ysdf2_circle *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_circle_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_circle_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_circle_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_circle_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_circle_ptr_result data = yetty_ysdf2_circle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_circle_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_circle_center_x_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_circle_ptr_result data = yetty_ysdf2_circle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_circle_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_circle_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_circle_ptr_result data = yetty_ysdf2_circle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_circle_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_circle_center_y_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_circle_ptr_result data = yetty_ysdf2_circle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_circle_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_circle_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_circle_ptr_result data = yetty_ysdf2_circle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_circle_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_circle_radius_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_circle_ptr_result data = yetty_ysdf2_circle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_circle_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_box_yetty_ydrawlist2_pack_ysdf2_box_pack_check =
    ysdf2_box_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_box_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_box");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_box",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_box),
        .data_align = _Alignof(struct yetty_ysdf2_box),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_box_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_box_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_box_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_box_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_box_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_box_ptr_result yetty_ysdf2_box_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_box_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_box_ptr, "yetty_ysdf2_box_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_box_ptr, "yetty_ysdf2_box_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_box_ptr, (struct yetty_ysdf2_box *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_box_to(struct yetty_ysdf2_box *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_box_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_box_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_box_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_box_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_box_center_x_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_box_center_y_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_half_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_half_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_width);
}

struct yetty_ycore_void_result yetty_ysdf2_box_half_width_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_half_width_set: data block", data);
    }
    data.value->half_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_half_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_box_half_height_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_half_height_set: data block", data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_corner_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_corner_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->corner_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_box_corner_radius_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_box_ptr_result data = yetty_ysdf2_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_corner_radius_set: data block", data);
    }
    data.value->corner_radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_segment_yetty_ydrawlist2_pack_ysdf2_segment_pack_check =
    ysdf2_segment_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_segment_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_segment");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_segment",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_segment),
        .data_align = _Alignof(struct yetty_ysdf2_segment),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_segment_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_segment_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_segment_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_segment_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_segment_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_segment_ptr_result yetty_ysdf2_segment_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_segment_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_segment_ptr, "yetty_ysdf2_segment_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_segment_ptr, "yetty_ysdf2_segment_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_segment_ptr, (struct yetty_ysdf2_segment *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_to(struct yetty_ysdf2_segment *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_segment_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_segment_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_segment_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_segment_start_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_segment_start_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->start_x);
}

struct yetty_ycore_void_result yetty_ysdf2_segment_start_x_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_segment_start_x_set: data block", data);
    }
    data.value->start_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_segment_start_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_segment_start_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->start_y);
}

struct yetty_ycore_void_result yetty_ysdf2_segment_start_y_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_segment_start_y_set: data block", data);
    }
    data.value->start_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_segment_end_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_segment_end_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->end_x);
}

struct yetty_ycore_void_result yetty_ysdf2_segment_end_x_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_segment_end_x_set: data block", data);
    }
    data.value->end_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_segment_end_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_segment_end_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->end_y);
}

struct yetty_ycore_void_result yetty_ysdf2_segment_end_y_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_segment_ptr_result data = yetty_ysdf2_segment_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_segment_end_y_set: data block", data);
    }
    data.value->end_y = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_triangle_yetty_ydrawlist2_pack_ysdf2_triangle_pack_check = ysdf2_triangle_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_triangle_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_triangle");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_triangle",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_triangle),
        .data_align = _Alignof(struct yetty_ysdf2_triangle),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_triangle_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_triangle_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_triangle_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_triangle_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_triangle_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_triangle_ptr_result yetty_ysdf2_triangle_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_triangle_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_triangle_ptr, "yetty_ysdf2_triangle_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_triangle_ptr, "yetty_ysdf2_triangle_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_triangle_ptr, (struct yetty_ysdf2_triangle *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_to(struct yetty_ysdf2_triangle *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_triangle_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_triangle_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_triangle_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_triangle_vertex_a_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_triangle_vertex_a_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->vertex_a_x);
}

struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_x_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_triangle_vertex_a_x_set: data block", data);
    }
    data.value->vertex_a_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_triangle_vertex_a_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_triangle_vertex_a_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->vertex_a_y);
}

struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_a_y_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_triangle_vertex_a_y_set: data block", data);
    }
    data.value->vertex_a_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_triangle_vertex_b_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_triangle_vertex_b_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->vertex_b_x);
}

struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_x_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_triangle_vertex_b_x_set: data block", data);
    }
    data.value->vertex_b_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_triangle_vertex_b_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_triangle_vertex_b_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->vertex_b_y);
}

struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_b_y_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_triangle_vertex_b_y_set: data block", data);
    }
    data.value->vertex_b_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_triangle_vertex_c_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_triangle_vertex_c_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->vertex_c_x);
}

struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_x_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_triangle_vertex_c_x_set: data block", data);
    }
    data.value->vertex_c_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_triangle_vertex_c_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_triangle_vertex_c_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->vertex_c_y);
}

struct yetty_ycore_void_result yetty_ysdf2_triangle_vertex_c_y_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_triangle_ptr_result data = yetty_ysdf2_triangle_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_triangle_vertex_c_y_set: data block", data);
    }
    data.value->vertex_c_y = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_ellipse_yetty_ydrawlist2_pack_ysdf2_ellipse_pack_check =
    ysdf2_ellipse_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_ellipse_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_ellipse");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_ellipse",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_ellipse),
        .data_align = _Alignof(struct yetty_ysdf2_ellipse),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_ellipse_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_ellipse_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_ellipse_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_ellipse_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_ellipse_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_ellipse_ptr_result yetty_ysdf2_ellipse_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_ellipse_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_ellipse_ptr, "yetty_ysdf2_ellipse_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_ellipse_ptr, "yetty_ysdf2_ellipse_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_ellipse_ptr, (struct yetty_ysdf2_ellipse *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_to(struct yetty_ysdf2_ellipse *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_ellipse_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_ellipse_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_ellipse_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_ellipse_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ellipse_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_x_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ellipse_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ellipse_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ellipse_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_ellipse_center_y_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ellipse_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ellipse_radius_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ellipse_radius_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_x);
}

struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_x_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ellipse_radius_x_set: data block", data);
    }
    data.value->radius_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ellipse_radius_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ellipse_radius_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_y);
}

struct yetty_ycore_void_result yetty_ysdf2_ellipse_radius_y_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_ellipse_ptr_result data = yetty_ysdf2_ellipse_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ellipse_radius_y_set: data block", data);
    }
    data.value->radius_y = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_arc_yetty_ydrawlist2_pack_ysdf2_arc_pack_check =
    ysdf2_arc_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_arc_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_arc");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_arc",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_arc),
        .data_align = _Alignof(struct yetty_ysdf2_arc),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_arc_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_arc_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_arc_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_arc_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_arc_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_arc_ptr_result yetty_ysdf2_arc_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_arc_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_arc_ptr, "yetty_ysdf2_arc_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_arc_ptr, "yetty_ysdf2_arc_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_arc_ptr, (struct yetty_ysdf2_arc *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_to(struct yetty_ysdf2_arc *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_arc_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_arc_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_arc_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_arc_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_arc_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_arc_center_x_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_arc_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_arc_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_arc_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_arc_center_y_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_arc_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_arc_aperture_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_arc_aperture_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->aperture_x);
}

struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_x_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_arc_aperture_x_set: data block", data);
    }
    data.value->aperture_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_arc_aperture_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_arc_aperture_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->aperture_y);
}

struct yetty_ycore_void_result yetty_ysdf2_arc_aperture_y_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_arc_aperture_y_set: data block", data);
    }
    data.value->aperture_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_arc_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_arc_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_arc_radius_set(struct yetty_yclass_object *obj,
                                                          float value)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_arc_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_arc_thickness_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_arc_thickness_get: data block", data);
    }
    return YETTY_OK(float, data.value->thickness);
}

struct yetty_ycore_void_result yetty_ysdf2_arc_thickness_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_arc_ptr_result data = yetty_ysdf2_arc_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_arc_thickness_set: data block", data);
    }
    data.value->thickness = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_rounded_box_yetty_ydrawlist2_pack_ysdf2_rounded_box_pack_check =
        ysdf2_rounded_box_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_rounded_box_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_rounded_box");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_rounded_box",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_rounded_box),
        .data_align = _Alignof(struct yetty_ysdf2_rounded_box),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_rounded_box_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_rounded_box_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_rounded_box_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_rounded_box_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_rounded_box_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_rounded_box_ptr_result yetty_ysdf2_rounded_box_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_rounded_box_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_rounded_box_ptr,
                         "yetty_ysdf2_rounded_box_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_rounded_box_ptr, "yetty_ysdf2_rounded_box_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_rounded_box_ptr, (struct yetty_ysdf2_rounded_box *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_to(
    struct yetty_ysdf2_rounded_box *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_rounded_box_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_rounded_box_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ysdf2_rounded_box_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_rounded_box_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_x_set(struct yetty_yclass_object *obj,
                                                                    float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_box_center_x_set: data block",
                         data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_center_y_set(struct yetty_yclass_object *obj,
                                                                    float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_box_center_y_set: data block",
                         data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_half_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_half_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_width);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_width_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_box_half_width_set: data block",
                         data);
    }
    data.value->half_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_half_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_half_height_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_box_half_height_set: data block",
                         data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_radius_top_right_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_radius_top_right_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_top_right);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_right_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_rounded_box_radius_top_right_set: data block", data);
    }
    data.value->radius_top_right = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_radius_bottom_right_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_radius_bottom_right_get: data block",
                         data);
    }
    return YETTY_OK(float, data.value->radius_bottom_right);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_right_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_rounded_box_radius_bottom_right_set: data block", data);
    }
    data.value->radius_bottom_right = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_radius_top_left_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_radius_top_left_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_top_left);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_top_left_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_rounded_box_radius_top_left_set: data block", data);
    }
    data.value->radius_top_left = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_box_radius_bottom_left_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_box_radius_bottom_left_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_bottom_left);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_box_radius_bottom_left_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_rounded_box_ptr_result data = yetty_ysdf2_rounded_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_rounded_box_radius_bottom_left_set: data block", data);
    }
    data.value->radius_bottom_left = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_rhombus_yetty_ydrawlist2_pack_ysdf2_rhombus_pack_check =
    ysdf2_rhombus_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_rhombus_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_rhombus");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_rhombus",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_rhombus),
        .data_align = _Alignof(struct yetty_ysdf2_rhombus),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_rhombus_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_rhombus_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_rhombus_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_rhombus_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_rhombus_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_rhombus_ptr_result yetty_ysdf2_rhombus_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_rhombus_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_rhombus_ptr, "yetty_ysdf2_rhombus_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_rhombus_ptr, "yetty_ysdf2_rhombus_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_rhombus_ptr, (struct yetty_ysdf2_rhombus *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_to(struct yetty_ysdf2_rhombus *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_rhombus_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_rhombus_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_rhombus_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_rhombus_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rhombus_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_x_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rhombus_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rhombus_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rhombus_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_rhombus_center_y_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rhombus_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rhombus_half_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rhombus_half_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_width);
}

struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_width_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rhombus_half_width_set: data block", data);
    }
    data.value->half_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rhombus_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rhombus_half_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_rhombus_half_height_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_rhombus_ptr_result data = yetty_ysdf2_rhombus_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rhombus_half_height_set: data block", data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_pentagon_yetty_ydrawlist2_pack_ysdf2_pentagon_pack_check = ysdf2_pentagon_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_pentagon_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_pentagon");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_pentagon",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_pentagon),
        .data_align = _Alignof(struct yetty_ysdf2_pentagon),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_pentagon_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_pentagon_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_pentagon_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_pentagon_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_pentagon_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_pentagon_ptr_result yetty_ysdf2_pentagon_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_pentagon_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_pentagon_ptr, "yetty_ysdf2_pentagon_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_pentagon_ptr, "yetty_ysdf2_pentagon_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_pentagon_ptr, (struct yetty_ysdf2_pentagon *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_to(struct yetty_ysdf2_pentagon *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_pentagon_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_pentagon_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_pentagon_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_pentagon_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pentagon_ptr_result data = yetty_ysdf2_pentagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pentagon_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_x_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_pentagon_ptr_result data = yetty_ysdf2_pentagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pentagon_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pentagon_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pentagon_ptr_result data = yetty_ysdf2_pentagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pentagon_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_pentagon_center_y_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_pentagon_ptr_result data = yetty_ysdf2_pentagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pentagon_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pentagon_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pentagon_ptr_result data = yetty_ysdf2_pentagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pentagon_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_pentagon_radius_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_pentagon_ptr_result data = yetty_ysdf2_pentagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pentagon_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_hexagon_yetty_ydrawlist2_pack_ysdf2_hexagon_pack_check =
    ysdf2_hexagon_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_hexagon_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_hexagon");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_hexagon",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_hexagon),
        .data_align = _Alignof(struct yetty_ysdf2_hexagon),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_hexagon_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_hexagon_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_hexagon_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_hexagon_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_hexagon_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_hexagon_ptr_result yetty_ysdf2_hexagon_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_hexagon_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_hexagon_ptr, "yetty_ysdf2_hexagon_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_hexagon_ptr, "yetty_ysdf2_hexagon_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_hexagon_ptr, (struct yetty_ysdf2_hexagon *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_to(struct yetty_ysdf2_hexagon *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_hexagon_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_hexagon_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_hexagon_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_hexagon_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_hexagon_ptr_result data = yetty_ysdf2_hexagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_hexagon_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_x_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_hexagon_ptr_result data = yetty_ysdf2_hexagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_hexagon_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_hexagon_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_hexagon_ptr_result data = yetty_ysdf2_hexagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_hexagon_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_hexagon_center_y_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_hexagon_ptr_result data = yetty_ysdf2_hexagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_hexagon_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_hexagon_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_hexagon_ptr_result data = yetty_ysdf2_hexagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_hexagon_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_hexagon_radius_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_hexagon_ptr_result data = yetty_ysdf2_hexagon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_hexagon_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_star_yetty_ydrawlist2_pack_ysdf2_star_pack_check =
    ysdf2_star_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_star_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_star");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_star",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_star),
        .data_align = _Alignof(struct yetty_ysdf2_star),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_star_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_star_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_star_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_star_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_star_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_star_ptr_result yetty_ysdf2_star_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_star_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_star_ptr, "yetty_ysdf2_star_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_star_ptr, "yetty_ysdf2_star_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_star_ptr, (struct yetty_ysdf2_star *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_star_to(struct yetty_ysdf2_star *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_star_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_star_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_star_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_star_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_star_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_star_center_x_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_star_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_star_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_star_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_star_center_y_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_star_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_star_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_star_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_star_radius_set(struct yetty_yclass_object *obj,
                                                           float value)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_star_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_star_num_points_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_star_num_points_get: data block", data);
    }
    return YETTY_OK(float, data.value->num_points);
}

struct yetty_ycore_void_result yetty_ysdf2_star_num_points_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_star_num_points_set: data block", data);
    }
    data.value->num_points = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_star_inner_ratio_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_star_inner_ratio_get: data block", data);
    }
    return YETTY_OK(float, data.value->inner_ratio);
}

struct yetty_ycore_void_result yetty_ysdf2_star_inner_ratio_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_star_ptr_result data = yetty_ysdf2_star_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_star_inner_ratio_set: data block", data);
    }
    data.value->inner_ratio = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_pie_yetty_ydrawlist2_pack_ysdf2_pie_pack_check =
    ysdf2_pie_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_pie_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_pie");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_pie",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_pie),
        .data_align = _Alignof(struct yetty_ysdf2_pie),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_pie_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_pie_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_pie_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_pie_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_pie_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_pie_ptr_result yetty_ysdf2_pie_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_pie_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_pie_ptr, "yetty_ysdf2_pie_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_pie_ptr, "yetty_ysdf2_pie_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_pie_ptr, (struct yetty_ysdf2_pie *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_to(struct yetty_ysdf2_pie *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_pie_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_pie_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_pie_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_pie_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pie_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_pie_center_x_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pie_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pie_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pie_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_pie_center_y_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pie_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pie_aperture_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pie_aperture_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->aperture_x);
}

struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_x_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pie_aperture_x_set: data block", data);
    }
    data.value->aperture_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pie_aperture_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pie_aperture_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->aperture_y);
}

struct yetty_ycore_void_result yetty_ysdf2_pie_aperture_y_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pie_aperture_y_set: data block", data);
    }
    data.value->aperture_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pie_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pie_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_pie_radius_set(struct yetty_yclass_object *obj,
                                                          float value)
{
    struct yetty_ysdf2_pie_ptr_result data = yetty_ysdf2_pie_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pie_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_ring_yetty_ydrawlist2_pack_ysdf2_ring_pack_check =
    ysdf2_ring_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_ring_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_ring");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_ring",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_ring),
        .data_align = _Alignof(struct yetty_ysdf2_ring),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_ring_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_ring_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_ring_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_ring_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_ring_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_ring_ptr_result yetty_ysdf2_ring_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_ring_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_ring_ptr, "yetty_ysdf2_ring_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_ring_ptr, "yetty_ysdf2_ring_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_ring_ptr, (struct yetty_ysdf2_ring *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_to(struct yetty_ysdf2_ring *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_ring_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_ring_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_ring_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_ring_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ring_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_ring_center_x_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ring_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ring_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ring_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_ring_center_y_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ring_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ring_normal_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ring_normal_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->normal_x);
}

struct yetty_ycore_void_result yetty_ysdf2_ring_normal_x_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ring_normal_x_set: data block", data);
    }
    data.value->normal_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ring_normal_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ring_normal_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->normal_y);
}

struct yetty_ycore_void_result yetty_ysdf2_ring_normal_y_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ring_normal_y_set: data block", data);
    }
    data.value->normal_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ring_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ring_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_ring_radius_set(struct yetty_yclass_object *obj,
                                                           float value)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ring_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_ring_thickness_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_ring_thickness_get: data block", data);
    }
    return YETTY_OK(float, data.value->thickness);
}

struct yetty_ycore_void_result yetty_ysdf2_ring_thickness_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_ring_ptr_result data = yetty_ysdf2_ring_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_ring_thickness_set: data block", data);
    }
    data.value->thickness = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_heart_yetty_ydrawlist2_pack_ysdf2_heart_pack_check =
    ysdf2_heart_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_heart_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_heart");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_heart",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_heart),
        .data_align = _Alignof(struct yetty_ysdf2_heart),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_heart_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_heart_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_heart_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_heart_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_heart_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_heart_ptr_result yetty_ysdf2_heart_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_heart_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_heart_ptr, "yetty_ysdf2_heart_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_heart_ptr, "yetty_ysdf2_heart_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_heart_ptr, (struct yetty_ysdf2_heart *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_to(struct yetty_ysdf2_heart *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_heart_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_heart_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_heart_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_heart_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_heart_ptr_result data = yetty_ysdf2_heart_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_heart_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_heart_center_x_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_heart_ptr_result data = yetty_ysdf2_heart_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_heart_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_heart_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_heart_ptr_result data = yetty_ysdf2_heart_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_heart_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_heart_center_y_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_heart_ptr_result data = yetty_ysdf2_heart_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_heart_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_heart_scale_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_heart_ptr_result data = yetty_ysdf2_heart_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_heart_scale_get: data block", data);
    }
    return YETTY_OK(float, data.value->scale);
}

struct yetty_ycore_void_result yetty_ysdf2_heart_scale_set(struct yetty_yclass_object *obj,
                                                           float value)
{
    struct yetty_ysdf2_heart_ptr_result data = yetty_ysdf2_heart_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_heart_scale_set: data block", data);
    }
    data.value->scale = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_cross_yetty_ydrawlist2_pack_ysdf2_cross_pack_check =
    ysdf2_cross_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_cross_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_cross");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_cross",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_cross),
        .data_align = _Alignof(struct yetty_ysdf2_cross),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_cross_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_cross_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_cross_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_cross_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_cross_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_cross_ptr_result yetty_ysdf2_cross_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_cross_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_cross_ptr, "yetty_ysdf2_cross_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_cross_ptr, "yetty_ysdf2_cross_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_cross_ptr, (struct yetty_ysdf2_cross *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_to(struct yetty_ysdf2_cross *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_cross_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_cross_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_cross_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_cross_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cross_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_cross_center_x_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cross_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cross_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cross_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_cross_center_y_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cross_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cross_half_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cross_half_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_width);
}

struct yetty_ycore_void_result yetty_ysdf2_cross_half_width_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cross_half_width_set: data block", data);
    }
    data.value->half_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cross_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cross_half_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_cross_half_height_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cross_half_height_set: data block", data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cross_corner_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cross_corner_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->corner_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_cross_corner_radius_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_cross_ptr_result data = yetty_ysdf2_cross_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cross_corner_radius_set: data block", data);
    }
    data.value->corner_radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_rounded_x_yetty_ydrawlist2_pack_ysdf2_rounded_x_pack_check = ysdf2_rounded_x_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_rounded_x_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_rounded_x");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_rounded_x",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_rounded_x),
        .data_align = _Alignof(struct yetty_ysdf2_rounded_x),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_rounded_x_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_rounded_x_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_rounded_x_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_rounded_x_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_rounded_x_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_rounded_x_ptr_result yetty_ysdf2_rounded_x_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_rounded_x_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_rounded_x_ptr, "yetty_ysdf2_rounded_x_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_rounded_x_ptr, "yetty_ysdf2_rounded_x_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_rounded_x_ptr, (struct yetty_ysdf2_rounded_x *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_to(struct yetty_ysdf2_rounded_x *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_rounded_x_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_rounded_x_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_rounded_x_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_rounded_x_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_x_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_x_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_x_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_x_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_x_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_x_center_y_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_x_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_x_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_x_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->width);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_x_width_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_x_width_set: data block", data);
    }
    data.value->width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_rounded_x_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_rounded_x_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_rounded_x_radius_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_rounded_x_ptr_result data = yetty_ysdf2_rounded_x_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_rounded_x_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_capsule_yetty_ydrawlist2_pack_ysdf2_capsule_pack_check =
    ysdf2_capsule_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_capsule_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_capsule");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_capsule",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_capsule),
        .data_align = _Alignof(struct yetty_ysdf2_capsule),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_capsule_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_capsule_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_capsule_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_capsule_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_capsule_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_capsule_ptr_result yetty_ysdf2_capsule_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_capsule_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_capsule_ptr, "yetty_ysdf2_capsule_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_capsule_ptr, "yetty_ysdf2_capsule_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_capsule_ptr, (struct yetty_ysdf2_capsule *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_to(struct yetty_ysdf2_capsule *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_capsule_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_capsule_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_capsule_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_capsule_start_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_capsule_start_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->start_x);
}

struct yetty_ycore_void_result yetty_ysdf2_capsule_start_x_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_capsule_start_x_set: data block", data);
    }
    data.value->start_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_capsule_start_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_capsule_start_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->start_y);
}

struct yetty_ycore_void_result yetty_ysdf2_capsule_start_y_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_capsule_start_y_set: data block", data);
    }
    data.value->start_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_capsule_end_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_capsule_end_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->end_x);
}

struct yetty_ycore_void_result yetty_ysdf2_capsule_end_x_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_capsule_end_x_set: data block", data);
    }
    data.value->end_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_capsule_end_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_capsule_end_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->end_y);
}

struct yetty_ycore_void_result yetty_ysdf2_capsule_end_y_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_capsule_end_y_set: data block", data);
    }
    data.value->end_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_capsule_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_capsule_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_capsule_radius_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_capsule_ptr_result data = yetty_ysdf2_capsule_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_capsule_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_moon_yetty_ydrawlist2_pack_ysdf2_moon_pack_check =
    ysdf2_moon_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_moon_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_moon");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_moon",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_moon),
        .data_align = _Alignof(struct yetty_ysdf2_moon),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_moon_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_moon_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_moon_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_moon_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_moon_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_moon_ptr_result yetty_ysdf2_moon_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_moon_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_moon_ptr, "yetty_ysdf2_moon_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_moon_ptr, "yetty_ysdf2_moon_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_moon_ptr, (struct yetty_ysdf2_moon *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_to(struct yetty_ysdf2_moon *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_moon_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_moon_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_moon_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_moon_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_moon_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_moon_center_x_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_moon_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_moon_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_moon_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_moon_center_y_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_moon_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_moon_offset_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_moon_offset_get: data block", data);
    }
    return YETTY_OK(float, data.value->offset);
}

struct yetty_ycore_void_result yetty_ysdf2_moon_offset_set(struct yetty_yclass_object *obj,
                                                           float value)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_moon_offset_set: data block", data);
    }
    data.value->offset = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_moon_radius_outer_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_moon_radius_outer_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_outer);
}

struct yetty_ycore_void_result yetty_ysdf2_moon_radius_outer_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_moon_radius_outer_set: data block", data);
    }
    data.value->radius_outer = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_moon_radius_inner_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_moon_radius_inner_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_inner);
}

struct yetty_ycore_void_result yetty_ysdf2_moon_radius_inner_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_moon_ptr_result data = yetty_ysdf2_moon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_moon_radius_inner_set: data block", data);
    }
    data.value->radius_inner = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_egg_yetty_ydrawlist2_pack_ysdf2_egg_pack_check =
    ysdf2_egg_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_egg_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_egg");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_egg",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_egg),
        .data_align = _Alignof(struct yetty_ysdf2_egg),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_egg_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_egg_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_egg_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_egg_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_egg_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_egg_ptr_result yetty_ysdf2_egg_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_egg_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_egg_ptr, "yetty_ysdf2_egg_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_egg_ptr, "yetty_ysdf2_egg_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_egg_ptr, (struct yetty_ysdf2_egg *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_to(struct yetty_ysdf2_egg *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_egg_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_egg_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_egg_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_egg_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_egg_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_egg_center_x_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_egg_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_egg_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_egg_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_egg_center_y_set(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_egg_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_egg_radius_outer_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_egg_radius_outer_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_outer);
}

struct yetty_ycore_void_result yetty_ysdf2_egg_radius_outer_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_egg_radius_outer_set: data block", data);
    }
    data.value->radius_outer = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_egg_radius_inner_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_egg_radius_inner_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius_inner);
}

struct yetty_ycore_void_result yetty_ysdf2_egg_radius_inner_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_egg_ptr_result data = yetty_ysdf2_egg_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_egg_radius_inner_set: data block", data);
    }
    data.value->radius_inner = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_octogon_yetty_ydrawlist2_pack_ysdf2_octogon_pack_check =
    ysdf2_octogon_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_octogon_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_octogon");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_octogon",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_octogon),
        .data_align = _Alignof(struct yetty_ysdf2_octogon),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_octogon_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_octogon_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_octogon_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_octogon_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_octogon_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_octogon_ptr_result yetty_ysdf2_octogon_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_octogon_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_octogon_ptr, "yetty_ysdf2_octogon_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_octogon_ptr, "yetty_ysdf2_octogon_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_octogon_ptr, (struct yetty_ysdf2_octogon *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_to(struct yetty_ysdf2_octogon *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_octogon_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_octogon_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_octogon_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_octogon_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_octogon_ptr_result data = yetty_ysdf2_octogon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_octogon_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_octogon_center_x_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_octogon_ptr_result data = yetty_ysdf2_octogon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_octogon_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_octogon_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_octogon_ptr_result data = yetty_ysdf2_octogon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_octogon_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_octogon_center_y_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_octogon_ptr_result data = yetty_ysdf2_octogon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_octogon_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_octogon_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_octogon_ptr_result data = yetty_ysdf2_octogon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_octogon_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_octogon_radius_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ysdf2_octogon_ptr_result data = yetty_ysdf2_octogon_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_octogon_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_hexagram_yetty_ydrawlist2_pack_ysdf2_hexagram_pack_check = ysdf2_hexagram_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_hexagram_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_hexagram");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_hexagram",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_hexagram),
        .data_align = _Alignof(struct yetty_ysdf2_hexagram),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_hexagram_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_hexagram_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_hexagram_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_hexagram_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_hexagram_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_hexagram_ptr_result yetty_ysdf2_hexagram_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_hexagram_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_hexagram_ptr, "yetty_ysdf2_hexagram_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_hexagram_ptr, "yetty_ysdf2_hexagram_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_hexagram_ptr, (struct yetty_ysdf2_hexagram *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_to(struct yetty_ysdf2_hexagram *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_hexagram_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_hexagram_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_hexagram_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_hexagram_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_hexagram_ptr_result data = yetty_ysdf2_hexagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_hexagram_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_x_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_hexagram_ptr_result data = yetty_ysdf2_hexagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_hexagram_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_hexagram_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_hexagram_ptr_result data = yetty_ysdf2_hexagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_hexagram_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_hexagram_center_y_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_hexagram_ptr_result data = yetty_ysdf2_hexagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_hexagram_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_hexagram_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_hexagram_ptr_result data = yetty_ysdf2_hexagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_hexagram_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_hexagram_radius_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ysdf2_hexagram_ptr_result data = yetty_ysdf2_hexagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_hexagram_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_pentagram_yetty_ydrawlist2_pack_ysdf2_pentagram_pack_check = ysdf2_pentagram_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_pentagram_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_pentagram");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_pentagram",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_pentagram),
        .data_align = _Alignof(struct yetty_ysdf2_pentagram),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_pentagram_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_pentagram_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_pentagram_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_pentagram_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_pentagram_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_pentagram_ptr_result yetty_ysdf2_pentagram_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_pentagram_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_pentagram_ptr, "yetty_ysdf2_pentagram_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_pentagram_ptr, "yetty_ysdf2_pentagram_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_pentagram_ptr, (struct yetty_ysdf2_pentagram *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_to(struct yetty_ysdf2_pentagram *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_pentagram_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_pentagram_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_pentagram_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_pentagram_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pentagram_ptr_result data = yetty_ysdf2_pentagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pentagram_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_x_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_pentagram_ptr_result data = yetty_ysdf2_pentagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pentagram_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pentagram_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pentagram_ptr_result data = yetty_ysdf2_pentagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pentagram_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_pentagram_center_y_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_pentagram_ptr_result data = yetty_ysdf2_pentagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pentagram_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_pentagram_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_pentagram_ptr_result data = yetty_ysdf2_pentagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_pentagram_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_pentagram_radius_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_pentagram_ptr_result data = yetty_ysdf2_pentagram_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_pentagram_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_linear_gradient_box_yetty_ydrawlist2_pack_ysdf2_linear_gradient_box_pack_check =
        ysdf2_linear_gradient_box_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_linear_gradient_box_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_linear_gradient_box");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_linear_gradient_box",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_linear_gradient_box),
        .data_align = _Alignof(struct yetty_ysdf2_linear_gradient_box),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_linear_gradient_box_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_linear_gradient_box_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_linear_gradient_box_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_linear_gradient_box_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_linear_gradient_box_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_linear_gradient_box_ptr_result yetty_ysdf2_linear_gradient_box_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_linear_gradient_box_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_linear_gradient_box_ptr,
                         "yetty_ysdf2_linear_gradient_box_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_linear_gradient_box_ptr,
                         "yetty_ysdf2_linear_gradient_box_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_linear_gradient_box_ptr,
                    (struct yetty_ysdf2_linear_gradient_box *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_to(
    struct yetty_ysdf2_linear_gradient_box *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_linear_gradient_box_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_linear_gradient_box_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ysdf2_linear_gradient_box_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_linear_gradient_box_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_x_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_center_y_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_half_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_half_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_width);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_width_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_half_width_set: data block", data);
    }
    data.value->half_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_half_height_get: data block",
                         data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_half_height_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_half_height_set: data block", data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_corner_radius_get(
    struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_corner_radius_get: data block",
                         data);
    }
    return YETTY_OK(float, data.value->corner_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_corner_radius_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_corner_radius_set: data block", data);
    }
    data.value->corner_radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_grad_x0_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_grad_x0_get: data block", data);
    }
    return YETTY_OK(float, data.value->grad_x0);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x0_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_grad_x0_set: data block", data);
    }
    data.value->grad_x0 = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_grad_y0_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_grad_y0_get: data block", data);
    }
    return YETTY_OK(float, data.value->grad_y0);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y0_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_grad_y0_set: data block", data);
    }
    data.value->grad_y0 = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_grad_x1_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_grad_x1_get: data block", data);
    }
    return YETTY_OK(float, data.value->grad_x1);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_x1_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_grad_x1_set: data block", data);
    }
    data.value->grad_x1 = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_linear_gradient_box_grad_y1_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_linear_gradient_box_grad_y1_get: data block", data);
    }
    return YETTY_OK(float, data.value->grad_y1);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_grad_y1_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_linear_gradient_box_grad_y1_set: data block", data);
    }
    data.value->grad_y1 = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ysdf2_linear_gradient_box_color0_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ysdf2_linear_gradient_box_color0_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->color0);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color0_set(
    struct yetty_yclass_object *obj, uint32_t value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_linear_gradient_box_color0_set: data block",
                         data);
    }
    data.value->color0 = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ysdf2_linear_gradient_box_color1_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ysdf2_linear_gradient_box_color1_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->color1);
}

struct yetty_ycore_void_result yetty_ysdf2_linear_gradient_box_color1_set(
    struct yetty_yclass_object *obj, uint32_t value)
{
    struct yetty_ysdf2_linear_gradient_box_ptr_result data =
        yetty_ysdf2_linear_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_linear_gradient_box_color1_set: data block",
                         data);
    }
    data.value->color1 = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_radial_gradient_box_yetty_ydrawlist2_pack_ysdf2_radial_gradient_box_pack_check =
        ysdf2_radial_gradient_box_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_radial_gradient_box_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_radial_gradient_box");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_radial_gradient_box",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_radial_gradient_box),
        .data_align = _Alignof(struct yetty_ysdf2_radial_gradient_box),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_radial_gradient_box_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_radial_gradient_box_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_radial_gradient_box_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_radial_gradient_box_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_radial_gradient_box_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_radial_gradient_box_ptr_result yetty_ysdf2_radial_gradient_box_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_radial_gradient_box_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_radial_gradient_box_ptr,
                         "yetty_ysdf2_radial_gradient_box_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_radial_gradient_box_ptr,
                         "yetty_ysdf2_radial_gradient_box_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_radial_gradient_box_ptr,
                    (struct yetty_ysdf2_radial_gradient_box *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_to(
    struct yetty_ysdf2_radial_gradient_box *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_radial_gradient_box_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_radial_gradient_box_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ysdf2_radial_gradient_box_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_radial_gradient_box_center_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_center_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_x);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_x_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_center_x_set: data block", data);
    }
    data.value->center_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_center_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_center_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->center_y);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_center_y_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_center_y_set: data block", data);
    }
    data.value->center_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_half_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_half_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_width);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_width_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_half_width_set: data block", data);
    }
    data.value->half_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_half_height_get: data block",
                         data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_half_height_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_half_height_set: data block", data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_corner_radius_get(
    struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_corner_radius_get: data block",
                         data);
    }
    return YETTY_OK(float, data.value->corner_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_corner_radius_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_corner_radius_set: data block", data);
    }
    data.value->corner_radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_grad_cx_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_grad_cx_get: data block", data);
    }
    return YETTY_OK(float, data.value->grad_cx);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cx_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_grad_cx_set: data block", data);
    }
    data.value->grad_cx = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_grad_cy_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_grad_cy_get: data block", data);
    }
    return YETTY_OK(float, data.value->grad_cy);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_cy_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_grad_cy_set: data block", data);
    }
    data.value->grad_cy = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_radial_gradient_box_grad_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_radial_gradient_box_grad_radius_get: data block",
                         data);
    }
    return YETTY_OK(float, data.value->grad_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_grad_radius_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_grad_radius_set: data block", data);
    }
    data.value->grad_radius = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ysdf2_radial_gradient_box_color_inner_get(
    struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ysdf2_radial_gradient_box_color_inner_get: data block",
                         data);
    }
    return YETTY_OK(uint32, data.value->color_inner);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_inner_set(
    struct yetty_yclass_object *obj, uint32_t value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_color_inner_set: data block", data);
    }
    data.value->color_inner = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ysdf2_radial_gradient_box_color_outer_get(
    struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ysdf2_radial_gradient_box_color_outer_get: data block",
                         data);
    }
    return YETTY_OK(uint32, data.value->color_outer);
}

struct yetty_ycore_void_result yetty_ysdf2_radial_gradient_box_color_outer_set(
    struct yetty_yclass_object *obj, uint32_t value)
{
    struct yetty_ysdf2_radial_gradient_box_ptr_result data =
        yetty_ysdf2_radial_gradient_box_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ysdf2_radial_gradient_box_color_outer_set: data block", data);
    }
    data.value->color_outer = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_sphere_3d_yetty_ydrawlist2_pack_ysdf2_sphere_3d_pack_check = ysdf2_sphere_3d_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_sphere_3d_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_sphere_3d");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_sphere_3d",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_sphere_3d),
        .data_align = _Alignof(struct yetty_ysdf2_sphere_3d),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_sphere_3d_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_sphere_3d_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_sphere_3d_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_sphere_3d_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_sphere_3d_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_sphere_3d_ptr_result yetty_ysdf2_sphere_3d_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_sphere_3d_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_sphere_3d_ptr, "yetty_ysdf2_sphere_3d_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_sphere_3d_ptr, "yetty_ysdf2_sphere_3d_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_sphere_3d_ptr, (struct yetty_ysdf2_sphere_3d *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_to(struct yetty_ysdf2_sphere_3d *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_sphere_3d_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_sphere_3d_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_sphere_3d_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_sphere_3d_position_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_sphere_3d_position_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_x);
}

struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_x_set(struct yetty_yclass_object *obj,
                                                                    float value)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_sphere_3d_position_x_set: data block",
                         data);
    }
    data.value->position_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_sphere_3d_position_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_sphere_3d_position_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_y);
}

struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_y_set(struct yetty_yclass_object *obj,
                                                                    float value)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_sphere_3d_position_y_set: data block",
                         data);
    }
    data.value->position_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_sphere_3d_position_z_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_sphere_3d_position_z_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_z);
}

struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_position_z_set(struct yetty_yclass_object *obj,
                                                                    float value)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_sphere_3d_position_z_set: data block",
                         data);
    }
    data.value->position_z = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_sphere_3d_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_sphere_3d_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_sphere_3d_radius_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ysdf2_sphere_3d_ptr_result data = yetty_ysdf2_sphere_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_sphere_3d_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ysdf2_box_3d_yetty_ydrawlist2_pack_ysdf2_box_3d_pack_check =
    ysdf2_box_3d_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_box_3d_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_box_3d");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_box_3d",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_box_3d),
        .data_align = _Alignof(struct yetty_ysdf2_box_3d),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_box_3d_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_box_3d_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_box_3d_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_box_3d_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_box_3d_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_box_3d_ptr_result yetty_ysdf2_box_3d_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_box_3d_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_box_3d_ptr, "yetty_ysdf2_box_3d_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_box_3d_ptr, "yetty_ysdf2_box_3d_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ysdf2_box_3d_ptr, (struct yetty_ysdf2_box_3d *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_to(struct yetty_ysdf2_box_3d *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_box_3d_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ysdf2_box_3d_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_box_3d_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_box_3d_position_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_3d_position_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_x);
}

struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_x_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_3d_position_x_set: data block", data);
    }
    data.value->position_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_3d_position_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_3d_position_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_y);
}

struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_y_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_3d_position_y_set: data block", data);
    }
    data.value->position_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_3d_position_z_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_3d_position_z_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_z);
}

struct yetty_ycore_void_result yetty_ysdf2_box_3d_position_z_set(struct yetty_yclass_object *obj,
                                                                 float value)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_3d_position_z_set: data block", data);
    }
    data.value->position_z = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_3d_half_size_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_3d_half_size_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_size_x);
}

struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_x_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_3d_half_size_x_set: data block", data);
    }
    data.value->half_size_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_3d_half_size_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_3d_half_size_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_size_y);
}

struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_y_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_3d_half_size_y_set: data block", data);
    }
    data.value->half_size_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_box_3d_half_size_z_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_box_3d_half_size_z_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_size_z);
}

struct yetty_ycore_void_result yetty_ysdf2_box_3d_half_size_z_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_box_3d_ptr_result data = yetty_ysdf2_box_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_box_3d_half_size_z_set: data block", data);
    }
    data.value->half_size_z = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_torus_3d_yetty_ydrawlist2_pack_ysdf2_torus_3d_pack_check = ysdf2_torus_3d_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_torus_3d_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_torus_3d");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_torus_3d",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_torus_3d),
        .data_align = _Alignof(struct yetty_ysdf2_torus_3d),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_torus_3d_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_torus_3d_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_torus_3d_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_torus_3d_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ysdf2_torus_3d_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_torus_3d_ptr_result yetty_ysdf2_torus_3d_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_torus_3d_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_torus_3d_ptr, "yetty_ysdf2_torus_3d_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_torus_3d_ptr, "yetty_ysdf2_torus_3d_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_torus_3d_ptr, (struct yetty_ysdf2_torus_3d *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_to(struct yetty_ysdf2_torus_3d *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_torus_3d_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_torus_3d_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ysdf2_torus_3d_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_torus_3d_position_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_torus_3d_position_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_x);
}

struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_x_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_torus_3d_position_x_set: data block", data);
    }
    data.value->position_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_torus_3d_position_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_torus_3d_position_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_y);
}

struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_y_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_torus_3d_position_y_set: data block", data);
    }
    data.value->position_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_torus_3d_position_z_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_torus_3d_position_z_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_z);
}

struct yetty_ycore_void_result yetty_ysdf2_torus_3d_position_z_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_torus_3d_position_z_set: data block", data);
    }
    data.value->position_z = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_torus_3d_major_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_torus_3d_major_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->major_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_torus_3d_major_radius_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_torus_3d_major_radius_set: data block",
                         data);
    }
    data.value->major_radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_torus_3d_minor_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_torus_3d_minor_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->minor_radius);
}

struct yetty_ycore_void_result yetty_ysdf2_torus_3d_minor_radius_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_torus_3d_ptr_result data = yetty_ysdf2_torus_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_torus_3d_minor_radius_set: data block",
                         data);
    }
    data.value->minor_radius = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ysdf2_cylinder_3d_yetty_ydrawlist2_pack_ysdf2_cylinder_3d_pack_check =
        ysdf2_cylinder_3d_pack;

struct yetty_yclass_ptr_result yetty_ysdf2_cylinder_3d_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ysdf2_cylinder_3d");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ysdf2_cylinder_3d",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ysdf2_cylinder_3d),
        .data_align = _Alignof(struct yetty_ysdf2_cylinder_3d),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)ysdf2_cylinder_3d_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ysdf2_cylinder_3d_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_cylinder_3d_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ysdf2_cylinder_3d_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ysdf2_cylinder_3d_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ysdf2_cylinder_3d_ptr_result yetty_ysdf2_cylinder_3d_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_cylinder_3d_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ysdf2_cylinder_3d_ptr,
                         "yetty_ysdf2_cylinder_3d_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ysdf2_cylinder_3d_ptr, "yetty_ysdf2_cylinder_3d_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ysdf2_cylinder_3d_ptr, (struct yetty_ysdf2_cylinder_3d *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_to(
    struct yetty_ysdf2_cylinder_3d *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ysdf2_cylinder_3d_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ysdf2_cylinder_3d_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ysdf2_cylinder_3d_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ysdf2_cylinder_3d_position_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cylinder_3d_position_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_x);
}

struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_x_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cylinder_3d_position_x_set: data block",
                         data);
    }
    data.value->position_x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cylinder_3d_position_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cylinder_3d_position_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_y);
}

struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_y_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cylinder_3d_position_y_set: data block",
                         data);
    }
    data.value->position_y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cylinder_3d_position_z_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cylinder_3d_position_z_get: data block", data);
    }
    return YETTY_OK(float, data.value->position_z);
}

struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_position_z_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cylinder_3d_position_z_set: data block",
                         data);
    }
    data.value->position_z = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cylinder_3d_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cylinder_3d_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->radius);
}

struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_radius_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cylinder_3d_radius_set: data block", data);
    }
    data.value->radius = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ysdf2_cylinder_3d_half_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ysdf2_cylinder_3d_half_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->half_height);
}

struct yetty_ycore_void_result yetty_ysdf2_cylinder_3d_half_height_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ysdf2_cylinder_3d_ptr_result data = yetty_ysdf2_cylinder_3d_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ysdf2_cylinder_3d_half_height_set: data block",
                         data);
    }
    data.value->half_height = value;
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_circle_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_circle");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_circle_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_circle_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_circle_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_box_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_box");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_box_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_box_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_box_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_segment_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_segment");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_segment_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_segment_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_segment_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_triangle_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_triangle");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_triangle_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_triangle_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_triangle_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ellipse_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_ellipse");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_ellipse_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_ellipse_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_ellipse_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_arc_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_arc");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_arc_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_arc_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_arc_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_box_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_rounded_box");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_rounded_box_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_rounded_box_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_rounded_box_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rhombus_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_rhombus");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_rhombus_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_rhombus_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_rhombus_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagon_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_pentagon");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_pentagon_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_pentagon_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_pentagon_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagon_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_hexagon");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_hexagon_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_hexagon_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_hexagon_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_star_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_star_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_star");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_star_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_star_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_star_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pie_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_pie");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_pie_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_pie_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_pie_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_ring_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_ring");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_ring_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_ring_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_ring_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_heart_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_heart");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_heart_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_heart_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_heart_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cross_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_cross");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_cross_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_cross_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_cross_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_rounded_x_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_rounded_x");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_rounded_x_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_rounded_x_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_rounded_x_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_capsule_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_capsule");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_capsule_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_capsule_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_capsule_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_moon_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_moon");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_moon_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_moon_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_moon_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_egg_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_egg");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_egg_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_egg_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_ysdf2_egg_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_octogon_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_octogon");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_octogon_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_octogon_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_octogon_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_hexagram_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_hexagram");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_hexagram_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_hexagram_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_hexagram_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_pentagram_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_pentagram");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_pentagram_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_pentagram_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_pentagram_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_linear_gradient_box_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_linear_gradient_box");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_linear_gradient_box_create: remote create unsupported for a "
                         "split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_linear_gradient_box_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_linear_gradient_box_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_radial_gradient_box_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_radial_gradient_box");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_radial_gradient_box_create: remote create unsupported for a "
                         "split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_radial_gradient_box_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_radial_gradient_box_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_sphere_3d_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_sphere_3d");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_sphere_3d_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_sphere_3d_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_sphere_3d_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_box_3d_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_box_3d");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_box_3d_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_box_3d_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_box_3d_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_torus_3d_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_torus_3d");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_torus_3d_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_torus_3d_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_torus_3d_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ysdf2_cylinder_3d_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ysdf2_cylinder_3d");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ysdf2_cylinder_3d_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ysdf2_cylinder_3d_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ysdf2_cylinder_3d_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ysdf2_circle_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_segment_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_triangle_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_ellipse_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_arc_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_rounded_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_rhombus_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_pentagon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_hexagon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_star_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_pie_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_ring_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_heart_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_cross_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_rounded_x_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_capsule_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_moon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_egg_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_octogon_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_hexagram_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_pentagram_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_linear_gradient_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_radial_gradient_box_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_sphere_3d_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_box_3d_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_torus_3d_class_get(void);
struct yetty_yclass_ptr_result yetty_ysdf2_cylinder_3d_class_get(void);
struct yetty_ycore_void_result yetty_ysdf2_register(void);

/* ---- ysdf2: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ysdf2_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ysdf2_circle") == 0) {
        return yetty_ysdf2_circle_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_box") == 0) {
        return yetty_ysdf2_box_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_segment") == 0) {
        return yetty_ysdf2_segment_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_triangle") == 0) {
        return yetty_ysdf2_triangle_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_ellipse") == 0) {
        return yetty_ysdf2_ellipse_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_arc") == 0) {
        return yetty_ysdf2_arc_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_rounded_box") == 0) {
        return yetty_ysdf2_rounded_box_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_rhombus") == 0) {
        return yetty_ysdf2_rhombus_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_pentagon") == 0) {
        return yetty_ysdf2_pentagon_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_hexagon") == 0) {
        return yetty_ysdf2_hexagon_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_star") == 0) {
        return yetty_ysdf2_star_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_pie") == 0) {
        return yetty_ysdf2_pie_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_ring") == 0) {
        return yetty_ysdf2_ring_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_heart") == 0) {
        return yetty_ysdf2_heart_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_cross") == 0) {
        return yetty_ysdf2_cross_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_rounded_x") == 0) {
        return yetty_ysdf2_rounded_x_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_capsule") == 0) {
        return yetty_ysdf2_capsule_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_moon") == 0) {
        return yetty_ysdf2_moon_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_egg") == 0) {
        return yetty_ysdf2_egg_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_octogon") == 0) {
        return yetty_ysdf2_octogon_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_hexagram") == 0) {
        return yetty_ysdf2_hexagram_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_pentagram") == 0) {
        return yetty_ysdf2_pentagram_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_linear_gradient_box") == 0) {
        return yetty_ysdf2_linear_gradient_box_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_radial_gradient_box") == 0) {
        return yetty_ysdf2_radial_gradient_box_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_sphere_3d") == 0) {
        return yetty_ysdf2_sphere_3d_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_box_3d") == 0) {
        return yetty_ysdf2_box_3d_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_torus_3d") == 0) {
        return yetty_ysdf2_torus_3d_class_get();
    }
    if (strcmp(name, "yetty_ysdf2_cylinder_3d") == 0) {
        return yetty_ysdf2_cylinder_3d_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ysdf2: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ysdf2_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ysdf2_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ysdf2_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
