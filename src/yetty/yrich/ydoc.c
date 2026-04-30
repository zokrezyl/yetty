/*
 * ydoc.c — paragraph-flow rich text document.
 *
 * Paragraphs and inline images both live as elements in the document base.
 * The ydoc subclass keeps an extra alias array for in-flow ordering. Layout
 * is naive — paragraphs stack vertically using bounds.h.
 */

#include <yetty/yrich/ydoc.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-element.h>

#include <yetty/ycore/types.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdlib.h>
#include <string.h>

#define YDOC_DEFAULT_PAGE_WIDTH 600.0f
#define YDOC_DEFAULT_MARGIN 20.0f
#define YDOC_DEFAULT_LINE_HEIGHT 20.0f

/*=============================================================================
 * Paragraph
 *===========================================================================*/

static struct yetty_yrich_rect paragraph_bounds(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_paragraph *p = (const struct yetty_yrich_paragraph *)e;
    return p->bounds;
}

static void paragraph_destroy(struct yetty_yrich_element *e)
{
    struct yetty_yrich_paragraph *p = (struct yetty_yrich_paragraph *)e;
    free(p->text);
    free(p->runs);
    free(p);
}

static bool paragraph_is_editable(const struct yetty_yrich_element *e)
{
    (void)e;
    return true;
}

static void paragraph_begin_edit(struct yetty_yrich_element *e)
{
    struct yetty_yrich_paragraph *p = (struct yetty_yrich_paragraph *)e;
    p->editing = true;
    p->cursor_pos = (int32_t)p->text_len;
    p->sel_start = 0;
    p->sel_end = (int32_t)p->text_len;
}

static void paragraph_end_edit(struct yetty_yrich_element *e)
{
    struct yetty_yrich_paragraph *p = (struct yetty_yrich_paragraph *)e;
    p->editing = false;
    p->sel_start = p->sel_end = p->cursor_pos;
}

static bool paragraph_is_editing(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_paragraph *p = (const struct yetty_yrich_paragraph *)e;
    return p->editing;
}

static struct yetty_ycore_void_result paragraph_render(struct yetty_yrich_element *e,
                                                       struct yetty_ypaint_core_buffer *buf,
                                                       uint32_t layer, bool selected)
{
    struct yetty_yrich_paragraph *p = (struct yetty_yrich_paragraph *)e;
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "paragraph_render: NULL buffer");
    }

    if (p->text_len > 0 && p->text) {
        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)p->text,
            .size = p->text_len,
            .capacity = p->text_len,
        };
        struct yetty_ycore_void_result tr = yetty_ypaint_core_buffer_add_text(
            buf, p->bounds.x, p->bounds.y + p->style.font_size, &text, p->style.font_size,
            p->style.color, layer, p->style.font_id, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "paragraph_render: add_text failed");
    }

    if (selected) {
        struct yetty_ysdf_box border = {
            .center_x = p->bounds.x + p->bounds.w * 0.5f,
            .center_y = p->bounds.y + p->bounds.h * 0.5f,
            .half_width = p->bounds.w * 0.5f,
            .half_height = p->bounds.h * 0.5f,
            .corner_radius = 0.0f,
        };
        struct yetty_ypaint_id_result br = yetty_ysdf_add_box(
            buf, layer + 1, 0, YETTY_YRICH_RGBA(0, 100, 200, 96), 1.0f, &border);
        if (br.error != YPAINT_OK) {
            return YETTY_ERR(yetty_ycore_void, "paragraph_render: selection box add failed");
        }
    }
    return YETTY_OK_VOID();
}

static void paragraph_insert_text(struct yetty_yrich_element *e, const char *text, size_t text_len)
{
    if (!text || text_len == 0) {
        return;
    }
    struct yetty_yrich_paragraph *p = (struct yetty_yrich_paragraph *)e;

    int32_t pos = p->cursor_pos;
    if (pos < 0) {
        pos = 0;
    }
    if ((size_t)pos > p->text_len) {
        pos = (int32_t)p->text_len;
    }

    size_t new_len = p->text_len + text_len;
    char *new_buf = realloc(p->text, new_len + 1);
    if (!new_buf) {
        return;
    }
    memmove(new_buf + pos + text_len, new_buf + pos, p->text_len - pos);
    memcpy(new_buf + pos, text, text_len);
    new_buf[new_len] = '\0';
    p->text = new_buf;
    p->text_len = new_len;
    p->cursor_pos = pos + (int32_t)text_len;
    p->sel_start = p->sel_end = p->cursor_pos;

    /* Recompute bounds height from line count. */
    size_t lines = 1;
    for (size_t i = 0; i < p->text_len; i++) {
        if (p->text[i] == '\n') {
            lines++;
        }
    }
    p->bounds.h = (float)lines * p->line_height;
}

static void paragraph_delete_sel(struct yetty_yrich_element *e)
{
    struct yetty_yrich_paragraph *p = (struct yetty_yrich_paragraph *)e;
    if (p->text_len == 0) {
        return;
    }
    if (p->cursor_pos > 0) {
        memmove(p->text + p->cursor_pos - 1, p->text + p->cursor_pos, p->text_len - p->cursor_pos);
        p->text_len--;
        p->text[p->text_len] = '\0';
        p->cursor_pos--;
        p->sel_start = p->sel_end = p->cursor_pos;
    }
}

static const struct yetty_yrich_element_ops paragraph_element_ops = {
    .destroy = paragraph_destroy,
    .bounds = paragraph_bounds,
    .render = paragraph_render,
    .is_editable = paragraph_is_editable,
    .begin_edit = paragraph_begin_edit,
    .end_edit = paragraph_end_edit,
    .is_editing = paragraph_is_editing,
    .insert_text = paragraph_insert_text,
    .delete_sel = paragraph_delete_sel,
};

struct yetty_yrich_paragraph_ptr_result yetty_yrich_paragraph_create(yetty_yrich_element_id id,
                                                                     float x, float y, float width)
{
    struct yetty_yrich_paragraph *p = calloc(1, sizeof(struct yetty_yrich_paragraph));
    if (!p) {
        return YETTY_ERR(yetty_yrich_paragraph_ptr, "yrich paragraph alloc failed");
    }
    p->base.ops = &paragraph_element_ops;
    p->base.id = id;
    p->bounds.x = x;
    p->bounds.y = y;
    p->bounds.w = width;
    p->bounds.h = YDOC_DEFAULT_LINE_HEIGHT;
    p->style = yetty_yrich_text_style_default();
    p->line_height = YDOC_DEFAULT_LINE_HEIGHT;
    return YETTY_OK(yetty_yrich_paragraph_ptr, p);
}

void yetty_yrich_paragraph_set_text(struct yetty_yrich_paragraph *p, const char *text, size_t len)
{
    if (!p) {
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
    free(p->text);
    p->text = buf;
    p->text_len = len;
    p->cursor_pos = (int32_t)len;

    size_t lines = 1;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            lines++;
        }
    }
    p->bounds.h = (float)lines * p->line_height;
}

/*=============================================================================
 * InlineImage
 *===========================================================================*/

static struct yetty_yrich_rect image_bounds(const struct yetty_yrich_element *e)
{
    const struct yetty_yrich_inline_image *im = (const struct yetty_yrich_inline_image *)e;
    return im->bounds;
}

static void image_destroy(struct yetty_yrich_element *e)
{
    struct yetty_yrich_inline_image *im = (struct yetty_yrich_inline_image *)e;
    free(im->source);
    free(im->alt_text);
    free(im->caption);
    free(im);
}

static struct yetty_ycore_void_result image_render(struct yetty_yrich_element *e,
                                                   struct yetty_ypaint_core_buffer *buf,
                                                   uint32_t layer, bool selected)
{
    struct yetty_yrich_inline_image *im = (struct yetty_yrich_inline_image *)e;
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "image_render: NULL buffer");
    }

    /* Placeholder until image atlasing lands — render a stroked box where
	 * the image would appear. */
    struct yetty_ysdf_box body = {
        .center_x = im->bounds.x + im->bounds.w * 0.5f,
        .center_y = im->bounds.y + im->bounds.h * 0.5f,
        .half_width = im->bounds.w * 0.5f,
        .half_height = im->bounds.h * 0.5f,
        .corner_radius = 4.0f,
    };
    uint32_t border =
        selected ? YETTY_YRICH_RGBA(0, 100, 200, 255) : YETTY_YRICH_RGBA(150, 150, 150, 255);
    struct yetty_ypaint_id_result br = yetty_ysdf_add_box(
        buf, layer, YETTY_YRICH_RGBA(245, 245, 245, 255), border, 1.0f, &body);
    if (br.error != YPAINT_OK) {
        return YETTY_ERR(yetty_ycore_void, "image_render: body box add failed");
    }

    if (im->caption) {
        size_t cap_len = strlen(im->caption);
        struct yetty_ycore_buffer text = {
            .data = (uint8_t *)im->caption,
            .size = cap_len,
            .capacity = cap_len,
        };
        struct yetty_ycore_void_result tr = yetty_ypaint_core_buffer_add_text(
            buf, im->bounds.x, im->bounds.y + im->bounds.h + 14.0f, &text, 12.0f,
            YETTY_YRICH_COLOR_BLACK, layer + 1, 0, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "image_render: caption add_text failed");
    }
    return YETTY_OK_VOID();
}

static const struct yetty_yrich_element_ops inline_image_element_ops = {
    .destroy = image_destroy,
    .bounds = image_bounds,
    .render = image_render,
};

struct yetty_yrich_inline_image_ptr_result yetty_yrich_inline_image_create(
    yetty_yrich_element_id id, float x, float y, float w, float h)
{
    struct yetty_yrich_inline_image *im = calloc(1, sizeof(struct yetty_yrich_inline_image));
    if (!im) {
        return YETTY_ERR(yetty_yrich_inline_image_ptr, "yrich inline image alloc failed");
    }
    im->base.ops = &inline_image_element_ops;
    im->base.id = id;
    im->bounds.x = x;
    im->bounds.y = y;
    im->bounds.w = w;
    im->bounds.h = h;
    im->align = YETTY_YRICH_HALIGN_CENTER;
    return YETTY_OK(yetty_yrich_inline_image_ptr, im);
}

/*=============================================================================
 * Document
 *===========================================================================*/

static float ydoc_content_width(const struct yetty_yrich_document *doc)
{
    const struct yetty_yrich_ydoc *d = (const struct yetty_yrich_ydoc *)doc;
    return d->page_width;
}

static float ydoc_content_height(const struct yetty_yrich_document *doc)
{
    const struct yetty_yrich_ydoc *d = (const struct yetty_yrich_ydoc *)doc;
    float h = d->margin;
    for (size_t i = 0; i < d->paragraph_count; i++) {
        h += d->paragraphs[i]->bounds.h;
    }
    h += d->margin;
    return h;
}

static void ydoc_destroy(struct yetty_yrich_document *doc)
{
    struct yetty_yrich_ydoc *d = (struct yetty_yrich_ydoc *)doc;
    yetty_yrich_document_fini(doc);
    free(d->paragraphs);
    free(d->images);
    free(d);
}

static const struct yetty_yrich_document_ops ydoc_doc_ops = {
    .destroy = ydoc_destroy,
    .content_width = ydoc_content_width,
    .content_height = ydoc_content_height,
};

struct yetty_yrich_ydoc_ptr_result yetty_yrich_ydoc_create(void)
{
    struct yetty_yrich_ydoc *d = calloc(1, sizeof(struct yetty_yrich_ydoc));
    if (!d) {
        return YETTY_ERR(yetty_yrich_ydoc_ptr, "yrich ydoc alloc failed");
    }

    struct yetty_ycore_void_result init_r = yetty_yrich_document_init(&d->base);
    if (YETTY_IS_ERR(init_r)) {
        free(d);
        return YETTY_ERR(yetty_yrich_ydoc_ptr, init_r.error.msg);
    }

    d->base.ops = &ydoc_doc_ops;
    d->page_width = YDOC_DEFAULT_PAGE_WIDTH;
    d->margin = YDOC_DEFAULT_MARGIN;
    return YETTY_OK(yetty_yrich_ydoc_ptr, d);
}

void yetty_yrich_ydoc_set_page_width(struct yetty_yrich_ydoc *d, float w)
{
    if (!d) {
        return;
    }
    d->page_width = w;
    for (size_t i = 0; i < d->paragraph_count; i++) {
        d->paragraphs[i]->bounds.w = w - 2.0f * d->margin;
    }
    yetty_yrich_document_mark_dirty(&d->base);
}

void yetty_yrich_ydoc_set_margin(struct yetty_yrich_ydoc *d, float m)
{
    if (!d) {
        return;
    }
    d->margin = m;
    yetty_yrich_document_mark_dirty(&d->base);
}

static int paragraph_list_push(struct yetty_yrich_ydoc *d, struct yetty_yrich_paragraph *p)
{
    if (d->paragraph_count == d->paragraph_capacity) {
        size_t new_cap = d->paragraph_capacity ? d->paragraph_capacity * 2 : 8;
        struct yetty_yrich_paragraph **new_arr = realloc(d->paragraphs, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return -1;
        }
        d->paragraphs = new_arr;
        d->paragraph_capacity = new_cap;
    }
    d->paragraphs[d->paragraph_count++] = p;
    return 0;
}

struct yetty_yrich_paragraph_ptr_result yetty_yrich_ydoc_add_paragraph(struct yetty_yrich_ydoc *d,
                                                                       const char *text,
                                                                       size_t text_len)
{
    if (!d) {
        return YETTY_ERR(yetty_yrich_paragraph_ptr, "yrich add_paragraph: NULL doc");
    }

    float y = d->margin;
    for (size_t i = 0; i < d->paragraph_count; i++) {
        y += d->paragraphs[i]->bounds.h;
    }

    float content_w = d->page_width - 2.0f * d->margin;

    yetty_yrich_element_id id = yetty_yrich_document_next_id(&d->base);
    struct yetty_yrich_paragraph_ptr_result pr =
        yetty_yrich_paragraph_create(id, d->margin, y, content_w);
    if (YETTY_IS_ERR(pr)) {
        return pr;
    }

    struct yetty_yrich_paragraph *p = pr.value;
    if (text && text_len > 0) {
        yetty_yrich_paragraph_set_text(p, text, text_len);
    }

    struct yetty_ycore_void_result add_r = yetty_yrich_document_add_element(&d->base, &p->base);
    if (YETTY_IS_ERR(add_r)) {
        return YETTY_ERR(yetty_yrich_paragraph_ptr, add_r.error.msg);
    }

    if (paragraph_list_push(d, p) < 0) {
        /* Document still owns it; alias array failed to grow. */
    }
    return YETTY_OK(yetty_yrich_paragraph_ptr, p);
}

struct yetty_yrich_paragraph *yetty_yrich_ydoc_paragraph_at(const struct yetty_yrich_ydoc *d,
                                                            int32_t index)
{
    if (!d || index < 0 || (size_t)index >= d->paragraph_count) {
        return NULL;
    }
    return d->paragraphs[index];
}

static int image_list_push(struct yetty_yrich_ydoc *d, struct yetty_yrich_inline_image *im)
{
    if (d->image_count == d->image_capacity) {
        size_t new_cap = d->image_capacity ? d->image_capacity * 2 : 4;
        struct yetty_yrich_inline_image **new_arr = realloc(d->images, new_cap * sizeof(*new_arr));
        if (!new_arr) {
            return -1;
        }
        d->images = new_arr;
        d->image_capacity = new_cap;
    }
    d->images[d->image_count++] = im;
    return 0;
}

struct yetty_yrich_inline_image_ptr_result yetty_yrich_ydoc_insert_image(struct yetty_yrich_ydoc *d,
                                                                         int32_t paragraph_index,
                                                                         float width, float height)
{
    if (!d) {
        return YETTY_ERR(yetty_yrich_inline_image_ptr, "yrich insert_image: NULL doc");
    }
    (void)paragraph_index; /* future: anchor to paragraph */

    float y = d->margin;
    for (size_t i = 0; i < d->paragraph_count; i++) {
        y += d->paragraphs[i]->bounds.h;
    }

    yetty_yrich_element_id id = yetty_yrich_document_next_id(&d->base);
    struct yetty_yrich_inline_image_ptr_result ir =
        yetty_yrich_inline_image_create(id, d->margin, y, width, height);
    if (YETTY_IS_ERR(ir)) {
        return ir;
    }

    struct yetty_yrich_inline_image *im = ir.value;
    struct yetty_ycore_void_result add_r = yetty_yrich_document_add_element(&d->base, &im->base);
    if (YETTY_IS_ERR(add_r)) {
        return YETTY_ERR(yetty_yrich_inline_image_ptr, add_r.error.msg);
    }

    if (image_list_push(d, im) < 0) {
        /* document owns; alias array missing. */
    }
    return YETTY_OK(yetty_yrich_inline_image_ptr, im);
}
