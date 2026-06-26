/* ygui-choicebox.c — multi-select list with check markers. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * choicebox.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_choicebox_ptr, struct yetty_ygui_choicebox *);
struct yetty_yclass_ptr_result yetty_ygui_choicebox_class_get(void);
struct yetty_ygui_choicebox_ptr_result yetty_ygui_choicebox_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <stdlib.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_ROW 0xFF1F1A14u
#define COLOR_ROW_ON 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_CHECK 0xFF92A86Bu
#define ROW_H 24.0f

struct cb_row {
    char *label;
    int selected;
};

struct YETTY_ANNOTATE("class@ygui:choicebox") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_choicebox {
    struct cb_row *rows;
    int n;
    int cap;
};

YETTY_ANNOTATE("override@ygui:choicebox:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_choicebox_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "choicebox: super");
    struct yetty_ygui_choicebox_ptr_result d_dr = yetty_ygui_choicebox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_choicebox *d = d_dr.value;
    d->rows = NULL;
    d->n = 0;
    d->cap = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:choicebox:destructor")
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_choicebox_ptr_result d_dr = yetty_ygui_choicebox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_choicebox *d = d_dr.value;
    for (int i = 0; i < d->n; i++) {
        free(d->rows[i].label);
    }
    free(d->rows);
    return yetty_ygui_super_void(obj, yetty_ygui_choicebox_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:choicebox:widget_on_press")
static struct yetty_ycore_int_result on_press(struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)x;
    (void)btn;
    struct yetty_ygui_choicebox_ptr_result d_dr = yetty_ygui_choicebox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "on_press: data_get");
    struct yetty_ygui_choicebox *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "on_press: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    int idx = (int)((y - r.min.y) / ROW_H);
    if (idx < 0 || idx >= d->n) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    d->rows[idx].selected = !d->rows[idx].selected;
    int count = 0;
    for (int i = 0; i < d->n; i++) {
        count += d->rows[i].selected;
    }
    struct yetty_ycore_void_result dr = yetty_ygui_widget_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "choicebox: dirty", dr);
    }
    struct yetty_ygui_event ev = {
        .type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj, .i0 = idx, .i1 = count};
    struct yetty_ycore_void_result er = yetty_ygui_widget_emit(obj, &ev);
    if (YETTY_IS_ERR(er)) {
        return YETTY_ERR(yetty_ycore_int, "choicebox: emit", er);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui:choicebox:widget_paint")
static struct yetty_ycore_void_result paint(struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "choicebox paint: NULL ctx");
    }
    struct yetty_ygui_choicebox_ptr_result d_dr = yetty_ygui_choicebox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_choicebox *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_122 = yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_122, "choicebox: bg");
    for (int i = 0; i < d->n; i++) {
        float y = r.min.y + i * ROW_H;
        if (y > r.max.y) {
            break;
        }
        struct yetty_ycore_void_result result_129 =
            yguix_box(ctx, r.min.x, y, w, ROW_H, d->rows[i].selected ? COLOR_ROW_ON : COLOR_ROW, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_129, "choicebox: row");
        float fs = 13.0f;
        float ty = y + (ROW_H + fs) * 0.5f - 3;
        struct yetty_ycore_void_result result_135 =
            yguix_text(ctx, d->rows[i].selected ? "[x]" : "[ ]", r.min.x + 8, ty, fs,
                       d->rows[i].selected ? COLOR_CHECK : COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_135, "choicebox: marker");
        struct yetty_ycore_void_result result_139 =
            yguix_text(ctx, d->rows[i].label, r.min.x + 40, ty, fs, COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_139, "choicebox: text");
    }
    return YETTY_OK_VOID();
}

static int grow(struct yetty_ygui_choicebox *d, int n)
{
    if (n <= d->cap) {
        return 1;
    }
    int c = d->cap ? d->cap * 2 : 8;
    while (c < n) {
        c *= 2;
    }
    struct cb_row *nr = realloc(d->rows, (size_t)c * sizeof(*nr));
    if (!nr) {
        return 0;
    }
    d->rows = nr;
    d->cap = c;
    return 1;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_choicebox_add(struct yetty_yclass_object *obj,
                                                        const char *label)
{
    if (!obj || !label) {
        return YETTY_ERR(yetty_ycore_void, "choicebox_add: NULL");
    }
    struct yetty_ygui_choicebox_ptr_result d_dr = yetty_ygui_choicebox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_choicebox_add: data_get");
    struct yetty_ygui_choicebox *d = d_dr.value;
    if (!grow(d, d->n + 1)) {
        return YETTY_ERR(yetty_ycore_void, "choicebox_add: realloc");
    }
    size_t n = strlen(label);
    d->rows[d->n].label = malloc(n + 1);
    if (!d->rows[d->n].label) {
        return YETTY_ERR(yetty_ycore_void, "choicebox_add: malloc");
    }
    memcpy(d->rows[d->n].label, label, n + 1);
    d->rows[d->n].selected = 0;
    d->n++;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui_choicebox_is_selected(
    const struct yetty_yclass_object *obj, int idx)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_choicebox_is_selected: invalid args");
    }
    struct yetty_ygui_choicebox_ptr_result d_dr =
        yetty_ygui_choicebox_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yetty_ygui_choicebox_is_selected: data_get");
    struct yetty_ygui_choicebox *d = d_dr.value;
    if (idx < 0 || idx >= d->n) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return YETTY_OK(yetty_ycore_int, d->rows[idx].selected);
}

#include "choicebox.gen.c"
