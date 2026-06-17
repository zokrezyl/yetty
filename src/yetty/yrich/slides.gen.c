/* GENERATED — do not edit. */
#include "yetty/yrich/document.h"
#include "yetty/yrich/element.h"
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

struct yetty_ycore_float_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *drawable_list,
    uint32_t layer, int selected);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_object *obj,
                                                              int32_t index);
struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(
    struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_set_current_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_next_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_prev_fn)(struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_shape_yetty_yrich_constructor_check =
    shape_constructor;
[[maybe_unused]]
static yetty_yrich_element_destroy_fn yetty_yrich_shape_yetty_yrich_element_destroy_check =
    shape_destroy;
[[maybe_unused]]
static yetty_yrich_element_bounds_fn yetty_yrich_shape_yetty_yrich_element_bounds_check =
    shape_bounds;
[[maybe_unused]]
static yetty_yrich_element_is_editable_fn yetty_yrich_shape_yetty_yrich_element_is_editable_check =
    shape_is_editable;
[[maybe_unused]]
static yetty_yrich_element_begin_edit_fn yetty_yrich_shape_yetty_yrich_element_begin_edit_check =
    shape_begin_edit;
[[maybe_unused]]
static yetty_yrich_element_end_edit_fn yetty_yrich_shape_yetty_yrich_element_end_edit_check =
    shape_end_edit;
[[maybe_unused]]
static yetty_yrich_element_is_editing_fn yetty_yrich_shape_yetty_yrich_element_is_editing_check =
    shape_is_editing;
[[maybe_unused]]
static yetty_yrich_element_render_fn yetty_yrich_shape_yetty_yrich_element_render_check =
    shape_render;
[[maybe_unused]]
static yetty_yrich_element_insert_text_fn yetty_yrich_shape_yetty_yrich_element_insert_text_check =
    shape_insert_text;
[[maybe_unused]]
static yetty_yrich_element_delete_sel_fn yetty_yrich_shape_yetty_yrich_element_delete_sel_check =
    shape_delete_sel;

struct yetty_yclass_ptr_result yetty_yrich_shape_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_shape");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_shape",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_shape),
        .data_align = _Alignof(struct yetty_yrich_shape),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)shape_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy,
         (yetty_yclass_impl_t)shape_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds,
         (yetty_yclass_impl_t)shape_bounds},
        {"yetty_yrich", "element_is_editable",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editable,
         (yetty_yclass_impl_t)shape_is_editable},
        {"yetty_yrich", "element_begin_edit",
         (yetty_yclass_method_id_t)yetty_yrich_element_begin_edit,
         (yetty_yclass_impl_t)shape_begin_edit},
        {"yetty_yrich", "element_end_edit", (yetty_yclass_method_id_t)yetty_yrich_element_end_edit,
         (yetty_yclass_impl_t)shape_end_edit},
        {"yetty_yrich", "element_is_editing",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editing,
         (yetty_yclass_impl_t)shape_is_editing},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render,
         (yetty_yclass_impl_t)shape_render},
        {"yetty_yrich", "element_insert_text",
         (yetty_yclass_method_id_t)yetty_yrich_element_insert_text,
         (yetty_yclass_impl_t)shape_insert_text},
        {"yetty_yrich", "element_delete_sel",
         (yetty_yclass_method_id_t)yetty_yrich_element_delete_sel,
         (yetty_yclass_impl_t)shape_delete_sel},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_shape_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_shape_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_shape_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_shape_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_shape_ptr_result yetty_yrich_shape_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_shape_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_shape_ptr, "yetty_yrich_shape_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_shape_ptr, "yetty_yrich_shape_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yrich_shape_ptr, (struct yetty_yrich_shape *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_shape_to(struct yetty_yrich_shape *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_shape_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}

struct uint32_result yetty_yrich_shape_fill_color_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_yrich_shape_fill_color_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->fill_color);
}

struct yetty_ycore_void_result yetty_yrich_shape_fill_color_set(struct yetty_yclass_object *obj,
                                                                uint32_t value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_fill_color_set: data block", data);
    }
    data.value->fill_color = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_yrich_shape_stroke_color_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_yrich_shape_stroke_color_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->stroke_color);
}

struct yetty_ycore_void_result yetty_yrich_shape_stroke_color_set(struct yetty_yclass_object *obj,
                                                                  uint32_t value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_stroke_color_set: data block", data);
    }
    data.value->stroke_color = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_yrich_shape_stroke_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_yrich_shape_stroke_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->stroke_width);
}

struct yetty_ycore_void_result yetty_yrich_shape_stroke_width_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_stroke_width_set: data block", data);
    }
    data.value->stroke_width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_yrich_shape_rotation_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_yrich_shape_rotation_get: data block", data);
    }
    return YETTY_OK(float, data.value->rotation);
}

struct yetty_ycore_void_result yetty_yrich_shape_rotation_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_rotation_set: data block", data);
    }
    data.value->rotation = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_yrich_shape_corner_radius_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_yrich_shape_corner_radius_get: data block", data);
    }
    return YETTY_OK(float, data.value->corner_radius);
}

struct yetty_ycore_void_result yetty_yrich_shape_corner_radius_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_corner_radius_set: data block", data);
    }
    data.value->corner_radius = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_yrich_shape_text_align_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_yrich_shape_text_align_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->text_align);
}

struct yetty_ycore_void_result yetty_yrich_shape_text_align_set(struct yetty_yclass_object *obj,
                                                                uint32_t value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_text_align_set: data block", data);
    }
    data.value->text_align = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_yrich_shape_text_valign_get(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_yrich_shape_text_valign_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->text_valign);
}

struct yetty_ycore_void_result yetty_yrich_shape_text_valign_set(struct yetty_yclass_object *obj,
                                                                 uint32_t value)
{
    struct yetty_yrich_shape_ptr_result data = yetty_yrich_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_shape_text_valign_set: data block", data);
    }
    data.value->text_valign = value;
    return YETTY_OK_VOID();
}

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_slides_yetty_yrich_constructor_check =
    slides_constructor;
[[maybe_unused]]
static yetty_yrich_document_destroy_fn yetty_yrich_slides_yetty_yrich_document_destroy_check =
    slides_destroy;
[[maybe_unused]]
static yetty_yrich_document_content_width_fn
    yetty_yrich_slides_yetty_yrich_document_content_width_check = slides_content_width;
[[maybe_unused]]
static yetty_yrich_document_content_height_fn
    yetty_yrich_slides_yetty_yrich_document_content_height_check = slides_content_height;
[[maybe_unused]]
static yetty_yrich_document_render_fn yetty_yrich_slides_yetty_yrich_document_render_check =
    slides_render;
[[maybe_unused]]
static yetty_yrich_slides_set_current_fn yetty_yrich_slides_yetty_yrich_slides_set_current_check =
    slides_set_current;
[[maybe_unused]]
static yetty_yrich_slides_next_fn yetty_yrich_slides_yetty_yrich_slides_next_check = slides_next;
[[maybe_unused]]
static yetty_yrich_slides_prev_fn yetty_yrich_slides_yetty_yrich_slides_prev_check = slides_prev;

struct yetty_yclass_ptr_result yetty_yrich_slides_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_slides");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_slides",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_slides),
        .data_align = _Alignof(struct yetty_yrich_slides),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)slides_constructor},
        {"yetty_yrich", "document_destroy", (yetty_yclass_method_id_t)yetty_yrich_document_destroy,
         (yetty_yclass_impl_t)slides_destroy},
        {"yetty_yrich", "document_content_width",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_width,
         (yetty_yclass_impl_t)slides_content_width},
        {"yetty_yrich", "document_content_height",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_height,
         (yetty_yclass_impl_t)slides_content_height},
        {"yetty_yrich", "document_render", (yetty_yclass_method_id_t)yetty_yrich_document_render,
         (yetty_yclass_impl_t)slides_render},
        {"yetty_yrich", "slides_set_current",
         (yetty_yclass_method_id_t)yetty_yrich_slides_set_current,
         (yetty_yclass_impl_t)slides_set_current},
        {"yetty_yrich", "slides_next", (yetty_yclass_method_id_t)yetty_yrich_slides_next,
         (yetty_yclass_impl_t)slides_next},
        {"yetty_yrich", "slides_prev", (yetty_yclass_method_id_t)yetty_yrich_slides_prev,
         (yetty_yclass_impl_t)slides_prev},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_slides_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_slides_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_slides_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_slides_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_slides_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_slides_ptr, "yetty_yrich_slides_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_slides_ptr, "yetty_yrich_slides_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yrich_slides_ptr, (struct yetty_yrich_slides *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_slides_to(struct yetty_yrich_slides *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_slides_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}

struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_object *obj,
                                                              int32_t index)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_slides_set_current);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_slides_set_current: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_set_current: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_slides_set_current: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            int32_t index;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            index};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_slides_set_current: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_slides_set_current: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_slides_set_current: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_slides_set_current: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_slides_set_current: dispatch_lookup failed");
        return ((yetty_yrich_slides_set_current_fn)dispatch_impl_r.value)(obj, index);
    }
}

struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_slides_next);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_next: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_next: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_slides_next: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_slides_next: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_next: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_slides_next: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_slides_next: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_slides_next: dispatch_lookup failed");
        return ((yetty_yrich_slides_next_fn)dispatch_impl_r.value)(obj);
    }
}

struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_slides_prev);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_prev: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_prev: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_slides_prev: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_slides_prev: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_slides_prev: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_slides_prev: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_slides_prev: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_slides_prev: dispatch_lookup failed");
        return ((yetty_yrich_slides_prev_fn)dispatch_impl_r.value)(obj);
    }
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_slides_set_current_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_slides_set_current_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        int32_t index;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_slides_set_current: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_slides_set_current(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.index);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_slides_set_current", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_slides_next_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_slides_next_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_slides_next: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_slides_next((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_slides_next", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_slides_prev_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_slides_prev_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_slides_prev: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yrich_slides_prev((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_slides_prev", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_shape");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_shape_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_shape_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        struct yetty_ycore_void_result ctor_r = yetty_yrich_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) {
                yetty_ycore_error_destroy(free_r.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yrich_shape_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_shape");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr, "yetty_yrich_shape_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_shape";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_shape_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_shape_create: CREATE returned no/invalid handle");
    }

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_shape_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_yrich_slides_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_slides_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_slides");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_slides_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_slides_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        struct yetty_ycore_void_result ctor_r = yetty_yrich_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) {
                yetty_ycore_error_destroy(free_r.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yrich_slides_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_slides");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr, "yetty_yrich_slides_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_slides";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_slides_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_slides_create: CREATE returned no/invalid handle");
    }

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_slides_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
