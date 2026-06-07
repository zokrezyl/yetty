/*
 * yetty/yclient-lib/event-loop.h — libuv-backed event loop for yetty
 * client apps (ymgui frontends, ygui, yrich, ycat, …).
 *
 * Same shape as a GLFW backend, just driven by libuv instead of an OS
 * windowing layer:
 *
 *   - the loop owns stdin I/O (uv_poll_t)
 *   - bytes from yetty are streamed through yface, which fires typed
 *     callbacks for OSC events (mouse / resize / …) and forwards
 *     non-envelope bytes verbatim through on_raw (keyboard / CSI)
 *   - the loop coalesces input arrivals into a single on_frame tick so
 *     the app does NewFrame/Render once per batch
 *   - app code can register its own fds, timers, and posted tasks for
 *     network or whatever else
 *
 * Loop and codec are owned by yclient-lib; consumers (ymgui's
 * imgui_impl_yetty, ygui's engine, yrich-runner) bridge the typed
 * callbacks into their own state.
 */

#ifndef YETTY_YCLIENT_LIB_EVENT_LOOP_H
#define YETTY_YCLIENT_LIB_EVENT_LOOP_H

#include <stdint.h>
#include <stddef.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclient_event_loop;

YETTY_YRESULT_DECLARE(yetty_yclient_event_loop_ptr, struct yetty_yclient_event_loop *);

/*=============================================================================
 * Input callbacks (GLFW-style: one setter per event type)
 *
 * Every event carries a `figure_id` (see <yetty/ymgui/wire.h> — cards are
 * placed sub-regions of the terminal grid). Coordinates are card-local
 * pixels, origin at the card's top-left. Buttons follow ImGui ordering:
 * 0=left, 1=right, 2=middle, 3=x1, 4=x2.
 *===========================================================================*/
typedef void (*yetty_yclient_mouse_pos_cb)(void *user, uint32_t figure_id, double x, double y,
                                           uint32_t buttons_held);
typedef void (*yetty_yclient_mouse_button_cb)(void *user, uint32_t figure_id, int button,
                                              int pressed, double x, double y);
typedef void (*yetty_yclient_mouse_wheel_cb)(void *user, uint32_t figure_id, double dy, double x,
                                             double y);
typedef void (*yetty_yclient_resize_cb)(void *user, uint32_t figure_id, double width,
                                        double height);
/* Click-focus transition. gained=1 → card focused, gained=0 → card lost
 * focus (use this to drain key-up / mouse-up on the previously-focused
 * card before switching contexts). */
typedef void (*yetty_yclient_focus_cb)(void *user, uint32_t figure_id, int gained);
/* Keyboard. kind: 0=down, 1=up, 2=char (codepoint set, key=-1). */
typedef void (*yetty_yclient_key_cb)(void *user, uint32_t figure_id, int kind, int key, int mods,
                                     uint32_t codepoint);

/* Raw passthrough — bytes outside any OSC envelope. The app plugs
 * their keyboard / CSI parser here. May be NULL to drop them. */
typedef void (*yetty_yclient_raw_cb)(void *user, const char *bytes, size_t n);

/* "A batch of input was drained — your state may have changed, draw a
 * frame." Coalesces multiple input events into one tick. */
typedef void (*yetty_yclient_frame_cb)(void *user);

/*=============================================================================
 * App extensions (network fds, timers, posted tasks)
 *===========================================================================*/
#define YETTY_YCLIENT_FD_READABLE 0x1
#define YETTY_YCLIENT_FD_WRITABLE 0x2

typedef void (*yetty_yclient_fd_cb)(void *user, int fd, int events_mask);
typedef void (*yetty_yclient_timer_cb)(void *user);
typedef void (*yetty_yclient_task_cb)(void *user);

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

struct yetty_yclient_lib_event_loop_config {
    int in_fd;  /* if <0, defaults to STDIN_FILENO */
    void *user; /* opaque, passed to all callbacks */
};

/* Create a loop. Allocates the libuv loop, opens uv_poll on in_fd, and
 * spins up a yface for stream decoding. Does NOT start running yet —
 * call run / poll. */
struct yetty_yclient_event_loop_ptr_result yetty_yclient_event_loop_create(
    const struct yetty_yclient_lib_event_loop_config *cfg);
/* Best-effort teardown — always frees the loop; the Result surfaces the
 * first sub-destroy failure (e.g. the owned yface) for callers that care. */
struct yetty_ycore_void_result yetty_yclient_event_loop_destroy(
    struct yetty_yclient_event_loop *loop);

/*=============================================================================
 * Callback wiring
 *===========================================================================*/
void yetty_yclient_event_loop_set_user(struct yetty_yclient_event_loop *, void *user);
void yetty_yclient_event_loop_set_mouse_pos_cb(struct yetty_yclient_event_loop *,
                                               yetty_yclient_mouse_pos_cb);
void yetty_yclient_event_loop_set_mouse_button_cb(struct yetty_yclient_event_loop *,
                                                  yetty_yclient_mouse_button_cb);
void yetty_yclient_event_loop_set_mouse_wheel_cb(struct yetty_yclient_event_loop *,
                                                 yetty_yclient_mouse_wheel_cb);
void yetty_yclient_event_loop_set_resize_cb(struct yetty_yclient_event_loop *,
                                            yetty_yclient_resize_cb);
void yetty_yclient_event_loop_set_focus_cb(struct yetty_yclient_event_loop *,
                                           yetty_yclient_focus_cb);
void yetty_yclient_event_loop_set_key_cb(struct yetty_yclient_event_loop *, yetty_yclient_key_cb);
void yetty_yclient_event_loop_set_raw_cb(struct yetty_yclient_event_loop *, yetty_yclient_raw_cb);
void yetty_yclient_event_loop_set_frame_cb(struct yetty_yclient_event_loop *,
                                           yetty_yclient_frame_cb);

/*=============================================================================
 * Run modes
 *===========================================================================*/

/* Block forever, draining I/O and dispatching callbacks, until
 * yetty_yclient_event_loop_stop is called from any thread. Returns 0 on
 * clean exit, non-zero on libuv error. */
int yetty_yclient_event_loop_run(struct yetty_yclient_event_loop *loop);

/* Pump one iteration. wait != 0 → block until at least one handle
 * fires; wait == 0 → return immediately if nothing ready. Mirrors
 * uv_run UV_RUN_ONCE / UV_RUN_NOWAIT. */
int yetty_yclient_event_loop_poll(struct yetty_yclient_event_loop *loop, int wait);

/* Wake the loop and ask it to exit. Thread-safe — uses uv_async. */
void yetty_yclient_event_loop_stop(struct yetty_yclient_event_loop *loop);

/* Coalescing wakeup — request that the next iteration call frame_cb
 * even if no input arrives. Thread-safe. Useful when the app's own
 * state changed (e.g., a network reply landed) and the UI needs a
 * redraw. */
void yetty_yclient_event_loop_request_frame(struct yetty_yclient_event_loop *loop);

/*=============================================================================
 * App extensions — return a handle id (>0) or 0 on failure.
 *===========================================================================*/

int yetty_yclient_event_loop_add_fd(struct yetty_yclient_event_loop *loop, int fd, int events_mask,
                                    yetty_yclient_fd_cb cb, void *cb_user);
void yetty_yclient_event_loop_remove_fd(struct yetty_yclient_event_loop *loop, int id);

int yetty_yclient_event_loop_add_timer(struct yetty_yclient_event_loop *loop, int timeout_ms,
                                       int repeat_ms, yetty_yclient_timer_cb cb, void *cb_user);
void yetty_yclient_event_loop_remove_timer(struct yetty_yclient_event_loop *loop, int id);

/* Schedule fn(arg) to run on the loop thread. Thread-safe. */
void yetty_yclient_event_loop_post(struct yetty_yclient_event_loop *loop, yetty_yclient_task_cb cb,
                                   void *cb_user);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YCLIENT_LIB_EVENT_LOOP_H */
