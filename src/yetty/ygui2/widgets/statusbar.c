/*
 * ygui2 statusbar — a full-width row surface with left- and right-aligned
 * text. No interaction; both texts are independent skin updates.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_statusbar_ptr, struct yetty_ygui2_statusbar *);
struct yetty_yclass_ptr_result yetty_ygui2_statusbar_class_get(void);
struct yetty_ygui2_statusbar_ptr_result yetty_ygui2_statusbar_from(struct yetty_yclass_object *obj);

enum { YGUI2_STATUSBAR_TEXT_MAX = 96 };

struct YETTY_ANNOTATE("class@ygui2:statusbar") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_statusbar {
    char left_text[YGUI2_STATUSBAR_TEXT_MAX];
    uint32_t left_length;
    char right_text[YGUI2_STATUSBAR_TEXT_MAX];
    uint32_t right_length;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result statusbar_paint(struct yetty_yclass_object *obj,
                                                      struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_statusbar_ptr_result data_res = yetty_ygui2_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 statusbar paint: data");
    struct yetty_ygui2_statusbar *statusbar = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 statusbar paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 statusbar paint: rect");
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 0.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_row, 0u, 0.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 statusbar paint: body");
    float glyph_size = 12.0f;
    float text_baseline = height * 0.5f + glyph_size * 0.35f;
    if (statusbar->left_length) {
        struct yetty_ycore_buffer left_buffer = {.data = (uint8_t *)statusbar->left_text,
                                                 .size = statusbar->left_length};
        struct yetty_ycore_void_result left_res = yetty_ydraw_drawable_list_add_text(
            list, 8.0f, text_baseline, &left_buffer, glyph_size, theme.text_secondary,
            /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, left_res, "ygui2 statusbar paint: left");
    }
    if (statusbar->right_length) {
        float right_width = (float)statusbar->right_length * glyph_size * 0.55f;
        struct yetty_ycore_buffer right_buffer = {.data = (uint8_t *)statusbar->right_text,
                                                  .size = statusbar->right_length};
        struct yetty_ycore_void_result right_res = yetty_ydraw_drawable_list_add_text(
            list, width - right_width - 8.0f, text_baseline, &right_buffer, glyph_size,
            theme.text_muted, /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, right_res, "ygui2 statusbar paint: right");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result statusbar_store(struct yetty_yclass_object *obj,
                                                      char *destination,
                                                      uint32_t *destination_length,
                                                      const char *text)
{
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_STATUSBAR_TEXT_MAX) {
        length = YGUI2_STATUSBAR_TEXT_MAX - 1u;
    }
    if (*destination_length == (uint32_t)length && memcmp(destination, text, length) == 0) {
        return YETTY_OK_VOID();
    }
    memcpy(destination, text, length);
    destination[length] = '\0';
    *destination_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_statusbar_set_left(struct yetty_yclass_object *obj,
                                                              const char *text)
{
    struct yetty_ygui2_statusbar_ptr_result data_res = yetty_ygui2_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 statusbar_set_left: data");
    return statusbar_store(obj, data_res.value->left_text, &data_res.value->left_length, text);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_statusbar_set_right(struct yetty_yclass_object *obj,
                                                               const char *text)
{
    struct yetty_ygui2_statusbar_ptr_result data_res = yetty_ygui2_statusbar_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 statusbar_set_right: data");
    return statusbar_store(obj, data_res.value->right_text, &data_res.value->right_length, text);
}

#include "yetty/gen/impl/ygui2/widgets/statusbar.c"
