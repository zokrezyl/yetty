/*
 * ygui2 textinput — single-line text entry. Focusable (set at add time by
 * the framework helper or the app); while focused it consumes printable
 * bytes, Backspace, and Enter (fires on_submit). The focus ring is the
 * accent border — focus transitions arrive as skin dirt from the
 * framework, so the reopen repaints the ring automatically.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_textinput_ptr, struct yetty_ygui2_textinput *);
struct yetty_yclass_ptr_result yetty_ygui2_textinput_class_get(void);
struct yetty_ygui2_textinput_ptr_result yetty_ygui2_textinput_from(struct yetty_yclass_object *obj);

enum { YGUI2_TEXTINPUT_TEXT_MAX = 256 };

struct YETTY_ANNOTATE("class@ygui2:textinput") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_textinput {
    char text[YGUI2_TEXTINPUT_TEXT_MAX];
    uint32_t text_length;
    char placeholder[YGUI2_TEXTINPUT_TEXT_MAX];
    uint32_t placeholder_length;
    yetty_ygui2_click_cb on_submit;
    void *on_submit_userdata;
    yetty_ygui2_click_cb on_change;
    void *on_change_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result textinput_paint(struct yetty_yclass_object *obj,
                                                      struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 textinput paint: data");
    struct yetty_ygui2_textinput *textinput = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 textinput paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 textinput paint: rect");
    struct yetty_ycore_int_result focus_res = yetty_ygui2_widget_has_focus(obj);
    int focused = YETTY_IS_OK(focus_res) ? focus_res.value : 0;
    if (YETTY_IS_ERR(focus_res)) {
        yetty_ycore_error_destroy(focus_res.error);
    }
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 4.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, focused ? theme.accent : theme.border,
        focused ? 1.5f : 1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 textinput paint: body");
    float glyph_size = 13.0f;
    float text_baseline = height * 0.5f + glyph_size * 0.35f;
    float text_advance = glyph_size * 0.55f;
    if (textinput->text_length) {
        struct yetty_ycore_buffer text_buffer = {.data = (uint8_t *)textinput->text,
                                                 .size = textinput->text_length};
        struct yetty_ycore_void_result text_res = yetty_ydraw_drawable_list_add_text(
            list, 8.0f, text_baseline, &text_buffer, glyph_size, theme.text_primary,
            /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "ygui2 textinput paint: text");
    } else if (textinput->placeholder_length && !focused) {
        struct yetty_ycore_buffer placeholder_buffer = {.data = (uint8_t *)textinput->placeholder,
                                                        .size = textinput->placeholder_length};
        struct yetty_ycore_void_result placeholder_res = yetty_ydraw_drawable_list_add_text(
            list, 8.0f, text_baseline, &placeholder_buffer, glyph_size, theme.text_muted,
            /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, placeholder_res,
                            "ygui2 textinput paint: placeholder");
    }
    if (focused) {
        float caret_x = 8.0f + (float)textinput->text_length * text_advance + 1.0f;
        struct yetty_ysdf_box caret = {
            .center_x = caret_x,
            .center_y = height * 0.5f,
            .half_width = 1.0f,
            .half_height = glyph_size * 0.55f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result caret_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            list, /*id=*/0, /*z_order=*/0, theme.accent_bright, 0u, 0.0f, &caret);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, caret_res, "ygui2 textinput paint: caret");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_key")
static struct yetty_ycore_int_result textinput_on_key(struct yetty_yclass_object *obj, uint32_t key,
                                                      uint32_t mods)
{
    (void)mods;
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 textinput key: data");
    struct yetty_ygui2_textinput *textinput = data_res.value;
    if (key == YETTY_YGUI2_KEY_ENTER) {
        if (textinput->on_submit) {
            textinput->on_submit(obj, textinput->on_submit_userdata);
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (key == YETTY_YGUI2_KEY_BACKSPACE || key == 0x08) {
        if (textinput->text_length) {
            textinput->text_length--;
            textinput->text[textinput->text_length] = '\0';
            struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 textinput key: dirty");
            if (textinput->on_change) {
                textinput->on_change(obj, textinput->on_change_userdata);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    if (key >= 0x20 && key < 0x7f) {
        if (textinput->text_length + 1u < YGUI2_TEXTINPUT_TEXT_MAX) {
            textinput->text[textinput->text_length++] = (char)key;
            textinput->text[textinput->text_length] = '\0';
            struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 textinput key: dirty");
            if (textinput->on_change) {
                textinput->on_change(obj, textinput->on_change_userdata);
            }
        }
        return YETTY_OK(yetty_ycore_int, 1);
    }
    return YETTY_OK(yetty_ycore_int, 0);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_textinput_set_text(struct yetty_yclass_object *obj,
                                                              const char *text)
{
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 textinput_set_text: data");
    struct yetty_ygui2_textinput *textinput = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_TEXTINPUT_TEXT_MAX) {
        length = YGUI2_TEXTINPUT_TEXT_MAX - 1u;
    }
    if (textinput->text_length == (uint32_t)length && memcmp(textinput->text, text, length) == 0) {
        return YETTY_OK_VOID();
    }
    memcpy(textinput->text, text, length);
    textinput->text[length] = '\0';
    textinput->text_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_textinput_set_placeholder(
    struct yetty_yclass_object *obj, const char *text)
{
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 textinput_set_placeholder: data");
    struct yetty_ygui2_textinput *textinput = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_TEXTINPUT_TEXT_MAX) {
        length = YGUI2_TEXTINPUT_TEXT_MAX - 1u;
    }
    memcpy(textinput->placeholder, text, length);
    textinput->placeholder[length] = '\0';
    textinput->placeholder_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

/* Copy the current text into `out_text` (NUL-terminated, truncating to
 * `out_capacity`). Returns the full length. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_size_result yetty_ygui2_textinput_text_copy(struct yetty_yclass_object *obj,
                                                               char *out_text, size_t out_capacity)
{
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, data_res, "ygui2 textinput_text_copy: data");
    struct yetty_ygui2_textinput *textinput = data_res.value;
    if (out_text && out_capacity) {
        size_t copy_length = textinput->text_length;
        if (copy_length >= out_capacity) {
            copy_length = out_capacity - 1u;
        }
        memcpy(out_text, textinput->text, copy_length);
        out_text[copy_length] = '\0';
    }
    return YETTY_OK(yetty_ycore_size, (size_t)textinput->text_length);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_textinput_on_submit_set(struct yetty_yclass_object *obj,
                                                                   yetty_ygui2_click_cb callback,
                                                                   void *userdata)
{
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 textinput_on_submit_set: data");
    data_res.value->on_submit = callback;
    data_res.value->on_submit_userdata = userdata;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_textinput_on_change_set(struct yetty_yclass_object *obj,
                                                                   yetty_ygui2_click_cb callback,
                                                                   void *userdata)
{
    struct yetty_ygui2_textinput_ptr_result data_res = yetty_ygui2_textinput_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 textinput_on_change_set: data");
    data_res.value->on_change = callback;
    data_res.value->on_change_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/textinput.c"
