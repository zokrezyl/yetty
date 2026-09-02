/*
 * ygui2 separator — a thin horizontal rule in the border color. Pure
 * chrome: no state, no interaction; the layout basis gives it its slot
 * (a 1px line centered in whatever height the flex pass assigns).
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

YETTY_YRESULT_DECLARE(yetty_ygui2_separator_ptr, struct yetty_ygui2_separator *);
struct yetty_yclass_ptr_result yetty_ygui2_separator_class_get(void);
struct yetty_ygui2_separator_ptr_result yetty_ygui2_separator_from(struct yetty_yclass_object *obj);

struct YETTY_ANNOTATE("class@ygui2:separator") YETTY_ANNOTATE("parent@ygui2:widget")
    yetty_ygui2_separator {
    uint32_t color; /* packed 0xAABBGGRR; 0 = theme border */
};

YETTY_ANNOTATE("override@ygui2:widget:widget_paint")
static struct yetty_ycore_void_result separator_paint(struct yetty_yclass_object *obj,
                                                      struct yetty_ydraw_drawable_list *list)
{
    struct yetty_ygui2_separator_ptr_result data_res = yetty_ygui2_separator_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 separator paint: data");
    struct yetty_ygui2_theme theme;
    struct yetty_ycore_void_result theme_res = yetty_ygui2_widget_theme_copy(obj, &theme);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, theme_res, "ygui2 separator paint: theme");
    float width = 0.0f;
    float height = 0.0f;
    struct yetty_ycore_void_result rect_res =
        yetty_ygui2_widget_rect(obj, NULL, NULL, &width, &height);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "ygui2 separator paint: rect");
    uint32_t color = data_res.value->color ? data_res.value->color : theme.border;
    struct yetty_ysdf_box line = {
        .center_x = width * 0.5f,
        .center_y = height * 0.5f,
        .half_width = width * 0.5f,
        .half_height = 0.5f,
        .corner_radius = 0.0f,
    };
    return yetty_ydraw_drawable_list_add_cmd_add_box(list, /*id=*/0, /*z_order=*/0, color, 0u, 0.0f,
                                                     &line);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ygui2_separator_set_color(struct yetty_yclass_object *obj,
                                                               uint32_t packed_rgba)
{
    struct yetty_ygui2_separator_ptr_result data_res = yetty_ygui2_separator_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "ygui2 separator_set_color: data");
    if (data_res.value->color == packed_rgba) {
        return YETTY_OK_VOID();
    }
    data_res.value->color = packed_rgba;
    return yetty_ygui2_widget_mark_skin_dirty(obj);
}

#include "yetty/gen/impl/ygui2/widgets/separator.c"
