#ifndef YETTY_YGUI_OLD_YGUI_YZOO_H
#define YETTY_YGUI_OLD_YGUI_YZOO_H

/*
 * ygui_yzoo — dedicated yzoo widget for ygui.
 *
 * Owns its yzoo producer + buffers + render config. The widget's
 * resolved layout box always drives the producer's scene size; the
 * host calls `yetty_ygui_old_widget_yzoo_tick` from a timer to advance
 * the simulation (yzoo is a snapshot-style producer: each tick
 * rewrites the buffer from scratch).
 *
 * Pass `config = NULL` to use yetty_yzoo_config_default. The config's
 * scene_width / scene_height are ignored — the widget overrides them
 * with the live layout box on every tick.
 */

#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_old_engine;
struct yetty_ygui_old_widget;
struct yetty_yzoo_config;

/* Create a yzoo widget; the producer is instantiated lazily on the
 * first tick (or first render if no tick has fired yet) so the
 * scene size matches the resolved layout box. `seed = 0` seeds from
 * CLOCK_MONOTONIC. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yzoo(struct yetty_ygui_old_engine *engine,
                                                         const char *id, float x, float y, float w,
                                                         float h,
                                                         const struct yetty_yzoo_config *config,
                                                         uint32_t seed);

/* Advance the yzoo simulation by `dt_seconds` and rebuild the
 * cached paint buffer. The host typically calls this from a ~33ms
 * uv_timer. Calling tick on a freshly-attached widget builds the
 * initial frame. */
struct yetty_ycore_void_result yetty_ygui_old_widget_yzoo_tick(struct yetty_ygui_old_widget *widget,
                                                               float dt_seconds);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OLD_YGUI_YZOO_H */
