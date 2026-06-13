/*
 * yrich:slides — slideshow document (+ its shape element kind).
 *
 * Two classes live in this TU:
 *   slides — parent@yrich:document; slide list + navigation
 *   shape  — parent@yrich:element; rectangle/ellipse/textbox/line/image
 *
 * Each slide (a plain value aggregate, see yrich-types.h) carries a flat
 * list of shape-object aliases — the document base's element list owns the
 * shapes. Rendering walks the current slide's shapes; the non-current
 * slides exist for navigation but aren't drawn.
 *
 * Navigation (set_current/next/prev) is a set of wire-marshallable slots,
 * so a deck can be driven over RPC / FFI bindings.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yrich/yrich-types.h>

#include <yetty/yrich/document.h>
#include <yetty/yrich/element.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>
#include <string.h>

/* Accessors / downcasts / factories defined in the appended slides.gen.c. */
YETTY_YRESULT_DECLARE(yetty_yrich_slides_ptr, struct yetty_yrich_slides *);
YETTY_YRESULT_DECLARE(yetty_yrich_shape_ptr, struct yetty_yrich_shape *);
struct yetty_yclass_ptr_result yetty_yrich_slides_class_get(void);
struct yetty_yclass_ptr_result yetty_yrich_shape_class_get(void);
struct yetty_yrich_slides_ptr_result yetty_yrich_slides_from(struct yetty_yclass_object *obj);
struct yetty_yrich_shape_ptr_result yetty_yrich_shape_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_shape_create(struct yetty_yclass_ctx *ctx);

/* Exposed helpers defined below, used before their definitions. */
struct yetty_yrich_slide_ptr_result yetty_yrich_slides_add_slide(struct yetty_yclass_object *obj);
struct yetty_yrich_slide *yetty_yrich_slides_slide_at(struct yetty_yclass_object *obj,
                                                      int32_t index);
struct yetty_ycore_void_result yetty_yrich_shape_set_text(struct yetty_yclass_object *obj,
                                                          const char *text, size_t len);

/*===========================================================================
 * Shape
 *=========================================================================*/

struct [[clang::annotate("class@yrich:shape")]] [[clang::annotate("parent@yrich:element")]]
yetty_yrich_shape {
    uint32_t kind; /* enum yetty_yrich_shape_kind */
    struct yetty_yrich_rect bounds;

    [[clang::annotate("property")]] uint32_t fill_color;
    [[clang::annotate("property")]] uint32_t stroke_color;
    [[clang::annotate("property")]] float stroke_width;
    [[clang::annotate("property")]] float rotation; /* degrees */
    [[clang::annotate("property")]] float corner_radius;

    /* Text-box only — kept here so a single shape data slice covers all
	 * shape kinds without needing per-kind subtypes. */
    char *text; /* owned, may be NULL */
    size_t text_len;
    struct yetty_yrich_text_style text_style;
    [[clang::annotate("property")]] uint32_t text_align;  /* enum yetty_yrich_halign */
    [[clang::annotate("property")]] uint32_t text_valign; /* enum yetty_yrich_valign */

    int editing;
    int32_t cursor_pos;
    int32_t sel_start;
    int32_t sel_end;

    /* Image only. */
    char *image_source; /* owned base64/path, may be NULL */
    int preserve_aspect;
};

/* Shape kinds — header-destined; the real build takes them from the
 * generated slides.h consumers include, this TU defines them below. */
#ifndef YCLASS_CODEGEN
enum yetty_yrich_shape_kind {
    YETTY_YRICH_SHAPE_RECTANGLE = 0,
    YETTY_YRICH_SHAPE_ELLIPSE,
    YETTY_YRICH_SHAPE_TEXTBOX,
    YETTY_YRICH_SHAPE_LINE,
    YETTY_YRICH_SHAPE_ARROW,
    YETTY_YRICH_SHAPE_IMAGE,
};
#endif

[[clang::annotate("override@yrich:shape:constructor")]]
static struct yetty_ycore_void_result shape_constructor(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ycore_void_result super_res =
        yetty_yrich_super_void(obj, yetty_yrich_shape_class_get().value,
                               (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "shape_constructor: super");
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_constructor: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    shape->fill_color = YETTY_YRICH_COLOR_WHITE;
    shape->stroke_color = YETTY_YRICH_COLOR_BLACK;
    shape->stroke_width = 1.0f;
    shape->text_style = yetty_yrich_text_style_default();
    shape->text_style.font_size = 24.0f;
    shape->text_align = YETTY_YRICH_HALIGN_CENTER;
    shape->text_valign = YETTY_YRICH_VALIGN_MIDDLE;
    shape->preserve_aspect = 1;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:shape:element_destroy")]]
static struct yetty_ycore_void_result shape_destroy(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_destroy: data_get");
    free(data_res.value->text);
    free(data_res.value->image_source);
    return yetty_yrich_super_void(obj, yetty_yrich_shape_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_element_destroy);
}

[[clang::annotate("override@yrich:shape:element_bounds")]]
static struct yetty_ycore_void_result shape_bounds(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj,
                                                   struct yetty_yrich_rect *out_bounds)
{
    (void)ctx;
    if (!out_bounds) {
        return YETTY_ERR(yetty_ycore_void, "shape_bounds: NULL out_bounds");
    }
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_bounds: data_get");
    *out_bounds = data_res.value->bounds;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:shape:element_is_editable")]]
static struct yetty_ycore_int_result shape_is_editable(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "shape_is_editable: data_get");
    return YETTY_OK(yetty_ycore_int, data_res.value->kind == YETTY_YRICH_SHAPE_TEXTBOX ? 1 : 0);
}

[[clang::annotate("override@yrich:shape:element_begin_edit")]]
static struct yetty_ycore_void_result shape_begin_edit(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_begin_edit: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    shape->editing = 1;
    shape->cursor_pos = (int32_t)shape->text_len;
    shape->sel_start = 0;
    shape->sel_end = (int32_t)shape->text_len;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:shape:element_end_edit")]]
static struct yetty_ycore_void_result shape_end_edit(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_end_edit: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    shape->editing = 0;
    shape->sel_start = shape->sel_end = shape->cursor_pos;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:shape:element_is_editing")]]
static struct yetty_ycore_int_result shape_is_editing(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "shape_is_editing: data_get");
    return YETTY_OK(yetty_ycore_int, data_res.value->editing);
}

static struct yetty_ycore_void_result emit_text(struct yetty_ydraw_drawable_list *drawable_list,
                                                const struct yetty_yrich_shape *shape,
                                                uint32_t layer)
{
    if (shape->text_len == 0 || !shape->text) {
        return YETTY_OK_VOID();
    }

    float text_w = (float)shape->text_len * shape->text_style.font_size * 0.6f;
    float text_x;
    switch (shape->text_align) {
    case YETTY_YRICH_HALIGN_LEFT:
        text_x = shape->bounds.x + 4.0f;
        break;
    case YETTY_YRICH_HALIGN_RIGHT:
        text_x = shape->bounds.x + shape->bounds.w - text_w - 4.0f;
        break;
    default: /* CENTER */
        text_x = shape->bounds.x + (shape->bounds.w - text_w) * 0.5f;
        break;
    }
    float text_y;
    switch (shape->text_valign) {
    case YETTY_YRICH_VALIGN_TOP:
        text_y = shape->bounds.y + shape->text_style.font_size;
        break;
    case YETTY_YRICH_VALIGN_BOTTOM:
        text_y = shape->bounds.y + shape->bounds.h - 4.0f;
        break;
    default: /* MIDDLE */
        text_y = shape->bounds.y + (shape->bounds.h + shape->text_style.font_size) * 0.5f;
        break;
    }

    struct yetty_ycore_buffer text = {
        .data = (uint8_t *)shape->text,
        .size = shape->text_len,
        .capacity = shape->text_len,
    };
    return yetty_ydraw_drawable_list_add_text(drawable_list, text_x, text_y, &text,
                                              shape->text_style.font_size, shape->text_style.color,
                                              layer + 1, shape->text_style.font_id, 0.0f);
}

[[clang::annotate("override@yrich:shape:element_render")]]
static struct yetty_ycore_void_result shape_render(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj,
                                                   struct yetty_ydraw_drawable_list *drawable_list,
                                                   uint32_t layer, int selected)
{
    (void)ctx;
    if (!drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "shape_render: NULL drawable list");
    }
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_render: data_get");
    struct yetty_yrich_shape *shape = data_res.value;

    float center_x = shape->bounds.x + shape->bounds.w * 0.5f;
    float center_y = shape->bounds.y + shape->bounds.h * 0.5f;

    switch (shape->kind) {
    case YETTY_YRICH_SHAPE_RECTANGLE:
    case YETTY_YRICH_SHAPE_TEXTBOX:
    case YETTY_YRICH_SHAPE_IMAGE: {
        struct yetty_ysdf_box body = {
            .center_x = center_x,
            .center_y = center_y,
            .half_width = shape->bounds.w * 0.5f,
            .half_height = shape->bounds.h * 0.5f,
            .corner_radius = shape->corner_radius,
        };
        struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            drawable_list, 0, layer, shape->fill_color, shape->stroke_color, shape->stroke_width,
            &body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "shape_render: rect add failed");
        if (shape->kind == YETTY_YRICH_SHAPE_TEXTBOX) {
            struct yetty_ycore_void_result text_res = emit_text(drawable_list, shape, layer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "shape_render: emit_text failed");
        }
        break;
    }
    case YETTY_YRICH_SHAPE_ELLIPSE: {
        struct yetty_ysdf_ellipse body = {
            .center_x = center_x,
            .center_y = center_y,
            .radius_x = shape->bounds.w * 0.5f,
            .radius_y = shape->bounds.h * 0.5f,
        };
        struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_ellipse(
            drawable_list, 0, layer, shape->fill_color, shape->stroke_color, shape->stroke_width,
            &body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "shape_render: ellipse add failed");
        break;
    }
    case YETTY_YRICH_SHAPE_LINE:
    case YETTY_YRICH_SHAPE_ARROW: {
        struct yetty_ysdf_segment segment = {
            .start_x = shape->bounds.x,
            .start_y = shape->bounds.y,
            .end_x = shape->bounds.x + shape->bounds.w,
            .end_y = shape->bounds.y + shape->bounds.h,
        };
        struct yetty_ycore_void_result segment_res = yetty_ydraw_drawable_list_add_cmd_add_segment(
            drawable_list, 0, layer, 0, shape->stroke_color, shape->stroke_width, &segment);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, segment_res, "shape_render: segment add failed");
        break;
    }
    default:
        break;
    }

    if (selected) {
        struct yetty_ysdf_box border = {
            .center_x = center_x,
            .center_y = center_y,
            .half_width = shape->bounds.w * 0.5f + 2.0f,
            .half_height = shape->bounds.h * 0.5f + 2.0f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result border_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            drawable_list, 0, layer + 4, 0, YETTY_YRICH_RGBA(0, 100, 200, 255), 1.5f, &border);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, border_res, "shape_render: selection box add failed");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:shape:element_insert_text")]]
static struct yetty_ycore_void_result shape_insert_text(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_buffer text)
{
    (void)ctx;
    if (!text.data || text.size == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_insert_text: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    if (shape->kind != YETTY_YRICH_SHAPE_TEXTBOX) {
        return YETTY_OK_VOID();
    }

    int32_t pos = shape->cursor_pos;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos > shape->text_len) {
        pos = (int32_t)shape->text_len;
    }

    size_t new_len = shape->text_len + text.size;
    char *new_buf = realloc(shape->text, new_len + 1);
    if (!new_buf) {
        return YETTY_ERR(yetty_ycore_void, "shape_insert_text: text grow failed");
    }
    memmove(new_buf + pos + text.size, new_buf + pos, shape->text_len - (size_t)pos);
    memcpy(new_buf + pos, text.data, text.size);
    new_buf[new_len] = '\0';
    shape->text = new_buf;
    shape->text_len = new_len;
    shape->cursor_pos = pos + (int32_t)text.size;
    shape->sel_start = shape->sel_end = shape->cursor_pos;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:shape:element_delete_sel")]]
static struct yetty_ycore_void_result shape_delete_sel(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_delete_sel: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    if (shape->kind != YETTY_YRICH_SHAPE_TEXTBOX || shape->text_len == 0) {
        return YETTY_OK_VOID();
    }
    if (shape->cursor_pos > 0) {
        memmove(shape->text + shape->cursor_pos - 1, shape->text + shape->cursor_pos,
                shape->text_len - (size_t)shape->cursor_pos);
        shape->text_len--;
        shape->text[shape->text_len] = '\0';
        shape->cursor_pos--;
        shape->sel_start = shape->sel_end = shape->cursor_pos;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_shape_set_text(struct yetty_yclass_object *obj,
                                                          const char *text, size_t len)
{
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_set_text: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    char *buf = malloc(len + 1);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "shape_set_text: text alloc failed");
    }
    if (len > 0) {
        memcpy(buf, text, len);
    }
    buf[len] = '\0';
    free(shape->text);
    shape->text = buf;
    shape->text_len = len;
    shape->cursor_pos = (int32_t)len;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_shape_set_font_size(struct yetty_yclass_object *obj,
                                                               float font_size)
{
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_set_font_size: data_get");
    if (font_size > 0.0f) {
        data_res.value->text_style.font_size = font_size;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_shape_set_text_color(struct yetty_yclass_object *obj,
                                                                uint32_t color)
{
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_set_text_color: data_get");
    data_res.value->text_style.color = color;
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_shape_set_image_source(struct yetty_yclass_object *obj,
                                                                  const char *source)
{
    struct yetty_yrich_shape_ptr_result data_res = yetty_yrich_shape_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "shape_set_image_source: data_get");
    struct yetty_yrich_shape *shape = data_res.value;
    char *copy = NULL;
    if (source) {
        size_t source_len = strlen(source);
        copy = malloc(source_len + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "shape_set_image_source: alloc failed");
        }
        memcpy(copy, source, source_len + 1);
    }
    free(shape->image_source);
    shape->image_source = copy;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Slides document
 *=========================================================================*/

struct [[clang::annotate("class@yrich:slides")]] [[clang::annotate("parent@yrich:document")]]
yetty_yrich_slides {
    float slide_width;
    float slide_height;

    struct yetty_yrich_slide **slides;
    size_t slide_count;
    size_t slide_capacity;

    int32_t current_slide;
};

static struct yetty_yrich_slide_ptr_result slide_create(int32_t index)
{
    struct yetty_yrich_slide *slide = calloc(1, sizeof(struct yetty_yrich_slide));
    if (!slide) {
        return YETTY_ERR(yetty_yrich_slide_ptr, "yrich slide alloc failed");
    }
    slide->index = index;
    slide->bg_color = YETTY_YRICH_COLOR_WHITE;
    return YETTY_OK(yetty_yrich_slide_ptr, slide);
}

static void slide_destroy(struct yetty_yrich_slide *slide)
{
    if (!slide) {
        return;
    }
    free(slide->shapes);
    free(slide);
}

static struct yetty_ycore_void_result slide_add_shape(struct yetty_yrich_slide *slide,
                                                      struct yetty_yclass_object *shape_obj)
{
    if (slide->shape_count == slide->shape_capacity) {
        size_t new_cap = slide->shape_capacity ? slide->shape_capacity * 2 : 8;
        struct yetty_yclass_object **new_arr = realloc(slide->shapes, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_ycore_void, "yrich slide: shape alias array grow failed");
        }
        slide->shapes = new_arr;
        slide->shape_capacity = new_cap;
    }
    slide->shapes[slide->shape_count++] = shape_obj;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:slides:constructor")]]
static struct yetty_ycore_void_result slides_constructor(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_ycore_void_result super_res =
        yetty_yrich_super_void(obj, yetty_yrich_slides_class_get().value,
                               (yetty_yclass_method_id_t)yetty_yrich_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, super_res, "slides_constructor: super");
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_constructor: data_get");
    data_res.value->slide_width = 960.0f;
    data_res.value->slide_height = 540.0f; /* 16:9 */
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yrich:slides:document_destroy")]]
static struct yetty_ycore_void_result slides_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_destroy: data_get");
    struct yetty_yrich_slides *slides = data_res.value;
    for (size_t i = 0; i < slides->slide_count; i++) {
        slide_destroy(slides->slides[i]);
    }
    free(slides->slides);
    /* The document base frees the shapes (elements) and the object. */
    return yetty_yrich_super_void(obj, yetty_yrich_slides_class_get().value,
                                  (yetty_yclass_method_id_t)yetty_yrich_document_destroy);
}

[[clang::annotate("override@yrich:slides:document_content_width")]]
static struct yetty_ycore_float_result slides_content_width(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "slides_content_width: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->slide_width);
}

[[clang::annotate("override@yrich:slides:document_content_height")]]
static struct yetty_ycore_float_result slides_content_height(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "slides_content_height: data_get");
    return YETTY_OK(yetty_ycore_float, data_res.value->slide_height);
}

[[clang::annotate("override@yrich:slides:document_render")]]
static struct yetty_ycore_void_result slides_render(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_render: data_get");
    struct yetty_yrich_slides *slides = data_res.value;
    struct yetty_ydraw_drawable_list *drawable_list = yetty_yrich_document_buffer(obj);
    if (!drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "slides_render: NULL buffer");
    }

    yetty_ydraw_drawable_list_clear(drawable_list);
    yetty_ydraw_drawable_list_set_scene_bounds(drawable_list, 0.0f, 0.0f, slides->slide_width,
                                               slides->slide_height);

    struct yetty_yrich_slide *slide = yetty_yrich_slides_slide_at(obj, slides->current_slide);
    if (!slide) {
        struct yetty_ycore_void_result clear_res = yetty_yrich_document_clear_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, clear_res, "slides_render: clear_dirty");
        return YETTY_OK_VOID();
    }

    /* Background. */
    struct yetty_ysdf_box background = {
        .center_x = slides->slide_width * 0.5f,
        .center_y = slides->slide_height * 0.5f,
        .half_width = slides->slide_width * 0.5f,
        .half_height = slides->slide_height * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result background_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        drawable_list, 0, 0, slide->bg_color, 0, 0.0f, &background);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, background_res, "slides_render: bg add failed");

    uint32_t layer = 1;
    for (size_t i = 0; i < slide->shape_count; i++) {
        struct yetty_yclass_object *shape_obj = slide->shapes[i];
        struct yetty_yrich_element_id_result id_res = yetty_yrich_element_id_value(shape_obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, id_res, "slides_render: shape id");
        int selected = yetty_yrich_document_is_selected(obj, id_res.value);
        struct yetty_ycore_void_result render_res =
            yetty_yrich_element_render(NULL, shape_obj, drawable_list, layer, selected);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "slides_render: shape failed");
        layer += 5; /* leave room for shape's selection layer */
    }

    return yetty_yrich_document_clear_dirty(obj);
}

/*===========================================================================
 * Slide management
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_slides_set_slide_size(struct yetty_yclass_object *obj,
                                                                 float width, float height)
{
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_set_slide_size: data_get");
    data_res.value->slide_width = width;
    data_res.value->slide_height = height;
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_yrich_slide_ptr_result yetty_yrich_slides_add_slide(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yrich_slide_ptr, data_res, "slides_add_slide: data_get");
    struct yetty_yrich_slides *slides = data_res.value;
    if (slides->slide_count == slides->slide_capacity) {
        size_t new_cap = slides->slide_capacity ? slides->slide_capacity * 2 : 4;
        struct yetty_yrich_slide **new_arr = realloc(slides->slides, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return YETTY_ERR(yetty_yrich_slide_ptr, "slides_add_slide: slide array grow failed");
        }
        slides->slides = new_arr;
        slides->slide_capacity = new_cap;
    }
    struct yetty_yrich_slide_ptr_result slide_res = slide_create((int32_t)slides->slide_count);
    YETTY_RETURN_IF_ERR(yetty_yrich_slide_ptr, slide_res, "slides_add_slide: slide create failed");
    slides->slides[slides->slide_count++] = slide_res.value;
    struct yetty_ycore_void_result dirty_res = yetty_yrich_document_mark_dirty(obj);
    if (YETTY_IS_ERR(dirty_res)) {
        yetty_ycore_error_destroy(dirty_res.error);
    }
    return YETTY_OK(yetty_yrich_slide_ptr, slide_res.value);
}

[[clang::annotate("expose")]]
struct yetty_yrich_slide *yetty_yrich_slides_slide_at(struct yetty_yclass_object *obj,
                                                      int32_t index)
{
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return NULL;
    }
    struct yetty_yrich_slides *slides = data_res.value;
    if (index < 0 || (size_t)index >= slides->slide_count) {
        return NULL;
    }
    return slides->slides[index];
}

[[clang::annotate("expose")]]
int32_t yetty_yrich_slides_current(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return 0;
    }
    return data_res.value->current_slide;
}

/*===========================================================================
 * Navigation slots — scalar args, wire-marshallable.
 *=========================================================================*/

[[clang::annotate("virtual@yrich:slides:slides_set_current")]]
static struct yetty_ycore_void_result slides_set_current(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         int32_t index)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_set_current: data_get");
    struct yetty_yrich_slides *slides = data_res.value;
    if (index < 0) {
        index = 0;
    }
    if ((size_t)index >= slides->slide_count && slides->slide_count > 0) {
        index = (int32_t)slides->slide_count - 1;
    }
    slides->current_slide = index;
    return yetty_yrich_document_mark_dirty(obj);
}

[[clang::annotate("virtual@yrich:slides:slides_next")]]
static struct yetty_ycore_void_result slides_next(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_next: data_get");
    return slides_set_current(NULL, obj, data_res.value->current_slide + 1);
}

[[clang::annotate("virtual@yrich:slides:slides_prev")]]
static struct yetty_ycore_void_result slides_prev(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "slides_prev: data_get");
    return slides_set_current(NULL, obj, data_res.value->current_slide - 1);
}

/*===========================================================================
 * Shape creation on the current slide. The document base takes ownership
 * of the new shape (via its element list); the slide stores an alias.
 *=========================================================================*/

static struct yetty_yclass_object_ptr_result add_shape_to_current(struct yetty_yclass_object *obj,
                                                                  uint32_t kind,
                                                                  struct yetty_yrich_rect bounds)
{
    struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, data_res, "yrich slides add: data_get");
    struct yetty_yrich_slides *slides = data_res.value;

    struct yetty_yrich_slide *slide = yetty_yrich_slides_slide_at(obj, slides->current_slide);
    if (!slide) {
        struct yetty_yrich_slide_ptr_result slide_res = yetty_yrich_slides_add_slide(obj);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, slide_res,
                            "yrich slides add: no current slide");
        slide = slide_res.value;
    }

    struct yetty_yclass_object_ptr_result create_res = yetty_yrich_shape_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, create_res, "yrich slides add: create");
    struct yetty_yclass_object *shape_obj = create_res.value;
    struct yetty_yrich_shape_ptr_result shape_res = yetty_yrich_shape_from(shape_obj);
    if (YETTY_IS_ERR(shape_res)) {
        struct yetty_ycore_void_result destroy_res = yetty_yrich_element_destroy(NULL, shape_obj);
        if (YETTY_IS_ERR(destroy_res)) {
            yetty_ycore_error_destroy(destroy_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich slides add: data_get", shape_res);
    }
    shape_res.value->kind = kind;
    shape_res.value->bounds = bounds;

    struct yetty_yrich_element_id_result id_res = yetty_yrich_document_next_id(obj);
    if (YETTY_IS_OK(id_res)) {
        struct yetty_ycore_void_result set_id_res =
            yetty_yrich_element_set_id(shape_obj, id_res.value);
        if (YETTY_IS_ERR(set_id_res)) {
            yetty_ycore_error_destroy(set_id_res.error);
        }
    } else {
        yetty_ycore_error_destroy(id_res.error);
    }

    struct yetty_ycore_void_result add_res = yetty_yrich_document_add_element(obj, shape_obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, add_res, "yrich slides add: add_element");

    struct yetty_ycore_void_result alias_res = slide_add_shape(slide, shape_obj);
    if (YETTY_IS_ERR(alias_res)) {
        /* The element list owns the shape; without the alias entry the
		 * slide would never render it, so unwind and surface the failure. */
        struct yetty_yrich_element_id_result unwind_id_res =
            yetty_yrich_element_id_value(shape_obj);
        if (YETTY_IS_OK(unwind_id_res)) {
            struct yetty_ycore_void_result remove_res =
                yetty_yrich_document_remove_element(obj, unwind_id_res.value);
            if (YETTY_IS_ERR(remove_res)) {
                yetty_ycore_error_destroy(remove_res.error);
            }
        } else {
            yetty_ycore_error_destroy(unwind_id_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yrich slides add: alias push failed", alias_res);
    }
    return YETTY_OK(yetty_yclass_object_ptr, shape_obj);
}

[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_rectangle(
    struct yetty_yclass_object *obj, float x, float y, float width, float height)
{
    struct yetty_yrich_rect bounds = {x, y, width, height};
    return add_shape_to_current(obj, YETTY_YRICH_SHAPE_RECTANGLE, bounds);
}

[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_ellipse(
    struct yetty_yclass_object *obj, float x, float y, float width, float height)
{
    struct yetty_yrich_rect bounds = {x, y, width, height};
    return add_shape_to_current(obj, YETTY_YRICH_SHAPE_ELLIPSE, bounds);
}

[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_textbox(
    struct yetty_yclass_object *obj, float x, float y, float width, float height, const char *text,
    size_t text_len)
{
    struct yetty_yrich_rect bounds = {x, y, width, height};
    struct yetty_yclass_object_ptr_result shape_res =
        add_shape_to_current(obj, YETTY_YRICH_SHAPE_TEXTBOX, bounds);
    if (YETTY_IS_OK(shape_res) && text && text_len > 0) {
        struct yetty_ycore_void_result text_res =
            yetty_yrich_shape_set_text(shape_res.value, text, text_len);
        if (YETTY_IS_ERR(text_res)) {
            /* Unwind: drop the freshly appended slide alias before the
			 * element (and the shape behind it) is destroyed. */
            struct yetty_yrich_slides_ptr_result data_res = yetty_yrich_slides_from(obj);
            if (YETTY_IS_OK(data_res)) {
                struct yetty_yrich_slide *slide =
                    yetty_yrich_slides_slide_at(obj, data_res.value->current_slide);
                if (slide && slide->shape_count > 0 &&
                    slide->shapes[slide->shape_count - 1] == shape_res.value) {
                    slide->shape_count--;
                }
            } else {
                yetty_ycore_error_destroy(data_res.error);
            }
            struct yetty_yrich_element_id_result id_res =
                yetty_yrich_element_id_value(shape_res.value);
            if (YETTY_IS_OK(id_res)) {
                struct yetty_ycore_void_result remove_res =
                    yetty_yrich_document_remove_element(obj, id_res.value);
                if (YETTY_IS_ERR(remove_res)) {
                    yetty_ycore_error_destroy(remove_res.error);
                }
            } else {
                yetty_ycore_error_destroy(id_res.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr, "slides_add_textbox: set_text failed",
                             text_res);
        }
    }
    return shape_res;
}

[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_line(struct yetty_yclass_object *obj,
                                                                  float x1, float y1, float x2,
                                                                  float y2)
{
    struct yetty_yrich_rect bounds = {x1, y1, x2 - x1, y2 - y1};
    return add_shape_to_current(obj, YETTY_YRICH_SHAPE_LINE, bounds);
}

[[clang::annotate("expose")]]
struct yetty_yclass_object_ptr_result yetty_yrich_slides_add_image(struct yetty_yclass_object *obj,
                                                                   float x, float y, float width,
                                                                   float height)
{
    struct yetty_yrich_rect bounds = {x, y, width, height};
    return add_shape_to_current(obj, YETTY_YRICH_SHAPE_IMAGE, bounds);
}

#ifdef YCLASS_CODEGEN
/* Header-destined content for the generated slides.h (skipped by the real
 * build, which defines the enum in the #ifndef block above). The types
 * include carries the slide aggregate + its Result the slide-management
 * API above references. */
#include <yetty/yrich/yrich-types.h>

enum yetty_yrich_shape_kind {
    YETTY_YRICH_SHAPE_RECTANGLE = 0,
    YETTY_YRICH_SHAPE_ELLIPSE,
    YETTY_YRICH_SHAPE_TEXTBOX,
    YETTY_YRICH_SHAPE_LINE,
    YETTY_YRICH_SHAPE_ARROW,
    YETTY_YRICH_SHAPE_IMAGE,
};
#endif

#include "slides.gen.c"
