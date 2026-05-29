/*
 * ygui-label.c — static text widget.
 *
 * The paint hook writes a {"LABL", id, font_size, rgba, text_len, text}
 * marker into the engine's ygrid body. The actual GLYPH/TEXT_SPAN
 * record encoding will land when the receiver-side renderer hook is
 * stabilised — for now this proves the pass-2 walk hits the widget
 * and that data_get returns the right slice.
 */

#include "../internal.h"

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/widgets/label.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:label")]] [[clang::annotate("parent@ygui:primitive_widget")]]
label_data {
    char *text;
    float font_size;
    struct yetty_ycore_rgba color;
};

[[clang::annotate("override@ygui:label:constructor")]]
static struct yetty_ycore_void_result label_constructor(struct yetty_yclass_ctx *_yc_ctx,
                                                        struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_label_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "label_constructor: super");
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get().value);
    d->text = NULL;
    d->font_size = 14.0f;
    d->color = (struct yetty_ycore_rgba){224, 229, 228, 255}; /* BRAND_TEXT_PRIMARY */
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:label:destructor")]]
static struct yetty_ycore_void_result label_destructor(struct yetty_yclass_ctx *_yc_ctx,
                                                       struct yetty_yclass_object *_yc_obj)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get().value);
    free(d->text);
    d->text = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_label_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static uint32_t pack_rgba(struct yetty_ycore_rgba c)
{
    return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

[[clang::annotate("override@ygui:label:widget_paint")]]
static struct yetty_ycore_void_result label_paint(struct yetty_yclass_ctx *_yc_ctx,
                                                  struct yetty_yclass_object *_yc_obj,
                                                  struct yetty_ygui_emit_ctx *ctx)
{
    (void)_yc_ctx;
    struct yetty_ygui_object *obj = (struct yetty_ygui_object *)_yc_obj;
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "label_paint: NULL ctx");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get().value);
    if (!d->text || d->text[0] == '\0') {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    /* Position the baseline a font-size below the top of the widget rect
     * — TEXT_SPAN's y coord is the baseline (font ascender lifts above). */
    float x = r.min.x;
    float y = r.min.y + d->font_size;
    /* yetty_ydraw_draw_list_add_text wants a yetty_ycore_buffer view of the
     * text bytes. Construct one in-place — `data` pointer is borrowed for
     * the duration of the call. */
    struct yetty_ycore_buffer text_buf = {
        .data = (uint8_t *)d->text,
        .capacity = strlen(d->text),
        .size = strlen(d->text),
    };
    return yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, x, y, &text_buf, d->font_size,
                                          pack_rgba(d->color), /*layer=*/0, /*font_id=*/-1,
                                          /*rotation=*/0.0f);
}

struct yetty_ycore_void_result yetty_ygui_label_set_text(struct yetty_ygui_object *obj,
                                                         const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_text: NULL obj");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get().value);
    free(d->text);
    if (!text) {
        d->text = NULL;
    } else {
        size_t n = strlen(text);
        d->text = malloc(n + 1);
        if (!d->text) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_text: malloc failed");
        }
        memcpy(d->text, text, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

const char *yetty_ygui_label_get_text(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct label_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_label_class_get().value);
    return d->text;
}

struct yetty_ycore_void_result yetty_ygui_label_set_font_size(struct yetty_ygui_object *obj,
                                                              float size_px)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_font_size: NULL obj");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get().value);
    d->font_size = size_px;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_label_set_color(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rgba color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_color: NULL obj");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get().value);
    d->color = color;
    return yetty_ygui_object_set_dirty(obj);
}

#include "label.gen.c"
