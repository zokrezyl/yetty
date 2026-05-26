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

#include <yetty/ygui/primitive-widget.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/widgets/tooltip.h>
#include <stdlib.h>
#include <string.h>

struct tooltip_data {
    char *text;
};

static struct yetty_ycore_void_result tooltip_constructor(struct yetty_ygui_object *obj)
{
    /* Chain to parent first so the widget data slice is initialised
     * before we touch our own. */
    struct yetty_ycore_void_result sr = yetty_ygui_super_void(
        obj, yetty_ygui_tooltip_class_get(), (yetty_ygui_method_id_t)yetty_ygui_constructor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "tooltip_constructor: super");

    struct tooltip_data *td = yetty_ygui_data_get(obj, yetty_ygui_tooltip_class_get());
    td->text = NULL;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tooltip_destructor(struct yetty_ygui_object *obj)
{
    struct tooltip_data *td = yetty_ygui_data_get(obj, yetty_ygui_tooltip_class_get());
    free(td->text);
    td->text = NULL;
    return yetty_ygui_super_void(obj, yetty_ygui_tooltip_class_get(),
                                 (yetty_ygui_method_id_t)yetty_ygui_destructor);
}

static struct yetty_ycore_void_result tooltip_paint(struct yetty_ygui_object *obj,
                                                    struct yetty_ygui_emit_ctx *ctx)
{
    if (!ctx || !ctx->ygrid_draw_list) {
        return YETTY_ERR(yetty_ycore_void, "tooltip_paint: NULL ctx");
    }
    struct tooltip_data *td = yetty_ygui_data_get(obj, yetty_ygui_tooltip_class_get());
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
    return yetty_ydraw_draw_list_add_text(ctx->ygrid_draw_list, r.min.x + 4.0f,
                                          r.min.y + font_size + 4.0f, &text_buf, font_size,
                                          0xFFE4E5E0u, /*layer=*/0,
                                          /*font_id=*/-1, /*rotation=*/0.0f);
}

struct yetty_ycore_void_result yetty_ygui_tooltip_set_text(struct yetty_ygui_object *obj,
                                                           const char *text)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_tooltip_set_text: NULL obj");
    }
    struct tooltip_data *td = yetty_ygui_data_get(obj, yetty_ygui_tooltip_class_get());
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
    return yetty_ygui_object_set_dirty(obj);
}

const char *yetty_ygui_tooltip_get_text(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct tooltip_data *td =
        yetty_ygui_data_get((struct yetty_ygui_object *)obj, yetty_ygui_tooltip_class_get());
    return td->text;
}

/* Parent: primitive_widget. No mixins (sentinel NULL). */
static const struct yetty_ygui_class *tooltip_get_parent(void)
{
    return yetty_ygui_primitive_widget_class_get();
}

const struct yetty_ygui_class *yetty_ygui_tooltip_class_get(void)
{
    static const struct yetty_ygui_class *cls = NULL;
    if (!cls) {
        static const struct yetty_ygui_op ops[] = {
    YETTY_YGUI_OP(yetty_ygui_constructor, tooltip_constructor),
    YETTY_YGUI_OP(yetty_ygui_destructor, tooltip_destructor),
    YETTY_YGUI_OP(yetty_ygui_widget_paint, tooltip_paint),
        };
        static const struct yetty_ygui_class_descriptor desc = {.name = "yetty_ygui_tooltip",
    .type = YETTY_YGUI_CLASS_TYPE_REGULAR,
    .data_size = sizeof(struct tooltip_data),};
        const struct yetty_ygui_class *mixins[] = {NULL};
        struct yetty_ygui_class_ptr_result r = yetty_ygui_class_register(
            &desc, ops, sizeof(ops) / sizeof(ops[0]),
            tooltip_get_parent(), mixins, 0);
        if (YETTY_IS_ERR(r)) {
            return NULL;
        }
        cls = r.value;
    }
    return cls;
}
