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
 * context, and the OS window_chrome (from yframework); everything else is
 * internal.
 *
 * Usage:
 *   struct yetty_ychrome_host *chrome =
 *       yetty_ychrome_host_create(container, font, &ctx, rt->window_chrome,
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
struct yetty_yfigure_producer_session;
struct yetty_yfont_font;
struct yetty_context;
struct yetty_yclass_object;
struct yetty_yui_event;

YETTY_YRESULT_DECLARE(yetty_ychrome_host_ptr, struct yetty_ychrome_host *);

/* Create the chrome engine + its pinned caption figure under `container`.
 * `caption_height` / `edge_size` are in px; `flags` is YETTY_YCHROME_FLAG_*
 * (see <yetty/ychrome/chrome.h>). `window_chrome` is the yplatform yclass
 * object (yframework->window_chrome); pass NULL to render the bar without live
 * OS-window control (e.g. in-terminal). All pointers are borrowed. */
struct yetty_ychrome_host_ptr_result yetty_ychrome_host_create(
    struct yetty_yclass_object *container_obj, struct yetty_yfont_font *font,
    const struct yetty_context *ctx, struct yetty_yclass_object *window_chrome, float width,
    float height, float caption_height, float edge_size, unsigned int flags);

/* Wire variant for client / in-terminal mode: instead of pinning figures into
 * a local container, DRIVE the backdrop + caption onto the hosting yetty's root
 * figure container PROXY via the generated typed yclass-RPC stubs, so they
 * render in the host's pane. The opaque backdrop is what hides the host
 * terminal's text beneath the app. The owner attaches the producer session
 * (yetty_yfigure_producer_attach) and passes both the root container proxy
 * (yetty_yfigure_producer_session_container) and the owning session in here;
 * both are borrowed — the owner detaches the session after this host is
 * destroyed. `window_chrome` is typically a wire adapter (close/min/max → pane
 * ops) or NULL. */
struct yetty_ychrome_host_ptr_result yetty_ychrome_host_create_wire(
    struct yetty_yclass_object *container, struct yetty_yfigure_producer_session *producer_session,
    struct yetty_yclass_object *window_chrome, float width, float height, float caption_height,
    float edge_size, unsigned int flags);

/* Explicitly remove the wire chrome's figures (backdrop + caption) from the
 * host pane via the typed delete_child stubs. Each is one-way and flushes its
 * request synchronously with a blocking write to the session's write_fd, so
 * this is safe at process teardown after the event loop has stopped. No-op for
 * a local host. Call this BEFORE yetty_ychrome_host_destroy; the owner detaches
 * the producer session afterwards. */
struct yetty_ycore_void_result yetty_ychrome_host_clear(struct yetty_ychrome_host *host);

/* Re-emit the wire chrome's figures (backdrop + caption). No-op for a local
 * host. Use it to re-establish the chrome when the host pane may not have the
 * figures yet — e.g. the first frames over a slow guest/telnet transport where
 * the initial emit can land before the pane container is ready, or after a
 * container clear. Cheap to call a bounded number of times at startup. */
struct yetty_ycore_void_result yetty_ychrome_host_resync(struct yetty_ychrome_host *host);

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

/* Destroy the chrome engine + caption figure ownership held by the host, then
 * free the host. Handles NULL. */
struct yetty_ycore_void_result yetty_ychrome_host_destroy(struct yetty_ychrome_host *host);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCHROME_HOST_H */
