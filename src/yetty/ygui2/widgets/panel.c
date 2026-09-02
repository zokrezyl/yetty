/*
 * ygui2 panel — filled / outlined rectangle container. Paints its box at
 * the local origin sized to the layout rect; children paint into their own
 * groups on top.
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

YETTY_YRESULT_DECLARE(yetty_ygui2_panel_ptr, struct yetty_ygui2_panel *);
struct yetty_yclass_ptr_result yetty_ygui2_panel_class_get(void);
struct yetty_ygui2_panel_ptr_result yetty_ygui2_panel_from(struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:panel") YETTY_ANNOTATE("parent@ygui2:widget") yetty_ygui2_panel {
    uint32_t bg;     /* packed 0xAABBGGRR; 0 = no fill */
    uint32_t border; /* packed 0xAABBGGRR; 0 = no border */
    float border_width;
    float corner_radius;
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result panel_paint(struct yetty_yclass_object *obj,
                                                  struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_panel_ptr_result data_res = yetty_ygui2_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 panel paint: data");
    struct yetty_ygui2_panel *panel = data_res.value;
    if (panel->bg == 0 && panel->border == 0) {
        return YETTY_OK_VOID();
    }
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 panel paint: rect");
    if (width <= 0.0f || height <= 0.0f) {
        return YETTY_OK_VOID();
    }
    struct yetty_ysdf_box geometry = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = height * 0.5f,
        .corner_radius = panel->corner_radius,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_box(list, /*id=*/0, /*z_order=*/0, panel->bg,
                                                     panel->border, panel->border_width, &geometry);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_panel_set_bg(struct yetty_yclass_object *obj,
                                                        uint32_t packed_rgba)
{
    struct yetty_ygui2_panel_ptr_result data_res = yetty_ygui2_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 panel_set_bg: data");
    if (data_res.value->bg == packed_rgba) {
        return YETTY_OK_VOID();
    }
    data_res.value->bg = packed_rgba;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_panel_set_border(struct yetty_yclass_object *obj,
                                                            uint32_t packed_rgba, float width_px)
{
    struct yetty_ygui2_panel_ptr_result data_res = yetty_ygui2_panel_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 panel_set_border: data");
    data_res.value->border = packed_rgba;
    data_res.value->border_width = width_px;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

#include "yetty/gen/impl/ygui2/widgets/panel.c"
