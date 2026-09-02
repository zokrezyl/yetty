/*
 * ygui2 popup_menu — an overlay item list. Mounted via overlay_add with
 * dismiss-on-outside behavior (a press elsewhere closes it); a press on an
 * item fires on_select(index) and closes. Hover tracks under the pointer
 * (skin dirt only on row change — one reopen per row crossing).
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

YETTY_YRESULT_DECLARE(yetty_ygui2_popup_menu_ptr, struct yetty_ygui2_popup_menu *);
struct yetty_yclass_ptr_result yetty_ygui2_popup_menu_class_get(void);
struct yetty_ygui2_popup_menu_ptr_result yetty_ygui2_popup_menu_from(
    struct yetty_yclass_object *obj);

enum {
    YGUI2_POPUP_MENU_ITEM_MAX = 16,
    YGUI2_POPUP_MENU_ITEM_TEXT_MAX = 48,
    YGUI2_POPUP_MENU_ROW_HEIGHT = 24,
    YGUI2_POPUP_MENU_PAD = 4,
};

struct YETTY_ANNOTATE("class@ygui2:popup_menu") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_popup_menu {
    char items[YGUI2_POPUP_MENU_ITEM_MAX][YGUI2_POPUP_MENU_ITEM_TEXT_MAX];
    uint32_t item_lengths[YGUI2_POPUP_MENU_ITEM_MAX];
    uint32_t item_count;
    /* Hovered row + 1; 0 = none (zero-init = no hover). */
    uint32_t hover_plus_one;
    yetty_ygui2_select_cb on_select;
    void *on_select_userdata;
};

static int popup_menu_row_at(float local_y)
{
    return (int)((local_y - (float)YGUI2_POPUP_MENU_PAD) / (float)YGUI2_POPUP_MENU_ROW_HEIGHT);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result popup_menu_paint(struct yetty_yclass_object *obj,
                                                       struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 popup paint: data");
    struct yetty_ygui2_popup_menu *popup = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 popup paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 popup paint: rect");
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 5.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, theme.border, 1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 popup paint: body");
    float glyph_size = 13.0f;
    for (uint32_t index = 0; index < popup->item_count; ++index) {
        float row_top =
            (float)YGUI2_POPUP_MENU_PAD + (float)index * (float)YGUI2_POPUP_MENU_ROW_HEIGHT;
        if (index + 1u == popup->hover_plus_one) {
            struct yetty_ysdf_box hover_row = {
                .center_x = width * 0.5f,
                .center_y = row_top + (float)YGUI2_POPUP_MENU_ROW_HEIGHT * 0.5f,
                .half_width = width * 0.5f - (float)YGUI2_POPUP_MENU_PAD,
                .half_height = (float)YGUI2_POPUP_MENU_ROW_HEIGHT * 0.5f,
                .corner_radius = 3.0f,
            };
            struct yetty_ycore_void_result hover_res = yetty_ydraw_drawable_list_add_cmd_add_box(
                list, /*id=*/0, /*z_order=*/0, theme.bg_row, 0u, 0.0f, &hover_row);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, hover_res, "ygui2 popup paint: hover");
        }
        if (popup->item_lengths[index]) {
            struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)popup->items[index],
                                                     .size = popup->item_lengths[index]};
            struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
                list, 10.0f,
                row_top + (float)YGUI2_POPUP_MENU_ROW_HEIGHT * 0.5f + glyph_size * 0.35f,
                &text_buffer, glyph_size, theme.text_primary, /*layer=*/0u, /*font_id=*/-1,
                /*rotation=*/0.0f);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 popup paint: item");
        }
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_motion")
static struct yetty_ycore_int_result popup_menu_on_motion(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y,
                                                          uint32_t buttons_held)
{
    (void)local_x;
    (void)buttons_held;
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 popup motion: data");
    struct yetty_ygui2_popup_menu *popup = data_res.value;
    int row = popup_menu_row_at(local_y);
    uint32_t next_hover_plus_one =
        (row >= 0 && (uint32_t)row < popup->item_count) ? (uint32_t)row + 1u : 0u;
    if (next_hover_plus_one != popup->hover_plus_one) {
        popup->hover_plus_one = next_hover_plus_one;
        struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 popup motion: dirty");
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result popup_menu_on_press(struct yetty_yclass_object *obj,
                                                         float local_x, float local_y,
                                                         int button_id, int mods)
{
    (void)local_x;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 1); /* modal surface: swallow */
    }
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 popup press: data");
    struct yetty_ygui2_popup_menu *popup = data_res.value;
    int row = popup_menu_row_at(local_y);
    if (row >= 0 && (uint32_t)row < popup->item_count) {
        struct yetty_ycore_void_result hide_res = yetty_ygui2_widget_set_visible(obj, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, hide_res, "ygui2 popup press: hide");
        if (popup->on_select) {
            popup->on_select(obj, (uint32_t)row, popup->on_select_userdata);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_popup_menu_item_add(struct yetty_yclass_object *obj,
                                                               const char *text)
{
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 popup_item_add: data");
    struct yetty_ygui2_popup_menu *popup = data_res.value;
    if (popup->item_count >= YGUI2_POPUP_MENU_ITEM_MAX) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 popup_item_add: full");
    }
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_POPUP_MENU_ITEM_TEXT_MAX) {
        length = YGUI2_POPUP_MENU_ITEM_TEXT_MAX - 1u;
    }
    memcpy(popup->items[popup->item_count], text, length);
    popup->items[popup->item_count][length] = '\0';
    popup->item_lengths[popup->item_count] = (uint32_t)length;
    popup->item_count++;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_popup_menu_items_clear(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 popup_items_clear: data");
    data_res.value->item_count = 0;
    data_res.value->hover_plus_one = 0;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

/* Content height for the current item count — the app sizes the popup with
 * this before showing it. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_ygui2_popup_menu_content_height(
    struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ygui2 popup_content_height: data");
    return YETTY_OK(yetty_ycore_float,
                    (float)(data_res.value->item_count * YGUI2_POPUP_MENU_ROW_HEIGHT +
                            2u * YGUI2_POPUP_MENU_PAD));
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_popup_menu_on_select_set(struct yetty_yclass_object *obj,
                                                                    yetty_ygui2_select_cb callback,
                                                                    void *userdata)
{
    struct yetty_ygui2_popup_menu_ptr_result data_res = yetty_ygui2_popup_menu_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 popup_on_select_set: data");
    data_res.value->on_select = callback;
    data_res.value->on_select_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/popup_menu.c"
