/*
 * ygui2 button — push button: filled box + label, pressed state, click
 * callback fired on release inside the widget.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_button_ptr, struct yetty_ygui2_button *);
struct yetty_yclass_ptr_result yetty_ygui2_button_class_get(void);
struct yetty_ygui2_button_ptr_result yetty_ygui2_button_from(struct yetty_yclass_object *obj);

enum { YGUI2_BUTTON_TEXT_MAX = 64 };

struct YETTY_ANNOTATE("class@ygui2:button") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_button {
    char label[YGUI2_BUTTON_TEXT_MAX];
    uint32_t label_length;
    int pressed;
    yetty_ygui2_click_cb on_click;
    void *on_click_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result button_paint(struct yetty_yclass_object *obj,
                                                   struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_button_ptr_result data_res = yetty_ygui2_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 button paint: data");
    struct yetty_ygui2_button *button = data_res.value;
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 button paint: rect");
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 button paint: theme");
    /* Pressed: accent fill; idle: lifted bg with border. */
    uint32_t fill = button->pressed ? theme.accent : theme.bg_lifted;
    uint32_t border = theme.border;
    struct yetty_ysdf_box geometry = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 4.0f,
    };
    struct yetty_ycore_void_result box_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, fill, border, 1.0f, &geometry);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, box_res, "ygui2 button paint: box");
    if (button->label_length) {
        float font_size = 13.0f;
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)button->label,
                                                 .size = button->label_length};
        uint32_t text_color = button->pressed ? theme.bg : theme.text_primary;
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            list, 8.0f, height * 0.5f + font_size * 0.4f, &text_buffer, font_size, text_color,
            /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 button paint: text");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result button_on_press(struct yetty_yclass_object *obj, float local_x,
                                                     float local_y, int button_id, int mods)
{
    (void)local_x;
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_button_ptr_result data_res = yetty_ygui2_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 button press: data");
    data_res.value->pressed = 1;
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 button press: dirty");
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_release")
static struct yetty_ycore_int_result button_on_release(struct yetty_yclass_object *obj,
                                                       float local_x, float local_y, int button_id,
                                                       int mods)
{
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_button_ptr_result data_res = yetty_ygui2_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 button release: data");
    struct yetty_ygui2_button *button = data_res.value;
    int was_pressed = button->pressed;
    button->pressed = 0;
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 button release: dirty");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "ygui2 button release: rect");
    int inside = local_x >= 0.0f && local_y >= 0.0f && local_x < width && local_y < height;
    if (was_pressed && inside && button->on_click) {
        button->on_click(obj, button->on_click_userdata);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_button_set_label(struct yetty_yclass_object *obj,
                                                            const char *text)
{
    struct yetty_ygui2_button_ptr_result data_res = yetty_ygui2_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 button_set_label: data");
    struct yetty_ygui2_button *button = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_BUTTON_TEXT_MAX) {
        length = YGUI2_BUTTON_TEXT_MAX - 1u;
    }
    memcpy(button->label, text, length);
    button->label[length] = '\0';
    button->label_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_button_on_click_set(struct yetty_yclass_object *obj,
                                                               yetty_ygui2_click_cb callback,
                                                               void *userdata)
{
    struct yetty_ygui2_button_ptr_result data_res = yetty_ygui2_button_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 button_on_click_set: data");
    data_res.value->on_click = callback;
    data_res.value->on_click_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/button.c"
