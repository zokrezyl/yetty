/*
 * ygui_ypdf.c — PDF viewer widget with per-page culling + scrolling.
 *
 * Construction:
 *   pdfioFileOpen → yetty_ypdf_render_pdf_streaming → one sub-buffer per
 *   page (page-local coordinates). The streaming variant emits the full
 *   FONT prim only the first time a font is referenced; subsequent pages
 *   carry a hash-only FONT prim of the same font. The widget therefore
 *   walks every page once to harvest the full FONT prims into a single
 *   shared "font header" sub-buffer.
 *
 * Render:
 *   - Emit the font header so any visible-page TEXT_SPAN can resolve its
 *     font_id even when the page that originally carried the full FONT
 *     is currently scrolled off-screen.
 *   - Walk pages[]; skip those whose [abs_y, abs_y + h] band does not
 *     overlap [scroll_y, scroll_y + widget_h]. For each visible page,
 *     walk its primitives, skip FONT (already emitted via the header),
 *     translate paint prims by (widget_origin + page_abs_y - scroll_y),
 *     emit.
 *
 * Wheel events are absorbed by the on_scroll vtable handler.
 * Public scroll API + on_scroll_change callback let callers drive the
 * widget from keyboard / scrollbar code.
 */

#include "ygui_internal.h"

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_ypdf.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/font-prim.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/yplatform/fs.h>
#include <yetty/ypdf/ypdf.h>
#include <yetty/ysdf/types.gen.h>

#include <pdfio.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * pdfio glue (file + buffer entry points).
 *===========================================================================*/

YETTY_EXTERNAL_CALLBACK
static bool pdfio_silent(pdfio_file_t *pdf, const char *message, void *data)
{
    (void)pdf;
    (void)message;
    (void)data;
    return true;
}

/*=============================================================================
 * Primitive walker — same convention as ygui_rich.c and ygui_flatten.c.
 *===========================================================================*/

#define YPDF_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

static size_t paint_prim_size(const uint8_t *p, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = *(const uint32_t *)p;
    uint32_t base = YPDF_TYPE_BASE(type);

    size_t sdf_bytes = yetty_ysdf_primitive_size(base);
    if (sdf_bytes > 0) {
        size_t s = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        return (s <= remaining) ? s : 0;
    }
    if (remaining < 2 * sizeof(uint32_t)) {
        return 0;
    }
    uint32_t payload_size = ((const uint32_t *)p)[1];
    size_t s = 2 * sizeof(uint32_t) + payload_size;
    return (s <= remaining) ? s : 0;
}

static void translate_sdf(uint32_t *prim, size_t words, float dx, float dy)
{
    uint32_t type = prim[0];
    uint32_t base = YPDF_TYPE_BASE(type);
    size_t shift = (type & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
    size_t geom = 5u + shift;
    if (words < geom + 2u) {
        return;
    }
    float *fprim = (float *)prim;
    fprim[geom + 0] += dx;
    fprim[geom + 1] += dy;
    switch (base) {
    case YETTY_YSDF_SEGMENT:
        if (words >= geom + 4u) {
            fprim[geom + 2] += dx;
            fprim[geom + 3] += dy;
        }
        break;
    case YETTY_YSDF_TRIANGLE:
        if (words >= geom + 6u) {
            fprim[geom + 2] += dx;
            fprim[geom + 3] += dy;
            fprim[geom + 4] += dx;
            fprim[geom + 5] += dy;
        }
        break;
    default:
        break;
    }
}

static void translate_prim_inplace(uint32_t *prim, size_t bytes, float dx, float dy)
{
    if (bytes < sizeof(uint32_t)) {
        return;
    }
    uint32_t type = prim[0];
    size_t words = bytes / sizeof(uint32_t);

    if (type < 0x00010000u) {
        return; /* CMD — nothing to translate */
    }
    if (yetty_ysdf_primitive_size(YPDF_TYPE_BASE(type)) > 0u) {
        translate_sdf(prim, words, dx, dy);
        return;
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        /* [type, payload_size, x, y, font_size, rotation, ...] */
        if (words >= 4) {
            ((float *)prim)[2] += dx;
            ((float *)prim)[3] += dy;
        }
        return;
    }
    if (type >= 0x80000000u) {
        /* complex prim [type, payload_size, bounds_x, bounds_y, ...] */
        if (words >= 4) {
            ((float *)prim)[2] += dx;
            ((float *)prim)[3] += dy;
        }
        return;
    }
    /* FONT and other position-less flyweights — caller should have
     * already filtered FONT; nothing to translate for the rest. */
}

/* Emit page sub-buffer translated by (dx, dy), skipping FONT prims
 * (the widget's font_header carries them once at envelope head). */
static struct yetty_ycore_void_result emit_page_translated(
    struct yetty_ydraw_draw_list *dst, const struct yetty_ydraw_draw_list *src,
    float dx, float dy)
{
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(src);
    size_t size = yetty_ydraw_draw_list_size(src);
    if (!bytes || size == 0) {
        return YETTY_OK_VOID();
    }

    uint8_t stack[4096];
    uint8_t *heap = NULL;
    size_t heap_cap = 0;

    size_t off = 0;
    while (off + sizeof(uint32_t) <= size) {
        size_t s = paint_prim_size(bytes + off, size - off);
        if (s == 0) {
            free(heap);
            return YETTY_ERR(yetty_ycore_void, "ypdf emit: malformed prim in page buf");
        }
        uint32_t type = *(const uint32_t *)(bytes + off);
        if (type == YETTY_YDRAW_TYPE_FONT) {
            off += s;
            continue;
        }
        uint8_t *work = stack;
        if (s > sizeof(stack)) {
            if (s > heap_cap) {
                uint8_t *g = (uint8_t *)realloc(heap, s);
                if (!g) {
                    free(heap);
                    return YETTY_ERR(yetty_ycore_void, "ypdf emit: oom");
                }
                heap = g;
                heap_cap = s;
            }
            work = heap;
        }
        memcpy(work, bytes + off, s);
        translate_prim_inplace((uint32_t *)work, s, dx, dy);
        struct yetty_ydraw_id_result r =
            yetty_ydraw_draw_list_add_prim(dst, work, s);
        if (YETTY_IS_ERR(r)) {
            free(heap);
            return YETTY_ERR(yetty_ycore_void, "ypdf emit: add_prim failed", r);
        }
        off += s;
    }
    free(heap);
    return YETTY_OK_VOID();
}

/* Walk one page buffer; append every full FONT prim (ttf_len > 0) to
 * `header_dst` UNLESS `seen_hashes` already records that font's name
 * (FNV1a16 hex string). `seen_hashes` is grown as needed. */
struct font_seen_set {
    char **names;
    int n, cap;
};

static int font_seen_contains(const struct font_seen_set *s, const char *name, uint32_t name_len)
{
    for (int i = 0; i < s->n; i++) {
        if (strlen(s->names[i]) == name_len && memcmp(s->names[i], name, name_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static struct yetty_ycore_void_result font_seen_add(struct font_seen_set *s, const char *name,
                                                    uint32_t name_len)
{
    if (s->n == s->cap) {
        int nc = s->cap ? s->cap * 2 : 8;
        char **g = (char **)realloc(s->names, (size_t)nc * sizeof(char *));
        if (!g) {
            return YETTY_ERR(yetty_ycore_void, "font_seen: oom");
        }
        s->names = g;
        s->cap = nc;
    }
    char *dup = (char *)malloc(name_len + 1);
    if (!dup) {
        return YETTY_ERR(yetty_ycore_void, "font_seen: oom");
    }
    memcpy(dup, name, name_len);
    dup[name_len] = '\0';
    s->names[s->n++] = dup;
    return YETTY_OK_VOID();
}

static void font_seen_clear(struct font_seen_set *s)
{
    for (int i = 0; i < s->n; i++) {
        free(s->names[i]);
    }
    free(s->names);
    s->names = NULL;
    s->n = s->cap = 0;
}

static struct yetty_ycore_void_result harvest_fonts_from_page(
    struct yetty_ydraw_draw_list *header_dst, struct font_seen_set *seen,
    const struct yetty_ydraw_draw_list *page)
{
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(page);
    size_t size = yetty_ydraw_draw_list_size(page);
    if (!bytes || size == 0) {
        return YETTY_OK_VOID();
    }
    size_t off = 0;
    while (off + sizeof(uint32_t) <= size) {
        size_t s = paint_prim_size(bytes + off, size - off);
        if (s == 0) {
            return YETTY_ERR(yetty_ycore_void, "ypdf harvest: malformed prim");
        }
        uint32_t type = *(const uint32_t *)(bytes + off);
        if (type != YETTY_YDRAW_TYPE_FONT) {
            off += s;
            continue;
        }
        struct yetty_ydraw_font_drawable_view view;
        if (yetty_ydraw_font_drawable_parse((const uint32_t *)(bytes + off), &view) == 0) {
            if (view.ttf_len > 0 && !font_seen_contains(seen, view.name, view.name_len)) {
                struct yetty_ydraw_id_result ar =
                    yetty_ydraw_draw_list_add_prim(header_dst, bytes + off, s);
                if (YETTY_IS_ERR(ar)) {
                    return YETTY_ERR(yetty_ycore_void, "ypdf harvest: add_prim", ar);
                }
                struct yetty_ycore_void_result sr =
                    font_seen_add(seen, view.name, view.name_len);
                if (YETTY_IS_ERR(sr)) {
                    return sr;
                }
            }
        }
        off += s;
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Streaming render bridge: collect per-page sub-buffers.
 *===========================================================================*/

struct page_collector {
    struct ypdf_page_entry *pages;
    int count;
    int cap;
    int err;
};

YETTY_EXTERNAL_CALLBACK
static struct yetty_ycore_void_result on_page_emit(
    void *user_data, int page_index, int page_count,
    const struct yetty_ydraw_draw_list *envelope)
{
    (void)page_index;
    (void)page_count;
    struct page_collector *c = (struct page_collector *)user_data;
    if (c->count == c->cap) {
        int nc = c->cap ? c->cap * 2 : 8;
        struct ypdf_page_entry *g =
            (struct ypdf_page_entry *)realloc(c->pages, (size_t)nc * sizeof(*c->pages));
        if (!g) {
            c->err = 1;
            return YETTY_ERR(yetty_ycore_void, "ypdf collector: oom");
        }
        c->pages = g;
        c->cap = nc;
    }
    /* The envelope passed to the callback is owned by the renderer and
     * destroyed after this call returns. Copy its bytes into a fresh
     * sub-buffer we own. */
    struct yetty_ydraw_draw_list_result br =
        yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(br)) {
        c->err = 1;
        return YETTY_ERR(yetty_ycore_void, "ypdf collector: buf create", br);
    }
    struct yetty_ydraw_draw_list *dst = br.value;
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(envelope);
    size_t size = yetty_ydraw_draw_list_size(envelope);
    size_t off = 0;
    while (off + sizeof(uint32_t) <= size) {
        size_t s = paint_prim_size(bytes + off, size - off);
        if (s == 0) {
            yetty_ydraw_draw_list_destroy(dst);
            c->err = 1;
            return YETTY_ERR(yetty_ycore_void, "ypdf collector: malformed envelope");
        }
        uint32_t type = *(const uint32_t *)(bytes + off);
        /* Skip CMD_ZERO and the streaming variant's per-envelope control
         * records — we restitch coordinates from page_abs_y at render. */
        if (type == YETTY_YDRAW_CMD_ZERO) {
            off += s;
            continue;
        }
        struct yetty_ydraw_id_result ar =
            yetty_ydraw_draw_list_add_prim(dst, bytes + off, s);
        if (YETTY_IS_ERR(ar)) {
            yetty_ydraw_draw_list_destroy(dst);
            c->err = 1;
            return YETTY_ERR(yetty_ycore_void, "ypdf collector: add_prim", ar);
        }
        off += s;
    }
    c->pages[c->count].buf = dst;
    /* Page height comes from the envelope's scene_max_y (the streaming
     * comment guarantees that per-envelope coords are envelope-relative
     * with the page origin at y=0). */
    c->pages[c->count].h = yetty_ydraw_draw_list_scene_max_y(envelope);
    c->pages[c->count].abs_y = 0.0f; /* filled in by build_stacking */
    c->count++;
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Widget vtable
 *===========================================================================*/

static struct yetty_ycore_void_result ypdf_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    if (!ctx || !ctx->buffer) {
        return YETTY_OK_VOID();
    }
    /* 1. Font header — emit unchanged at envelope head. */
    if (self->data.ypdf.font_header) {
        const uint8_t *hb =
            (const uint8_t *)yetty_ydraw_draw_list_data(self->data.ypdf.font_header);
        size_t hs = yetty_ydraw_draw_list_size(self->data.ypdf.font_header);
        if (hb && hs > 0) {
            struct yetty_ydraw_id_result ar =
                yetty_ydraw_draw_list_add_prim(ctx->buffer, hb, hs);
            if (YETTY_IS_ERR(ar)) {
                return YETTY_ERR(yetty_ycore_void, "ypdf render: font header", ar);
            }
        }
    }

    /* 2. Visible pages — translated. */
    float top = self->data.ypdf.scroll_y;
    float bot = top + self->layout_h;
    float dx = self->layout_x;
    float dy_base = self->layout_y - top;
    for (int i = 0; i < self->data.ypdf.n_pages; i++) {
        struct ypdf_page_entry *p = &self->data.ypdf.pages[i];
        float p_top = p->abs_y;
        float p_bot = p->abs_y + p->h;
        if (p_bot <= top || p_top >= bot) {
            continue; /* fully outside viewport */
        }
        struct yetty_ycore_void_result er =
            emit_page_translated(ctx->buffer, p->buf, dx, dy_base + p->abs_y);
        if (YETTY_IS_ERR(er)) {
            return er;
        }
    }
    return YETTY_OK_VOID();
}

static int ypdf_on_scroll(struct yetty_ygui_widget *self, float dx, float dy,
                           ygui_event_t *out)
{
    (void)dx;
    /* Route through the public scroll_to so observer notify + scroll_observer
     * dirty marking happen uniformly with the keyboard / scrollbar paths. */
    const float speed = 60.0f;
    float prev = self->data.ypdf.scroll_y;
    yetty_ygui_widget_ypdf_scroll_to(self, prev - dy * speed);
    if (self->data.ypdf.scroll_y == prev) {
        return 0;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    out->data.scroll.x = 0;
    out->data.scroll.y = self->data.ypdf.scroll_y;
    return 1;
}

static void ypdf_destroy(struct yetty_ygui_widget *self)
{
    if (self->data.ypdf.font_header) {
        yetty_ydraw_draw_list_destroy(self->data.ypdf.font_header);
        self->data.ypdf.font_header = NULL;
    }
    for (int i = 0; i < self->data.ypdf.n_pages; i++) {
        if (self->data.ypdf.pages[i].buf) {
            yetty_ydraw_draw_list_destroy(self->data.ypdf.pages[i].buf);
        }
    }
    free(self->data.ypdf.pages);
    self->data.ypdf.pages = NULL;
    self->data.ypdf.n_pages = 0;
}

/*=============================================================================
 * Construction
 *===========================================================================*/

void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

static const struct yetty_ygui_widget_vtable *ypdf_vtable_ptr(void)
{
    /* Static const local — same program-lifetime storage as a file-scope
     * static, no file-scope symbol. */
    static const struct yetty_ygui_widget_vtable vt = {
        .render = ypdf_render,
        .on_scroll = ypdf_on_scroll,
        .destroy = ypdf_destroy,
    };
    return &vt;
}

/*=============================================================================
 * Scrollable interface — lets a vscrollbar drive the widget via
 * yetty_ygui_widget_scrollbar_bind(scrollbar, ypdf_widget).
 *===========================================================================*/

static float ypdf_scrollable_content(const struct yetty_ygui_widget *self)
{
    return self->data.ypdf.total_h;
}

static float ypdf_scrollable_viewport(const struct yetty_ygui_widget *self)
{
    return self->layout_h;
}

static float ypdf_scrollable_scroll(const struct yetty_ygui_widget *self)
{
    return self->data.ypdf.scroll_y;
}

static float ypdf_scrollable_max(const struct yetty_ygui_widget *self)
{
    return ygui_max(0.0f, self->data.ypdf.total_h - self->layout_h);
}

static void ypdf_scrollable_scroll_to(struct yetty_ygui_widget *self, float y)
{
    yetty_ygui_widget_ypdf_scroll_to(self, y);
}

static const struct yetty_ygui_scrollable *ypdf_scrollable_ptr(void)
{
    static const struct yetty_ygui_scrollable ops = {
        .get_content_h = ypdf_scrollable_content,
        .get_viewport_h = ypdf_scrollable_viewport,
        .get_scroll = ypdf_scrollable_scroll,
        .get_max_scroll = ypdf_scrollable_max,
        .scroll_to = ypdf_scrollable_scroll_to,
    };
    return &ops;
}

static void build_stacking(struct ypdf_page_entry *pages, int n, float page_gap,
                           float *out_total)
{
    float y = 0.0f;
    for (int i = 0; i < n; i++) {
        pages[i].abs_y = y;
        y += pages[i].h;
        if (i + 1 < n) {
            y += page_gap;
        }
    }
    *out_total = y;
}

static struct yetty_ygui_widget *build_widget_from_pdf(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, pdfio_file_t *pdf)
{
    if (!pdf) {
        return NULL;
    }

    struct page_collector col = {0};
    struct yetty_ypdf_stream_render_result sr =
        yetty_ypdf_render_pdf_streaming(pdf, on_page_emit, &col);
    pdfioFileClose(pdf);
    if (col.err || YETTY_IS_ERR(sr)) {
        if (YETTY_IS_ERR(sr)) {
            yetty_ycore_error_destroy(sr.error);
        }
        for (int i = 0; i < col.count; i++) {
            yetty_ydraw_draw_list_destroy(col.pages[i].buf);
        }
        free(col.pages);
        return NULL;
    }
    if (col.count == 0) {
        free(col.pages);
        return NULL;
    }

    /* Font header — walk pages in order, collect first full FONT per name. */
    struct yetty_ydraw_draw_list_result fhr =
        yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(fhr)) {
        yetty_ycore_error_destroy(fhr.error);
        for (int i = 0; i < col.count; i++) {
            yetty_ydraw_draw_list_destroy(col.pages[i].buf);
        }
        free(col.pages);
        return NULL;
    }
    struct yetty_ydraw_draw_list *font_header = fhr.value;
    struct font_seen_set seen = {0};
    for (int i = 0; i < col.count; i++) {
        struct yetty_ycore_void_result hr =
            harvest_fonts_from_page(font_header, &seen, col.pages[i].buf);
        if (YETTY_IS_ERR(hr)) {
            yetty_ycore_error_destroy(hr.error);
            font_seen_clear(&seen);
            yetty_ydraw_draw_list_destroy(font_header);
            for (int j = 0; j < col.count; j++) {
                yetty_ydraw_draw_list_destroy(col.pages[j].buf);
            }
            free(col.pages);
            return NULL;
        }
    }
    font_seen_clear(&seen);

    struct yetty_ygui_widget *widget =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_YPDF, id);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(font_header);
        for (int i = 0; i < col.count; i++) {
            yetty_ydraw_draw_list_destroy(col.pages[i].buf);
        }
        free(col.pages);
        return NULL;
    }
    yetty_ygui_widget_init_base(widget, x, y, w, h);
    widget->data.ypdf.pages = col.pages;
    widget->data.ypdf.n_pages = col.count;
    widget->data.ypdf.page_gap = 8.0f;
    widget->data.ypdf.font_header = font_header;
    widget->data.ypdf.scroll_y = 0.0f;
    widget->data.ypdf.on_scroll_change = NULL;
    widget->data.ypdf.on_scroll_change_userdata = NULL;
    build_stacking(col.pages, col.count, widget->data.ypdf.page_gap,
                   &widget->data.ypdf.total_h);
    widget->vtable = ypdf_vtable_ptr();
    widget->scrollable = ypdf_scrollable_ptr();
    yetty_ygui_engine_attach_widget(engine, widget);
    return widget;
}

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    if (!path) {
        return NULL;
    }
    pdfio_file_t *pdf = pdfioFileOpen(path, NULL, NULL, pdfio_silent, NULL);
    return build_widget_from_pdf(engine, id, x, y, w, h, pdf);
}

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return NULL;
    }
    /* pdfio only opens files by path, so the buffer goes through a temp
     * file. Original code used mkstemp + POSIX unlink-while-open; that
     * doesn't work on Windows, so use ANSI stdio + delete after the pdf
     * has been read and closed (build_widget_from_pdf calls
     * pdfioFileClose before returning). */
    char path[L_tmpnam];
    if (!tmpnam(path)) {
        return NULL;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return NULL;
    }
    if (fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        yetty_yplatform_unlink(path);
        return NULL;
    }
    fclose(fp);

    pdfio_file_t *pdf = pdfioFileOpen(path, NULL, NULL, pdfio_silent, NULL);
    struct yetty_ygui_widget *widget =
        build_widget_from_pdf(engine, id, x, y, w, h, pdf);
    yetty_yplatform_unlink(path);
    return widget;
}

/* Built-in default sample (test/ut/ypdf/test-comprehensive.pdf), baked
 * into the library via ygui_embed_default_asset (incbin / RCDATA). */
#include "ygui_ypdf_ypdf_default_manifest.h"

static const uint8_t *g_ypdf_default_data = NULL;
static size_t g_ypdf_default_size = 0;
static void ypdf_capture_default(const char *name, const uint8_t *data,
                                  size_t size, int compressed)
{
    (void)name;
    (void)compressed;
    g_ypdf_default_data = data;
    g_ypdf_default_size = size;
}

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_default(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h)
{
    if (!g_ypdf_default_data) {
        register_ypdf_default_assets_c(ypdf_capture_default);
    }
    return yetty_ygui_engine_ypdf_from_buffer(
        engine, id, x, y, w, h,
        g_ypdf_default_data, g_ypdf_default_size);
}

/*=============================================================================
 * Public scroll API
 *===========================================================================*/

static int is_ypdf(const struct yetty_ygui_widget *widget)
{
    return widget && widget->type == YETTY_YGUI_WIDGET_YPDF;
}

void yetty_ygui_widget_ypdf_scroll_to(struct yetty_ygui_widget *widget, float y)
{
    if (!is_ypdf(widget)) {
        return;
    }
    float max_scroll = ygui_max(0.0f, widget->data.ypdf.total_h - widget->layout_h);
    float prev = widget->data.ypdf.scroll_y;
    widget->data.ypdf.scroll_y = ygui_clamp(y, 0.0f, max_scroll);
    if (widget->data.ypdf.scroll_y == prev) {
        return;
    }
    widget->dirty = 1;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
    /* Bound view (typically a vscrollbar) needs to re-emit its thumb at
     * the new position in the same frame. */
    if (widget->scroll_observer) {
        widget->scroll_observer->dirty = 1;
    }
    if (widget->data.ypdf.on_scroll_change) {
        widget->data.ypdf.on_scroll_change(widget, widget->data.ypdf.scroll_y, max_scroll,
                                            widget->data.ypdf.on_scroll_change_userdata);
    }
}

void yetty_ygui_widget_ypdf_scroll_by(struct yetty_ygui_widget *widget, float dy)
{
    if (!is_ypdf(widget)) {
        return;
    }
    yetty_ygui_widget_ypdf_scroll_to(widget, widget->data.ypdf.scroll_y + dy);
}

float yetty_ygui_widget_ypdf_get_scroll(const struct yetty_ygui_widget *widget)
{
    return is_ypdf(widget) ? widget->data.ypdf.scroll_y : 0.0f;
}

float yetty_ygui_widget_ypdf_content_height(const struct yetty_ygui_widget *widget)
{
    return is_ypdf(widget) ? widget->data.ypdf.total_h : 0.0f;
}

float yetty_ygui_widget_ypdf_viewport_height(const struct yetty_ygui_widget *widget)
{
    return is_ypdf(widget) ? widget->layout_h : 0.0f;
}

float yetty_ygui_widget_ypdf_max_scroll(const struct yetty_ygui_widget *widget)
{
    if (!is_ypdf(widget)) {
        return 0.0f;
    }
    return ygui_max(0.0f, widget->data.ypdf.total_h - widget->layout_h);
}

int yetty_ygui_widget_ypdf_page_count(const struct yetty_ygui_widget *widget)
{
    return is_ypdf(widget) ? widget->data.ypdf.n_pages : 0;
}

void yetty_ygui_widget_ypdf_on_scroll_change(
    struct yetty_ygui_widget *widget,
    yetty_ygui_ypdf_scroll_change_fn cb, void *userdata)
{
    if (!is_ypdf(widget)) {
        return;
    }
    widget->data.ypdf.on_scroll_change = cb;
    widget->data.ypdf.on_scroll_change_userdata = userdata;
}
