/* GENERATED — do not edit. */
#include "yetty/gen/impl/yrich/document.h"
#include "yetty/gen/impl/yrich/element.h"
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

struct yetty_ycore_float_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_operation;
struct yetty_yrich_rect;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object * obj, struct yetty_yrich_rect * out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_render(struct yetty_yclass_object * obj, struct yetty_ydraw_drawable_list * drawable_list, uint32_t layer, int selected);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object * obj, struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_object * obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_object * obj, uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_object * obj, struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(struct yetty_yclass_object * obj, float x, float y, uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_object * obj, struct yetty_yrich_operation * op, int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_object * obj, uint32_t format_flag);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_object * obj, uint32_t color);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_object * obj, uint32_t halign);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_line_spacing(struct yetty_yclass_object * obj, float spacing);
struct yetty_ycore_void_result yetty_yrich_ydoc_adjust_indent(struct yetty_yclass_object * obj, int32_t direction);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_highlight(struct yetty_yclass_object * obj, uint32_t bg_color);
struct yetty_ycore_void_result yetty_yrich_ydoc_clear_format(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_object * obj, uint32_t level);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_object * obj, float delta);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_font_size(struct yetty_yclass_object * obj, float size);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_toggle_format_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_text_color_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_alignment_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_line_spacing_fn)(struct yetty_yclass_object *, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_adjust_indent_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_highlight_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_clear_format_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_heading_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_change_font_size_fn)(struct yetty_yclass_object *, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_font_size_fn)(struct yetty_yclass_object *, float);

YETTY_MAYBE_UNUSED
static yetty_yrich_constructor_fn yetty_yrich_paragraph_yetty_yrich_constructor_check = paragraph_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_destroy_fn yetty_yrich_paragraph_yetty_yrich_element_destroy_check = paragraph_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_bounds_fn yetty_yrich_paragraph_yetty_yrich_element_bounds_check = paragraph_bounds;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_is_editable_fn yetty_yrich_paragraph_yetty_yrich_element_is_editable_check = paragraph_is_editable;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_begin_edit_fn yetty_yrich_paragraph_yetty_yrich_element_begin_edit_check = paragraph_begin_edit;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_end_edit_fn yetty_yrich_paragraph_yetty_yrich_element_end_edit_check = paragraph_end_edit;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_is_editing_fn yetty_yrich_paragraph_yetty_yrich_element_is_editing_check = paragraph_is_editing;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_render_fn yetty_yrich_paragraph_yetty_yrich_element_render_check = paragraph_render;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_insert_text_fn yetty_yrich_paragraph_yetty_yrich_element_insert_text_check = paragraph_insert_text;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_delete_sel_fn yetty_yrich_paragraph_yetty_yrich_element_delete_sel_check = paragraph_delete_sel;

struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yrich_paragraph");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_paragraph",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_paragraph),
        .data_align = _Alignof(struct yetty_yrich_paragraph),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor, (yetty_yclass_impl_t)paragraph_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy, (yetty_yclass_impl_t)paragraph_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds, (yetty_yclass_impl_t)paragraph_bounds},
        {"yetty_yrich", "element_is_editable", (yetty_yclass_method_id_t)yetty_yrich_element_is_editable, (yetty_yclass_impl_t)paragraph_is_editable},
        {"yetty_yrich", "element_begin_edit", (yetty_yclass_method_id_t)yetty_yrich_element_begin_edit, (yetty_yclass_impl_t)paragraph_begin_edit},
        {"yetty_yrich", "element_end_edit", (yetty_yclass_method_id_t)yetty_yrich_element_end_edit, (yetty_yclass_impl_t)paragraph_end_edit},
        {"yetty_yrich", "element_is_editing", (yetty_yclass_method_id_t)yetty_yrich_element_is_editing, (yetty_yclass_impl_t)paragraph_is_editing},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render, (yetty_yclass_impl_t)paragraph_render},
        {"yetty_yrich", "element_insert_text", (yetty_yclass_method_id_t)yetty_yrich_element_insert_text, (yetty_yclass_impl_t)paragraph_insert_text},
        {"yetty_yrich", "element_delete_sel", (yetty_yclass_method_id_t)yetty_yrich_element_delete_sel, (yetty_yclass_impl_t)paragraph_delete_sel},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_paragraph_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_paragraph_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_paragraph_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_paragraph_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_paragraph_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yrich_paragraph_ptr, "yetty_yrich_paragraph_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yrich_paragraph_ptr, "yetty_yrich_paragraph_from: object_data", slice_r);
    return YETTY_OK(yetty_yrich_paragraph_ptr, (struct yetty_yrich_paragraph *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_to(struct yetty_yrich_paragraph *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yrich_paragraph_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yrich_paragraph_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yrich_paragraph_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_yrich_constructor_fn yetty_yrich_inline_image_yetty_yrich_constructor_check = inline_image_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_destroy_fn yetty_yrich_inline_image_yetty_yrich_element_destroy_check = inline_image_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_bounds_fn yetty_yrich_inline_image_yetty_yrich_element_bounds_check = inline_image_bounds;
YETTY_MAYBE_UNUSED
static yetty_yrich_element_render_fn yetty_yrich_inline_image_yetty_yrich_element_render_check = inline_image_render;

struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yrich_inline_image");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_inline_image",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_inline_image),
        .data_align = _Alignof(struct yetty_yrich_inline_image),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor, (yetty_yclass_impl_t)inline_image_constructor},
        {"yetty_yrich", "element_destroy", (yetty_yclass_method_id_t)yetty_yrich_element_destroy, (yetty_yclass_impl_t)inline_image_destroy},
        {"yetty_yrich", "element_bounds", (yetty_yclass_method_id_t)yetty_yrich_element_bounds, (yetty_yclass_impl_t)inline_image_bounds},
        {"yetty_yrich", "element_render", (yetty_yclass_method_id_t)yetty_yrich_element_render, (yetty_yclass_impl_t)inline_image_render},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_element_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_inline_image_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_inline_image_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_inline_image_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_inline_image_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_inline_image_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yrich_inline_image_ptr, "yetty_yrich_inline_image_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yrich_inline_image_ptr, "yetty_yrich_inline_image_from: object_data", slice_r);
    return YETTY_OK(yetty_yrich_inline_image_ptr, (struct yetty_yrich_inline_image *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_to(struct yetty_yrich_inline_image *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yrich_inline_image_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yrich_inline_image_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yrich_inline_image_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_yrich_constructor_fn yetty_yrich_ydoc_yetty_yrich_constructor_check = ydoc_constructor;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_destroy_fn yetty_yrich_ydoc_yetty_yrich_document_destroy_check = ydoc_destroy;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_width_fn yetty_yrich_ydoc_yetty_yrich_document_content_width_check = ydoc_content_width;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_content_height_fn yetty_yrich_ydoc_yetty_yrich_document_content_height_check = ydoc_content_height;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_down_fn yetty_yrich_ydoc_yetty_yrich_document_on_mouse_down_check = ydoc_on_mouse_down;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_drag_fn yetty_yrich_ydoc_yetty_yrich_document_on_mouse_drag_check = ydoc_on_mouse_drag;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_key_down_fn yetty_yrich_ydoc_yetty_yrich_document_on_key_down_check = ydoc_on_key_down;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_text_input_fn yetty_yrich_ydoc_yetty_yrich_document_on_text_input_check = ydoc_on_text_input;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_on_mouse_double_click_fn yetty_yrich_ydoc_yetty_yrich_document_on_mouse_double_click_check = ydoc_on_mouse_double_click;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_apply_op_fn yetty_yrich_ydoc_yetty_yrich_document_apply_op_check = ydoc_apply_op;
YETTY_MAYBE_UNUSED
static yetty_yrich_document_render_fn yetty_yrich_ydoc_yetty_yrich_document_render_check = ydoc_render;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_toggle_format_fn yetty_yrich_ydoc_yetty_yrich_ydoc_toggle_format_check = ydoc_toggle_format_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_set_text_color_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_text_color_check = ydoc_set_text_color_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_set_alignment_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_alignment_check = ydoc_set_alignment_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_set_line_spacing_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_line_spacing_check = ydoc_set_line_spacing_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_adjust_indent_fn yetty_yrich_ydoc_yetty_yrich_ydoc_adjust_indent_check = ydoc_adjust_indent_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_set_highlight_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_highlight_check = ydoc_set_highlight_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_clear_format_fn yetty_yrich_ydoc_yetty_yrich_ydoc_clear_format_check = ydoc_clear_format_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_set_heading_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_heading_check = ydoc_set_heading_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_change_font_size_fn yetty_yrich_ydoc_yetty_yrich_ydoc_change_font_size_check = ydoc_change_font_size_impl;
YETTY_MAYBE_UNUSED
static yetty_yrich_ydoc_set_font_size_fn yetty_yrich_ydoc_yetty_yrich_ydoc_set_font_size_check = ydoc_set_font_size_impl;

struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yrich_ydoc");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_ydoc",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_ydoc),
        .data_align = _Alignof(struct yetty_yrich_ydoc),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor, (yetty_yclass_impl_t)ydoc_constructor},
        {"yetty_yrich", "document_destroy", (yetty_yclass_method_id_t)yetty_yrich_document_destroy, (yetty_yclass_impl_t)ydoc_destroy},
        {"yetty_yrich", "document_content_width", (yetty_yclass_method_id_t)yetty_yrich_document_content_width, (yetty_yclass_impl_t)ydoc_content_width},
        {"yetty_yrich", "document_content_height", (yetty_yclass_method_id_t)yetty_yrich_document_content_height, (yetty_yclass_impl_t)ydoc_content_height},
        {"yetty_yrich", "document_on_mouse_down", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_down, (yetty_yclass_impl_t)ydoc_on_mouse_down},
        {"yetty_yrich", "document_on_mouse_drag", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_drag, (yetty_yclass_impl_t)ydoc_on_mouse_drag},
        {"yetty_yrich", "document_on_key_down", (yetty_yclass_method_id_t)yetty_yrich_document_on_key_down, (yetty_yclass_impl_t)ydoc_on_key_down},
        {"yetty_yrich", "document_on_text_input", (yetty_yclass_method_id_t)yetty_yrich_document_on_text_input, (yetty_yclass_impl_t)ydoc_on_text_input},
        {"yetty_yrich", "document_on_mouse_double_click", (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_double_click, (yetty_yclass_impl_t)ydoc_on_mouse_double_click},
        {"yetty_yrich", "document_apply_op", (yetty_yclass_method_id_t)yetty_yrich_document_apply_op, (yetty_yclass_impl_t)ydoc_apply_op},
        {"yetty_yrich", "document_render", (yetty_yclass_method_id_t)yetty_yrich_document_render, (yetty_yclass_impl_t)ydoc_render},
        {"yetty_yrich", "ydoc_toggle_format", (yetty_yclass_method_id_t)yetty_yrich_ydoc_toggle_format, (yetty_yclass_impl_t)ydoc_toggle_format_impl},
        {"yetty_yrich", "ydoc_set_text_color", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_text_color, (yetty_yclass_impl_t)ydoc_set_text_color_impl},
        {"yetty_yrich", "ydoc_set_alignment", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_alignment, (yetty_yclass_impl_t)ydoc_set_alignment_impl},
        {"yetty_yrich", "ydoc_set_line_spacing", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_line_spacing, (yetty_yclass_impl_t)ydoc_set_line_spacing_impl},
        {"yetty_yrich", "ydoc_adjust_indent", (yetty_yclass_method_id_t)yetty_yrich_ydoc_adjust_indent, (yetty_yclass_impl_t)ydoc_adjust_indent_impl},
        {"yetty_yrich", "ydoc_set_highlight", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_highlight, (yetty_yclass_impl_t)ydoc_set_highlight_impl},
        {"yetty_yrich", "ydoc_clear_format", (yetty_yclass_method_id_t)yetty_yrich_ydoc_clear_format, (yetty_yclass_impl_t)ydoc_clear_format_impl},
        {"yetty_yrich", "ydoc_set_heading", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_heading, (yetty_yclass_impl_t)ydoc_set_heading_impl},
        {"yetty_yrich", "ydoc_change_font_size", (yetty_yclass_method_id_t)yetty_yrich_ydoc_change_font_size, (yetty_yclass_impl_t)ydoc_change_font_size_impl},
        {"yetty_yrich", "ydoc_set_font_size", (yetty_yclass_method_id_t)yetty_yrich_ydoc_set_font_size, (yetty_yclass_impl_t)ydoc_set_font_size_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yrich_ydoc_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_ydoc_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_ydoc_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_ydoc_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_ydoc_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yetty_yrich_ydoc_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yetty_yrich_ydoc_from: object_data", slice_r);
    return YETTY_OK(yetty_yrich_ydoc_ptr, (struct yetty_yrich_ydoc *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_to(struct yetty_yrich_ydoc *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yrich_ydoc_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yrich_ydoc_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yrich_ydoc_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yrich_ydoc_toggle_format_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_toggle_format_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
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
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_toggle_format: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_toggle_format((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.format_flag);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_toggle_format", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_set_text_color_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
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
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_set_text_color: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_text_color((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.color);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_text_color", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_set_alignment_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
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
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_set_alignment: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_alignment((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.halign);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_alignment", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_set_line_spacing_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_set_line_spacing_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float spacing;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_set_line_spacing: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_line_spacing((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.spacing);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_line_spacing", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_adjust_indent_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_adjust_indent_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        int32_t direction;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_adjust_indent: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_adjust_indent((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.direction);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_adjust_indent", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_set_highlight_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_set_highlight_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t bg_color;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_set_highlight: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_highlight((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.bg_color);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_highlight", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_clear_format_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_clear_format_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
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
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_clear_format: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_clear_format((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_clear_format", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_set_heading_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
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
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_set_heading: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_heading((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.level);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_heading", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_change_font_size_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
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
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_change_font_size: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_change_font_size((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.delta);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_change_font_size", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
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
size_t yetty_yrich_ydoc_set_font_size_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yrich_ydoc_set_font_size_skel(const void *body, size_t body_len,
                   void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        float size;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) return 0;
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
            "[skel] yetty_yrich_ydoc_set_font_size: handle_resolve", obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yrich_ydoc_set_font_size((struct yetty_yclass_object *)obj_resolve_r.value, wire_args.size);
    if (resp_max < 1) return 0;
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yrich_ydoc_set_font_size", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_paragraph_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_paragraph");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_paragraph_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_paragraph_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_paragraph_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r =
        yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        struct yetty_ycore_void_result ctor_r =
            yetty_yrich_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r =
                yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) yetty_ycore_error_destroy(free_r.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yrich_paragraph_create: constructor failed", ctor_r);
        }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_inline_image_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_inline_image");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_inline_image_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_inline_image_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_inline_image_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r =
        yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        struct yetty_ycore_void_result ctor_r =
            yetty_yrich_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r =
                yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) yetty_ycore_error_destroy(free_r.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yrich_inline_image_create: constructor failed", ctor_r);
        }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yrich_ydoc_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yrich_ydoc");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_ydoc_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yrich_ydoc_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yrich_ydoc_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r =
        yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        struct yetty_ycore_void_result ctor_r =
            yetty_yrich_constructor(alloc_r.value);
        if (YETTY_IS_ERR(ctor_r)) {
            struct yetty_ycore_void_result free_r =
                yetty_yclass_object_free(alloc_r.value);
            if (YETTY_IS_ERR(free_r)) yetty_ycore_error_destroy(free_r.error);
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yrich_ydoc_create: constructor failed", ctor_r);
        }
    return alloc_r;
}


/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yrich_paragraph_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_inline_image_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_ydoc_class_get(void);
size_t yetty_yrich_ydoc_toggle_format_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_text_color_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_alignment_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_line_spacing_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_adjust_indent_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_highlight_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_clear_format_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_heading_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_change_font_size_skel(const void *, size_t, void *, size_t);
size_t yetty_yrich_ydoc_set_font_size_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yrich_ydoc_register(void);

/* ---- yrich_ydoc: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yrich_ydoc_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yrich_paragraph") == 0)
        return yetty_yrich_paragraph_class_get();
    if (strcmp(name, "yetty_yrich_inline_image") == 0)
        return yetty_yrich_inline_image_class_get();
    if (strcmp(name, "yetty_yrich_ydoc") == 0)
        return yetty_yrich_ydoc_class_get();
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yrich_ydoc: slot -> skel, name-keyed static data --------------- */

struct yetty_yrich_ydoc_skel_row { const char *name; yetty_yclass_rpc_skel_fn fn; };

static const struct yetty_yrich_ydoc_skel_row yetty_yrich_ydoc_skel_rows[] = {
    {"yetty_yrich_ydoc_toggle_format", yetty_yrich_ydoc_toggle_format_skel},
    {"yetty_yrich_ydoc_set_text_color", yetty_yrich_ydoc_set_text_color_skel},
    {"yetty_yrich_ydoc_set_alignment", yetty_yrich_ydoc_set_alignment_skel},
    {"yetty_yrich_ydoc_set_line_spacing", yetty_yrich_ydoc_set_line_spacing_skel},
    {"yetty_yrich_ydoc_adjust_indent", yetty_yrich_ydoc_adjust_indent_skel},
    {"yetty_yrich_ydoc_set_highlight", yetty_yrich_ydoc_set_highlight_skel},
    {"yetty_yrich_ydoc_clear_format", yetty_yrich_ydoc_clear_format_skel},
    {"yetty_yrich_ydoc_set_heading", yetty_yrich_ydoc_set_heading_skel},
    {"yetty_yrich_ydoc_change_font_size", yetty_yrich_ydoc_change_font_size_skel},
    {"yetty_yrich_ydoc_set_font_size", yetty_yrich_ydoc_set_font_size_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yrich_ydoc_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) { yetty_ycore_error_destroy(slot_name_r.error); return NULL; }
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_yrich_ydoc_skel_rows) / sizeof(yetty_yrich_ydoc_skel_rows[0]); ++i)
        if (strcmp(yetty_yrich_ydoc_skel_rows[i].name, name) == 0)
            return yetty_yrich_ydoc_skel_rows[i].fn;
    return NULL;
}

/* ---- yrich_ydoc: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yrich_ydoc_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yrich_ydoc_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yrich_ydoc_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yrich_ydoc_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yrich_ydoc_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}
