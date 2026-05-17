#ifndef YETTY_YGUI_YGUI_YJUNGLE_H
#define YETTY_YGUI_YGUI_YJUNGLE_H

/*
 * ygui_yjungle — yjungle snapshot widget for ygui.
 *
 * Instantiates a yjungle producer with the supplied (or default)
 * configuration, drives its first tick to obtain the initial chain,
 * flattens the resulting GROUP/CMD_ZERO records into a flat list of
 * paintable primitives, and hands the buffer to a RICH-style widget.
 *
 * This is a *snapshot* widget — no animation. The widget shows the
 * randomized initial state. Pass a non-zero seed for reproducibility.
 *
 * Pass `config = NULL` to use yetty_yjungle_config_default with its
 * scene_width/height overridden to (w, h). When `config` is supplied,
 * its scene_width/height are also overridden to (w, h) so the chain
 * stays inside the widget.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_engine;
struct yetty_ygui_widget;
struct yetty_yjungle_config;

struct yetty_ygui_widget *yetty_ygui_engine_yjungle(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const struct yetty_yjungle_config *config, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_YGUI_YJUNGLE_H */
