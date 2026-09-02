/*
 * ygui2 checkbox — box + check fill + label; toggles on click, fires the
 * click callback with the new state readable via checkbox_checked.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_checkbox_ptr, struct yetty_ygui2_checkbox *);
struct yetty_yclass_ptr_result yetty_ygui2_checkbox_class_get(void);
struct yetty_ygui2_checkbox_ptr_result yetty_ygui2_checkbox_from(struct yetty_yclass_object *obj);

enum { YGUI2_CHECKBOX_TEXT_MAX = 64 };

struct YETTY_ANNOTATE("class@ygui2:checkbox") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_checkbox {
    char label[YGUI2_CHECKBOX_TEXT_MAX];
    uint32_t label_length;
    int checked;
    yetty_ygui2_click_cb on_toggle;
    void *on_toggle_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result checkbox_paint(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_checkbox_ptr_result data_res = yetty_ygui2_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 checkbox paint: data");
    struct yetty_ygui2_checkbox *checkbox = data_res.value;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, NULL, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 checkbox paint: rect");
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 checkbox paint: theme");
    float box = height < 16.0f ? height : 16.0f;
    struct yetty_ysdf_box outline = {
        .center_x = box * 0.5f,
        .center_y = height * 0.5f,
        .half_width = box * 0.5f,
        .half_height = box * 0.5f,
        .corner_radius = 3.0f,
    };
    struct yetty_ycore_void_result outline_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, 0, 0, checkbox->checked ? theme.accent : theme.bg_lifted, theme.border, 1.0f,
        &outline);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, outline_res, "ygui2 checkbox paint: box");
    if (checkbox->label_length) {
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)checkbox->label,
                                                 .size = checkbox->label_length};
        struct yetty_ycore_void_result text_res =
            yetty_ydraw_drawable_list_add_text(list, box + 8.0f, height * 0.5f + 5.0f, &text_buffer,
                                               13.0f, theme.text_primary, 0u, -1, 0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 checkbox paint: text");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result checkbox_on_press(struct yetty_yclass_object *obj,
                                                       float local_x, float local_y, int button_id,
                                                       int mods)
{
    (void)local_x;
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_checkbox_ptr_result data_res = yetty_ygui2_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 checkbox press: data");
    struct yetty_ygui2_checkbox *checkbox = data_res.value;
    checkbox->checked = !checkbox->checked;
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 checkbox press: dirty");
    if (checkbox->on_toggle) {
        checkbox->on_toggle(obj, checkbox->on_toggle_userdata);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_checkbox_set_label(struct yetty_yclass_object *obj,
                                                              const char *text)
{
    struct yetty_ygui2_checkbox_ptr_result data_res = yetty_ygui2_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 checkbox_set_label: data");
    struct yetty_ygui2_checkbox *checkbox = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_CHECKBOX_TEXT_MAX) {
        length = YGUI2_CHECKBOX_TEXT_MAX - 1u;
    }
    memcpy(checkbox->label, text, length);
    checkbox->label[length] = '\0';
    checkbox->label_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_checkbox_set_checked(struct yetty_yclass_object *obj,
                                                                int checked)
{
    struct yetty_ygui2_checkbox_ptr_result data_res = yetty_ygui2_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 checkbox_set_checked: data");
    if (data_res.value->checked == (checked ? 1 : 0)) {
        return YETTY_OK_VOID();
    }
    data_res.value->checked = checked ? 1 : 0;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_checkbox_checked(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_checkbox_ptr_result data_res = yetty_ygui2_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 checkbox_checked: data");
    return YETTY_OK(yetty_ycore_int, data_res.value->checked);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_checkbox_on_toggle_set(struct yetty_yclass_object *obj,
                                                                  yetty_ygui2_click_cb callback,
                                                                  void *userdata)
{
    struct yetty_ygui2_checkbox_ptr_result data_res = yetty_ygui2_checkbox_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 checkbox_on_toggle_set: data");
    data_res.value->on_toggle = callback;
    data_res.value->on_toggle_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/checkbox.c"
