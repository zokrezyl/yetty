/*
 * ygui2 spinner — numeric stepper: [-] value [+]. A press in the left
 * button zone decrements, in the right zone increments, clamped to the
 * range; fires on_change (value readable via spinner_value).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/ysdf/funcs.gen.h>

#include "yetty/gen/impl/ygui2/widget.h"

YETTY_YRESULT_DECLARE(yetty_ygui2_spinner_ptr, struct yetty_ygui2_spinner *);
struct yetty_yclass_ptr_result yetty_ygui2_spinner_class_get(void);
struct yetty_ygui2_spinner_ptr_result yetty_ygui2_spinner_from(struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:spinner") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_spinner {
    float minimum;
    float maximum; /* maximum <= minimum == unset: defaults to [0,100] */
    float step;    /* 0 == unset: defaults to 1 */
    float value;
    yetty_ygui2_click_cb on_change;
    void *on_change_userdata;
};

static void spinner_bounds(const struct yetty_ygui2_spinner *spinner, float *out_minimum,
                           float *out_maximum, float *out_step)
{
    *out_minimum = spinner->minimum;
    *out_maximum = spinner->maximum;
    *out_step = spinner->step > 0.0f ? spinner->step : 1.0f;
    if (*out_maximum <= *out_minimum) {
        *out_minimum = 0.0f;
        *out_maximum = 100.0f;
    }
}

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result spinner_paint(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_spinner_ptr_result data_res = yetty_ygui2_spinner_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 spinner paint: data");
    struct yetty_ygui2_spinner *spinner = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 spinner paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 spinner paint: rect");
    float button_width = height; /* square press zones at both ends */
    struct yetty_ysdf_box body = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = 4.0f,
    };
    struct yetty_ycore_void_result body_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_lifted, theme.border, 1.0f, &body);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, body_res, "ygui2 spinner paint: body");
    float glyph_size = 14.0f;
    float text_baseline = height * 0.5f + glyph_size * 0.35f;
    struct yetty_ycore_buffer minus_buffer = {.data = (uint8_t *)"-", .size = 1};
    struct yetty_ycore_void_result minus_res = yetty_ydraw_drawable_list_add_text(
        list, button_width * 0.5f - 3.0f, text_baseline, &minus_buffer, glyph_size, theme.accent,
        /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, minus_res, "ygui2 spinner paint: minus");
    struct yetty_ycore_buffer plus_buffer = {.data = (uint8_t *)"+", .size = 1};
    struct yetty_ycore_void_result plus_res = yetty_ydraw_drawable_list_add_text(
        list, width - button_width * 0.5f - 4.0f, text_baseline, &plus_buffer, glyph_size,
        theme.accent, /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plus_res, "ygui2 spinner paint: plus");
    char value_text[32];
    int value_length = snprintf(value_text, sizeof(value_text), "%g", (double)spinner->value);
    if (value_length > 0) {
        struct yetty_ycore_buffer value_buffer = {.data = (uint8_t *)value_text,
                                                  .size = (size_t)value_length};
        float value_width = (float)value_length * glyph_size * 0.55f;
        struct yetty_ycore_void_result value_res = yetty_ydraw_drawable_list_add_text(
            list, (width - value_width) * 0.5f, text_baseline, &value_buffer, glyph_size,
            theme.text_primary, /*layer=*/0u, /*font_id=*/-1, /*rotation=*/0.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, value_res, "ygui2 spinner paint: value");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result spinner_on_press(struct yetty_yclass_object *obj,
                                                      float local_x, float local_y, int button_id,
                                                      int mods)
{
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ygui2_spinner_ptr_result data_res = yetty_ygui2_spinner_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 spinner press: data");
    struct yetty_ygui2_spinner *spinner = data_res.value;
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "ygui2 spinner press: rect");
    float minimum = 0.0f;
    float maximum = 100.0f;
    float step = 1.0f;
    spinner_bounds(spinner, &minimum, &maximum, &step);
    float next_value = spinner->value;
    if (local_x < height) {
        next_value -= step;
    } else if (local_x >= width - height) {
        next_value += step;
    } else {
        return YETTY_OK(yetty_ycore_int, 1); /* middle zone: consumed, no-op */
    }
    if (next_value < minimum) {
        next_value = minimum;
    }
    if (next_value > maximum) {
        next_value = maximum;
    }
    if (next_value != spinner->value) {
        spinner->value = next_value;
        struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 spinner press: dirty");
        if (spinner->on_change) {
            spinner->on_change(obj, spinner->on_change_userdata);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_spinner_configure(struct yetty_yclass_object *obj,
                                                             float minimum, float maximum,
                                                             float step)
{
    struct yetty_ygui2_spinner_ptr_result data_res = yetty_ygui2_spinner_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 spinner_configure: data");
    if (maximum <= minimum || step <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 spinner_configure: bad range/step");
    }
    data_res.value->minimum = minimum;
    data_res.value->maximum = maximum;
    data_res.value->step = step;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_spinner_set_value(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ygui2_spinner_ptr_result data_res = yetty_ygui2_spinner_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 spinner_set_value: data");
    if (data_res.value->value == value) {
        return YETTY_OK_VOID();
    }
    data_res.value->value = value;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_ygui2_spinner_value(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_spinner_ptr_result data_res = yetty_ygui2_spinner_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ygui2 spinner_value: data");
    return YETTY_OK(yetty_ycore_float, data_res.value->value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_spinner_on_change_set(struct yetty_yclass_object *obj,
                                                                 yetty_ygui2_click_cb callback,
                                                                 void *userdata)
{
    struct yetty_ygui2_spinner_ptr_result data_res = yetty_ygui2_spinner_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 spinner_on_change_set: data");
    data_res.value->on_change = callback;
    data_res.value->on_change_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/spinner.c"
