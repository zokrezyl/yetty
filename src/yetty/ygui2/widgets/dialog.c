/*
 * ygui2 dialog — overlay chrome container: body + title bar + close box.
 * Mounted via overlay_add, absolutely placed/sized by the app; child
 * widgets flow below the title bar (give the dialog's layout a pad_top of
 * at least the title height, YGUI2_DIALOG_TITLE_HEIGHT). Modal-ish: it
 * consumes presses; the close box (or Esc) hides it and fires on_close.
 * It does NOT dismiss on outside press — that is popup behavior.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_dialog_ptr, struct yetty_ygui2_dialog *);
struct yetty_yclass_ptr_result yetty_ygui2_dialog_class_get(void);
struct yetty_ygui2_dialog_ptr_result yetty_ygui2_dialog_from(struct yetty_yclass_object *obj);

enum {
    YGUI2_DIALOG_TEXT_MAX = 64,
    YGUI2_DIALOG_TITLE_HEIGHT = 32,
    YGUI2_DIALOG_CLOSE_SIZE = 20,
};

struct YETTY_ANNOTATE("class@ygui2:dialog") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_dialog {
    char title[YGUI2_DIALOG_TEXT_MAX];
    uint32_t title_length;
    yetty_ygui2_click_cb on_close;
    void *on_close_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result dialog_paint(struct yetty_yclass_object *obj,
                                                   struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_dialog_ptr_result data_res = yetty_ygui2_dialog_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dialog paint: data");
    struct yetty_ygui2_dialog *dialog = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 dialog paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 dialog paint: rect");
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 6.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, theme.border, 1.5f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 dialog paint: body");
    struct yetty_ysdf_box title_bar = {
        .center_x = width * 0.5f,
        .center_y = (float)YGUI2_DIALOG_TITLE_HEIGHT * 0.5f,
        .half_width = width * 0.5f,
        .half_height = (float)YGUI2_DIALOG_TITLE_HEIGHT * 0.5f,
        .corner_radius = 6.0f,
    };
    struct yetty_ycore_void_result title_bar_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_row, 0u, 0.0f, &title_bar);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, title_bar_res, "ygui2 dialog paint: title bar");
    float glyph_size = 14.0f;
    if (dialog->title_length) {
        struct yetty_ycore_buffer title_buffer = {.data = (uint8_t *)dialog->title,
                                                  .size = dialog->title_length};
        struct yetty_ycore_void_result title_res = yetty_ydraw_drawable_list_add_text(
            list, 10.0f, (float)YGUI2_DIALOG_TITLE_HEIGHT * 0.5f + glyph_size * 0.35f,
            &title_buffer, glyph_size, theme.text_primary, /*layer=*/0u, /*font_id=*/-1,
            /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "ygui2 dialog paint: title");
    }
    struct yetty_ycore_buffer close_buffer = {.data = (uint8_t *)"x", .size = 1};
    struct yetty_ycore_void_result close_res = yetty_ydraw_drawable_list_add_text(
        list, width - (float)YGUI2_DIALOG_CLOSE_SIZE,
        (float)YGUI2_DIALOG_TITLE_HEIGHT * 0.5f + glyph_size * 0.35f, &close_buffer, glyph_size,
        theme.text_secondary, /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "ygui2 dialog paint: close");
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result dialog_on_press(struct yetty_yclass_object *obj, float local_x,
                                                     float local_y, int button_id, int mods)
{
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 1); /* modal surface: swallow */
    }
    struct yetty_ygui2_dialog_ptr_result data_res = yetty_ygui2_dialog_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 dialog press: data");
    struct yetty_ygui2_dialog *dialog = data_res.value;
    float width = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "ygui2 dialog press: rect");
    if (local_y < (float)YGUI2_DIALOG_TITLE_HEIGHT &&
        local_x >= width - (float)YGUI2_DIALOG_CLOSE_SIZE - 8.0f) {
        struct yetty_ycore_void_result hide_res = yetty_ygui2_widget_set_visible(obj, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, hide_res, "ygui2 dialog press: hide");
        if (dialog->on_close) {
            dialog->on_close(obj, dialog->on_close_userdata);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_dialog_set_title(struct yetty_yclass_object *obj,
                                                            const char *text)
{
    struct yetty_ygui2_dialog_ptr_result data_res = yetty_ygui2_dialog_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dialog_set_title: data");
    struct yetty_ygui2_dialog *dialog = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_DIALOG_TEXT_MAX) {
        length = YGUI2_DIALOG_TEXT_MAX - 1u;
    }
    memcpy(dialog->title, text, length);
    dialog->title[length] = '\0';
    dialog->title_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_dialog_on_close_set(struct yetty_yclass_object *obj,
                                                               yetty_ygui2_click_cb callback,
                                                               void *userdata)
{
    struct yetty_ygui2_dialog_ptr_result data_res = yetty_ygui2_dialog_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dialog_on_close_set: data");
    data_res.value->on_close = callback;
    data_res.value->on_close_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/dialog.c"
