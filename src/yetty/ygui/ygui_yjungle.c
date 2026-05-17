/*
 * ygui_yjungle.c — yjungle snapshot widget.
 *
 * Drives yjungle's first tick to obtain the initial chain (CMD_ZERO +
 * N CMD_GROUPs each wrapping a paint primitive — possibly nested), then
 * flattens the result into a flat paint list and hands it to a RICH
 * widget.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yjungle.h>
#include <yetty/yjungle/yjungle.h>

#include "ygui_flatten.h"

struct yetty_ygui_widget *yetty_ygui_engine_yjungle(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const struct yetty_yjungle_config *config, uint32_t seed)
{
    struct yetty_yjungle_config cfg = config ? *config : yetty_yjungle_config_default();
    cfg.scene_width = w;
    cfg.scene_height = h;

    struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, seed);
    if (YETTY_IS_ERR(jr)) {
        yetty_ycore_error_destroy(jr.error);
        return NULL;
    }
    struct yetty_yjungle *jungle = jr.value;

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
        yetty_yjungle_destroy(jungle);
        return NULL;
    }
    struct yetty_ydraw_draw_list *raw = tr.value;

    /* Force-fire the first event by passing a large now_ms; the producer
     * always emits CMD_ZERO + the initial chain on its first tick. */
    struct yetty_ycore_void_result tick = yetty_yjungle_tick(jungle, raw, 0);
    yetty_yjungle_destroy(jungle);
    if (YETTY_IS_ERR(tick)) {
        yetty_ycore_error_destroy(tick.error);
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
