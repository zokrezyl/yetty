/* ygui-chip.c — pill label. */
#include "paint-helpers.h"
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/chip.h>
#include <stdlib.h>

#define COLOR_BG 0xFF2C261Eu
#define COLOR_TEXT 0xFFE4E5E0u

struct chip_data {
    char *label;
    int closable;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_chip_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "chip: super");
    struct chip_data *d = yetty_ygui_data_get(obj, yetty_ygui_chip_class_get());
    d->label = NULL;
    d->closable = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct chip_data *d = yetty_ygui_data_get(obj, yetty_ygui_chip_class_get());
    free(d->label);
    return yetty_ygui_super_void(obj, yetty_ygui_chip_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "chip paint: NULL ctx");
    struct chip_data *d = yetty_ygui_data_get(obj, yetty_ygui_chip_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, h * 0.5f),
                        "chip: bg");
    if (d->label) {
        float fs = h * 0.55f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, d->label, r.min.x + h * 0.5f, ty, fs, COLOR_TEXT),
                            "chip: label");
    }
    if (d->closable) {
        float fs = h * 0.55f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, "x", r.max.x - h * 0.5f - 4, ty, fs, COLOR_TEXT),
                            "chip: x");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_chip_set_label(struct yetty_ygui_object *obj,
                                                         const char *label)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "chip_set_label: NULL");
    struct chip_data *d = yetty_ygui_data_get(obj, yetty_ygui_chip_class_get());
    free(d->label);
    d->label = NULL;
    if (label) {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) return YETTY_ERR(yetty_ycore_void, "chip_set_label: malloc");
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_chip_set_closable(struct yetty_ygui_object *obj, int c)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "chip_set_closable: NULL");
    struct chip_data *d = yetty_ygui_data_get(obj, yetty_ygui_chip_class_get());
    d->closable = c ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}


static const struct yetty_ygui_op chip_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
};

static const struct yetty_ygui_class_descriptor chip_desc = {
    .name = "yetty_ygui_chip",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct chip_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_chip_class_get, &chip_desc, chip_ops, yetty_ygui_primitive_widget_class_get(), yetty_ygui_clickable_mixin_get(), NULL)
