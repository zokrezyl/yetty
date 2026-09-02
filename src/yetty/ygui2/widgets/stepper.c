/*
 * ygui2 stepper — "step N of M" progress dots: done steps in the deep
 * accent, the current step bright, remaining steps in the row surface.
 * Display-only; the app advances it via set_current.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_stepper_ptr, struct yetty_ygui2_stepper *);
struct yetty_yclass_ptr_result yetty_ygui2_stepper_class_get(void);
struct yetty_ygui2_stepper_ptr_result yetty_ygui2_stepper_from(struct yetty_yclass_object *obj);

enum { YGUI2_STEPPER_MAX_STEPS = 16 };

struct YETTY_ANNOTATE("class@ygui2:stepper") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_stepper {
    uint32_t step_count; /* 0 == unset: defaults to 3 */
    uint32_t current;    /* 0-based */
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result stepper_paint(struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_stepper_ptr_result data_res = yetty_ygui2_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 stepper paint: data");
    struct yetty_ygui2_stepper *stepper = data_res.value;
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 stepper paint: theme");
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, NULL, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 stepper paint: rect");
    uint32_t step_count = stepper->step_count ? stepper->step_count : 3u;
    float dot_radius = 5.0f;
    float spacing = 22.0f;
    float connector_half_height = 1.0f;
    for (uint32_t index = 0; index < step_count; ++index) {
        float center_x = dot_radius + (float)index * spacing;
        if (index > 0) {
            struct yetty_ysdf_box connector = {
                .center_x = center_x - spacing * 0.5f,
                .center_y = height * 0.5f,
                .half_width = (spacing - dot_radius * 2.0f) * 0.5f,
                .half_height = connector_half_height,
                .corner_radius = 0.0f,
            };
            struct yetty_ycore_void_result connector_res =
                yetty_ydraw_drawable_list_add_cmd_add_box(
                    list, /*id=*/0, /*z_order=*/0,
                    index <= stepper->current ? theme.accent_deep : theme.bg_row, 0u, 0.0f,
                    &connector);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, connector_res, "ygui2 stepper paint: connector");
        }
        uint32_t fill = theme.bg_row;
        if (index < stepper->current) {
            fill = theme.accent_deep;
        } else if (index == stepper->current) {
            fill = theme.accent_bright;
        }
        struct yetty_ysdf_circle dot = {
            .center_x = center_x,
            .center_y = height * 0.5f,
            .radius = dot_radius,
        };
        struct yetty_ycore_void_result dot_res = yetty_ydraw_drawable_list_add_cmd_add_circle(
            list, /*id=*/0, /*z_order=*/0, fill, theme.border, 1.0f, &dot);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dot_res, "ygui2 stepper paint: dot");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_stepper_set_count(struct yetty_yclass_object *obj,
                                                             uint32_t step_count)
{
    struct yetty_ygui2_stepper_ptr_result data_res = yetty_ygui2_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 stepper_set_count: data");
    if (step_count == 0 || step_count > YGUI2_STEPPER_MAX_STEPS) {
        return YETTY_ERR(yetty_ycore_void, "ygui2 stepper_set_count: bad count");
    }
    if (data_res.value->step_count == step_count) {
        return YETTY_OK_VOID();
    }
    data_res.value->step_count = step_count;
    if (data_res.value->current >= step_count) {
        data_res.value->current = step_count - 1u;
    }
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_stepper_set_current(struct yetty_yclass_object *obj,
                                                               uint32_t current)
{
    struct yetty_ygui2_stepper_ptr_result data_res = yetty_ygui2_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 stepper_set_current: data");
    uint32_t step_count = data_res.value->step_count ? data_res.value->step_count : 3u;
    if (current >= step_count) {
        current = step_count - 1u;
    }
    if (data_res.value->current == current) {
        return YETTY_OK_VOID();
    }
    data_res.value->current = current;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ygui2_stepper_current(struct yetty_yclass_object *obj)
{
    struct yetty_ygui2_stepper_ptr_result data_res = yetty_ygui2_stepper_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 stepper_current: data");
    return YETTY_OK(yetty_ycore_int, (int)data_res.value->current);
}

#include "yetty/gen/impl/ygui2/widgets/stepper.c"
