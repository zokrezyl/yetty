/* GENERATED — do not edit. */
#include "yetty/yrich/document.h"
#include "yetty/yrich/element.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_float_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ydraw_drawable_list *drawable_list, uint32_t layer, int selected);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_slides_set_current(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              int32_t index);
struct yetty_ycore_void_result yetty_yrich_slides_next(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_slides_prev(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *,
    uint32_t, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_set_current_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_next_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_slides_prev_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);

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
