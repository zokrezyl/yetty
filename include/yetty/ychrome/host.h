#ifndef YETTY_YCHROME_HOST_H
#define YETTY_YCHROME_HOST_H

/*
 * ychrome host — the reusable glue that drops window chrome (draggable /
 * resizable / maximizable titlebar with minimize/maximize/close) into any
 * framework app, ygui or not.
 *
 * It owns a ychrome:chrome engine (the gesture + self-rendered titlebar) plus a
 * pinned ygrid figure that composites the chrome's painted caption on top of
 * the app's content. The app supplies its yfigure container, a font, the GPU
 * context, and the OS window_manager (from yframework); everything else is
 * internal.
 *
 * Usage:
 *   struct yetty_ychrome_host *chrome =
 *       yetty_ychrome_host_create(container, font, &ctx, rt->window_manager,
 *                                 width, height, 34.0f, 8.0f,
 *                                 YETTY_YCHROME_FLAG_ALL).value;
 *   // in the mouse path, after your own controls had their chance:
 *   yetty_ychrome_handle_event(NULL, yetty_ychrome_host_chrome(chrome), &ev);
 *   // on resize:
 *   yetty_ychrome_host_resized(chrome, width, height);
 *   // teardown:
 *   yetty_ychrome_host_destroy(chrome);
 *
 * Coordinates are in the same pixel space the app feeds mouse events / sizes the
 * viewport in (framebuffer px in yetty's stack).
 */

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* yetty_ycore_int_result */

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ychrome_host;
struct yetty_yfigure_container;
struct yetty_yfont_font;
struct yetty_context;
struct yetty_yclass_object;
struct yetty_yui_event;

YETTY_YRESULT_DECLARE(yetty_ychrome_host_ptr, struct yetty_ychrome_host *);

/* Create the chrome engine + its pinned caption figure under `container`.
 * `caption_height` / `edge_size` are in px; `flags` is YETTY_YCHROME_FLAG_*
 * (see <yetty/ychrome/chrome.h>). `window_manager` is the yplatform yclass
 * object (yframework->window_manager); pass NULL to render the bar without live
 * OS-window control (e.g. in-terminal). All pointers are borrowed. */
struct yetty_ychrome_host_ptr_result yetty_ychrome_host_create(
    struct yetty_yfigure_container *container, struct yetty_yfont_font *font,
    const struct yetty_context *ctx, struct yetty_yclass_object *window_manager, float width,
    float height, float caption_height, float edge_size, unsigned int flags);

/* The underlying ychrome:chrome object — feed it mouse events via
 * yetty_ychrome_handle_event. NULL if `host` is NULL. */
struct yetty_yclass_object *yetty_ychrome_host_chrome(struct yetty_ychrome_host *host);

/* Forward one event to the chrome engine and re-paint the caption if the hover
 * highlight changed. Returns 1 if chrome claimed the event (the app should stop
 * processing it), 0 otherwise. Prefer this over calling
 * yetty_ychrome_handle_event directly so the hover highlight tracks the pointer. */
struct yetty_ycore_int_result yetty_ychrome_host_handle_event(struct yetty_ychrome_host *host,
                                                              const struct yetty_yui_event *event);

/* Update the window size: re-sizes the engine's edge bands and repositions +
 * repaints the caption figure. Call on every resize. */
struct yetty_ycore_void_result yetty_ychrome_host_resized(struct yetty_ychrome_host *host,
                                                          float width, float height);

/* Declare the header-bar content band — the slot the app fills with its own UI
 * (e.g. a ygui tabbar laid out across the top of its scene). With `active`
 * non-zero, chrome yields pointer events inside [left, right] to the app and
 * paints no caption background there (the app's content paints the strip),
 * while the rest of the caption stays a drag handle and the window controls stay
 * on the right. Keep `right` at or left of (width - 3 * 46 px) so the controls
 * stay clickable. Call whenever the content's extent changes (tabs added). Pass
 * active=0 to take the whole caption back. */
struct yetty_ycore_void_result yetty_ychrome_host_set_content_band(struct yetty_ychrome_host *host,
                                                                   int active, float left,
                                                                   float right);

/* Destroy the chrome engine + caption figure ownership held by the host, then
 * free the host. Handles NULL. */
struct yetty_ycore_void_result yetty_ychrome_host_destroy(struct yetty_ychrome_host *host);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCHROME_HOST_H */
