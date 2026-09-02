/*
 * ygui2 slider — horizontal value control: track + filled portion + knob.
 * Press sets the value from the click position; the framework's capture
 * then routes drag motion here, so the knob follows the pointer. Fires
 * on_change (value readable via slider_value).
 */
#include <stdbool.h>
#include <stdint.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ygui2/defs.h>
#include <yetty/ysdf/funcs.gen.h>

#include "yetty/gen/impl/ygui2/widget.h"

YETTY_YRESULT_DECLARE(yetty_ygui2_slider_ptr, struct yetty_ygui2_slider *);
struct yetty_yclass_ptr_result yetty_ygui2_slider_class_get(void);
struct yetty_ygui2_slider_ptr_result yetty_ygui2_slider_from(struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:slider") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_slider {
    float minimum;
    float maximum; /* 0 == unset: defaults to [0,1] */
    float value;
    yetty_ygui2_click_cb on_change;
    void *on_change_userdata;
};

static void slider_range(const struct yetty_ygui2_slider *slider, float *out_minimum,
                         float *out_maximum)
{
    *out_minimum = slider->minimum;
    *out_maximum = slider->maximum;
    if (*out_maximum <= *out_minimum) {
        *out_minimum = 0.0f;
        *out_maximum = 1.0f;
    }
}

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result slider_paint(struct yetty_yclass_object *obj,
                                                   struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_slider_ptr_result data_res = yetty_ygui2_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 slider paint: data");
    struct yetty_ygui2_slider *slider = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 slider paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 slider paint: rect");
    float minimum = 0.0f;
    float maximum = 1.0f;
    slider_range(slider, &minimum, &maximum);
    float span = maximum - minimum;
    float fraction = span > 0.0f ? (slider->value - minimum) / span : 0.0f;
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    float track_height = 6.0f;
    struct yetty_ysdf_box track = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = track_height * 0.5f,
        .corner_radius = track_height * 0.5f,
    };
    struct yetty_ycore_void_result track_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, theme.bg_row, theme.border, 1.0f, &track);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, track_res, "ygui2 slider paint: track");
    if (fraction > 0.0f) {
        struct yetty_ysdf_box filled = {
            .center_x = width * fraction * 0.5f,
            .center_y = height * 0.5f,
            .half_width = width * fraction * 0.5f,
            .half_height = track_height * 0.5f,
            .corner_radius = track_height * 0.5f,
        };
        struct yetty_ycore_void_result filled_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            list, /*id=*/0, /*z_order=*/0, theme.accent_deep, 0u, 0.0f, &filled);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, filled_res, "ygui2 slider paint: filled");
    }
    struct yetty_ysdf_circle knob = {
        .center_x = width * fraction,
        .center_y = height * 0.5f,
        .radius = 8.0f,
    };
    struct yetty_ycore_void_result knob_res = yetty_ydraw_drawable_list_add_cmd_add_circle(
        list, /*id=*/0, /*z_order=*/0, theme.accent, theme.bg, 1.0f, &knob);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, knob_res, "ygui2 slider paint: knob");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result slider_pick(struct yetty_yclass_object *obj, float local_x)
{
    struct yetty_ygui2_slider_ptr_result data_res = yetty_ygui2_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 slider pick: data");
    struct yetty_ygui2_slider *slider = data_res.value;
    float width = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rect_res, "ygui2 slider pick: rect");
    if (width <= 0.0f) {
        return YETTY_OK(yetty_ycore_int, 1);
    }
    float minimum = 0.0f;
    float maximum = 1.0f;
    slider_range(slider, &minimum, &maximum);
    float fraction = local_x / width;
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    float next_value = minimum + fraction * (maximum - minimum);
    if (next_value != slider->value) {
        slider->value = next_value;
        struct yetty_ycore_void_result dirty_res = yetty_ygui2_widget_mark_skin_dirty(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, dirty_res, "ygui2 slider pick: dirty");
        if (slider->on_change) {
            slider->on_change(obj, slider->on_change_userdata);
        }
    }
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_press")
static struct yetty_ycore_int_result slider_on_press(struct yetty_yclass_object *obj, float local_x,
                                                     float local_y, int button_id, int mods)
{
    (void)local_y;
    (void)mods;
    if (button_id != 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return slider_pick(obj, local_x);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_on_motion")
static struct yetty_ycore_int_result slider_on_motion(struct yetty_yclass_object *obj,
                                                      float local_x, float local_y,
                                                      uint32_t buttons_held)
{
    (void)local_y;
    if (!buttons_held) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return slider_pick(obj, local_x);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_slider_set_range(struct yetty_yclass_object *obj,
                                                            float minimum, float maximum)
{
    struct yetty_ygui2_slider_ptr_result data_res = yetty_ygui2_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 slider_set_range: data");
    if (maximum <= minimum) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 slider_set_range: empty range");
    }
    data_res.value->minimum = minimum;
    data_res.value->maximum = maximum;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_slider_set_value(struct yetty_yclass_object *obj,
                                                            float value)
{
    struct yetty_ygui2_slider_ptr_result data_res = yetty_ygui2_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 slider_set_value: data");
    if (data_res.value->value == value) {
        return YETTY_OK_VOID();
    }
    data_res.value->value = value;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_float_result yetty_ygui2_slider_value(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_slider_ptr_result data_res = yetty_ygui2_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_float, data_res, "ygui2 slider_value: data");
    return YETTY_OK(yetty_ycore_float, data_res.value->value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_slider_on_change_set(struct yetty_yclass_object *obj,
                                                                yetty_ygui2_click_cb callback,
                                                                void *userdata)
{
    struct yetty_ygui2_slider_ptr_result data_res = yetty_ygui2_slider_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 slider_on_change_set: data");
    data_res.value->on_change = callback;
    data_res.value->on_change_userdata = userdata;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ygui2/widgets/slider.c"
