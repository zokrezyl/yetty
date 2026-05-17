/*
 * ygui_yzoo.c — yzoo snapshot widget.
 *
 * Renders one yzoo frame at time=0, strips the leading CMD_ZERO record
 * (the producer prepends one for the scrolling-canvas envelope), and
 * hands the remaining paint primitives to a RICH widget.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yzoo.h>
#include <yetty/yzoo/yzoo.h>

#include "ygui_flatten.h"

struct yetty_ygui_widget *yetty_ygui_engine_yzoo(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const struct yetty_yzoo_config *config, uint32_t seed)
{
    struct yetty_yzoo_config cfg = config ? *config : yetty_yzoo_config_default();
    cfg.scene_width = w;
    cfg.scene_height = h;

    struct yetty_yzoo_ptr_result zr = yetty_yzoo_create(&cfg, seed);
    if (YETTY_IS_ERR(zr)) {
        yetty_ycore_error_destroy(zr.error);
        return NULL;
    }
    struct yetty_yzoo *zoo = zr.value;

    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = w,
        .scene_max_y = h,
    };
    struct yetty_ydraw_draw_list_result tr =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_destroy(tr.error);
        yetty_yzoo_destroy(zoo);
        return NULL;
    }
    struct yetty_ydraw_draw_list *raw = tr.value;

    struct yetty_ycore_void_result rr = yetty_yzoo_render(zoo, raw, 0.0f);
    yetty_yzoo_destroy(zoo);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        yetty_ydraw_draw_list_destroy(raw);
        return NULL;
    }

    struct yetty_ydraw_draw_list_result fr =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        yetty_ydraw_draw_list_destroy(raw);
        return NULL;
    }
    struct yetty_ydraw_draw_list *flat = fr.value;

    struct yetty_ycore_void_result fl = yetty_ygui_flatten_draw_list(flat, raw);
    yetty_ydraw_draw_list_destroy(raw);
    if (YETTY_IS_ERR(fl)) {
        yetty_ycore_error_destroy(fl.error);
        yetty_ydraw_draw_list_destroy(flat);
        return NULL;
    }

    struct yetty_ygui_widget *widget = yetty_ygui_engine_rich(engine, id, x, y, w, h);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(flat);
        return NULL;
    }
    yetty_ygui_widget_rich_set_buffer(widget, flat);
    return widget;
}
