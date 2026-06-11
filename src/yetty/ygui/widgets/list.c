/* ygui-list.c — list of rows with one selected. */
#include "paint-helpers.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * list.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_list_ptr, struct yetty_ygui_list *);
struct yetty_yclass_ptr_result yetty_ygui_list_class_get(void);
struct yetty_ygui_list_ptr_result yetty_ygui_list_from(struct yetty_yclass_object *obj);
#include <yetty/ygui/primitive-widget.h>
#include <stdlib.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_ROW 0xFF1F1A14u
#define COLOR_ROW_ON 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_BAR 0xFF92A86Bu
#define ROW_H 24.0f

struct [[clang::annotate("class@ygui:list")]] [[clang::annotate("parent@ygui:primitive_widget")]]
yetty_ygui_list {
    char **rows;
    int n;
    int cap;
    int selected;
};

[[clang::annotate("override@ygui:list:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_list_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "list: super");
    struct yetty_ygui_list_ptr_result d_dr = yetty_ygui_list_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct yetty_ygui_list *d = d_dr.value;
    d->rows = NULL;
    d->n = 0;
    d->cap = 0;
    d->selected = -1;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:list:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_list_ptr_result d_dr = yetty_ygui_list_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct yetty_ygui_list *d = d_dr.value;
    for (int i = 0; i < d->n; i++) {
        free(d->rows[i]);
    }
    free(d->rows);
    return yetty_ygui_super_void(obj, yetty_ygui_list_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:list:widget_on_press")]]
static struct yetty_ycore_int_result on_press(struct yetty_yclass_ctx *yclass_ctx,
                                              struct yetty_yclass_object *yclass_obj, float x,
                                              float y, int btn)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)x;
    (void)btn;
    struct yetty_ygui_list_ptr_result d_dr = yetty_ygui_list_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "on_press: data_get");
    struct yetty_ygui_list *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    int idx = (int)((y - r.min.y) / ROW_H);
    if (idx < 0 || idx >= d->n) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    d->selected = idx;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return YETTY_ERR(yetty_ycore_int, "list press: dirty", dr);
    }
    struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj, .i0 = idx};
    struct yetty_ycore_void_result er = yetty_ygui_object_emit(obj, &ev);
    if (YETTY_IS_ERR(er)) {
        return YETTY_ERR(yetty_ycore_int, "list press: emit", er);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

[[clang::annotate("override@ygui:list:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "list paint: NULL ctx");
    }
    struct yetty_ygui_list_ptr_result d_dr = yetty_ygui_list_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct yetty_ygui_list *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_112 = yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_112, "list: bg");
    for (int i = 0; i < d->n; i++) {
        float y = r.min.y + i * ROW_H;
        if (y > r.max.y) {
            break;
        }
        int on = i == d->selected;
        struct yetty_ycore_void_result result_120 =
            yguix_box(ctx, r.min.x, y, w, ROW_H, on ? COLOR_ROW_ON : COLOR_ROW, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_120, "list: row");
        if (on) {
            struct yetty_ycore_void_result result_124 =
                yguix_box(ctx, r.min.x, y, 3, ROW_H, COLOR_BAR, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, result_124, "list: bar");
        }
        float fs = 13.0f;
        struct yetty_ycore_void_result result_128 =
            yguix_text(ctx, d->rows[i], r.min.x + 10, y + (ROW_H + fs) * 0.5f - 3, fs, COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_128, "list: text");
    }
    return YETTY_OK_VOID();
}

static int grow(struct yetty_ygui_list *d, int n)
{
    if (n <= d->cap) {
        return 1;
    }
    int c = d->cap ? d->cap * 2 : 8;
    while (c < n) {
        c *= 2;
    }
    char **nr = realloc(d->rows, (size_t)c * sizeof(*nr));
    if (!nr) {
        return 0;
    }
    d->rows = nr;
    d->cap = c;
    return 1;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_list_add(struct yetty_yclass_object *obj,
                                                   const char *label)
{
    if (!obj || !label) {
        return YETTY_ERR(yetty_ycore_void, "list_add: NULL");
    }
    struct yetty_ygui_list_ptr_result d_dr = yetty_ygui_list_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_list_add: data_get");
    struct yetty_ygui_list *d = d_dr.value;
    if (!grow(d, d->n + 1)) {
        return YETTY_ERR(yetty_ycore_void, "list_add: realloc");
    }
    size_t n = strlen(label);
    d->rows[d->n] = malloc(n + 1);
    if (!d->rows[d->n]) {
        return YETTY_ERR(yetty_ycore_void, "list_add: malloc");
    }
    memcpy(d->rows[d->n], label, n + 1);
    d->n++;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_list_set_selected(struct yetty_yclass_object *obj, int i)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "list_set_selected: NULL");
    }
    struct yetty_ygui_list_ptr_result d_dr = yetty_ygui_list_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_list_set_selected: data_get");
    struct yetty_ygui_list *d = d_dr.value;
    d->selected = i;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_int_result yetty_ygui_list_get_selected(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_list_get_selected: NULL obj");
    }
    struct yetty_ygui_list_ptr_result data_result = yetty_ygui_list_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_result, "yetty_ygui_list_get_selected: data_get");
    return YETTY_OK(yetty_ycore_int, ((struct yetty_ygui_list *)data_result.value)->selected);
}

#include "list.gen.c"
