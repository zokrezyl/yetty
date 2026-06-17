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
struct yetty_yrich_operation;
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
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object *obj,
                                                                uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(
    struct yetty_yclass_object *obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_operation *op,
                                                             int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_object *obj,
                                                               uint32_t color);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_object *obj,
                                                              uint32_t halign);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object *obj,
                                                            uint32_t level);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object *obj,
                                                                 float delta);
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
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(
    struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(
    struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_toggle_format_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_text_color_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_alignment_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_heading_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_change_font_size_fn)(
    struct yetty_yclass_object *, float);

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_paragraph_yetty_yrich_constructor_check =
    paragraph_constructor;
[[maybe_unused]]
static yetty_yrich_element_destroy_fn yetty_yrich_paragraph_yetty_yrich_element_destroy_check =
    paragraph_destroy;
[[maybe_unused]]
static yetty_yrich_element_bounds_fn yetty_yrich_paragraph_yetty_yrich_element_bounds_check =
    paragraph_bounds;
[[maybe_unused]]
static yetty_yrich_element_is_editable_fn
    yetty_yrich_paragraph_yetty_yrich_element_is_editable_check = paragraph_is_editable;
[[maybe_unused]]
static yetty_yrich_element_begin_edit_fn
    yetty_yrich_paragraph_yetty_yrich_element_begin_edit_check = paragraph_begin_edit;
[[maybe_unused]]
static yetty_yrich_element_end_edit_fn yetty_yrich_paragraph_yetty_yrich_element_end_edit_check =
    paragraph_end_edit;
[[maybe_unused]]
static yetty_yrich_element_is_editing_fn
    yetty_yrich_paragraph_yetty_yrich_element_is_editing_check = paragraph_is_editing;
[[maybe_unused]]
static yetty_yrich_element_render_fn yetty_yrich_paragraph_yetty_yrich_element_render_check =
    paragraph_render;
[[maybe_unused]]
static yetty_yrich_element_insert_text_fn
    yetty_yrich_paragraph_yetty_yrich_element_insert_text_check = paragraph_insert_text;
[[maybe_unused]]
static yetty_yrich_element_delete_sel_fn
    yetty_yrich_paragraph_yetty_yrich_element_delete_sel_check = paragraph_delete_sel;

struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_paragraph");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_paragraph",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_paragraph),
        .data_align = _Alignof(struct yetty_yrich_paragraph),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)paragraph_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy,
         (yetty_yclass_impl_t)paragraph_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds,
         (yetty_yclass_impl_t)paragraph_bounds},
        {"yetty_yrich", "element_is_editable",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editable,
         (yetty_yclass_impl_t)paragraph_is_editable},
        {"yetty_yrich", "element_begin_edit",
         (yetty_yclass_method_id_t)yetty_yrich_element_begin_edit,
         (yetty_yclass_impl_t)paragraph_begin_edit},
        {"yetty_yrich", "element_end_edit", (yetty_yclass_method_id_t)yetty_yrich_element_end_edit,
         (yetty_yclass_impl_t)paragraph_end_edit},
        {"yetty_yrich", "element_is_editing",
         (yetty_yclass_method_id_t)yetty_yrich_element_is_editing,
         (yetty_yclass_impl_t)paragraph_is_editing},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render,
         (yetty_yclass_impl_t)paragraph_render},
        {"yetty_yrich", "element_insert_text",
         (yetty_yclass_method_id_t)yetty_yrich_element_insert_text,
         (yetty_yclass_impl_t)paragraph_insert_text},
        {"yetty_yrich", "element_delete_sel",
         (yetty_yclass_method_id_t)yetty_yrich_element_delete_sel,
         (yetty_yclass_impl_t)paragraph_delete_sel},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_paragraph_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yrich_paragraph_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_paragraph_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_paragraph_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_paragraph_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_paragraph_ptr, "yetty_yrich_paragraph_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_paragraph_ptr, "yetty_yrich_paragraph_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yrich_paragraph_ptr, (struct yetty_yrich_paragraph *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_paragraph_to(struct yetty_yrich_paragraph *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_paragraph_class_get();
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

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_inline_image_yetty_yrich_constructor_check =
    inline_image_constructor;
[[maybe_unused]]
static yetty_yrich_element_destroy_fn yetty_yrich_inline_image_yetty_yrich_element_destroy_check =
    inline_image_destroy;
[[maybe_unused]]
static yetty_yrich_element_bounds_fn yetty_yrich_inline_image_yetty_yrich_element_bounds_check =
    inline_image_bounds;
[[maybe_unused]]
static yetty_yrich_element_render_fn yetty_yrich_inline_image_yetty_yrich_element_render_check =
    inline_image_render;

struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_inline_image");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_inline_image",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_inline_image),
        .data_align = _Alignof(struct yetty_yrich_inline_image),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)inline_image_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy,
         (yetty_yclass_impl_t)inline_image_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds,
         (yetty_yclass_impl_t)inline_image_bounds},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render,
         (yetty_yclass_impl_t)inline_image_render},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_inline_image_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yrich_inline_image_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_inline_image_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yrich_inline_image_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_inline_image_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_inline_image_ptr,
                         "yetty_yrich_inline_image_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_inline_image_ptr, "yetty_yrich_inline_image_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yrich_inline_image_ptr, (struct yetty_yrich_inline_image *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_inline_image_to(struct yetty_yrich_inline_image *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_inline_image_class_get();
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

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_ydoc_yetty_yrich_constructor_check = ydoc_constructor;
[[maybe_unused]]
static yetty_yrich_document_destroy_fn yetty_yrich_ydoc_yetty_yrich_document_destroy_check =
    ydoc_destroy;
[[maybe_unused]]
static yetty_yrich_document_content_width_fn
    yetty_yrich_ydoc_yetty_yrich_document_content_width_check = ydoc_content_width;
[[maybe_unused]]
static yetty_yrich_document_content_height_fn
    yetty_yrich_ydoc_yetty_yrich_document_content_height_check = ydoc_content_height;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_down_fn
    yetty_yrich_ydoc_yetty_yrich_document_on_mouse_down_check = ydoc_on_mouse_down;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_drag_fn
    yetty_yrich_ydoc_yetty_yrich_document_on_mouse_drag_check = ydoc_on_mouse_drag;
[[maybe_unused]]
static yetty_yrich_document_on_key_down_fn yetty_yrich_ydoc_yetty_yrich_document_on_key_down_check =
    ydoc_on_key_down;
[[maybe_unused]]
static yetty_yrich_document_on_text_input_fn
    yetty_yrich_ydoc_yetty_yrich_document_on_text_input_check = ydoc_on_text_input;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_double_click_fn
    yetty_yrich_ydoc_yetty_yrich_document_on_mouse_double_click_check = ydoc_on_mouse_double_click;
[[maybe_unused]]
static yetty_yrich_document_apply_op_fn yetty_yrich_ydoc_yetty_yrich_document_apply_op_check =
    ydoc_apply_op;
[[maybe_unused]]
static yetty_yrich_document_render_fn yetty_yrich_ydoc_yetty_yrich_document_render_check =
    ydoc_render;
[[maybe_unused]]
static yetty_yrich_ydoc_toggle_format_fn yetty_yrich_ydoc_yetty_yrich_ydoc_toggle_format_check =
    ydoc_toggle_format_impl;
[[maybe_unused]]
static yetty_yrich_ydoc_set_text_color_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_text_color_check =
    ydoc_set_text_color_impl;
[[maybe_unused]]
static yetty_yrich_ydoc_set_alignment_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_alignment_check =
    ydoc_set_alignment_impl;
[[maybe_unused]]
static yetty_yrich_ydoc_set_heading_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_heading_check =
    ydoc_set_heading_impl;
[[maybe_unused]]
static yetty_yrich_ydoc_change_font_size_fn
    yetty_yrich_ydoc_yetty_yrich_ydoc_change_font_size_check = ydoc_change_font_size_impl;

struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_ydoc");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_ydoc",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_ydoc),
        .data_align = _Alignof(struct yetty_yrich_ydoc),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)ydoc_constructor},
        {"yetty_yrich", "document_destroy", (yetty_yclass_method_id_t)yetty_yrich_document_destroy,
         (yetty_yclass_impl_t)ydoc_destroy},
        {"yetty_yrich", "document_content_width",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_width,
         (yetty_yclass_impl_t)ydoc_content_width},
        {"yetty_yrich", "document_content_height",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_height,
         (yetty_yclass_impl_t)ydoc_content_height},
        {"yetty_yrich", "document_on_mouse_down",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_down,
         (yetty_yclass_impl_t)ydoc_on_mouse_down},
        {"yetty_yrich", "document_on_mouse_drag",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_drag,
         (yetty_yclass_impl_t)ydoc_on_mouse_drag},
        {"yetty_yrich", "document_on_key_down",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_key_down,
         (yetty_yclass_impl_t)ydoc_on_key_down},
        {"yetty_yrich", "document_on_text_input",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_text_input,
         (yetty_yclass_impl_t)ydoc_on_text_input},
        {"yetty_yrich", "document_on_mouse_double_click",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_double_click,
         (yetty_yclass_impl_t)ydoc_on_mouse_double_click},
        {"yetty_yrich", "document_apply_op",
         (yetty_yclass_method_id_t)yetty_yrich_document_apply_op,
         (yetty_yclass_impl_t)ydoc_apply_op},
        {"yetty_yrich", "document_render", (yetty_yclass_method_id_t)yetty_yrich_document_render,
         (yetty_yclass_impl_t)ydoc_render},
        {"yetty_yrich", "ydoc_toggle_format",
         (yetty_yclass_method_id_t)yetty_yrich_ydoc_toggle_format,
         (yetty_yclass_impl_t)ydoc_toggle_format_impl},
        {"yetty_yrich", "ydoc_set_text_color",
         (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_text_color,
         (yetty_yclass_impl_t)ydoc_set_text_color_impl},
        {"yetty_yrich", "ydoc_set_alignment",
         (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_alignment,
         (yetty_yclass_impl_t)ydoc_set_alignment_impl},
        {"yetty_yrich", "ydoc_set_heading", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_heading,
         (yetty_yclass_impl_t)ydoc_set_heading_impl},
        {"yetty_yrich", "ydoc_change_font_size",
         (yetty_yclass_method_id_t)yetty_yrich_ydoc_change_font_size,
         (yetty_yclass_impl_t)ydoc_change_font_size_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_ydoc_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_ydoc_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_ydoc_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_ydoc_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_ydoc_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yetty_yrich_ydoc_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yetty_yrich_ydoc_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yrich_ydoc_ptr, (struct yetty_yrich_ydoc *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_ydoc_to(struct yetty_yrich_ydoc *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_ydoc_class_get();
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

struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_object *obj,
                                                              uint32_t format_flag)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_ydoc_toggle_format);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_toggle_format: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_ydoc_toggle_format: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_ydoc_toggle_format: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t format_flag;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            format_flag};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_ydoc_toggle_format: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_toggle_format: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_toggle_format: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_ydoc_toggle_format: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_ydoc_toggle_format: dispatch_lookup failed");
        return ((yetty_yrich_ydoc_toggle_format_fn)dispatch_impl_r.value)(obj, format_flag);
    }
}

struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_object *obj,
                                                               uint32_t color)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_text_color);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_text_color: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_ydoc_set_text_color: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_ydoc_set_text_color: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t color;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            color};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_ydoc_set_text_color: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_text_color: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_text_color: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_ydoc_set_text_color: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_ydoc_set_text_color: dispatch_lookup failed");
        return ((yetty_yrich_ydoc_set_text_color_fn)dispatch_impl_r.value)(obj, color);
    }
}

struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_object *obj,
                                                              uint32_t halign)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_alignment);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_alignment: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_ydoc_set_alignment: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_ydoc_set_alignment: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t halign;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            halign};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_ydoc_set_alignment: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_alignment: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_alignment: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_ydoc_set_alignment: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_ydoc_set_alignment: dispatch_lookup failed");
        return ((yetty_yrich_ydoc_set_alignment_fn)dispatch_impl_r.value)(obj, halign);
    }
}

struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object *obj,
                                                            uint32_t level)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_heading);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_heading: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_ydoc_set_heading: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_ydoc_set_heading: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            uint32_t level;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            level};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_ydoc_set_heading: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yrich_ydoc_set_heading: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_set_heading: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_ydoc_set_heading: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_ydoc_set_heading: dispatch_lookup failed");
        return ((yetty_yrich_ydoc_set_heading_fn)dispatch_impl_r.value)(obj, level);
    }
}

struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object *obj,
                                                                 float delta)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yrich", (yetty_yclass_method_id_t)yetty_yrich_ydoc_change_font_size);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_change_font_size: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yrich_ydoc_change_font_size: NULL object");
    }

    if (obj->session) {
        struct uint32_result remote_id_r =
            yetty_yclass_rpc_session_ensure_remote_id(obj->session, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, remote_id_r,
                            "yetty_yrich_ydoc_change_font_size: ensure_remote_id failed");
        uint32_t remote_id = remote_id_r.value;
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
        struct {
            uint64_t obj_handle;
            float delta;
        } wire_args = {
            container_of((struct yetty_yclass_object *)obj, struct yetty_yclass_proxy, header)
                ->handle,
            delta};
#pragma pack(pop)
        uint8_t resp_buf[1];
        struct yetty_ycore_size_result rpc_call_r =
            yetty_yclass_rpc_call(obj->session, YETTY_YCLASS_RPC_OP_CALL, remote_id, &wire_args,
                                  sizeof(wire_args), resp_buf, sizeof(resp_buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rpc_call_r,
                            "yetty_yrich_ydoc_change_font_size: RPC call failed");
        size_t response_len = rpc_call_r.value;
        if (response_len < 1) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_change_font_size: short RPC response");
        }
        if (resp_buf[0] != 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yrich_ydoc_change_font_size: remote impl returned error");
        }
        return YETTY_OK_VOID();
    } else {
        struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                            "yetty_yrich_ydoc_change_font_size: object_class failed");
        struct yetty_yclass_impl_t_result dispatch_impl_r =
            yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                            "yetty_yrich_ydoc_change_font_size: dispatch_lookup failed");
        return ((yetty_yrich_ydoc_change_font_size_fn)dispatch_impl_r.value)(obj, delta);
    }
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_ydoc_toggle_format_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_toggle_format_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t format_flag;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_toggle_format: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_toggle_format(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.format_flag);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_toggle_format", call_r.error);
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
size_t yetty_yrich_ydoc_set_text_color_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_set_text_color_skel(const void *body, size_t body_len, void *resp,
                                            size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t color;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_text_color: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_text_color(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.color);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_text_color", call_r.error);
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
size_t yetty_yrich_ydoc_set_alignment_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_set_alignment_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t halign;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_alignment: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_alignment(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.halign);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_alignment", call_r.error);
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
size_t yetty_yrich_ydoc_set_heading_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_set_heading_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t level;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_heading: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_heading(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.level);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_heading", call_r.error);
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
size_t yetty_yrich_ydoc_change_font_size_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_change_font_size_skel(const void *body, size_t body_len, void *resp,
                                              size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float delta;
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
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_change_font_size: handle_resolve",
                                obj_resolve_r.error);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        if (resp_max < 1) {
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_change_font_size(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.delta);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_change_font_size", call_r.error);
        yetty_ycore_error_destroy(call_r.error);
        ((uint8_t *)resp)[0] = 1;
        return 1;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_paragraph");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_paragraph_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_paragraph_create: class accessor failed", class_accessor_r);
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
                             "yetty_yrich_paragraph_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_paragraph");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_yrich_paragraph_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_paragraph";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_paragraph_create: CREATE call failed", create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_paragraph_create: CREATE returned no/invalid handle");
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
                         "yetty_yrich_paragraph_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_inline_image");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_inline_image_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_inline_image_create: class accessor failed",
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
                             "yetty_yrich_inline_image_create: constructor failed", ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_inline_image");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_yrich_inline_image_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_inline_image";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_inline_image_create: CREATE call failed", create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_inline_image_create: CREATE returned no/invalid handle");
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
                         "yetty_yrich_inline_image_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_ydoc");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_ydoc_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_ydoc_create: class accessor failed",
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
            return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_ydoc_create: constructor failed",
                             ctor_r);
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yrich_ydoc");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr, "yetty_yrich_ydoc_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yrich_ydoc";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_ydoc_create: CREATE call failed",
                         create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_ydoc_create: CREATE returned no/invalid handle");
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
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yrich_ydoc_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
