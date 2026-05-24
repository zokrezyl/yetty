/*
 * yslides.c — slideshow document.
 *
 * Each slide is a flat list of shape pointers (aliasing — the document owns
 * the actual element). Rendering walks the current slide's shapes; the
 * non-current slides exist for navigation but aren't drawn.
 */

#include <yetty/yrich/yslides.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>

#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Shape vtable
 *===========================================================================*/

static struct yetty_yrich_rect shape_bounds(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_shape *s = (const struct yetty_yrich_shape *)e;
    return s->bounds;
}

static void shape_destroy(struct yetty_yrich_element *e)
{
    struct yetty_yrich_shape *s = (struct yetty_yrich_shape *)e;
    free(s->text);
    free(s->image_source);
    free(s);
}

static bool shape_is_editable(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_shape *s = (const struct yetty_yrich_shape *)e;
    return s->kind == YETTY_YRICH_SHAPE_TEXTBOX;
}

static void shape_begin_edit(struct yetty_yrich_element *e)
{
    struct yetty_yrich_shape *s = (struct yetty_yrich_shape *)e;
    s->editing = true;
    s->cursor_pos = (int32_t)s->text_len;
    s->sel_start = 0;
    s->sel_end = (int32_t)s->text_len;
}

static void shape_end_edit(struct yetty_yrich_element *e)
{
    struct yetty_yrich_shape *s = (struct yetty_yrich_shape *)e;
    s->editing = false;
    s->sel_start = s->sel_end = s->cursor_pos;
}

static bool shape_is_editing(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_shape *s = (const struct yetty_yrich_shape *)e;
    return s->editing;
}

static struct yetty_ycore_void_result emit_text(struct yetty_ydraw_draw_list *buf,
                                                const struct yetty_yrich_shape *s, uint32_t layer)
{
    if (s->text_len == 0 || !s->text) {
        return YETTY_OK_VOID();
    }

    float text_w = (float)s->text_len * s->text_style.font_size * 0.6f;
    float text_x;
    switch (s->text_align) {
    case YETTY_YRICH_HALIGN_LEFT:
        text_x = s->bounds.x + 4.0f;
        break;
    case YETTY_YRICH_HALIGN_RIGHT:
        text_x = s->bounds.x + s->bounds.w - text_w - 4.0f;
        break;
    default: /* CENTER */
        text_x = s->bounds.x + (s->bounds.w - text_w) * 0.5f;
        break;
    }
    float text_y;
    switch (s->text_valign) {
    case YETTY_YRICH_VALIGN_TOP:
        text_y = s->bounds.y + s->text_style.font_size;
        break;
    case YETTY_YRICH_VALIGN_BOTTOM:
        text_y = s->bounds.y + s->bounds.h - 4.0f;
        break;
    default: /* MIDDLE */
        text_y = s->bounds.y + (s->bounds.h + s->text_style.font_size) * 0.5f;
        break;
    }

    struct yetty_ycore_buffer text = {
        .data = (uint8_t *)s->text,
        .size = s->text_len,
        .capacity = s->text_len,
    };
    return yetty_ydraw_draw_list_add_text(buf, text_x, text_y, &text, s->text_style.font_size,
                                          s->text_style.color, layer + 1, s->text_style.font_id,
                                          0.0f);
}

static struct yetty_ycore_void_result shape_render(struct yetty_yrich_element *e,
                                                   struct yetty_ydraw_draw_list *buf,
                                                   uint32_t layer, bool selected)
{
    struct yetty_yrich_shape *s = (struct yetty_yrich_shape *)e;
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "shape_render: NULL buffer");
    }

    float cx = s->bounds.x + s->bounds.w * 0.5f;
    float cy = s->bounds.y + s->bounds.h * 0.5f;

    switch (s->kind) {
    case YETTY_YRICH_SHAPE_RECTANGLE:
    case YETTY_YRICH_SHAPE_TEXTBOX:
    case YETTY_YRICH_SHAPE_IMAGE: {
        struct yetty_ysdf_box body = {
            .center_x = cx,
            .center_y = cy,
            .half_width = s->bounds.w * 0.5f,
            .half_height = s->bounds.h * 0.5f,
            .corner_radius = s->corner_radius,
        };
        struct yetty_ycore_void_result idr = yetty_ydraw_draw_list_add_cmd_add_box(
            buf, 0, layer, s->fill_color, s->stroke_color, s->stroke_width, &body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, idr, "shape_render: rect add failed");
        if (s->kind == YETTY_YRICH_SHAPE_TEXTBOX) {
            struct yetty_ycore_void_result tr = emit_text(buf, s, layer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "shape_render: emit_text failed");
        }
        break;
    }
    case YETTY_YRICH_SHAPE_ELLIPSE: {
        struct yetty_ysdf_ellipse body = {
            .center_x = cx,
            .center_y = cy,
            .radius_x = s->bounds.w * 0.5f,
            .radius_y = s->bounds.h * 0.5f,
        };
        struct yetty_ycore_void_result idr = yetty_ydraw_draw_list_add_cmd_add_ellipse(
            buf, 0, layer, s->fill_color, s->stroke_color, s->stroke_width, &body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, idr, "shape_render: ellipse add failed");
        break;
    }
    case YETTY_YRICH_SHAPE_LINE:
    case YETTY_YRICH_SHAPE_ARROW: {
        struct yetty_ysdf_segment seg = {
            .start_x = s->bounds.x,
            .start_y = s->bounds.y,
            .end_x = s->bounds.x + s->bounds.w,
            .end_y = s->bounds.y + s->bounds.h,
        };
        struct yetty_ycore_void_result idr = yetty_ydraw_draw_list_add_cmd_add_segment(
            buf, 0, layer, 0, s->stroke_color, s->stroke_width, &seg);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, idr, "shape_render: segment add failed");
        break;
    }
    default:
        break;
    }

    if (selected) {
        struct yetty_ysdf_box border = {
            .center_x = cx,
            .center_y = cy,
            .half_width = s->bounds.w * 0.5f + 2.0f,
            .half_height = s->bounds.h * 0.5f + 2.0f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result idr = yetty_ydraw_draw_list_add_cmd_add_box(
            buf, 0, layer + 4, 0, YETTY_YRICH_RGBA(0, 100, 200, 255), 1.5f, &border);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, idr, "shape_render: selection box add failed");
    }
    return YETTY_OK_VOID();
}

static void shape_insert_text(struct yetty_yrich_element *e, const char *text, size_t text_len)
{
    if (!text || text_len == 0) {
        return;
    }
    struct yetty_yrich_shape *s = (struct yetty_yrich_shape *)e;
    if (s->kind != YETTY_YRICH_SHAPE_TEXTBOX) {
        return;
    }

    int32_t pos = s->cursor_pos;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos > s->text_len) {
        pos = (int32_t)s->text_len;
    }

    size_t new_len = s->text_len + text_len;
    char *new_buf = realloc(s->text, new_len + 1);
    if (!new_buf) {
        return;
    }
    memmove(new_buf + pos + text_len, new_buf + pos, s->text_len - pos);
    memcpy(new_buf + pos, text, text_len);
    new_buf[new_len] = '\0';
    s->text = new_buf;
    s->text_len = new_len;
    s->cursor_pos = pos + (int32_t)text_len;
    s->sel_start = s->sel_end = s->cursor_pos;
}

static void shape_delete_sel(struct yetty_yrich_element *e)
{
    struct yetty_yrich_shape *s = (struct yetty_yrich_shape *)e;
    if (s->kind != YETTY_YRICH_SHAPE_TEXTBOX || s->text_len == 0) {
        return;
    }
    if (s->cursor_pos > 0) {
        memmove(s->text + s->cursor_pos - 1, s->text + s->cursor_pos, s->text_len - s->cursor_pos);
        s->text_len--;
        s->text[s->text_len] = '\0';
        s->cursor_pos--;
        s->sel_start = s->sel_end = s->cursor_pos;
    }
}

static const struct yetty_yrich_element_ops shape_element_ops = {
    .destroy = shape_destroy,
    .bounds = shape_bounds,
    .render = shape_render,
    .is_editable = shape_is_editable,
    .begin_edit = shape_begin_edit,
    .end_edit = shape_end_edit,
    .is_editing = shape_is_editing,
    .insert_text = shape_insert_text,
    .delete_sel = shape_delete_sel,
};

/*=============================================================================
 * Shape creation
 *===========================================================================*/

struct yetty_yrich_shape_ptr_result yetty_yrich_shape_create(yetty_yrich_element_id id,
                                                             uint32_t kind,
                                                             struct yetty_yrich_rect bounds)
{
    struct yetty_yrich_shape *s = calloc(1, sizeof(struct yetty_yrich_shape));
    if (!s) {
        return YETTY_ERR(yetty_yrich_shape_ptr, "yrich shape alloc failed");
    }
    s->base.ops = &shape_element_ops;
    s->base.id = id;
    s->kind = kind;
    s->bounds = bounds;
    s->fill_color = YETTY_YRICH_COLOR_WHITE;
    s->stroke_color = YETTY_YRICH_COLOR_BLACK;
    s->stroke_width = 1.0f;
    s->text_style = yetty_yrich_text_style_default();
    s->text_style.font_size = 24.0f;
    s->text_align = YETTY_YRICH_HALIGN_CENTER;
    s->text_valign = YETTY_YRICH_VALIGN_MIDDLE;
    s->preserve_aspect = true;
    return YETTY_OK(yetty_yrich_shape_ptr, s);
}

void yetty_yrich_shape_set_text(struct yetty_yrich_shape *s, const char *text, size_t len)
{
    if (!s) {
        return;
    }
    char *buf = malloc(len + 1);
    if (!buf) {
        return;
    }
    if (len > 0) {
        memcpy(buf, text, len);
    }
    buf[len] = '\0';
    free(s->text);
    s->text = buf;
    s->text_len = len;
    s->cursor_pos = (int32_t)len;
}

/*=============================================================================
 * Slide
 *===========================================================================*/

static struct yetty_yrich_slide *slide_create(int32_t index)
{
    struct yetty_yrich_slide *s = calloc(1, sizeof(struct yetty_yrich_slide));
    if (!s) {
        return NULL;
    }
    s->index = index;
    s->bg_color = YETTY_YRICH_COLOR_WHITE;
    return s;
}

static void slide_destroy(struct yetty_yrich_slide *s)
{
    if (!s) {
        return;
    }
    free(s->shapes);
    free(s);
}

static int slide_add_shape(struct yetty_yrich_slide *slide, struct yetty_yrich_shape *shape)
{
    if (slide->shape_count == slide->shape_capacity) {
        size_t new_cap = slide->shape_capacity ? slide->shape_capacity * 2 : 8;
        struct yetty_yrich_shape **new_arr = realloc(slide->shapes, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return -1;
        }
        slide->shapes = new_arr;
        slide->shape_capacity = new_cap;
    }
    slide->shapes[slide->shape_count++] = shape;
    return 0;
}

/*=============================================================================
 * Slides document vtable
 *===========================================================================*/

static float slides_content_width(const struct yetty_yrich_document *doc)
{
    const struct yetty_yrich_slides *s = (const struct yetty_yrich_slides *)doc;
    return s->slide_width;
}

static float slides_content_height(const struct yetty_yrich_document *doc)
{
    const struct yetty_yrich_slides *s = (const struct yetty_yrich_slides *)doc;
    return s->slide_height;
}

static struct yetty_ycore_void_result slides_render(struct yetty_yrich_document *doc)
{
    struct yetty_yrich_slides *s = (struct yetty_yrich_slides *)doc;
    if (!doc->buffer) {
        return YETTY_ERR(yetty_ycore_void, "slides_render: NULL buffer");
    }

    yetty_ydraw_draw_list_clear(doc->buffer);
    yetty_ydraw_draw_list_set_scene_bounds(doc->buffer, 0.0f, 0.0f, s->slide_width,
                                           s->slide_height);

    struct yetty_yrich_slide *slide = yetty_yrich_slides_slide_at(s, s->current_slide);
    if (!slide) {
        doc->dirty = false;
        return YETTY_OK_VOID();
    }

    /* Background. */
    struct yetty_ysdf_box bg = {
        .center_x = s->slide_width * 0.5f,
        .center_y = s->slide_height * 0.5f,
        .half_width = s->slide_width * 0.5f,
        .half_height = s->slide_height * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result br =
        yetty_ydraw_draw_list_add_cmd_add_box(doc->buffer, 0, 0, slide->bg_color, 0, 0.0f, &bg);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "slides_render: bg add failed");

    uint32_t layer = 1;
    for (size_t i = 0; i < slide->shape_count; i++) {
        struct yetty_yrich_shape *sh = slide->shapes[i];
        bool selected = yetty_yrich_document_is_selected(doc, sh->base.id);
        struct yetty_ycore_void_result rr =
            yetty_yrich_element_render(&sh->base, doc->buffer, layer, selected);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "slides_render: shape failed");
        layer += 5; /* leave room for shape's selection layer */
    }

    doc->dirty = false;
    return YETTY_OK_VOID();
}

static void slides_destroy(struct yetty_yrich_document *doc)
{
    struct yetty_yrich_slides *s = (struct yetty_yrich_slides *)doc;
    yetty_yrich_document_fini(doc);
    for (size_t i = 0; i < s->slide_count; i++) {
        slide_destroy(s->slides[i]);
    }
    free(s->slides);
    free(s);
}

static const struct yetty_yrich_document_ops slides_doc_ops = {
    .destroy = slides_destroy,
    .content_width = slides_content_width,
    .content_height = slides_content_height,
    .render = slides_render,
};

/*=============================================================================
 * Create
 *===========================================================================*/

struct yetty_yrich_slides_ptr_result yetty_yrich_slides_create(void)
{
    struct yetty_yrich_slides *s = calloc(1, sizeof(struct yetty_yrich_slides));
    if (!s) {
        return YETTY_ERR(yetty_yrich_slides_ptr, "yrich slides alloc failed");
    }

    struct yetty_ycore_void_result init_r = yetty_yrich_document_init(&s->base);
    if (YETTY_IS_ERR(init_r)) {
        free(s);
        return YETTY_ERR(yetty_yrich_slides_ptr, init_r.error.msg);
    }

    s->base.ops = &slides_doc_ops;
    s->slide_width = 960.0f;
    s->slide_height = 540.0f; /* 16:9 */
    s->current_slide = 0;
    return YETTY_OK(yetty_yrich_slides_ptr, s);
}

void yetty_yrich_slides_set_slide_size(struct yetty_yrich_slides *s, float w, float h)
{
    if (!s) {
        return;
    }
    s->slide_width = w;
    s->slide_height = h;
    yetty_yrich_document_mark_dirty(&s->base);
}

/*=============================================================================
 * Slide management
 *===========================================================================*/

struct yetty_yrich_slide *yetty_yrich_slides_add_slide(struct yetty_yrich_slides *s)
{
    if (!s) {
        return NULL;
    }
    if (s->slide_count == s->slide_capacity) {
        size_t new_cap = s->slide_capacity ? s->slide_capacity * 2 : 4;
        struct yetty_yrich_slide **new_arr = realloc(s->slides, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return NULL;
        }
        s->slides = new_arr;
        s->slide_capacity = new_cap;
    }
    struct yetty_yrich_slide *slide = slide_create((int32_t)s->slide_count);
    if (!slide) {
        return NULL;
    }
    s->slides[s->slide_count++] = slide;
    yetty_yrich_document_mark_dirty(&s->base);
    return slide;
}

struct yetty_yrich_slide *yetty_yrich_slides_slide_at(const struct yetty_yrich_slides *s,
                                                      int32_t index)
{
    if (!s || index < 0 || (size_t)index >= s->slide_count) {
        return NULL;
    }
    return s->slides[index];
}

void yetty_yrich_slides_set_current(struct yetty_yrich_slides *s, int32_t index)
{
    if (!s) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if ((size_t)index >= s->slide_count && s->slide_count > 0) {
        index = (int32_t)s->slide_count - 1;
    }
    s->current_slide = index;
    yetty_yrich_document_mark_dirty(&s->base);
}

void yetty_yrich_slides_next(struct yetty_yrich_slides *s)
{
    if (s) {
        yetty_yrich_slides_set_current(s, s->current_slide + 1);
    }
}

void yetty_yrich_slides_prev(struct yetty_yrich_slides *s)
{
    if (s) {
        yetty_yrich_slides_set_current(s, s->current_slide - 1);
    }
}

/*=============================================================================
 * Shape creation on current slide
 *===========================================================================*/

static struct yetty_yrich_shape_ptr_result add_shape_to_current(struct yetty_yrich_slides *s,
                                                                uint32_t kind,
                                                                struct yetty_yrich_rect bounds)
{
    if (!s) {
        return YETTY_ERR(yetty_yrich_shape_ptr, "yrich slides add: NULL slides");
    }

    struct yetty_yrich_slide *slide = yetty_yrich_slides_slide_at(s, s->current_slide);
    if (!slide) {
        slide = yetty_yrich_slides_add_slide(s);
    }
    if (!slide) {
        return YETTY_ERR(yetty_yrich_shape_ptr, "yrich slides: no current slide");
    }

    yetty_yrich_element_id id = yetty_yrich_document_next_id(&s->base);
    struct yetty_yrich_shape_ptr_result r = yetty_yrich_shape_create(id, kind, bounds);
    if (YETTY_IS_ERR(r)) {
        return r;
    }

    struct yetty_yrich_shape *shape = r.value;
    struct yetty_ycore_void_result add_r = yetty_yrich_document_add_element(&s->base, &shape->base);
    if (YETTY_IS_ERR(add_r)) {
        return YETTY_ERR(yetty_yrich_shape_ptr, add_r.error.msg);
    }

    if (slide_add_shape(slide, shape) < 0) {
        /* Document still owns the element; the slide just lacks the
		 * alias entry. Continue. */
    }
    return YETTY_OK(yetty_yrich_shape_ptr, shape);
}

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_rectangle(struct yetty_yrich_slides *s,
                                                                     float x, float y, float w,
                                                                     float h)
{
    struct yetty_yrich_rect r = {x, y, w, h};
    return add_shape_to_current(s, YETTY_YRICH_SHAPE_RECTANGLE, r);
}

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_ellipse(struct yetty_yrich_slides *s,
                                                                   float x, float y, float w,
                                                                   float h)
{
    struct yetty_yrich_rect r = {x, y, w, h};
    return add_shape_to_current(s, YETTY_YRICH_SHAPE_ELLIPSE, r);
}

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_textbox(struct yetty_yrich_slides *s,
                                                                   float x, float y, float w,
                                                                   float h, const char *text,
                                                                   size_t text_len)
{
    struct yetty_yrich_rect r = {x, y, w, h};
    struct yetty_yrich_shape_ptr_result sr = add_shape_to_current(s, YETTY_YRICH_SHAPE_TEXTBOX, r);
    if (YETTY_IS_OK(sr) && text && text_len > 0) {
        yetty_yrich_shape_set_text(sr.value, text, text_len);
    }
    return sr;
}

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_line(struct yetty_yrich_slides *s,
                                                                float x1, float y1, float x2,
                                                                float y2)
{
    struct yetty_yrich_rect r = {x1, y1, x2 - x1, y2 - y1};
    return add_shape_to_current(s, YETTY_YRICH_SHAPE_LINE, r);
}

struct yetty_yrich_shape_ptr_result yetty_yrich_slides_add_image(struct yetty_yrich_slides *s,
                                                                 float x, float y, float w, float h)
{
    struct yetty_yrich_rect r = {x, y, w, h};
    return add_shape_to_current(s, YETTY_YRICH_SHAPE_IMAGE, r);
}
