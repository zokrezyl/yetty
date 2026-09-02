/*
 * ygui2 label — static text. Paints one text run at the widget's local
 * origin; text/color/size changes mark the skin dirty (one addressed
 * reopen on the wire, strategy.md §4).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>

#include "yetty/gen/impl/ygui2/widget.h"

YETTY_YRESULT_DECLARE(yetty_ygui2_label_ptr, struct yetty_ygui2_label *);
struct yetty_yclass_ptr_result yetty_ygui2_label_class_get(void);
struct yetty_ygui2_label_ptr_result yetty_ygui2_label_from(struct yetty_yclass_object *obj);

enum { YGUI2_LABEL_TEXT_MAX = 256 };

struct YETTY_ANNOTATE("class@ygui2:label") YETTY_ANNOTATE("parent@ygui2:widget") yetty_ygui2_label {
    char text[YGUI2_LABEL_TEXT_MAX];
    uint32_t text_length;
    uint32_t color; /* packed 0xAABBGGRR; 0 = default off-white */
    float font_size;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result label_paint(struct yetty_yclass_object *obj,
                                                  struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_label_ptr_result data_res = yetty_ygui2_label_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 label paint: data");
    struct yetty_ygui2_label *label = data_res.value;
    if (label->text_length == 0) {
        return YETTY_OK_VOID();
    }
    float font_size = label->font_size > 0.0f ? label->font_size : 14.0f;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 label paint: theme");
    uint32_t color = label->color ? label->color : theme.text_primary;
    struct yetty_ycore_buffer text_buffer = {
        .data = (uint8_t *)label->text,
        .size = label->text_length,
    };
    /* Local coords: baseline-ish at the font size (top-left anchored run). */
    return yetty_ydraw_drawable_list_add_text(list, 0.0f, font_size, &text_buffer, font_size, color,
                                              /*layer=*/0u, /*font_id=*/-1,
                                              /*rotation=*/0.0f);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_label_set_text(struct yetty_yclass_object *obj,
                                                          const char *text)
{
    struct yetty_ygui2_label_ptr_result data_res = yetty_ygui2_label_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 label_set_text: data");
    struct yetty_ygui2_label *label = data_res.value;
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_LABEL_TEXT_MAX) {
        length = YGUI2_LABEL_TEXT_MAX - 1u;
    }
    if (length == label->text_length && length > 0 && memcmp(label->text, text, length) == 0) {
        return YETTY_OK_VOID(); /* unchanged — no dirty, no wire cost */
    }
    memcpy(label->text, text, length);
    label->text[length] = '\0';
    label->text_length = (uint32_t)length;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_label_set_color(struct yetty_yclass_object *obj,
                                                           uint32_t packed_rgba)
{
    struct yetty_ygui2_label_ptr_result data_res = yetty_ygui2_label_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 label_set_color: data");
    if (data_res.value->color == packed_rgba) {
        return YETTY_OK_VOID();
    }
    data_res.value->color = packed_rgba;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_label_set_font_size(struct yetty_yclass_object *obj,
                                                               float font_size)
{
    struct yetty_ygui2_label_ptr_result data_res = yetty_ygui2_label_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 label_set_font_size: data");
    if (data_res.value->font_size == font_size) {
        return YETTY_OK_VOID();
    }
    data_res.value->font_size = font_size;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

#include "yetty/gen/impl/ygui2/widgets/label.c"
