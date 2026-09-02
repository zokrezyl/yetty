/*
 * ygui2 radio — circle + selection dot + label. The widget is deliberately
 * dumb about groups: a press SELECTS it (never deselects) and fires
 * on_select; the app (or a helper container) clears the siblings. That
 * keeps group semantics in one place — the owner of the group.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_radio_ptr, struct yetty_ygui2_radio *);
struct yetty_yclass_ptr_result yetty_ygui2_radio_class_get(void);
struct yetty_ygui2_radio_ptr_result yetty_ygui2_radio_from(struct yetty_yclass_object *obj);

enum { YGUI2_RADIO_TEXT_MAX = 64 };

struct YETTY_ANNOTATE("class@ygui2:radio") YETTY_ANNOTATE("parent@ygui2:widget") yetty_ygui2_radio {
    char label[YGUI2_RADIO_TEXT_MAX];
    uint32_t label_length;
    int selected;
    yetty_ygui2_click_cb on_select;
    void *on_select_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result radio_paint(struct yetty_yclass_object *obj,
                                                  struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_radio_ptr_result data_res = yetty_ygui2_radio_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 radio paint: data");
    struct yetty_ygui2_radio *radio = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 radio paint: theme");
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, NULL, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 radio paint: rect");
    float ring_diameter = height < 16.0f ? height : 16.0f;
    struct yetty_ysdf_circle ring = {
        .center_x = ring_diameter * 0.5f,
        .center_y = height * 0.5f,
        .radius = ring_diameter * 0.5f,
    };
    struct yetty_ycore_void_result ring_res = yetty_ydraw_drawable_list_add_cmd_add_circle(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, theme.border, 1.0f, &ring);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ring_res, "ygui2 radio paint: ring");
    if (radio->selected) {
        struct yetty_ysdf_circle dot = {
            .center_x = ring_diameter * 0.5f,
            .center_y = height * 0.5f,
            .radius = ring_diameter * 0.25f,
        };
        struct yetty_ycore_void_result dot_res = yetty_ydraw_drawable_list_add_cmd_add_circle(
            list, /*id=*/0, /*z_order=*/0, theme.accent, 0u, 0.0f, &dot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dot_res, "ygui2 radio paint: dot");
    }
    if (radio->label_length) {
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)radio->label,
                                                 .size = radio->label_length};
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            list, ring_diameter + 8.0f, height * 0.5f + 5.0f, &text_buffer, 13.0f,
            theme.text_primary, /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 radio paint: text");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result radio_on_press(struct yetty_yclass_object *obj, float local_x,
                                                    float local_y, int button_id, int mods)
{
    (void)local_x;
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_radio_ptr_result data_res = yetty_ygui2_radio_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 radio press: data");
    struct yetty_ygui2_radio *radio = data_res.value;
    if (!radio->selected) {
        radio->selected = 1;
        struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 radio press: dirty");
        if (radio->on_select) {
            radio->on_select(obj, radio->on_select_userdata);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_radio_set_label(struct yetty_yclass_object *obj,
                                                           const char *text)
{
    struct yetty_ygui2_radio_ptr_result data_res = yetty_ygui2_radio_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 radio_set_label: data");
    struct yetty_ygui2_radio *radio = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_RADIO_TEXT_MAX) {
        length = YGUI2_RADIO_TEXT_MAX - 1u;
    }
    memcpy(radio->label, text, length);
    radio->label[length] = '\0';
    radio->label_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_radio_set_selected(struct yetty_yclass_object *obj,
                                                              int selected)
{
    struct yetty_ygui2_radio_ptr_result data_res = yetty_ygui2_radio_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 radio_set_selected: data");
    if (data_res.value->selected == (selected ? 1 : 0)) {
        return YETTY_OK_VOID();
    }
    data_res.value->selected = selected ? 1 : 0;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_radio_selected(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_radio_ptr_result data_res = yetty_ygui2_radio_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 radio_selected: data");
    return YETTY_OK(yetty_ycore_int, data_res.value->selected);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_radio_on_select_set(struct yetty_yclass_object *obj,
                                                               yetty_ygui2_click_cb callback,
                                                               void *userdata)
{
    struct yetty_ygui2_radio_ptr_result data_res = yetty_ygui2_radio_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 radio_on_select_set: data");
    data_res.value->on_select = callback;
    data_res.value->on_select_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/radio.c"
