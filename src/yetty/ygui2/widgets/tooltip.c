/*
 * ygui2 tooltip — a small overlay text bubble. Mounted via overlay_add,
 * placed absolutely by the app, shown/hidden with set_visible. Inert: it
 * consumes nothing, so clicks pass to whatever is under it.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/ysdf/funcs.gen.h>

#include "yetty/gen/impl/ygui2/widget.h"

YETTY_YRESULT_DECLARE(yetty_ygui2_tooltip_ptr, struct yetty_ygui2_tooltip *);
struct yetty_yclass_ptr_result yetty_ygui2_tooltip_class_get(void);
struct yetty_ygui2_tooltip_ptr_result yetty_ygui2_tooltip_from(struct yetty_yclass_object *obj);

enum { YGUI2_TOOLTIP_TEXT_MAX = 96 };

struct YETTY_ANNOTATE("class@ygui2:tooltip") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_tooltip {
    char text[YGUI2_TOOLTIP_TEXT_MAX];
    uint32_t text_length;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result tooltip_paint(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_tooltip_ptr_result data_res = yetty_ygui2_tooltip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 tooltip paint: data");
    struct yetty_ygui2_tooltip *tooltip = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 tooltip paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 tooltip paint: rect");
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 4.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, theme.accent_deep, 1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 tooltip paint: body");
    if (tooltip->text_length) {
        float glyph_size = 12.0f;
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)tooltip->text,
                                                 .size = tooltip->text_length};
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            list, 8.0f, height * 0.5f + glyph_size * 0.35f, &text_buffer, glyph_size,
            theme.text_primary, /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 tooltip paint: text");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_tooltip_set_text(struct yetty_yclass_object *obj,
                                                            const char *text)
{
    struct yetty_ygui2_tooltip_ptr_result data_res = yetty_ygui2_tooltip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 tooltip_set_text: data");
    struct yetty_ygui2_tooltip *tooltip = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_TOOLTIP_TEXT_MAX) {
        length = YGUI2_TOOLTIP_TEXT_MAX - 1u;
    }
    memcpy(tooltip->text, text, length);
    tooltip->text[length] = '\0';
    tooltip->text_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

#include "yetty/gen/impl/ygui2/widgets/tooltip.c"
