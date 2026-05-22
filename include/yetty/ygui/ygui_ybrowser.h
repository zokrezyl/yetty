#ifndef YETTY_YGUI_YGUI_YBROWSER_H
#define YETTY_YGUI_YGUI_YBROWSER_H

/*
 * ygui_ybrowser — HTML widget for ygui.
 *
 * Builds a draw_list with ybrowser (ylexbor backend) and hands it to a
 * RICH-style widget that translates every primitive by its resolved
 * (x, y) at render time. The widget owns the produced buffer.
 *
 * Both constructors set ylexbor's base URL so external stylesheets /
 * scripts referenced relatively from the document resolve against the
 * local directory:
 *   - from_file: base URL is file://<absolute dir of path>/
 *   - from_buffer: caller passes an optional base_url (e.g. file:///some/dir/
 *     or https://example.com/page.html); NULL → no base, only absolute
 *     URLs in the document will resolve.
 *
 * Widget (w, h) is used as the rendering viewport. NULL is returned on
 * failure.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_engine;
struct yetty_ygui_widget;

struct yetty_ygui_widget *yetty_ygui_engine_ybrowser_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path);

struct yetty_ygui_widget *yetty_ygui_engine_ybrowser_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len, const char *base_url);

/* Default sample HTML embedded into ygui_ybrowser. Returns NULL on
 * failure or when the library was built without an asset. */
struct yetty_ygui_widget *yetty_ygui_engine_ybrowser_default(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_YGUI_YBROWSER_H */
