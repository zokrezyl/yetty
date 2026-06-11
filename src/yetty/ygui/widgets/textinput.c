/*
 * ygui-textinput.c — single-line text input.
 *
 * Inherits primitive_widget + clickable. Click-to-focus; the app reads
 * the focused widget and routes typed chars there. Backspace deletes a
 * char.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * textinput.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_textinput_ptr, struct yetty_ygui_textinput *);
struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void);
struct yetty_ygui_textinput_ptr_result yetty_ygui_textinput_from(struct yetty_yclass_object *obj);

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/mixins/clickable.h>
#include <yetty/ygui/primitive-widget.h>
#include <yetty/ysdf/funcs.gen.h>

#include <stdlib.h>
#include <string.h>

#define COLOR_BG 0xFF14100Bu
#define COLOR_BORDER 0xFF474A36u
#define COLOR_BORDER_FOCUS 0xFF92A86Bu
#define COLOR_TEXT 0xFFE4E5E0u
#define COLOR_PLACEHOLDER 0xFFA8A79Fu

struct [[clang::annotate("class@ygui:textinput")]] [[clang::annotate(
    "parent@ygui:primitive_widget")]] [[clang::annotate("uses@ygui:clickable")]]
yetty_ygui_textinput {
    char *text;
    size_t text_len;
    size_t cap;
    char *placeholder;
    int focused;
    size_t cursor; /* caret byte offset into text (0..text_len) */
};

/* Font metrics used for caret placement + click-to-position. Must match the
 * values used by the paint code below. */
#define TEXTINPUT_FONT_SIZE 14.0f
#define TEXTINPUT_TEXT_PAD 10.0f
#define TEXTINPUT_CHAR_W (TEXTINPUT_FONT_SIZE * 0.55f)

static struct yetty_ycore_void_result on_click_focus(struct yetty_yclass_ctx *yclass_ctx,
                                                     struct yetty_yclass_object *yclass_obj,
                                                     void *userdata)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    (void)userdata;
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "on_click_focus: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->focused = 1;
    /* Place the caret at the clicked character. */
    float px = 0.0f, py = 0.0f;
    {
        struct yetty_ycore_void_result pp_r = yetty_ygui_clickable_press_pos(obj, &px, &py);
        if (YETTY_IS_ERR(pp_r)) {
            yetty_ycore_error_destroy(pp_r.error);
        }
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float rel = px - (r.min.x + TEXTINPUT_TEXT_PAD);
    long idx = (long)((rel / TEXTINPUT_CHAR_W) + 0.5f);
    if (idx < 0) {
        idx = 0;
    }
    if ((size_t)idx > d->text_len) {
        idx = (long)d->text_len;
    }
    d->cursor = (size_t)idx;
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("override@ygui:textinput:constructor")]]
static struct yetty_ycore_void_result textinput_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                            struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_textinput_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "textinput_constructor: super");
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_constructor: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->text = NULL;
    d->text_len = 0;
    d->cap = 0;
    d->placeholder = NULL;
    d->focused = 0;
    d->cursor = 0;
    return yetty_ygui_clickable_on_click_set(obj, on_click_focus, NULL);
}

[[clang::annotate("override@ygui:textinput:destructor")]]
static struct yetty_ycore_void_result textinput_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                           struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_destructor: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    free(d->text);
    free(d->placeholder);
    d->text = NULL;
    d->placeholder = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_textinput_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result paint_box(struct yetty_ygui_emit_ctx *ctx, float x, float y,
                                                float w, float h, uint32_t fill, float radius)
{
    if (radius <= 0.0f) {
        struct yetty_ysdf_box geom = {
            .center_x = x + w * 0.5f,
            .center_y = y + h * 0.5f,
            .half_width = w * 0.5f,
            .half_height = h * 0.5f,
        };
        return yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u, fill, 0u,
                                                         0.0f, &geom);
    }
    if (radius > w * 0.5f) {
        radius = w * 0.5f;
    }
    if (radius > h * 0.5f) {
        radius = h * 0.5f;
    }
    struct yetty_ysdf_rounded_box geom = {
        .center_x = x + w * 0.5f,
        .center_y = y + h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .radius_top_right = radius,
        .radius_bottom_right = radius,
        .radius_top_left = radius,
        .radius_bottom_left = radius,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_rounded_box(ctx->ygrid_drawable_list, 0u, 0u, fill,
                                                             0u, 0.0f, &geom);
}

[[clang::annotate("override@ygui:textinput:widget_paint")]]
static struct yetty_ycore_void_result textinput_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                      struct yetty_yclass_object *yclass_obj,
                                                      struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "textinput_paint: NULL ctx");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "textinput_paint: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float w = r.max.x - r.min.x;
    float h = r.max.y - r.min.y;
    if (w <= 0.0f || h <= 0.0f) {
        return YETTY_OK_VOID();
    }
    /* Outline = 1.5px frame: paint the border-colored rounded box, then
     * inset the fill on top so a thin ring remains. Without a visible frame
     * the field is invisible whenever its fill matches the surrounding
     * surface (e.g. a toolbar on the same brand background). */
    uint32_t border = d->focused ? COLOR_BORDER_FOCUS : COLOR_BORDER;
    struct yetty_ycore_void_result rr = paint_box(ctx, r.min.x, r.min.y, w, h, border, 4.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: border");
    const float inset = 1.5f;
    if (w > 2.0f * inset && h > 2.0f * inset) {
        rr = paint_box(ctx, r.min.x + inset, r.min.y + inset, w - 2.0f * inset, h - 2.0f * inset,
                       COLOR_BG, 3.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: bg");
    }
    const char *text = (d->text && d->text[0]) ? d->text : d->placeholder;
    uint32_t color = (d->text && d->text[0]) ? COLOR_TEXT : COLOR_PLACEHOLDER;
    if (text && text[0]) {
        float fs = 14.0f;
        float ty = r.min.y + (h + fs) * 0.5f - 3.0f;
        struct yetty_ycore_buffer tb = {
            .data = (uint8_t *)text, .capacity = strlen(text), .size = strlen(text)};
        rr = yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, r.min.x + 10.0f, ty, &tb,
                                                fs, color, 0, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: text");
    }
    if (d->focused) {
        /* Caret — thin vertical bar at the cursor's character offset. */
        float text_w = (float)d->cursor * TEXTINPUT_CHAR_W;
        struct yetty_ysdf_box geom = {
            .center_x = r.min.x + TEXTINPUT_TEXT_PAD + text_w + 1.0f,
            .center_y = r.min.y + h * 0.5f,
            .half_width = 1.0f,
            .half_height = (h - 8.0f) * 0.5f,
        };
        rr = yetty_ydraw_drawable_list_add_cmd_add_box(ctx->ygrid_drawable_list, 0u, 0u,
                                                       COLOR_BORDER_FOCUS, 0u, 0.0f, &geom);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "textinput_paint: caret");
    }
    return YETTY_OK_VOID();
}

static int ensure_cap(struct yetty_ygui_textinput *d, size_t need)
{
    if (need <= d->cap) {
        return 1;
    }
    size_t cap = d->cap ? d->cap * 2 : 32;
    while (cap < need) {
        cap *= 2;
    }
    char *nb = realloc(d->text, cap);
    if (!nb) {
        return 0;
    }
    d->text = nb;
    d->cap = cap;
    return 1;
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_yclass_object *obj,
                                                             const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_text: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textinput_set_text: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    size_t n = text ? strlen(text) : 0;
    if (!ensure_cap(d, n + 1)) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_text: realloc");
    }
    if (text) {
        memcpy(d->text, text, n);
    }
    if (d->text) {
        d->text[n] = '\0';
    }
    d->text_len = n;
    d->cursor = n; /* caret to end on programmatic set */
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_const_char_ptr_result yetty_ygui_textinput_get_text(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ygui_textinput_get_text: invalid args");
    }
    struct yetty_ygui_textinput_ptr_result d_dr =
        yetty_ygui_textinput_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr,
                        "yetty_ygui_textinput_get_text: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->text ? d->text : "");
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_yclass_object *obj,
                                                                    const char *placeholder)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_placeholder: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textinput_set_placeholder: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    free(d->placeholder);
    if (!placeholder) {
        d->placeholder = NULL;
    } else {
        size_t n = strlen(placeholder);
        d->placeholder = malloc(n + 1);
        if (!d->placeholder) {
            return YETTY_ERR(yetty_ycore_void, "textinput_set_placeholder: malloc");
        }
        memcpy(d->placeholder, placeholder, n + 1);
    }
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_yclass_object *obj,
                                                              int focused)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "textinput_set_focus: NULL obj");
    }
    struct yetty_ygui_textinput_ptr_result d_dr = yetty_ygui_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_textinput_set_focus: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    d->focused = focused ? 1 : 0;
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_int_result yetty_ygui_textinput_handle_key(struct yetty_yclass_object *obj,
                                                              uint32_t key)
{
    if (!obj) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_yclass_ptr_result class_result = yetty_ygui_textinput_class_get();
    YETTY_RETURN_IF_ERR(yetty_ycore_int, class_result, "yetty_ygui_textinput_handle_key: class");
    struct yetty_yclass_void_ptr_result d_dr = yetty_yclass_object_data(obj, class_result.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, d_dr, "yetty_ygui_textinput_handle_key: data_get");
    struct yetty_ygui_textinput *d = d_dr.value;
    if (!d->focused) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    if (d->cursor > d->text_len) {
        d->cursor = d->text_len;
    }
    int changed = 0;
    int consumed = 1;
    switch (key) {
    case 0x08:
    case 0x7F: /* backspace — delete the char before the caret */
        if (d->cursor > 0) {
            memmove(d->text + d->cursor - 1, d->text + d->cursor, d->text_len - d->cursor);
            d->cursor--;
            d->text_len--;
            d->text[d->text_len] = '\0';
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_DELETE: /* delete the char at the caret */
        if (d->cursor < d->text_len) {
            memmove(d->text + d->cursor, d->text + d->cursor + 1, d->text_len - d->cursor - 1);
            d->text_len--;
            d->text[d->text_len] = '\0';
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_ARROW_LEFT:
        if (d->cursor > 0) {
            d->cursor--;
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_ARROW_RIGHT:
        if (d->cursor < d->text_len) {
            d->cursor++;
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_HOME:
        if (d->cursor != 0) {
            d->cursor = 0;
            changed = 1;
        }
        break;
    case YETTY_YGUI_KEY_END:
        if (d->cursor != d->text_len) {
            d->cursor = d->text_len;
            changed = 1;
        }
        break;
    default:
        if (key >= 32 && key < 127) { /* insert printable at the caret */
            if (!ensure_cap(d, d->text_len + 2)) {
                return YETTY_OK(yetty_ycore_int, 1);
            }
            memmove(d->text + d->cursor + 1, d->text + d->cursor, d->text_len - d->cursor);
            d->text[d->cursor] = (char)key;
            d->cursor++;
            d->text_len++;
            d->text[d->text_len] = '\0';
            changed = 1;
        } else {
            consumed = 0;
        }
        break;
    }
    if (changed) {
        struct yetty_ycore_void_result r = yetty_ygui_widget_set_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, r, "yetty_ygui_textinput_handle_key: dirty");
    }
    return YETTY_OK(yetty_ycore_int, consumed);
}

#include "textinput.gen.c"
