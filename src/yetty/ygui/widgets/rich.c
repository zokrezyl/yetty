/*
 * ygui-rich.c — styled multi-line text.
 *
 * Lines are stacked vertically using the largest font size in each
 * line as the row advance. Spans inside a line are placed
 * left-to-right using a glyph-width heuristic (font_size * 0.55 per
 * char — same heuristic statusbar/dropdown use, pending real glyph
 * measurement).
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/rich.h>

#include <stdlib.h>
#include <string.h>

struct rich_span {
    char *text;
    float font_size;
    uint32_t color;
};

struct rich_line {
    struct rich_span *spans;
    int n_spans;
    int cap;
};

struct [[clang::annotate("class@ygui:rich")]] [[clang::annotate("parent@ygui:primitive_widget")]]
rich_data {
    struct rich_line *lines;
    int n_lines;
    int cap;
};

[[clang::annotate("override@ygui:rich:constructor")]]
static struct yetty_ycore_void_result rich_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "rich_constructor: super");
    struct rich_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"));
    d->lines = NULL;
    d->n_lines = 0;
    d->cap = 0;
    return YETTY_OK_VOID();
}

static void free_line(struct rich_line *line)
{
    for (int i = 0; i < line->n_spans; ++i) {
        free(line->spans[i].text);
    }
    free(line->spans);
    line->spans = NULL;
    line->n_spans = 0;
    line->cap = 0;
}

[[clang::annotate("override@ygui:rich:destructor")]]
static struct yetty_ycore_void_result rich_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct rich_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"));
    for (int i = 0; i < d->n_lines; ++i) {
        free_line(&d->lines[i]);
    }
    free(d->lines);
    d->lines = NULL;
    return yetty_ygui_super_void(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"),
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:rich:widget_paint")]]
static struct yetty_ycore_void_result rich_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                 struct yetty_yclass_object *yclass_obj,
                                                 struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "rich_paint: NULL ctx");
    }
    struct rich_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"));
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    float cursor_y = r.min.y;
    for (int i = 0; i < d->n_lines; ++i) {
        struct rich_line *line = &d->lines[i];
        /* Row advance = largest font size in the line, +25 % leading. */
        float row_fs = 0.0f;
        for (int s = 0; s < line->n_spans; ++s) {
            if (line->spans[s].font_size > row_fs) {
                row_fs = line->spans[s].font_size;
            }
        }
        if (row_fs <= 0.0f) {
            row_fs = 14.0f;
        }
        float row_advance = row_fs * 1.25f;
        float baseline = cursor_y + row_fs;
        float cursor_x = r.min.x;
        for (int s = 0; s < line->n_spans; ++s) {
            struct rich_span *sp = &line->spans[s];
            if (!sp->text || !sp->text[0]) {
                continue;
            }
            size_t n = strlen(sp->text);
            struct yetty_ycore_buffer tb = {.data = (uint8_t *)sp->text, .capacity = n, .size = n};
            struct yetty_ycore_void_result tr =
                yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, cursor_x, baseline, &tb,
                                               sp->font_size, sp->color, 0, -1, 0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "rich_paint: text");
            cursor_x += (float)n * sp->font_size * 0.55f;
        }
        cursor_y += row_advance;
        if (cursor_y > r.max.y) {
            break;
        }
    }
    return YETTY_OK_VOID();
}

static int lines_grow(struct rich_data *d, int need)
{
    if (need <= d->cap) {
        return 1;
    }
    int cap = d->cap ? d->cap * 2 : 8;
    while (cap < need) {
        cap *= 2;
    }
    struct rich_line *nl = realloc(d->lines, (size_t)cap * sizeof(*nl));
    if (!nl) {
        return 0;
    }
    /* Zero the newly-grown slots. */
    for (int i = d->cap; i < cap; ++i) {
        nl[i].spans = NULL;
        nl[i].n_spans = 0;
        nl[i].cap = 0;
    }
    d->lines = nl;
    d->cap = cap;
    return 1;
}

static int spans_grow(struct rich_line *l, int need)
{
    if (need <= l->cap) {
        return 1;
    }
    int cap = l->cap ? l->cap * 2 : 4;
    while (cap < need) {
        cap *= 2;
    }
    struct rich_span *ns = realloc(l->spans, (size_t)cap * sizeof(*ns));
    if (!ns) {
        return 0;
    }
    l->spans = ns;
    l->cap = cap;
    return 1;
}

struct yetty_ycore_void_result yetty_ygui_rich_clear(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "rich_clear: NULL obj");
    }
    struct rich_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"));
    for (int i = 0; i < d->n_lines; ++i) {
        free_line(&d->lines[i]);
    }
    d->n_lines = 0;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_rich_add_line(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "rich_add_line: NULL obj");
    }
    struct rich_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"));
    if (!lines_grow(d, d->n_lines + 1)) {
        return YETTY_ERR(yetty_ycore_void, "rich_add_line: realloc");
    }
    d->lines[d->n_lines].spans = NULL;
    d->lines[d->n_lines].n_spans = 0;
    d->lines[d->n_lines].cap = 0;
    d->n_lines++;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_rich_add_span(struct yetty_ygui_object *obj,
                                                        const char *text, float font_size,
                                                        uint32_t color_rgba)
{
    if (!obj || !text) {
        return YETTY_ERR(yetty_ycore_void, "rich_add_span: NULL arg");
    }
    struct rich_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_class_expect(yetty_ygui_rich_class_get(), "yetty_ygui_rich_class_get"));
    if (d->n_lines == 0) {
        struct yetty_ycore_void_result lr = yetty_ygui_rich_add_line(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, lr, "rich_add_span: implicit line");
    }
    struct rich_line *line = &d->lines[d->n_lines - 1];
    if (!spans_grow(line, line->n_spans + 1)) {
        return YETTY_ERR(yetty_ycore_void, "rich_add_span: realloc");
    }
    size_t n = strlen(text);
    char *copy = malloc(n + 1);
    if (!copy) {
        return YETTY_ERR(yetty_ycore_void, "rich_add_span: malloc");
    }
    memcpy(copy, text, n + 1);
    line->spans[line->n_spans].text = copy;
    line->spans[line->n_spans].font_size = font_size > 0.0f ? font_size : 14.0f;
    line->spans[line->n_spans].color = color_rgba;
    line->n_spans++;
    return yetty_ygui_object_set_dirty(obj);
}

#include "rich.gen.c"
