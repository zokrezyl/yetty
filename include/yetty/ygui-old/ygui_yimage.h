#ifndef YETTY_YGUI_OLD_YGUI_YIMAGE_H
#define YETTY_YGUI_OLD_YGUI_YIMAGE_H

/*
 * ygui_yimage — image widget for ygui.
 *
 * Builds a draw_list with the yimage module (stb_image → PNG/JPEG/...
 * decoded to RGBA8, wrapped as a yimage complex prim) and hands it to
 * a RICH-style widget. Widget (w, h) defines the painted size; source
 * pixels are sampled at display resolution with hardware bilinear.
 *
 * set_file / set_buffer rebuild the prim and swap it on the same
 * widget — same id, so the receiving scene-canvas folds the change in
 * place rather than recreating the entity. Useful for cycling logos,
 * lazy-loaded thumbnails, etc.
 *
 * NULL is returned on failure (file missing, decode failure, OOM).
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_old_engine;
struct yetty_ygui_old_widget;

struct yetty_ygui_old_widget *yetty_ygui_old_engine_yimage_from_file(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const char *path);

struct yetty_ygui_old_widget *yetty_ygui_old_engine_yimage_from_buffer(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const uint8_t *data, size_t len);

/* Replace the widget's image content from a file path. The widget's
 * id is preserved so the receiving canvas treats this as an in-place
 * swap. ERR when the widget is NULL, the file is missing, or the
 * decoder rejects the source. */
struct yetty_ycore_void_result yetty_ygui_old_widget_yimage_set_file(
    struct yetty_ygui_old_widget *widget, const char *path);

/* Same, but from an in-memory image (PNG/JPEG/...). */
struct yetty_ycore_void_result yetty_ygui_old_widget_yimage_set_buffer(
    struct yetty_ygui_old_widget *widget, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OLD_YGUI_YIMAGE_H */
