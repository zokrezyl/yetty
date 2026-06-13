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
struct yetty_yrich_operation;
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
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x, float y,
    uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_operation *op,
                                                             int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_ydoc_toggle_format(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              uint32_t format_flag);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_text_color(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               uint32_t color);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_alignment(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              uint32_t halign);
struct yetty_ycore_void_result yetty_yrich_ydoc_set_heading(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            uint32_t level);
struct yetty_ycore_void_result yetty_yrich_ydoc_change_font_size(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj,
                                                                 float delta);
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
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_toggle_format_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_text_color_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_alignment_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_set_heading_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_ydoc_change_font_size_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float);

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
