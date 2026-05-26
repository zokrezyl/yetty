#ifndef YETTY_YGUI_OLD_YGUI_YJUNGLE_H
#define YETTY_YGUI_OLD_YGUI_YJUNGLE_H

/*
 * ygui_yjungle — dedicated yjungle widget for ygui.
 *
 * Owns its yjungle producer + delta/accumulator/flat buffers + a
 * mirror of the live segment map (parsed from CMD_ZERO/DELETE/GROUP
 * deltas). The widget's resolved layout box drives the producer's
 * scene size; the host calls `yetty_ygui_old_widget_yjungle_tick` from
 * a timer to advance the simulation.
 *
 * Pass `config = NULL` to use yetty_yjungle_config_default. The
 * config's scene_width / scene_height are ignored — the widget
 * overrides them with the live layout box.
 */

#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_old_engine;
struct yetty_ygui_old_widget;
struct yetty_yjungle_config;

/* Create a yjungle widget; the producer is instantiated lazily on
 * the first tick. `seed = 0` seeds from CLOCK_MONOTONIC. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yjungle(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const struct yetty_yjungle_config *config, uint32_t seed);

/* Advance the yjungle producer by one tick at the given monotonic
 * millisecond clock value. If no event fires this tick the cached
 * buffer is left alone. */
struct yetty_ycore_void_result yetty_ygui_old_widget_yjungle_tick(
    struct yetty_ygui_old_widget *widget, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OLD_YGUI_YJUNGLE_H */
