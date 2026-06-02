/* ygui-breadcrumbs.c — "Home › Settings › Network" trail. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/breadcrumbs.h>
#include <stdlib.h>

#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_SEP 0xFFA8A79Fu

struct [[clang::annotate("class@ygui:breadcrumbs")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] bc_data {
    char **items;
    int n_items;
    int cap;
};

[[clang::annotate("override@ygui:breadcrumbs:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj,
                              yetty_ygui_breadcrumbs_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "breadcrumbs: super");
    struct bc_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_breadcrumbs_class_get().value);
    d->items = NULL;
    d->n_items = 0;
    d->cap = 0;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:breadcrumbs:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct bc_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_breadcrumbs_class_get().value);
    for (int i = 0; i < d->n_items; i++) {
        free(d->items[i]);
    }
    free(d->items);
    return yetty_ygui_super_void(obj,
                                 yetty_ygui_breadcrumbs_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:breadcrumbs:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs paint: NULL ctx");
    }
    struct bc_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_breadcrumbs_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float h = r.max.y - r.min.y;
    if (h <= 0) {
        return YETTY_OK_VOID();
    }
    float fs = 13.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    float cx = r.min.x;
    for (int i = 0; i < d->n_items; i++) {
        if (i > 0) {
            YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_text(ctx, ">", cx + 4, ty, fs, COLOR_SEP),
                                "bc: sep");
            cx += 18;
        }
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->items[i], cx, ty, fs,
                                       i == d->n_items - 1 ? 0xFF92A86Bu : COLOR_TEXT),
                            "bc: item");
        cx += (float)strlen(d->items[i]) * fs * 0.55f + 8;
    }
    return YETTY_OK_VOID();
}

static int grow(struct bc_data *d, int n)
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

struct yetty_ycore_void_result yetty_ygui_breadcrumbs_add(struct yetty_ygui_object *obj,
                                                          const char *text)
{
    if (!obj || !text) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs_add: NULL");
    }
    struct bc_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_breadcrumbs_class_get().value);
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
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_breadcrumbs_clear(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "breadcrumbs_clear: NULL");
    }
    struct bc_data *d =
        yetty_ygui_data_get(obj, yetty_ygui_breadcrumbs_class_get().value);
    for (int i = 0; i < d->n_items; i++) {
        free(d->items[i]);
    }
    d->n_items = 0;
    return yetty_ygui_object_set_dirty(obj);
}

#include "breadcrumbs.gen.c"
