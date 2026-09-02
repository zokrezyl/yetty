/*
 * ygui2 scrollarea — the viewport widget (strategy T4): its group carries
 * the CLIP rect (contract §1a). User children mount beneath an OWNED
 * minted content group (the framework's widget_add redirects), and wheel
 * input moves that group — a scroll tick ships exactly ONE offset update
 * regardless of content size; nothing is ever re-sent.
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

/* Cross-class within-module: the shared content measure lives on the
 * framework (declared ahead of the regenerated header so the first
 * codegen pass resolves it). */
struct yetty_ycore_void_result yetty_ygui2_widget_scroll_limit(struct yetty_yclass_object *obj,
                                                               float *out_limit);

YETTY_YRESULT_DECLARE(yetty_ygui2_scrollarea_ptr, struct yetty_ygui2_scrollarea *);
struct yetty_yclass_ptr_result yetty_ygui2_scrollarea_class_get(void);
struct yetty_ygui2_scrollarea_ptr_result yetty_ygui2_scrollarea_from(
    struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:scrollarea") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_scrollarea {
    float wheel_step; /* px per wheel unit; 0 = 24 */
    float max_scroll; /* clamp; 0 = unbounded */
};

YETTY_ANNOTATE("override@ygui2:widget:widget_on_scroll")
static struct yetty_ycore_int_result scrollarea_on_scroll(struct yetty_yclass_object *obj,
                                                          float local_x, float local_y,
                                                          float wheel_dy)
{
    (void)local_x;
    (void)local_y;
    struct yetty_ygui2_scrollarea_ptr_result data_res = yetty_ygui2_scrollarea_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data_res, "ygui2 scrollarea scroll: data");
    struct yetty_ygui2_scrollarea *scrollarea = data_res.value;
    float step = scrollarea->wheel_step > 0.0f ? scrollarea->wheel_step : 24.0f;
    float scroll_x = 0.0f;
    float scroll_y = 0.0f;
    struct yetty_ycore_void_result get_res = yetty_ygui2_widget_scroll(obj, &scroll_x, &scroll_y);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, get_res, "ygui2 scrollarea scroll: get");
    /* The wheel moves the EFFECTIVE main axis: a ROW viewport scrolls
     * horizontally — the layout cursor reads scroll_x there and would
     * never see a scroll_y change. */
    struct yetty_ygui2_layout spec;
    struct yetty_ycore_void_result spec_res = yetty_ygui2_widget_layout_copy(obj, &spec);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, spec_res, "ygui2 scrollarea scroll: spec");
    int row = spec.direction == YETTY_YGUI2_DIRECTION_ROW;
    float along = row ? scroll_x : scroll_y;
    along -= wheel_dy * step;
    if (along < 0.0f) {
        along = 0.0f;
    }
    /* Clamp to the MEASURED content overhang (the same measure that
     * sizes the content group), so the wheel always reaches the last
     * rows and never scrolls into blank space; an explicit max_scroll
     * stays an app override. */
    float limit = scrollarea->max_scroll;
    if (limit <= 0.0f) {
        struct yetty_ycore_void_result limit_res = yetty_ygui2_widget_scroll_limit(obj, &limit);
        if (YETTY_IS_ERR(limit_res)) {
            yetty_ycore_error_destroy(limit_res.error);
            limit = 0.0f;
        }
    }
    if (along > limit) {
        along = limit;
    }
    struct yetty_ycore_void_result set_res =
        yetty_ygui2_widget_set_scroll(obj, row ? along : scroll_x, row ? scroll_y : along);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, set_res, "ygui2 scrollarea scroll: set");
    return YETTY_OK(yetty_ycore_int, 1);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_scrollarea_configure(struct yetty_yclass_object *obj,
                                                                float wheel_step, float max_scroll)
{
    struct yetty_ygui2_scrollarea_ptr_result data_res = yetty_ygui2_scrollarea_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 scrollarea_configure: data");
    data_res.value->wheel_step = wheel_step;
    data_res.value->max_scroll = max_scroll; /* 0 = clamp to the measured extent */
    /* The scrollarea's group is the viewport: emit its clip rect. */
    return yetty_ygui2_widget_set_clip_enabled(obj);
}

#include "yetty/gen/impl/ygui2/widgets/scrollarea.c"
