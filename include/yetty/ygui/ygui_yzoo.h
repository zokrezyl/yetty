#ifndef YETTY_YGUI_YGUI_YZOO_H
#define YETTY_YGUI_YGUI_YZOO_H

/*
 * ygui_yzoo — yzoo snapshot widget for ygui.
 *
 * Instantiates a yzoo producer with the supplied (or default)
 * configuration, calls its render at `time = 0` to obtain the initial
 * point/connection layout, strips the leading CMD_ZERO record, and
 * hands the remaining primitives to a RICH-style widget.
 *
 * This is a *snapshot* widget — no animation. Pass a non-zero seed for
 * reproducibility.
 *
 * Pass `config = NULL` to use yetty_yzoo_config_default with its
 * scene_width/height overridden to (w, h). When `config` is supplied,
 * its scene_width/height are also overridden to (w, h).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_engine;
struct yetty_ygui_widget;
struct yetty_yzoo_config;

struct yetty_ygui_widget *yetty_ygui_engine_yzoo(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const struct yetty_yzoo_config *config, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_YGUI_YZOO_H */
