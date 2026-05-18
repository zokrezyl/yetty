/*
 * yevent/dispatch.c - top-level event-loop wiring helpers.
 *
 * Owns the boilerplate of registering yetty's main listener for the full set
 * of event types it cares about, plus the small async-post helper used by
 * input-translation code paths (e.g. Ctrl+Scroll → ZOOM_VISUAL).
 */

#include <yetty/yevent/dispatch.h>

#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yevent/event.h>
#include <yetty/ytrace/ytrace.h>

#include <stddef.h>

struct yetty_ycore_void_result yetty_yevent_register_default_listeners(
    struct yetty_yevent_event_loop *el, struct yetty_yevent_event_listener *listener)
{
    struct yetty_ycore_void_result res;

    static const enum yetty_yevent_event_type kTypes[] = {
        /* Keyboard */
        YETTY_YCORE_KEY_DOWN,
        YETTY_YCORE_KEY_UP,
        YETTY_YCORE_CHAR,
        /* Mouse */
        YETTY_YCORE_MOUSE_DOWN,
        YETTY_YCORE_MOUSE_UP,
        YETTY_YCORE_MOUSE_MOVE,
        YETTY_YCORE_MOUSE_DRAG,
        YETTY_YCORE_MOUSE_SCROLL,
        /* Lifecycle / framing */
        YETTY_YCORE_RESIZE,
        YETTY_YCORE_RENDER,
        YETTY_YCORE_WINDOW_REFRESH,
        YETTY_YCORE_SHUTDOWN,
        /* Named zoom events — consumed by yetty (ZOOM_VISUAL / PAN) or
         * forwarded to the workspace (ZOOM_CELL_SIZE). Registering APPLY too
         * so the event loop knows the type exists even though it is normally
         * forwarded directly via workspace_on_event. */
        YETTY_YCORE_ZOOM_VISUAL,
        YETTY_YCORE_ZOOM_VISUAL_PAN,
        YETTY_YCORE_ZOOM_VISUAL_APPLY,
        YETTY_YCORE_ZOOM_CELL_SIZE,
        /* Capture: yetty handles via the screenshot module. */
        YETTY_YCORE_SCREENSHOT,
        /* Clipboard paste — posted by the clipboard manager (main thread)
         * onto the input pipe after fetching the system clipboard. The
         * focused terminal consumes it and writes the bytes to its PTY. */
        YETTY_YCORE_PASTE,
        /* Chrome-driven structural mutations. yui pre-allocates ids and
         * posts these so the workspace layer materialises tiles with the
         * exact same identifiers chrome already keyed its widget map on. */
        YETTY_YCORE_WORKSPACE_CREATE,
        YETTY_YCORE_PANE_CREATE,
        YETTY_YCORE_PANE_SPLIT,
        YETTY_YCORE_SPLIT_RESIZE,
    };

    for (size_t i = 0; i < sizeof(kTypes) / sizeof(kTypes[0]); ++i) {
        res = el->ops->register_listener(el, kTypes[i], listener, 0);
        if (!YETTY_IS_OK(res)) {
            return res;
        }
    }

    ydebug("yevent: registered default listeners (%zu types)", sizeof(kTypes) / sizeof(kTypes[0]));
    return YETTY_OK_VOID();
}

void yetty_yevent_post_async(struct yetty_ycore_xthread_event_pipe *pipe,
                             const struct yetty_yui_event *event)
{
    if (!pipe || !pipe->ops || !pipe->ops->write) {
        yerror("yetty_yevent_post_async: no input pipe available");
        return;
    }
    pipe->ops->write(pipe, event, sizeof(*event));
}
