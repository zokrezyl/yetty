/* ygui-choicebox.c — multi-select list with check markers. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/choicebox.h>
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

struct cb_data {
    struct cb_row *rows;
    int n;
    int cap;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_choicebox_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "choicebox: super");
    struct cb_data *d = yetty_ygui_data_get(obj, yetty_ygui_choicebox_class_get());
    d->rows = NULL;
    d->n = 0;
    d->cap = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct cb_data *d = yetty_ygui_data_get(obj, yetty_ygui_choicebox_class_get());
    for (int i = 0; i < d->n; i++) free(d->rows[i].label);
    free(d->rows);
    return yetty_ygui_super_void(obj, yetty_ygui_choicebox_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_int_result on_press(struct yetty_ygui_object *obj, float x, float y,
                                              int btn)
{
    (void)x;
    (void)btn;
    struct cb_data *d = yetty_ygui_data_get(obj, yetty_ygui_choicebox_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    int idx = (int)((y - r.min.y) / ROW_H);
    if (idx < 0 || idx >= d->n) return YETTY_OK(yetty_ycore_int, 0);
    d->rows[idx].selected = !d->rows[idx].selected;
    int count = 0;
    for (int i = 0; i < d->n; i++) count += d->rows[i].selected;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) return YETTY_ERR(yetty_ycore_int, "choicebox: dirty", dr);
    struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj,
                                  .i0 = idx, .i1 = count};
    struct yetty_ycore_void_result er = yetty_ygui_object_emit(obj, &ev);
    if (YETTY_IS_ERR(er)) return YETTY_ERR(yetty_ycore_int, "choicebox: emit", er);
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "choicebox paint: NULL ctx");
    struct cb_data *d = yetty_ygui_data_get(obj, yetty_ygui_choicebox_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4),
                        "choicebox: bg");
    for (int i = 0; i < d->n; i++) {
        float y = r.min.y + i * ROW_H;
        if (y > r.max.y) break;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_box(ctx, r.min.x, y, w, ROW_H,
                                      d->rows[i].selected ? COLOR_ROW_ON : COLOR_ROW, 0),
                            "choicebox: row");
        float fs = 13.0f;
        float ty = y + (ROW_H + fs) * 0.5f - 3;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->rows[i].selected ? "[x]" : "[ ]", r.min.x + 8, ty,
                                       fs, d->rows[i].selected ? COLOR_CHECK : COLOR_TEXT),
                            "choicebox: marker");
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->rows[i].label, r.min.x + 40, ty, fs, COLOR_TEXT),
                            "choicebox: text");
    }
    return YETTY_OK_VOID();
}

static int grow(struct cb_data *d, int n)
{
    if (n <= d->cap) return 1;
    int c = d->cap ? d->cap * 2 : 8;
    while (c < n) c *= 2;
    struct cb_row *nr = realloc(d->rows, (size_t)c * sizeof(*nr));
    if (!nr) return 0;
    d->rows = nr;
    d->cap = c;
    return 1;
}

struct yetty_ycore_void_result yetty_ygui_choicebox_add(struct yetty_ygui_object *obj,
                                                        const char *label)
{
    if (!obj || !label) return YETTY_ERR(yetty_ycore_void, "choicebox_add: NULL");
    struct cb_data *d = yetty_ygui_data_get(obj, yetty_ygui_choicebox_class_get());
    if (!grow(d, d->n + 1)) return YETTY_ERR(yetty_ycore_void, "choicebox_add: realloc");
    size_t n = strlen(label);
    d->rows[d->n].label = malloc(n + 1);
    if (!d->rows[d->n].label) return YETTY_ERR(yetty_ycore_void, "choicebox_add: malloc");
    memcpy(d->rows[d->n].label, label, n + 1);
    d->rows[d->n].selected = 0;
    d->n++;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_choicebox_is_selected(const struct yetty_ygui_object *obj, int idx)
{
    if (!obj) return 0;
    struct cb_data *d = yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                            yetty_ygui_choicebox_class_get());
    if (idx < 0 || idx >= d->n) return 0;
    return d->rows[idx].selected;
}


static const struct yetty_ygui_op choicebox_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
    YETTY_YGUI_OP(yetty_ygui_widget_on_press, on_press),
};

static const struct yetty_ygui_class_descriptor choicebox_desc = {
    .name = "yetty_ygui_choicebox",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct cb_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_choicebox_class_get, &choicebox_desc, choicebox_ops, yetty_ygui_primitive_widget_class_get(), NULL)
