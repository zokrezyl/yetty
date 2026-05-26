/* ygui-textarea.c — multi-line text. Paints each '\n'-delimited line. */
#include "paint-helpers.h"
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/textarea.h>
#include <stdlib.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_BORDER 0xFF474A36u
#define COLOR_TEXT 0xFFE4E5E0u

struct textarea_data {
    char *text;
};

static struct yetty_ycore_void_result ctor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_textarea_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "textarea: super");
    struct textarea_data *d = yetty_ygui_data_get(obj, yetty_ygui_textarea_class_get());
    d->text = NULL;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result dtor(struct yetty_ygui_object *obj)
{
    struct textarea_data *d = yetty_ygui_data_get(obj, yetty_ygui_textarea_class_get());
    free(d->text);
    return yetty_ygui_super_void(obj, yetty_ygui_textarea_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint(struct yetty_ygui_object *obj,
                                            struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx) return YETTY_ERR(yetty_ycore_void, "textarea paint: NULL ctx");
    struct textarea_data *d = yetty_ygui_data_get(obj, yetty_ygui_textarea_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
    if (w <= 0 || h <= 0) return YETTY_OK_VOID();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yguix_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4),
                        "textarea: bg");
    if (!d->text || !d->text[0]) return YETTY_OK_VOID();
    float fs = 13.0f;
    float ly = r.min.y + 8 + fs;
    const char *p = d->text;
    while (*p && ly < r.max.y) {
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;
        size_t n = eol - p;
        char tmp[512];
        if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
        memcpy(tmp, p, n);
        tmp[n] = 0;
        YETTY_RETURN_IF_ERR(yetty_ycore_void,
                            yguix_text(ctx, tmp, r.min.x + 8, ly, fs, COLOR_TEXT),
                            "textarea: line");
        ly += fs * 1.3f;
        if (*eol == '\n') eol++;
        p = eol;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_textarea_set_text(struct yetty_ygui_object *obj,
                                                            const char *text)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "textarea_set_text: NULL");
    struct textarea_data *d = yetty_ygui_data_get(obj, yetty_ygui_textarea_class_get());
    free(d->text);
    d->text = NULL;
    if (text) {
        size_t n = strlen(text);
        d->text = malloc(n + 1);
        if (!d->text) return YETTY_ERR(yetty_ycore_void, "textarea_set_text: malloc");
        memcpy(d->text, text, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

const char *yetty_ygui_textarea_get_text(const struct yetty_ygui_object *obj)
{
    if (!obj) return "";
    struct textarea_data *d = yetty_ygui_data_get((struct yetty_ygui_object *)obj,
                                                  yetty_ygui_textarea_class_get());
    return d->text ? d->text : "";
}


static const struct yetty_ygui_op textarea_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, ctor),
    YETTY_YGUI_OP(yetty_ygui_destructor, dtor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, paint),
};

static const struct yetty_ygui_class_descriptor textarea_desc = {
    .name = "yetty_ygui_textarea",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct textarea_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_textarea_class_get, &textarea_desc, textarea_ops, yetty_ygui_primitive_widget_class_get(), NULL)
