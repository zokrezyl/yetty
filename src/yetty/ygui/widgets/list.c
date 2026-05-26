/* ygui-list.c — list of rows with one selected. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/list.h>
#include <stdlib.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_ROW 0xFF1F1A14u
#define COLOR_ROW_ON 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_BAR 0xFF92A86Bu
#define ROW_H 24.0f

struct list_data {
    char **rows;
    int n;
    int cap;
    int selected;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_list_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "list: super");
    struct list_data *d = yetty_ygui_data_get(obj, yetty_ygui_list_class_get());
    d->rows = NULL;
    d->n = 0;
    d->cap = 0;
    d->selected = -1;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct list_data *d = yetty_ygui_data_get(obj, yetty_ygui_list_class_get());
    for (int i = 0; i < d->n; i++) free(d->rows[i]);
    free(d->rows);
    return yetty_ygui_super_void(obj, yetty_ygui_list_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_int_result on_press(struct yetty_ygui_object *obj, float x, float y,
                                              int btn)
{
    (void)x;
    (void)btn;
    struct list_data *d = yetty_ygui_data_get(obj, yetty_ygui_list_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    int idx = (int)((y - r.min.y) / ROW_H);
    if (idx < 0 || idx >= d->n) return YETTY_OK(yetty_ycore_int, 0);
    d->selected = idx;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) return YETTY_ERR(yetty_ycore_int, "list press: dirty", dr);
    struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj,
                                  .i0 = idx};
    struct yetty_ycore_void_result er = yetty_ygui_object_emit(obj, &ev);
    if (YETTY_IS_ERR(er)) return YETTY_ERR(yetty_ycore_int, "list press: emit", er);
    return YETTY_OK(yetty_ycore_int, 1);
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "list paint: NULL ctx");
    struct list_data *d = yetty_ygui_data_get(obj, yetty_ygui_list_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4),
                        "list: bg");
    for (int i = 0; i < d->n; i++) {
        float y = r.min.y + i * ROW_H;
        if (y > r.max.y) break;
        int on = i == d->selected;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_box(ctx, r.min.x, y, w, ROW_H,
                                      on ? COLOR_ROW_ON : COLOR_ROW, 0),
                            "list: row");
        if (on)
            YETTY_RETURN_IF_ERR(yetty_ycore_void,
                                yguix_box(ctx, r.min.x, y, 3, ROW_H, COLOR_BAR, 0),
                                "list: bar");
        float fs = 13.0f;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->rows[i], r.min.x + 10, y + (ROW_H + fs) * 0.5f - 3,
                                       fs, COLOR_TEXT),
                            "list: text");
    }
    return YETTY_OK_VOID();
}

static int grow(struct list_data *d, int n)
{
    if (n <= d->cap) return 1;
    int c = d->cap ? d->cap * 2 : 8;
    while (c < n) c *= 2;
    char **nr = realloc(d->rows, (size_t)c * sizeof(*nr));
    if (!nr) return 0;
    d->rows = nr;
    d->cap = c;
    return 1;
}

struct yetty_ycore_void_result yetty_ygui_list_add(struct yetty_ygui_object *obj,
                                                   const char *label)
{
    if (!obj || !label) return YETTY_ERR(yetty_ycore_void, "list_add: NULL");
    struct list_data *d = yetty_ygui_data_get(obj, yetty_ygui_list_class_get());
    if (!grow(d, d->n + 1)) return YETTY_ERR(yetty_ycore_void, "list_add: realloc");
    size_t n = strlen(label);
    d->rows[d->n] = malloc(n + 1);
    if (!d->rows[d->n]) return YETTY_ERR(yetty_ycore_void, "list_add: malloc");
    memcpy(d->rows[d->n], label, n + 1);
    d->n++;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_list_set_selected(struct yetty_ygui_object *obj, int i)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "list_set_selected: NULL");
    struct list_data *d = yetty_ygui_data_get(obj, yetty_ygui_list_class_get());
    d->selected = i;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_list_get_selected(const struct yetty_ygui_object *obj)
{
    if (!obj) return -1;
    return ((struct list_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                    yetty_ygui_list_class_get()))
        ->selected;
}


static const struct yetty_ygui_op list_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
    YETTY_YGUI_OP(yetty_ygui_widget_on_press, on_press),
};

static const struct yetty_ygui_class_descriptor list_desc = {
    .name = "yetty_ygui_list",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct list_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_list_class_get, &list_desc, list_ops, yetty_ygui_primitive_widget_class_get(), NULL)
