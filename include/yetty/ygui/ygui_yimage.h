#ifndef YETTY_YGUI_YGUI_YIMAGE_H
#define YETTY_YGUI_YGUI_YIMAGE_H

/*
 * ygui_yimage — image widget for ygui.
 *
 * Builds a draw_list with the yimage module (stb_image → PNG/JPEG/...
 * decoded to RGBA8, wrapped as a yimage complex prim) and hands it to a
 * RICH-style widget that translates the prim's bounds by the resolved
 * (x, y) at render time. The widget owns the produced buffer.
 *
 * Widget (w, h) defines the painted size; the source pixels are sampled
 * with hardware bilinear at display resolution.
 *
 * NULL is returned on failure (file missing, decode failure, OOM).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_engine;
struct yetty_ygui_widget;

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path);

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_YGUI_YIMAGE_H */
