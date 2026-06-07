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

struct [[clang::annotate("class@ygui:selectable")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] [[clang::annotate("uses@ygui:clickable")]] sel_data {
    char *text;
    int selected;
};

static struct yetty_ycore_void_result on_click(struct yetty_yclass_ctx *yclass_ctx,
                                               struct yetty_yclass_object *yclass_obj, void *ud)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    (void)ud;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "on_click: data_get");
    struct sel_data *d = d_dr.value;
    d->selected = !d->selected;
    struct yetty_ycore_void_result dr = yetty_ygui_object_set_dirty(obj);
    if (YETTY_IS_ERR(dr)) {
        return dr;
    }
    struct yetty_ygui_event ev = {
        .type = YETTY_YGUI_EVENT_VALUE_CHANGED, .source = obj, .i0 = d->selected};
    return yetty_ygui_object_emit(obj, &ev);
}

[[clang::annotate("override@ygui:selectable:constructor")]]
static struct yetty_ycore_void_result ctor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_selectable_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "selectable: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "ctor: data_get");
    struct sel_data *d = d_dr.value;
    d->text = NULL;
    d->selected = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click, NULL);
}

[[clang::annotate("override@ygui:selectable:destructor")]]
static struct yetty_ycore_void_result dtor(struct yetty_yclass_ctx *yclass_ctx,
                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "dtor: data_get");
    struct sel_data *d = d_dr.value;
    free(d->text);
    return yetty_ygui_super_void(obj, yetty_ygui_selectable_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:selectable:widget_paint")]]
static struct yetty_ycore_void_result paint(struct yetty_yclass_ctx *yclass_ctx,
                                            struct yetty_yclass_object *yclass_obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)yclass_obj;
    if (!ctx) {
        return YETTY_ERR(yetty_ycore_void, "selectable paint: NULL ctx");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "paint: data_get");
    struct sel_data *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result_94 =
        yguix_box(ctx, r.min.x, r.min.y, w, h, d->selected ? COLOR_BG_ON : COLOR_BG_OFF, 4);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, result_94, "selectable: bg");
    if (d->selected) {
        struct yetty_ycore_void_result result_99 =
            yguix_box(ctx, r.min.x, r.min.y, 3, h, COLOR_BAR, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_99, "selectable: bar");
    }
    float fs = 14.0f;
    float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
    if (d->text) {
        struct yetty_ycore_void_result result_105 =
            yguix_text(ctx, d->text, r.min.x + 12, ty, fs, COLOR_TEXT);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, result_105, "selectable: text");
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_selectable_set_text(struct yetty_ygui_object *obj,
                                                              const char *t)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "selectable_set_text: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_selectable_set_text: data_get");
    struct sel_data *d = d_dr.value;
    free(d->text);
    d->text = NULL;
    if (t) {
        size_t n = strlen(t);
        d->text = malloc(n + 1);
        if (!d->text) {
            return YETTY_ERR(yetty_ycore_void, "selectable_set_text: malloc");
        }
        memcpy(d->text, t, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_selectable_set_selected(struct yetty_ygui_object *obj,
                                                                  int s)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "selectable_set_selected: NULL");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_selectable_set_selected: data_get");
    struct sel_data *d = d_dr.value;
    d->selected = s ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_int_result yetty_ygui_selectable_is_selected(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_selectable_is_selected: NULL obj");
    }
    struct yetty_ygui_void_ptr_result data_result = yetty_ygui_data_get_result(
        (struct yetty_ygui_object *)obj, yetty_ygui_selectable_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_result,
                        "yetty_ygui_selectable_is_selected: data_get");
    return YETTY_OK(yetty_ycore_int, ((struct sel_data *)data_result.value)->selected);
}

#include "selectable.gen.c"
