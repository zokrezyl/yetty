/*
 * ygui2 toggle — an on/off switch: pill track + sliding knob + label.
 * Toggles on press; the click callback reads the new state via
 * toggle_checked. Same contract shape as checkbox, different skin.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_toggle_ptr, struct yetty_ygui2_toggle *);
struct yetty_yclass_ptr_result yetty_ygui2_toggle_class_get(void);
struct yetty_ygui2_toggle_ptr_result yetty_ygui2_toggle_from(struct yetty_yclass_object *obj);

enum { YGUI2_TOGGLE_TEXT_MAX = 64 };

struct YETTY_ANNOTATE("class@ygui2:toggle") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_toggle {
    char label[YGUI2_TOGGLE_TEXT_MAX];
    uint32_t label_length;
    int checked;
    yetty_ygui2_click_cb on_toggle;
    void *on_toggle_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result toggle_paint(struct yetty_yclass_object *obj,
                                                   struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_toggle_ptr_result data_res = yetty_ygui2_toggle_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 toggle paint: data");
    struct yetty_ygui2_toggle *toggle = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 toggle paint: theme");
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, NULL, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 toggle paint: rect");
    float track_height = height < 18.0f ? height : 18.0f;
    float track_width = track_height * 2.0f;
    struct yetty_ysdf_box track = {
        .center_x = track_width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = track_width * 0.5f,
        .half_height = track_height * 0.5f,
        .corner_radius = track_height * 0.5f,
    };
    struct yetty_ycore_void_result track_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, toggle->checked ? theme.accent : theme.bg_row, theme.border,
        1.0f, &track);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, track_res, "ygui2 toggle paint: track");
    float knob_radius = track_height * 0.5f - 3.0f;
    struct yetty_ysdf_circle knob = {
        .center_x = toggle->checked ? track_width - track_height * 0.5f : track_height * 0.5f,
        .center_y = height * 0.5f,
        .radius = knob_radius,
    };
    struct yetty_ycore_void_result knob_res = yetty_ydraw_drawable_list_add_cmd_add_circle(
        list, /*id=*/0, /*z_order=*/0, toggle->checked ? theme.bg : theme.text_secondary, 0u, 0.0f,
        &knob);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_res, "ygui2 toggle paint: knob");
    if (toggle->label_length) {
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)toggle->label,
                                                 .size = toggle->label_length};
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            list, track_width + 8.0f, height * 0.5f + 5.0f, &text_buffer, 13.0f, theme.text_primary,
            /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 toggle paint: text");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result toggle_on_press(struct yetty_yclass_object *obj, float local_x,
                                                     float local_y, int button_id, int mods)
{
    (void)local_x;
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_toggle_ptr_result data_res = yetty_ygui2_toggle_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 toggle press: data");
    struct yetty_ygui2_toggle *toggle = data_res.value;
    toggle->checked = !toggle->checked;
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 toggle press: dirty");
    if (toggle->on_toggle) {
        toggle->on_toggle(obj, toggle->on_toggle_userdata);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_toggle_set_label(struct yetty_yclass_object *obj,
                                                            const char *text)
{
    struct yetty_ygui2_toggle_ptr_result data_res = yetty_ygui2_toggle_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 toggle_set_label: data");
    struct yetty_ygui2_toggle *toggle = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_TOGGLE_TEXT_MAX) {
        length = YGUI2_TOGGLE_TEXT_MAX - 1u;
    }
    memcpy(toggle->label, text, length);
    toggle->label[length] = '\0';
    toggle->label_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_toggle_set_checked(struct yetty_yclass_object *obj,
                                                              int checked)
{
    struct yetty_ygui2_toggle_ptr_result data_res = yetty_ygui2_toggle_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 toggle_set_checked: data");
    if (data_res.value->checked == (checked ? 1 : 0)) {
        return YETTY_OK_VOID();
    }
    data_res.value->checked = checked ? 1 : 0;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_toggle_checked(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_toggle_ptr_result data_res = yetty_ygui2_toggle_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 toggle_checked: data");
    return YETTY_OK(yetty_ycore_int, data_res.value->checked);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_toggle_on_toggle_set(struct yetty_yclass_object *obj,
                                                                yetty_ygui2_click_cb callback,
                                                                void *userdata)
{
    struct yetty_ygui2_toggle_ptr_result data_res = yetty_ygui2_toggle_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 toggle_on_toggle_set: data");
    data_res.value->on_toggle = callback;
    data_res.value->on_toggle_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/toggle.c"
