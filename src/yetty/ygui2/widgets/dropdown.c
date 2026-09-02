/*
 * ygui2 dropdown — a select control (combobox without typing): a field
 * showing the current item + a chevron; press opens a popup_menu in the
 * overlay directly under the field. Selection updates the field and fires
 * on_change(index). The popup is created lazily once and reused.
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

/* Cross-class within-module (accessor pattern; the generated headers of
 * sibling classes are not included from widget sources). */
struct yetty_yclass_object_ptr_result yetty_ygui2_framework_overlay_add(
    struct yetty_yclass_object *framework_obj, const struct yetty_yclass *cls);
struct yetty_yclass_ptr_result yetty_ygui2_popup_menu_class_get(void);
struct yetty_ycore_void_result yetty_ygui2_popup_menu_item_add(struct yetty_yclass_object *obj,
                                                               const char *text);
struct yetty_ycore_void_result yetty_ygui2_popup_menu_items_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_ygui2_popup_menu_content_height(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_popup_menu_on_select_set(struct yetty_yclass_object *obj,
                                                                    yetty_ygui2_select_cb callback,
                                                                    void *userdata);
struct yetty_ycore_void_result yetty_ygui2_framework_orphan_overlay(
    struct yetty_yclass_object *framework_obj, struct yetty_yclass_object *widget_obj);

YETTY_YRESULT_DECLARE(yetty_ygui2_dropdown_ptr, struct yetty_ygui2_dropdown *);
struct yetty_yclass_ptr_result yetty_ygui2_dropdown_class_get(void);
struct yetty_ygui2_dropdown_ptr_result yetty_ygui2_dropdown_from(struct yetty_yclass_object *obj);

enum {
    YGUI2_DROPDOWN_ITEM_MAX = 16,
    YGUI2_DROPDOWN_ITEM_TEXT_MAX = 48,
};

struct YETTY_ANNOTATE("class@ygui2:dropdown") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_dropdown {
    char items[YGUI2_DROPDOWN_ITEM_MAX][YGUI2_DROPDOWN_ITEM_TEXT_MAX];
    uint32_t item_lengths[YGUI2_DROPDOWN_ITEM_MAX];
    uint32_t item_count;
    /* Selected item + 1; 0 = none (zero-init = the "(select)" state). */
    uint32_t selected_plus_one;
    struct yetty_yclass_object *popup; /* lazily created overlay child */
    int popup_stale;                   /* items changed since the popup was filled */
    yetty_ygui2_select_cb on_change;
    void *on_change_userdata;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result dropdown_paint(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dropdown paint: data");
    struct yetty_ygui2_dropdown *dropdown = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 dropdown paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 dropdown paint: rect");
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 4.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, theme.border, 1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 dropdown paint: body");
    float glyph_size = 13.0f;
    float text_baseline = height * 0.5f + glyph_size * 0.35f;
    const char *shown_text = "(select)";
    size_t shown_length = strlen(shown_text);
    uint32_t shown_color = theme.text_muted;
    if (dropdown->selected_plus_one && dropdown->selected_plus_one <= dropdown->item_count) {
        shown_text = dropdown->items[dropdown->selected_plus_one - 1u];
        shown_length = dropdown->item_lengths[dropdown->selected_plus_one - 1u];
        shown_color = theme.text_primary;
    }
    struct yetty_ycore_buffer shown_buffer = {.data = (uint8_t *)shown_text, .size = shown_length};
    struct yetty_ycore_void_result shown_res = yetty_ydraw_drawable_list_add_text(
        list, 8.0f, text_baseline, &shown_buffer, glyph_size, shown_color, /*layer=*/0u,
        /*font_id=*/-1, /*rotation=*/0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shown_res, "ygui2 dropdown paint: text");
    struct yetty_ycore_buffer chevron_buffer = {.data = (uint8_t *)"v", .size = 1};
    struct yetty_ycore_void_result chevron_res = yetty_ydraw_drawable_list_add_text(
        list, width - 16.0f, text_baseline, &chevron_buffer, glyph_size, theme.accent,
        /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, chevron_res, "ygui2 dropdown paint: chevron");
    return YETTY_OK_VOID();
}

static void dropdown_popup_selected(struct yetty_yclass_object *popup_obj, uint32_t index,
                                    void *userdata)
{
    (void)popup_obj;
    struct yetty_yclass_object *dropdown_obj = userdata;
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(dropdown_obj);
    if (YETTY_IS_ERR(data_res)) {
        yetty_ycore_error_destroy(data_res.error);
        return;
    }
    struct yetty_ygui2_dropdown *dropdown = data_res.value;
    if (index >= dropdown->item_count) {
        return;
    }
    dropdown->selected_plus_one = index + 1u;
    struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(dropdown_obj);
    if (YETTY_IS_ERR(dirty_res)) {
        yetty_ycore_error_destroy(dirty_res.error);
    }
    if (dropdown->on_change) {
        dropdown->on_change(dropdown_obj, index, dropdown->on_change_userdata);
    }
}

static struct yetty_ycore_int_result dropdown_open(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 dropdown open: data");
    struct yetty_ygui2_dropdown *dropdown = data_res.value;
    if (!dropdown->popup) {
        struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_widget_framework_obj(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, framework_res, "ygui2 dropdown open: framework");
        struct yetty_yclass_ptr_result popup_class_res = yetty_ygui2_popup_menu_class_get();
        YETTY_RETURN_IF_ERR(yetty_ycore_int, popup_class_res, "ygui2 dropdown open: class");
        struct yetty_yclass_object_ptr_result popup_res =
            yetty_ygui2_framework_overlay_add(framework_res.value, popup_class_res.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, popup_res, "ygui2 dropdown open: overlay_add");
        dropdown->popup = popup_res.value;
        dropdown->popup_stale = 1;
        struct yetty_ycore_void_result dismiss_res =
            yetty_ygui2_widget_set_dismiss_on_outside(dropdown->popup, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dismiss_res, "ygui2 dropdown open: dismiss flag");
        struct yetty_ycore_void_result select_res =
            yetty_ygui2_popup_menu_on_select_set(dropdown->popup, dropdown_popup_selected, obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, select_res, "ygui2 dropdown open: on_select");
    }
    if (dropdown->popup_stale) {
        struct yetty_ycore_void_result clear_res =
            yetty_ygui2_popup_menu_items_clear(dropdown->popup);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, clear_res, "ygui2 dropdown open: clear items");
        for (uint32_t index = 0; index < dropdown->item_count; ++index) {
            struct yetty_ycore_void_result add_res =
                yetty_ygui2_popup_menu_item_add(dropdown->popup, dropdown->items[index]);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, add_res, "ygui2 dropdown open: add item");
        }
        dropdown->popup_stale = 0;
    }
    float field_x = 0.0f;
    float field_y = 0.0f;
    float field_w = 0.0f;
    float field_h = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, &field_x, &field_y, &field_w, &field_h);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "ygui2 dropdown open: rect");
    struct yetty_ycore_float_result height_res =
        yetty_ygui2_popup_menu_content_height(dropdown->popup);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, height_res, "ygui2 dropdown open: height");
    struct yetty_ycore_void_result position_res =
        yetty_ygui2_widget_set_position(dropdown->popup, field_x, field_y + field_h + 2.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, position_res, "ygui2 dropdown open: position");
    struct yetty_ycore_void_result size_res =
        yetty_ygui2_widget_set_size(dropdown->popup, field_w, height_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, size_res, "ygui2 dropdown open: size");
    struct yetty_ycore_void_result show_res = yetty_ygui2_widget_set_visible(dropdown->popup, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, show_res, "ygui2 dropdown open: show");
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result dropdown_on_press(struct yetty_yclass_object *obj,
                                                       float local_x, float local_y, int button_id,
                                                       int mods)
{
    (void)local_x;
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return dropdown_open(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_dropdown_item_add(struct yetty_yclass_object *obj,
                                                             const char *text)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dropdown_item_add: data");
    struct yetty_ygui2_dropdown *dropdown = data_res.value;
    if (dropdown->item_count >= YGUI2_DROPDOWN_ITEM_MAX) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 dropdown_item_add: full");
    }
    if (!text) {
        text = "";
    }
    size_t length = strlen(text);
    if (length >= YGUI2_DROPDOWN_ITEM_TEXT_MAX) {
        length = YGUI2_DROPDOWN_ITEM_TEXT_MAX - 1u;
    }
    memcpy(dropdown->items[dropdown->item_count], text, length);
    dropdown->items[dropdown->item_count][length] = '\0';
    dropdown->item_lengths[dropdown->item_count] = (uint32_t)length;
    dropdown->item_count++;
    dropdown->popup_stale = 1;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_dropdown_set_selected(struct yetty_yclass_object *obj,
                                                                 int selected_index)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dropdown_set_selected: data");
    struct yetty_ygui2_dropdown *dropdown = data_res.value;
    if (selected_index >= (int)dropdown->item_count) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 dropdown_set_selected: out of range");
    }
    uint32_t selected_plus_one = selected_index < 0 ? 0u : (uint32_t)selected_index + 1u;
    if (dropdown->selected_plus_one == selected_plus_one) {
        return YETTY_OK_VOID();
    }
    dropdown->selected_plus_one = selected_plus_one;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_dropdown_selected(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 dropdown_selected: data");
    return YETTY_OK(yetty_ycore_int, (int)data_res.value->selected_plus_one - 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_dropdown_on_change_set(struct yetty_yclass_object *obj,
                                                                  yetty_ygui2_select_cb callback,
                                                                  void *userdata)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dropdown_on_change_set: data");
    data_res.value->on_change = callback;
    data_res.value->on_change_userdata = userdata;
    return YETTY_OK_VOID();
}

/* The popup lives under the OVERLAY root with THIS dropdown as its
 * callback userdata — left alone it outlives the dropdown and its next
 * selection dispatches into freed memory. Sever + hide make it inert
 * IMMEDIATELY (this cleanup may be running from inside the popup's own
 * selection dispatch, so destroying it here would free frames still on
 * the stack); the framework reclaims it at the next feed/emit boundary. */
YETTY_ANNOTATE("override@ygui2:widget:widget_cleanup")
static struct yetty_ycore_void_result dropdown_cleanup(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_dropdown_ptr_result data_res = yetty_ygui2_dropdown_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 dropdown cleanup: data");
    struct yetty_ygui2_dropdown *dropdown = data_res.value;
    if (!dropdown->popup) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_object *popup = dropdown->popup;
    dropdown->popup = NULL;
    /* Best-effort teardown steps: severing FIRST guarantees no dispatch
     * can reach this dropdown again even if a later step fails. */
    struct yetty_ycore_void_result sever_res =
        yetty_ygui2_popup_menu_on_select_set(popup, NULL, NULL);
    if (YETTY_IS_ERR(sever_res)) {
        yetty_ycore_error_destroy(sever_res.error);
    }
    struct yetty_ycore_void_result hide_res = yetty_ygui2_widget_set_visible(popup, 0);
    if (YETTY_IS_ERR(hide_res)) {
        yetty_ycore_error_destroy(hide_res.error);
    }
    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui2_widget_framework_obj(obj);
    if (YETTY_IS_OK(framework_res) && framework_res.value) {
        struct yetty_ycore_void_result orphan_res =
            yetty_ygui2_framework_orphan_overlay(framework_res.value, popup);
        if (YETTY_IS_ERR(orphan_res)) {
            yetty_ycore_error_destroy(orphan_res.error);
        }
    } else if (YETTY_IS_ERR(framework_res)) {
        yetty_ycore_error_destroy(framework_res.error);
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/dropdown.c"
