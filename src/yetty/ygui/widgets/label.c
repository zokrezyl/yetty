/*
 * ygui-label.c — static text widget.
 *
 * The paint hook writes a {"LABL", id, font_size, rgba, text_len, text}
 * marker into the engine's ygrid body. The actual GLYPH/TEXT_DRAWABLE_LIST
 * record encoding will land when the receiver-side renderer hook is
 * stabilised — for now this proves the pass-2 walk hits the widget
 * and that data_get returns the right slice.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * label.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_label_ptr, struct yetty_ygui_label *);
struct yetty_yclass_ptr_result yetty_ygui_label_class_get(void);
struct yetty_ygui_label_ptr_result yetty_ygui_label_from(struct yetty_yclass_object *obj);

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:label")]] [[clang::annotate("parent@ygui:primitive_widget")]]
yetty_ygui_label {
    char *text;
    float font_size;
    struct yetty_ycore_rgba color;
};

[[clang::annotate("override@ygui:label:constructor")]]
static struct yetty_ycore_void_result label_constructor(struct yetty_yclass_ctx *yclass_ctx,
                                                        struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_label_class_get().value, (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "label_constructor: super");
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "label_constructor: data_get");
    struct yetty_ygui_label *d = d_dr.value;
    d->text = NULL;
    d->font_size = 14.0f;
    d->color = (struct yetty_ycore_rgba){224, 229, 228, 255}; /* BRAND_TEXT_PRIMARY */
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:label:destructor")]]
static struct yetty_ycore_void_result label_destructor(struct yetty_yclass_ctx *yclass_ctx,
                                                       struct yetty_yclass_object *yclass_obj)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "label_destructor: data_get");
    struct yetty_ygui_label *d = d_dr.value;
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
static struct yetty_ycore_void_result label_paint(struct yetty_yclass_ctx *yclass_ctx,
                                                  struct yetty_yclass_object *yclass_obj,
                                                  struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "label_paint: NULL ctx");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "label_paint: data_get");
    struct yetty_ygui_label *d = d_dr.value;
    if (!d->text || d->text[0] == '\0') {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    /* Position the baseline a font-size below the top of the widget rect
     * — TEXT_DRAWABLE_LIST's y coord is the baseline (font ascender lifts above). */
    float x = r.min.x;
    float y = r.min.y + d->font_size;
    /* yetty_ydraw_drawable_list_add_text wants a yetty_ycore_buffer view of the
     * text bytes. Construct one in-place — `data` pointer is borrowed for
     * the duration of the call. */
    struct yetty_ycore_buffer text_buf = {
        .data = (uint8_t *)d->text,
        .capacity = strlen(d->text),
        .size = strlen(d->text),
    };
    return yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, x, y, &text_buf,
                                              d->font_size, pack_rgba(d->color), /*layer=*/0,
                                              /*font_id=*/-1,
                                              /*rotation=*/0.0f);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_label_set_text(struct yetty_yclass_object *obj,
                                                         const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_text: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_label_set_text: data_get");
    struct yetty_ygui_label *d = d_dr.value;
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

[[clang::annotate("expose")]]
struct yetty_ycore_const_char_ptr_result yetty_ygui_label_get_text(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ygui_label_get_text: invalid args");
    }
    struct yetty_ygui_void_ptr_result d_dr = yetty_ygui_data_get_result(
        (struct yetty_yclass_object *)obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, d_dr, "yetty_ygui_label_get_text: data_get");
    struct yetty_ygui_label *d = d_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, d->text);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_label_set_font_size(struct yetty_yclass_object *obj,
                                                              float size_px)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_font_size: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_label_set_font_size: data_get");
    struct yetty_ygui_label *d = d_dr.value;
    d->font_size = size_px;
    return yetty_ygui_object_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_label_set_color(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_rgba color)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_label_set_color: NULL obj");
    }
    struct yetty_ygui_void_ptr_result d_dr =
        yetty_ygui_data_get_result(obj, yetty_ygui_label_class_get().value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, d_dr, "yetty_ygui_label_set_color: data_get");
    struct yetty_ygui_label *d = d_dr.value;
    d->color = color;
    return yetty_ygui_object_set_dirty(obj);
}

#include "label.gen.c"
