#ifndef YETTY_YGUI_YGUI_YPDF_H
#define YETTY_YGUI_YGUI_YPDF_H

/*
 * ygui_ypdf — PDF widget for ygui.
 *
 * Builds a draw_list with ypdf and hands it to a RICH-style widget that
 * translates every primitive by its resolved (x, y) at render time. The
 * widget owns the produced buffer.
 *
 * Two constructors:
 *   - from_file(path)              opens via pdfio's filename entry point
 *   - from_buffer(bytes, len)      writes bytes to a temp file, opens, unlinks
 *                                  (pdfio's only public entry point is by
 *                                  filename; the temp-file dance is invisible
 *                                  to the caller).
 *
 * NULL is returned on any failure (file missing, malformed PDF, OOM).
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_engine;
struct yetty_ygui_widget;

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path);

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_YGUI_YPDF_H */
