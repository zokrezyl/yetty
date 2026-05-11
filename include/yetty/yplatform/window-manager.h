#ifndef YETTY_YPLATFORM_WINDOW_MANAGER_H
#define YETTY_YPLATFORM_WINDOW_MANAGER_H

/*
 * Window-control manager — the render thread's typed API for telling the
 * OS window to minimize / toggle-maximize / close / move. Mirrors the
 * shape of the clipboard manager: producer ops write events to the
 * shared output_pipe and post an empty event so the main thread wakes
 * up; handle_event() runs the actual GLFW call on the main thread.
 *
 * The output_pipe drain (currently inside clipboard_manager) calls
 * handle_event() for any YETTY_YCORE_WINDOW_* it pulls off the pipe so
 * one drain serves both managers — pipes are FIFO and can only be read
 * by one consumer, so the drain delegates by event type instead of
 * trying to share read access.
 *
 * Thread-safety: producer ops are safe from any thread (just write to
 * the pipe). handle_event() must run on the main thread (it touches
 * GLFW directly).
 */

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_window_manager;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yui_event;

YETTY_YRESULT_DECLARE(yetty_yplatform_window_manager_ptr,
                     struct yetty_yplatform_window_manager *);

struct yetty_yplatform_window_manager_ops {
    void (*destroy)(struct yetty_yplatform_window_manager *self);

    /* Producer side — write the request onto output_pipe and wake the
     * main thread. Fire-and-forget. Safe from any thread. */
    void (*iconify)(struct yetty_yplatform_window_manager *self);
    void (*toggle_maximize)(struct yetty_yplatform_window_manager *self);
    void (*request_close)(struct yetty_yplatform_window_manager *self);
    void (*drag_by)(struct yetty_yplatform_window_manager *self, int dx, int dy);

    /* Main-thread side — apply one drained WINDOW_* event by calling
     * GLFW. The clipboard manager's drain dispatches here. */
    void (*handle_event)(struct yetty_yplatform_window_manager *self,
                         const struct yetty_yui_event *event);
};

struct yetty_yplatform_window_manager {
    const struct yetty_yplatform_window_manager_ops *ops;
};

/* Construct a window manager bound to (os_window, output_pipe). os_window
 * is the opaque GLFWwindow* the platform layer hands out; output_pipe is
 * the same render→main pipe the clipboard manager uses (both borrowed —
 * the caller owns lifetimes). */
struct yetty_yplatform_window_manager_ptr_result yetty_yplatform_window_manager_create(
    void *os_window, struct yetty_ycore_xthread_event_pipe *output_pipe);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_WINDOW_MANAGER_H */
