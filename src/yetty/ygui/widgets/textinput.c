/*
 * ygui-textinput.c — single-line text input.
 *
 * Inherits primitive_widget + clickable. Click-to-focus; the app reads
 * the focused widget and routes typed chars there. Backspace deletes a
 * char.
 */

#include "../internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ygui/widgets/textinput.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_BORDER 0xFF474A36u
#define COLOR_BORDER_FOCUS 0xFF92A86Bu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_PLACEHOLDER 0xFFA8A79Fu

struct textinput_data {
    char *text;
    size_t text_len;
    size_t cap;
    char *placeholder;
    int focused;
};

static struct yetty_ycore_void_result on_click_focus(struct yetty_ygui_object *obj, void *userdata)
{
    (void)userdata;
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    d->focused = 1;
    return yetty_ygui_object_set_dirty(obj);
}

static struct yetty_ycore_void_result textinput_constructor(struct yetty_ygui_object *obj)
{
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_textinput_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "textinput_constructor: super");
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    d->text = NULL;
    d->text_len = 0;
    d->cap = 0;
    d->placeholder = NULL;
    d->focused = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click_focus, NULL);
}

static struct yetty_ycore_void_result textinput_destructor(struct yetty_ygui_object *obj)
{
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    free(d->text);
    free(d->placeholder);
    d->text = NULL;
    d->placeholder = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_textinput_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
    if (radius <= 0.0f) {
        struct yetty_ysdf_box geom = {
            .center_x = x + w * 0.5f, .center_y = y + h * 0.5f,
            .half_width = w * 0.5f, .half_height = h * 0.5f,
        };
        return yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u, 0.0f,
                                                     &geom);
    }
    if (radius > w * 0.5f) radius = w * 0.5f;
    if (radius > h * 0.5f) radius = h * 0.5f;
    struct yetty_ysdf_rounded_box geom = {
        .center_x = x + w * 0.5f, .center_y = y + h * 0.5f,
        .half_width = w * 0.5f, .half_height = h * 0.5f,
        .radius_top_right = radius, .radius_bottom_right = radius,
        .radius_top_left = radius, .radius_bottom_left = radius,
    };
    return yetty_ydraw_draw_list_add_cmd_add_rounded_box(ctx->ygrid_draw_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
}

static struct yetty_ycore_void_result textinput_paint(struct yetty_ygui_object *obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "textinput_paint: NULL ctx");
    }
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) return YETTY_OK_VOID();
    struct yetty_ycore_void_result rr = paint_box(ctx, r.min.x, r.min.y, w, h, COLOR_BG, 4.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: bg");
    /* Outline = 1.5px stroke approximation via two boxes — light. The
     * SDF stroke args on the box primitive could be used; quick path
     * draws the body and skips the stroke for now. */
    uint32_t border = d->focused ? COLOR_BORDER_FOCUS : COLOR_BORDER;
    (void)border;
    const char *text = (d->text && d->text[0]) ? d->text : d->placeholder;
    uint32_t color = (d->text && d->text[0]) ? COLOR_TEXT : COLOR_PLACEHOLDER;
    if (text && text[0]) {
        float fs = 14.0f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        struct yetty_ycore_buffer tb = {.data = (uint8_t *)text,
                                        .capacity = strlen(text),
                                        .size = strlen(text)};
        rr = yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.min.x + 10.0f, ty, &tb, fs,
                                            color, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: text");
    }
    if (d->focused) {
        /* Caret — thin vertical bar 2px wide right after the text. */
        float fs = 14.0f;
        float text_w = d->text ? (float)d->text_len * fs * 0.55f : 0.0f;
        struct yetty_ysdf_box geom = {
            .center_x = r.min.x + 10.0f + text_w + 1.0f,
            .center_y = r.min.y + h * 0.5f,
            .half_width = 1.0f,
            .half_height = (h - 8.0f) * 0.5f,
        };
        rr = yetty_ydraw_draw_list_add_cmd_add_box(ctx->ygrid_draw_list, 0u, 0u,
                                                   COLOR_BORDER_FOCUS, 0u, 0.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: caret");
    }
    return YETTY_OK_VOID();
}

static int ensure_cap(struct textinput_data *d, size_t need)
{
    if (need <= d->cap) return 1;
    size_t cap = d->cap ? d->cap * 2 : 32;
    while (cap < need) cap *= 2;
    char *nb = realloc(d->text, cap);
    if (!nb) return 0;
    d->text = nb;
    d->cap = cap;
    return 1;
}

struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_ygui_object *obj,
                                                             const char *text)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "textinput_set_text: NULL obj");
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    size_t n = text ? strlen(text) : 0;
    if (!ensure_cap(d, n + 1)) return YETTY_ERR(yetty_ycore_void, "textinput_set_text: realloc");
    if (text) memcpy(d->text, text, n);
    if (d->text) d->text[n] = '\0';
    d->text_len = n;
    return yetty_ygui_object_set_dirty(obj);
}

const char *yetty_ygui_textinput_get_text(const struct yetty_ygui_object *obj)
{
    if (!obj) return NULL;
    struct textinput_data *d =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_textinput_class_get());
    return d->text ? d->text : "";
}

struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_ygui_object *obj,
                                                                    const char *placeholder)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "textinput_set_placeholder: NULL obj");
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    free(d->placeholder);
    if (!placeholder) {
        d->placeholder = NULL;
    } else {
        size_t n = strlen(placeholder);
        d->placeholder = malloc(n + 1);
        if (!d->placeholder) return YETTY_ERR(yetty_ycore_void, "textinput_set_placeholder: malloc");
        memcpy(d->placeholder, placeholder, n + 1);
    }
    return yetty_ygui_object_set_dirty(obj);
}

struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_ygui_object *obj,
                                                              int focused)
{
    if (!obj) return YETTY_ERR(yetty_ycore_void, "textinput_set_focus: NULL obj");
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    d->focused = focused ? 1 : 0;
    return yetty_ygui_object_set_dirty(obj);
}

int yetty_ygui_textinput_handle_key(struct yetty_ygui_object *obj, uint32_t key)
{
    if (!obj) return 0;
    struct textinput_data *d = yetty_ygui_data_get(obj, yetty_ygui_textinput_class_get());
    if (!d->focused) return 0;
    if (key == 0x08 || key == 0x7F) {  /* backspace / DEL */
        if (d->text_len > 0) {
            d->text_len--;
            d->text[d->text_len] = '\0';
            struct yetty_ycore_void_result r = yetty_ygui_object_set_dirty(obj);
            if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        }
        return 1;
    }
    if (key >= 32 && key < 127) {
        if (!ensure_cap(d, d->text_len + 2)) return 1;
        d->text[d->text_len++] = (char)key;
        d->text[d->text_len] = '\0';
        struct yetty_ycore_void_result r = yetty_ygui_object_set_dirty(obj);
        if (YETTY_IS_ERR(r)) yetty_ycore_error_destroy(r.error);
        return 1;
    }
    return 0;
}


static const struct yetty_ygui_op textinput_ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, textinput_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, textinput_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, textinput_paint),
};

static const struct yetty_ygui_class_descriptor textinput_desc = {
    .name = "yetty_ygui_textinput",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct textinput_data),
};

YETTY_YGUI_DEFINE_CLASS(yetty_ygui_textinput_class_get, &textinput_desc, textinput_ops, yetty_ygui_primitive_widget_class_get(), yetty_ygui_clickable_mixin_get(), NULL)
