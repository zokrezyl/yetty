#ifndef YETTY_YPLATFORM_CLIPBOARD_MANAGER_H
#define YETTY_YPLATFORM_CLIPBOARD_MANAGER_H

#include <stddef.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_platform_clipboard_manager;
struct yetty_ycore_xthread_event_pipe;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yplatform_clipboard_manager, struct yetty_platform_clipboard_manager *);

/* Clipboard manager — render-thread API + main-thread drain.
 *
 * GLFW clipboard calls MUST happen on the main thread (the one that ran
 * glfwInit). On X11, glfwGetClipboardString blocks waiting for
 * SelectionNotify events that only the main thread can pump.
 *
 * Solution: a render→main "output pipe" (shared with future main-thread-
 * only operations like window minimize/maximize/setTitle) carries
 * clipboard requests. The existing input_pipe (main→render) carries the
 * paste response back as a YETTY_YCORE_PASTE event with the heap-
 * allocated text in event->payload (the receiver must free it).
 *
 * Both API ops (set_text, request_paste) are non-blocking writes into
 * output_pipe + a glfwPostEmptyEvent wakeup. The main thread drains
 * output_pipe between glfwWaitEvents calls. */
struct yetty_platform_clipboard_manager_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_platform_clipboard_manager *self);
    /* Push text to the system clipboard. Fire-and-forget on the wire,
     * but the cross-thread enqueue can fail (pipe full, allocation) —
     * the caller must learn about that. */
    struct yetty_ycore_void_result (*set_text)(struct yetty_platform_clipboard_manager *self,
                                               const char *text, size_t len);
    /* Ask the main thread to fetch the clipboard. The result arrives later
     * as a YETTY_YCORE_PASTE event on the input pipe with event->payload
     * set to a malloc'd UTF-8 C string the receiver must free. */
    struct yetty_ycore_void_result (*request_paste)(struct yetty_platform_clipboard_manager *self);
    /* Main-thread only: read pending requests from output_pipe and run the
     * actual GLFW calls. Safe to call when nothing is pending. */
    struct yetty_ycore_void_result (*drain)(struct yetty_platform_clipboard_manager *self);
};

/* Clipboard manager base */
struct yetty_platform_clipboard_manager {
    const struct yetty_platform_clipboard_manager_ops *ops;
};

struct yetty_yclass_object;

/* Construct a clipboard manager that uses output_pipe for render→main
 * requests and writes paste responses back to input_pipe (as
 * YETTY_YCORE_PASTE events with a heap-allocated payload). Both pipes
 * are borrowed — the caller owns them.
 *
 * The window_manager is an optional handler the drain hands non-clipboard
 * events to (window minimize/maximize/close/drag). Pipes are FIFO with
 * a single reader; this routing is how two managers share one pipe.
 * Pass NULL to drop unknown events on the floor (the historical
 * behaviour, kept for builds that don't compile the window manager). */
struct yetty_yplatform_clipboard_manager_result yetty_platform_clipboard_manager_create(
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe, struct yetty_yclass_object *window_manager);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_CLIPBOARD_MANAGER_H */
