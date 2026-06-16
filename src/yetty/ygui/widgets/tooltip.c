/*
 * ygui-tooltip.c — pilot widget for Phase 4.
 *
 * Chrome widget (figure_kind == 0). Inherits the base widget class.
 * Overrides the constructor to zero its label pointer, the destructor
 * to free it, and `paint` to write a placeholder marker into the
 * ygrid body so end-to-end pass-2 emission can be observed in tests.
 *
 * The paint marker is a 12-byte sentinel: u32 0x544F4F4C ("TOOL"), u32
 * obj->id, u32 strlen(label). The receiver doesn't currently render
 * this — it's a debug breadcrumb to prove the pass-2 walk reached the
 * widget and that data_get returned the right slice.
 */

#include "../internal.h"
#include <yetty/ygui/widget.h>

/* This TU deliberately does NOT include its own generated header — that
 * header is a downstream artifact for other modules and would redefine
 * the YETTY_YRESULT_DECLARE this TU declares manually below. The class
 * handle Result wrapper plus the codegen accessor/downcast the appended
 * tooltip.gen.c defines are declared here so the foot include and the impls
 * have them in scope. The generated public header publishes the identical
 * declarations for consumers. */
YETTY_YRESULT_DECLARE(yetty_ygui_tooltip_ptr, struct yetty_ygui_tooltip *);
struct yetty_yclass_ptr_result yetty_ygui_tooltip_class_get(void);
struct yetty_ygui_tooltip_ptr_result yetty_ygui_tooltip_from(struct yetty_yclass_object *obj);

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/drawable-list.h>
#include <stdlib.h>
#include <string.h>

struct [[clang::annotate("class@ygui:tooltip")]] [[clang::annotate("parent@ygui:primitive_widget")]]
yetty_ygui_tooltip {
    char *text;
};

[[clang::annotate("override@ygui:tooltip:constructor")]]
static struct yetty_ycore_void_result tooltip_constructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    /* Chain to parent first so the widget data slice is initialised
     * before we touch our own. */
    struct yetty_ycore_void_result sr =
        yetty_ygui_super_void(obj, yetty_ygui_tooltip_class_get().value,
                              (yetty_yclass_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tooltip_constructor: super");

    struct yetty_ygui_tooltip_ptr_result td_dr = yetty_ygui_tooltip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "tooltip_constructor: data_get");
    struct yetty_ygui_tooltip *td = td_dr.value;
    td->text = NULL;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ygui:tooltip:destructor")]]
static struct yetty_ycore_void_result tooltip_destructor(struct yetty_yclass_object *yclass_obj)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    struct yetty_ygui_tooltip_ptr_result td_dr = yetty_ygui_tooltip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "tooltip_destructor: data_get");
    struct yetty_ygui_tooltip *td = td_dr.value;
    free(td->text);
    td->text = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_tooltip_class_get().value,
                                 (yetty_yclass_method_id_t)yetty_ygui_destructor);
}

[[clang::annotate("override@ygui:tooltip:widget_paint")]]
static struct yetty_ycore_void_result tooltip_paint(struct yetty_yclass_object *yclass_obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    if (!ctx || !ctx->ygrid_drawable_list) {
        return YETTY_ERR(yetty_ycore_void, "tooltip_paint: NULL ctx");
    }
    struct yetty_ygui_tooltip_ptr_result td_dr = yetty_ygui_tooltip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "tooltip_paint: data_get");
    struct yetty_ygui_tooltip *td = td_dr.value;
    if (!td->text || td->text[0] == '\0') {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_rectangle r = yetty_ygui_widget_rect(obj);
    float font_size = 12.0f;
    struct yetty_ycore_buffer text_buf = {
        .data = (uint8_t *)td->text,
        .capacity = strlen(td->text),
        .size = strlen(td->text),
    };
    return yetty_ydraw_drawable_list_add_text(ctx->ygrid_drawable_list, r.min.x + 4.0f,
                                              r.min.y + font_size + 4.0f, &text_buf, font_size,
                                              0xFFE4E5E0u, /*layer=*/0,
                                              /*font_id=*/-1, /*rotation=*/0.0f);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_ygui_tooltip_set_text(struct yetty_yclass_object *obj,
                                                           const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tooltip_set_text: NULL obj");
    }
    struct yetty_ygui_tooltip_ptr_result td_dr = yetty_ygui_tooltip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, td_dr, "yetty_ygui_tooltip_set_text: data_get");
    struct yetty_ygui_tooltip *td = td_dr.value;
    free(td->text);
    if (!text) {
        td->text = NULL;
    } else {
        size_t n = strlen(text);
        td->text = malloc(n + 1);
        if (!td->text) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tooltip_set_text: malloc failed");
        }
        memcpy(td->text, text, n + 1);
    }
    return yetty_ygui_widget_set_dirty(obj);
}

[[clang::annotate("expose")]]
struct yetty_ycore_const_char_ptr_result yetty_ygui_tooltip_get_text(
    const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_const_char_ptr, "yetty_ygui_tooltip_get_text: invalid args");
    }
    struct yetty_ygui_tooltip_ptr_result td_dr =
        yetty_ygui_tooltip_from((struct yetty_yclass_object *)obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_char_ptr, td_dr, "yetty_ygui_tooltip_get_text: data_get");
    struct yetty_ygui_tooltip *td = td_dr.value;
    return YETTY_OK(yetty_ycore_const_char_ptr, td->text);
}

/* Parent: primitive_widget. */
static struct yetty_yclass_ptr_result tooltip_get_parent(void)
{
    return yetty_ygui_primitive_widget_class_get();
}

#include "tooltip.gen.c"
