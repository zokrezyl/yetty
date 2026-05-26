/* ygui-selectable.c — highlighted row that fires VALUE_CHANGED. */
#include "paint-helpers.h"
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/selectable.h>
#include <stdlib.h>

#define COLOR_BG_ON 0xFF2C261Eu
#define COLOR_BG_OFF 0xFF14100Bu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_BAR 0xFF92A86Bu

struct sel_data {
    char *text;
    int selected;
};

static struct yetty_ycore_void_result on_click(struct yetty_ygui_object *obj, void *ud)
{
    (void)ud;
    struct sel_data *d = yetty_ygui_data_get(obj, yetty_ygui_selectable_class_get());
    d->selected = !d->selected;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) return dr;
    struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj,
                                  .i0 = d->selected};
    return yetty_ygui_object_emit(obj, &ev);
}

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_selectable_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "selectable: super");
    struct sel_data *d = yetty_ygui_data_get(obj, yetty_ygui_selectable_class_get());
    d->text = NULL;
    d->selected = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click, NULL);
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct sel_data *d = yetty_ygui_data_get(obj, yetty_ygui_selectable_class_get());
    free(d->text);
    return yetty_ygui_super_void(obj, yetty_ygui_selectable_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "selectable paint: NULL ctx");
    struct sel_data *d = yetty_ygui_data_get(obj, yetty_ygui_selectable_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x, r.min.y, w, h,
                                  d->selected ? COLOR_BG_ON : COLOR_BG_OFF, 4),
                        "selectable: bg");
    if (d->selected)
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_box(ctx, r.min.x, r.min.y, 3, h, COLOR_BAR, 0),
                            "selectable: bar");
    float fs = 14.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    if (d->text)
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->text, r.min.x + 12, ty, fs, COLOR_TEXT),
                            "selectable: text");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_selectable_set_text(struct yetty_ygui_object *obj,
                                                              const char *t)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "selectable_set_text: NULL");
    struct sel_data *d = yetty_ygui_data_get(obj, yetty_ygui_selectable_class_get());
    free(d->text);
    d->text = NULL;
    if (t) {
        size_t n = strlen(t);
        d->text = malloc(n + 1);
        if (!d->text) return YETTY_ERR(yetty_ycore_void, "selectable_set_text: malloc");
        memcpy(d->text, t, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_selectable_set_selected(struct yetty_ygui_object *obj,
                                                                  int s)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "selectable_set_selected: NULL");
    struct sel_data *d = yetty_ygui_data_get(obj, yetty_ygui_selectable_class_get());
    d->selected = s ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_selectable_is_selected(const struct yetty_ygui_object *obj)
{
    if (!obj) return 0;
    return ((struct sel_data *)yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                   yetty_ygui_selectable_class_get()))
        ->selected;
}


static const struct yetty_ygui_op selectable_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
};

static const struct yetty_ygui_class_descriptor selectable_desc = {
    .name = "yetty_ygui_selectable",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct sel_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_selectable_class_get, &selectable_desc, selectable_ops, yetty_ygui_primitive_widget_class_get(), yetty_ygui_clickable_mixin_get(), NULL)
