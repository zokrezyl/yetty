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

struct label_data {
    char *text;
    float font_size;
    struct yetty_ycore_rgba color;
};

static struct yetty_ycore_void_result label_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_label_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "label_constructor: super");
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get());
    d->text = NULL;
    d->font_size = 14.0f;
    d->color = (struct yetty_ycore_rgba){224, 229, 228, 255}; /* BRAND_TEXT_PRIMARY */
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result label_destructor(struct yetty_ygui_object *obj)
{
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get());
    free(d->text);
    d->text = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_label_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static uint32_t pack_rgba(struct yetty_ycore_rgba c)
{
    return (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
}

static struct yetty_ycore_void_result label_paint(struct yetty_ygui_object *obj,
                                                  struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "label_paint: NULL ctx");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get());
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
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get());
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
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_label_class_get());
    return d->text;
}

struct yetty_ycore_void_result yetty_ygui_label_set_font_size(struct yetty_ygui_object *obj,
                                                              float size_px)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_font_size: NULL obj");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get());
    d->font_size = size_px;
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_label_set_color(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rgba color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_color: NULL obj");
    }
    struct label_data *d = yetty_ygui_data_get(obj, yetty_ygui_label_class_get());
    d->color = color;
    return yetty_ygui_object_set_dirty(obj);
}

static const struct yetty_ygui_op label_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, label_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, label_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, label_paint),
};

static const struct yetty_ygui_class_descriptor label_desc = {
    .name = "yetty_ygui_label",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct label_data),
};

const struct yetty_ygui_class *yetty_ygui_label_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (!cls) {
        const struct yetty_ygui_class *mixins[] = {NULL};
        struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
            &label_desc, label_ops, sizeof(label_ops) / sizeof(label_ops[0]),
            yetty_ygui_primitive_widget_class_get(), mixins, 0);
        if (YETTY_IS_ERR(r)) {
            return NULL;
        }
        cls = r.value;
    }
    return cls;
}
