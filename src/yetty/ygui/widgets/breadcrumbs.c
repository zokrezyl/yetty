/* ygui-breadcrumbs.c — "Home › Settings › Network" trail. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * breadcrumbs.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_breadcrumbs_ptr, struct yetty_ygui_breadcrumbs *);
struct yetty_yclass_ptr_result yetty_ygui_breadcrumbs_class_get(void);
struct yetty_ygui_breadcrumbs_ptr_result yetty_ygui_breadcrumbs_from(
    struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <stdlib.h>

#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_SEP 0xFFA8A79Fu

struct YETTY_ANNOTATE("class@ygui:breadcrumbs") YETTY_ANNOTATE("parent@ygui:primitive_widget")
    yetty_ygui_breadcrumbs {
    char **items;
    int n_items;
    int cap;
};

YETTY_ANNOTATE("override@ygui:breadcrumbs:constructor")
static struct yetty_ycore_void_result ctor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_breadcrumbs_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "breadcrumbs: super");
    struct yetty_ygui_breadcrumbs_ptr_result d_dr = yetty_ygui_breadcrumbs_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_breadcrumbs *d = d_dr.value;
    d->items = NULL;
    d->n_items = 0;
    d->cap = 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui:breadcrumbs:destructor")
static struct yetty_ycore_void_result dtor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_breadcrumbs_ptr_result d_dr = yetty_ygui_breadcrumbs_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_breadcrumbs *d = d_dr.value;
    for (int i = 0; i < d->n_items; i++) {
        free(d->items[i]);
    }
    free(d->items);
    return yetty_ygui_super_void(obj, yetty_ygui_breadcrumbs_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

YETTY_ANNOTATE("override@ygui:breadcrumbs:widget_paint")
static struct yetty_ycore_void_result paint(struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs paint: NULL ctx");
    }
    struct yetty_ygui_breadcrumbs_ptr_result d_dr = yetty_ygui_breadcrumbs_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_breadcrumbs *d = d_dr.value;
    struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "breadcrumbs paint: rect");
    struct yetty_ycore_rectangle r = rect_res.value;
    float h = r.max.y - r.min.y;
    if (h <= 0) {
        return YETTY_OK_VOID();
    }
    float fs = 13.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    float cx = r.min.x;
    for (int i = 0; i < d->n_items; i++) {
        if (i > 0) {
            struct yetty_ycore_void_result result_81 =
                yguix_text(ctx, ">", cx + 4, ty, fs, COLOR_SEP);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_81, "bc: sep");
            cx += 18;
        }
        struct yetty_ycore_void_result result_85 = yguix_text(
            ctx, d->items[i], cx, ty, fs, i == d->n_items - 1 ? 0xFF92A86Bu : COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_85, "bc: item");
        cx += (float)strlen(d->items[i]) * fs * 0.55f + 8;
    }
    return YETTY_OK_VOID();
}

static int grow(struct yetty_ygui_breadcrumbs *d, int n)
{
    if (n <= d->cap) {
        return 1;
    }
    int c = d->cap ? d->cap * 2 : 4;
    while (c < n) {
        c *= 2;
    }
    char **ni = realloc(d->items, (size_t)c * sizeof(*ni));
    if (!ni) {
        return 0;
    }
    d->items = ni;
    d->cap = c;
    return 1;
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_add(struct yetty_yclass_object *obj,
                                                          const char *text)
{
    if (!obj || !text) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs_add: NULL");
    }
    struct yetty_ygui_breadcrumbs_ptr_result d_dr = yetty_ygui_breadcrumbs_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_breadcrumbs_add: data_get");
    struct yetty_ygui_breadcrumbs *d = d_dr.value;
    if (!grow(d, d->n_items + 1)) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs_add: realloc");
    }
    size_t n = strlen(text);
    d->items[d->n_items] = malloc(n + 1);
    if (!d->items[d->n_items]) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs_add: malloc");
    }
    memcpy(d->items[d->n_items], text, n + 1);
    d->n_items++;
    return yetty_ygui_widget_set_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui_breadcrumbs_clear(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs_clear: NULL");
    }
    struct yetty_ygui_breadcrumbs_ptr_result d_dr = yetty_ygui_breadcrumbs_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_breadcrumbs_clear: data_get");
    struct yetty_ygui_breadcrumbs *d = d_dr.value;
    for (int i = 0; i < d->n_items; i++) {
        free(d->items[i]);
    }
    d->n_items = 0;
    return yetty_ygui_widget_set_dirty(obj);
}

#include "breadcrumbs.gen.c"
