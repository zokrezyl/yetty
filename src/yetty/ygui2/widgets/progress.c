/*
 * ygui2 progress — horizontal bar: track box + accent fill sized by value.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_progress_ptr, struct yetty_ygui2_progress *);
struct yetty_yclass_ptr_result yetty_ygui2_progress_class_get(void);
struct yetty_ygui2_progress_ptr_result yetty_ygui2_progress_from(struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:progress") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_progress {
    float value;     /* 0..1 */
    uint32_t accent; /* packed 0xAABBGGRR; 0 = brand mint */
    uint32_t track;  /* packed; 0 = brand row bg */
};

static struct yetty_ycore_void_result progress_box(struct yetty_ydraw_drawable_list *list, float x,
                                                   float w, float h, uint32_t fill)
{
    struct yetty_ysdf_box geometry = {
        .center_x = x + w * 0.5f,
        .center_y = h * 0.5f,
        .half_width = w * 0.5f,
        .half_height = h * 0.5f,
        .corner_radius = 2.0f,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_box(list, /*id=*/0, /*z_order=*/0, fill,
                                                     /*stroke=*/0u, /*stroke_width=*/0.0f,
                                                     &geometry);
}

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result progress_paint(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_progress_ptr_result data_res = yetty_ygui2_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 progress paint: data");
    struct yetty_ygui2_progress *progress = data_res.value;
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 progress paint: rect");
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 progress paint: theme");
    uint32_t track = progress->track ? progress->track : theme.bg_row;
    uint32_t accent = progress->accent ? progress->accent : theme.accent;
    struct yetty_ycore_void_result track_res = progress_box(list, 0.0f, width, height, track);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, track_res, "ygui2 progress paint: track");
    float clamped =
        progress->value < 0.0f ? 0.0f : (progress->value > 1.0f ? 1.0f : progress->value);
    if (clamped > 0.0f) {
        struct yetty_ycore_void_result fill_res =
            progress_box(list, 0.0f, width * clamped, height, accent);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, fill_res, "ygui2 progress paint: fill");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_progress_set_value(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ygui2_progress_ptr_result data_res = yetty_ygui2_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 progress_set_value: data");
    if (data_res.value->value == value) {
        return YETTY_OK_VOID();
    }
    data_res.value->value = value;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_progress_set_accent(struct yetty_yclass_object *obj,
                                                               uint32_t packed_rgba)
{
    struct yetty_ygui2_progress_ptr_result data_res = yetty_ygui2_progress_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 progress_set_accent: data");
    if (data_res.value->accent == packed_rgba) {
        return YETTY_OK_VOID();
    }
    data_res.value->accent = packed_rgba;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

#include "yetty/gen/impl/ygui2/widgets/progress.c"
