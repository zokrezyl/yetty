/* ygui-radio.c — single radio button. */
#include "paint-helpers.h"
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/radio.h>
#include <stdlib.h>

#define COLOR_OUTER 0xFF474A36u
#define COLOR_INNER_OFF 0xFF1F1A14u
#define COLOR_INNER_ON 0xFF92A86Bu
#define COLOR_TEXT 0xFFE4E5E0u

struct [[clang::annotate("class@ygui:radio")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] [[clang::annotate("uses@ygui:clickable")]] radio_data {
    char *label;
    int selected;
};

static struct yetty_ycore_void_result on_click(struct yetty_yclass_ctx *yclass_ctx,
                                               struct yetty_yclass_object *yclass_obj, void *ud)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)ud;
    struct radio_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_radio_class_get().value);
    d->selected = 1;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return dr;
    }
    struct yetty_ygui_event ev = {.type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj, .i0 = 1};
    return yetty_ygui_object_emit(obj, &ev);
}

[[clang::annotate("override@ygui:radio:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_radio_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "radio: super");
    struct radio_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_radio_class_get().value);
    d->label = NULL;
    d->selected = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click, NULL);
}

[[clang::annotate("override@ygui:radio:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct radio_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_radio_class_get().value);
    free(d->label);
    return yetty_ygui_super_void(
        obj, yetty_ygui_radio_class_get().value,
        (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:radio:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "radio paint: NULL ctx");
    }
    struct radio_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_radio_class_get().value);
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float h = r.max.y - r.min.y;
    float cy = r.min.y + h * 0.5f;
    float radius = (h - 6.0f) * 0.5f;
    if (radius < 7.0f) {
        radius = 7.0f;
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_circle(ctx, r.min.x + radius + 2, cy, radius, COLOR_OUTER),
                        "radio: outer");
    YETTY_RETURN_IF_ERR(yetty_ycore_void,
                        yguix_circle(ctx, r.min.x + radius + 2, cy, radius - 2, COLOR_INNER_OFF),
                        "radio: inner_bg");
    if (d->selected) {
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void,
            yguix_circle(ctx, r.min.x + radius + 2, cy, radius * 0.5f, COLOR_INNER_ON),
            "radio: dot");
    }
    if (d->label) {
        float fs = 14.0f;
        YETTY_RETURN_IF_ERR(
            yetty_ycore_void,
            yguix_text(ctx, d->label, r.min.x + 2 * radius + 12, cy + fs * 0.4f, fs, COLOR_TEXT),
            "radio: label");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_radio_set_label(struct yetty_ygui_object *obj,
                                                          const char *label)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "radio_set_label: NULL");
    }
    struct radio_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_radio_class_get().value);
    free(d->label);
    d->label = NULL;
    if (label) {
        size_t n = strlen(label);
        d->label = malloc(n + 1);
        if (!d->label) {
            return YETTY_ERR(yetty_ycore_void, "radio_set_label: malloc");
        }
        memcpy(d->label, label, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_radio_set_selected(struct yetty_ygui_object *obj, int s)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "radio_set_selected: NULL");
    }
    struct radio_data *d = yetty_ygui_data_get(
        obj, yetty_ygui_radio_class_get().value);
    d->selected = s ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_radio_is_selected(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return 0;
    }
    return ((struct radio_data *)yetty_ygui_data_get(
                (struct yetty_ygui_object *)obj,
                yetty_ygui_radio_class_get().value))
        ->selected;
}

#include "radio.gen.c"
