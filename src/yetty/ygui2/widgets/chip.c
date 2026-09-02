/*
 * ygui2 chip — a small rounded tag. Optionally selectable: a press toggles
 * the selected state (accent background) and fires on_toggle.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_chip_ptr, struct yetty_ygui2_chip *);
struct yetty_yclass_ptr_result yetty_ygui2_chip_class_get(void);
struct yetty_ygui2_chip_ptr_result yetty_ygui2_chip_from(struct yetty_yclass_object *obj);

enum { YGUI2_CHIP_TEXT_MAX = 48 };

struct YETTY_ANNOTATE("class@ygui2:chip") YETTY_ANNOTATE("parent@ygui2:widget") yetty_ygui2_chip {
    char label[YGUI2_CHIP_TEXT_MAX];
    uint32_t label_length;
    int selectable;
    int selected;
    yetty_ygui2_click_cb on_toggle;
    void *on_toggle_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result chip_paint(struct yetty_yclass_object *obj,
                                                 struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 chip paint: data");
    struct yetty_ygui2_chip *chip = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 chip paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 chip paint: rect");
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = height * 0.5f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, chip->selected ? theme.accent : theme.bg_row, theme.border,
        1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 chip paint: body");
    if (chip->label_length) {
        float glyph_size = 12.0f;
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)chip->label,
                                                 .size = chip->label_length};
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            list, 10.0f, height * 0.5f + glyph_size * 0.35f, &text_buffer, glyph_size,
            chip->selected ? theme.bg : theme.text_secondary, /*layer=*/0u, /*font_id=*/-1,
            /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 chip paint: text");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result chip_on_press(struct yetty_yclass_object *obj, float local_x,
                                                   float local_y, int button_id, int mods)
{
    (void)local_x;
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 chip press: data");
    struct yetty_ygui2_chip *chip = data_res.value;
    if (!chip->selectable) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    chip->selected = !chip->selected;
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 chip press: dirty");
    if (chip->on_toggle) {
        chip->on_toggle(obj, chip->on_toggle_userdata);
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_chip_set_label(struct yetty_yclass_object *obj,
                                                          const char *text)
{
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 chip_set_label: data");
    struct yetty_ygui2_chip *chip = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_CHIP_TEXT_MAX) {
        length = YGUI2_CHIP_TEXT_MAX - 1u;
    }
    memcpy(chip->label, text, length);
    chip->label[length] = '\0';
    chip->label_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_chip_set_selectable(struct yetty_yclass_object *obj,
                                                               int selectable)
{
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 chip_set_selectable: data");
    data_res.value->selectable = selectable ? 1 : 0;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_chip_set_selected(struct yetty_yclass_object *obj,
                                                             int selected)
{
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 chip_set_selected: data");
    if (data_res.value->selected == (selected ? 1 : 0)) {
        return YETTY_OK_VOID();
    }
    data_res.value->selected = selected ? 1 : 0;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_chip_selected(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 chip_selected: data");
    return YETTY_OK(yetty_ycore_int, data_res.value->selected);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_chip_on_toggle_set(struct yetty_yclass_object *obj,
                                                              yetty_ygui2_click_cb callback,
                                                              void *userdata)
{
    struct yetty_ygui2_chip_ptr_result data_res = yetty_ygui2_chip_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 chip_on_toggle_set: data");
    data_res.value->on_toggle = callback;
    data_res.value->on_toggle_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/chip.c"
