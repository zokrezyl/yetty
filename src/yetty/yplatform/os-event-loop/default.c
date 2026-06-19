/* glfw-event-loop.c - OS event loop using GLFW */

#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/time.h>
#include <yetty/yevent/event.h>
#include <yetty/ytrace/ytrace.h>
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>

/* Per-window state attached to the GLFWwindow via its user pointer. The
 * callbacks below pull this back out instead of reading a global. The
 * struct is intentionally small — it only holds what the OS loop itself
 * needs to know (the pipe it pushes events into, plus the per-button
 * timestamp of the previous PRESS that's still eligible to pair into a
 * double-click). */
struct yetty_yplatform_os_event_state {
    struct yetty_ycore_xthread_event_pipe *input_pipe;

    /* Per GLFW mouse button. last_press_sec[i] is the monotonic time of
     * the most recent MOUSE_DOWN for button i, OR 0.0 to mean "no
     * eligible previous press" (the slot is cleared on three occasions:
     *   - on construction (calloc zeroes it),
     *   - immediately after firing a MOUSE_DOUBLE_CLICK, so a third
     *     quick press doesn't fire again, and
     *   - when the window-chrome tells us a render-side WINDOW_DRAG_BY
     *     or WINDOW_RESIZE_BY command just moved/resized the window —
     *     i.e. the press was a drag, not a click). */
    double last_press_sec[8];
};

#define YETTY_OS_DBL_CLICK_THRESHOLD_SEC 0.4

static struct yetty_yplatform_os_event_state *state_of(GLFWwindow *window)
{
    return (struct yetty_yplatform_os_event_state *)glfwGetWindowUserPointer(window);
}

static struct yetty_ycore_xthread_event_pipe *pipe_of(GLFWwindow *window)
{
    struct yetty_yplatform_os_event_state *st = state_of(window);
    return st ? st->input_pipe : NULL;
}

void yetty_yplatform_os_event_invalidate_click_pairing(GLFWwindow *window)
{
    struct yetty_yplatform_os_event_state *st = state_of(window);
    if (!st) {
        return;
    }
    memset(st->last_press_sec, 0, sizeof(st->last_press_sec));
}

/* Scale cursor coords (which GLFW reports in *screen coordinates*, i.e.
 * logical pixels) into framebuffer-pixel space. The rest of yetty —
 * tabbar hit-tests, pane bounds, render target dimensions — works in
 * framebuffer pixels because framebuffer_size_callback drives RESIZE.
 * On HiDPI displays (notably macOS Retina, where the scale is 2×) the
 * two coordinate systems differ; without this normalisation tabbar
 * button rects sit at framebuffer-x but the mouse arrives at
 * window-x, so clicks land "off" by the scale factor. */
static void scale_cursor_to_framebuffer(GLFWwindow *window, double *x, double *y)
{
    int ww = 0, wh = 0, fw = 0, fh = 0;
    glfwGetWindowSize(window, &ww, &wh);
    glfwGetFramebufferSize(window, &fw, &fh);
    if (ww > 0 && wh > 0 && fw > 0 && fh > 0) {
        *x *= (double)fw / (double)ww;
        *y *= (double)fh / (double)wh;
    }
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};

    if (!pipe) {
        return;
    }

    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) {
            if (key == GLFW_KEY_SPACE) {
                event.type = YETTY_YCORE_CHAR;
                event.chr.codepoint = ' ';
                event.chr.mods = mods;
                pipe->ops->write(pipe, &event, sizeof(event));
                return;
            }
            const char *key_name = glfwGetKeyName(key, scancode);
            if (key_name && key_name[0] && !key_name[1]) {
                event.type = YETTY_YCORE_CHAR;
                event.chr.codepoint = (uint32_t)(unsigned char)key_name[0];
                event.chr.mods = mods;
                pipe->ops->write(pipe, &event, sizeof(event));
                return;
            }
        }
        event.type = YETTY_YCORE_KEY_DOWN;
        event.key.key = key;
        event.key.mods = mods;
        event.key.scancode = scancode;
    } else if (action == GLFW_RELEASE) {
        event.type = YETTY_YCORE_KEY_UP;
        event.key.key = key;
        event.key.mods = mods;
        event.key.scancode = scancode;
    } else {
        return;
    }

    pipe->ops->write(pipe, &event, sizeof(event));
}

static void char_callback(GLFWwindow *window, unsigned int codepoint)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};

    if (!pipe) {
        return;
    }

    event.type = YETTY_YCORE_CHAR;
    event.chr.codepoint = codepoint;
    event.chr.mods = 0;
    pipe->ops->write(pipe, &event, sizeof(event));
}

static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    struct yetty_yplatform_os_event_state *st = state_of(window);
    if (!st) {
        return;
    }
    struct yetty_ycore_xthread_event_pipe *pipe = st->input_pipe;
    double x, y;

    glfwGetCursorPos(window, &x, &y);
    scale_cursor_to_framebuffer(window, &x, &y);

    struct yetty_yui_event event = {0};
    if (action == GLFW_PRESS) {
        event.type = YETTY_YCORE_MOUSE_DOWN;
    } else {
        event.type = YETTY_YCORE_MOUSE_UP;
    }
    event.mouse.x = (float)x;
    event.mouse.y = (float)y;
    event.mouse.button = button;
    event.mouse.mods = mods;
    pipe->ops->write(pipe, &event, sizeof(event));

    /* Synthesize MOUSE_DOUBLE_CLICK on a PRESS that pairs with the
     * previous PRESS for the same button within the time threshold.
     * Whether the previous press was a "clean click" (vs. a drag) is
     * authoritatively cleared by the window-chrome via
     * yetty_yplatform_os_event_invalidate_click_pairing — when the
     * render thread sends back a WINDOW_DRAG_BY / WINDOW_RESIZE_BY
     * command, the main-thread handler runs that AFTER moving the
     * window, which resets last_press_sec to 0 before we get here.
     *
     * No position check: on Wayland glfwGetWindowPos returns (0, 0) so
     * a screen-coord delta isn't computable, and window-relative deltas
     * are misleading during a drag (the window follows the cursor, so
     * the cursor's window-relative position rebounds to the anchor).
     * The "did a drag happen" signal from the window-chrome is enough. */
    if (action == GLFW_PRESS && button >= 0 &&
        (size_t)button < sizeof(st->last_press_sec) / sizeof(st->last_press_sec[0])) {
        double now_sec = yetty_yplatform_ytime_monotonic_sec();
        int is_double = st->last_press_sec[button] > 0.0 &&
                        (now_sec - st->last_press_sec[button]) < YETTY_OS_DBL_CLICK_THRESHOLD_SEC;
        if (is_double) {
            struct yetty_yui_event dbl = {0};
            dbl.type = YETTY_YCORE_MOUSE_DOUBLE_CLICK;
            dbl.mouse.x = (float)x;
            dbl.mouse.y = (float)y;
            dbl.mouse.button = button;
            dbl.mouse.mods = mods;
            pipe->ops->write(pipe, &dbl, sizeof(dbl));
            st->last_press_sec[button] = 0.0;
        } else {
            st->last_press_sec[button] = now_sec;
        }
    }
}

static void cursor_pos_callback(GLFWwindow *window, double x, double y)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};

    if (!pipe) {
        return;
    }

    scale_cursor_to_framebuffer(window, &x, &y);
    event.type = YETTY_YCORE_MOUSE_MOVE;
    event.mouse.x = (float)x;
    event.mouse.y = (float)y;

    pipe->ops->write(pipe, &event, sizeof(event));
}

static void scroll_callback(GLFWwindow *window, double xoffset, double yoffset)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};
    double x, y;
    int mods = 0;

    if (!pipe) {
        return;
    }

    glfwGetCursorPos(window, &x, &y);
    scale_cursor_to_framebuffer(window, &x, &y);

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        mods |= GLFW_MOD_SHIFT;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
        mods |= GLFW_MOD_CONTROL;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
        mods |= GLFW_MOD_ALT;
    }

    event.type = YETTY_YCORE_MOUSE_SCROLL;
    event.mouse_scroll.x = (float)x;
    event.mouse_scroll.y = (float)y;
    event.mouse_scroll.dx = (float)xoffset;
    event.mouse_scroll.dy = (float)yoffset;
    event.mouse_scroll.mods = mods;

    pipe->ops->write(pipe, &event, sizeof(event));
}

static void framebuffer_size_callback(GLFWwindow *window, int width, int height)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};

    if (!pipe) {
        return;
    }

    event.type = YETTY_YCORE_RESIZE;
    event.resize.width = (float)width;
    event.resize.height = (float)height;

    pipe->ops->write(pipe, &event, sizeof(event));
}

static void window_close_callback(GLFWwindow *window)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};

    if (!pipe) {
        return;
    }

    event.type = YETTY_YCORE_SHUTDOWN;
    pipe->ops->write(pipe, &event, sizeof(event));
}

static void window_refresh_callback(GLFWwindow *window)
{
    struct yetty_ycore_xthread_event_pipe *pipe = pipe_of(window);
    struct yetty_yui_event event = {0};

    yinfo("glfw: window_refresh_callback fired");

    if (!pipe) {
        return;
    }

    /* Expose/refresh: GPU texture is unchanged but the window contents were
     * clobbered. Post WINDOW_REFRESH so damage-aware targets (X11-tile) can
     * mark every tile dirty before the RENDER dispatch; targets whose
     * present() repaints the full surface (the WebGPU texture-surface path)
     * are unaffected since yetty.c falls through to a normal render. */
    event.type = YETTY_YCORE_WINDOW_REFRESH;
    pipe->ops->write(pipe, &event, sizeof(event));
}

void yetty_yplatform_setup_window_callbacks(GLFWwindow *window,
                                            struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    /* The state struct holds the pipe + per-button double-click
     * timestamps the callbacks need. Attaching it to the window via the
     * user pointer keeps every callback global-free — they read it back
     * with glfwGetWindowUserPointer. Caller (main) provides the pipe and
     * never sees the state struct. */
    struct yetty_yplatform_os_event_state *st = calloc(1, sizeof(*st));
    if (!st) {
        yerror("os_event_loop: state alloc failed");
        return;
    }
    st->input_pipe = input_pipe;
    glfwSetWindowUserPointer(window, st);

    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowCloseCallback(window, window_close_callback);
    glfwSetWindowRefreshCallback(window, window_refresh_callback);
}

/* The main-thread event loop itself (glfwWaitEvents + draining the render→main
 * buses) lives in the platform bootstrap (yplatform/yplatform/glfw.c), which
 * knows about the clipboard and window-chrome objects. This module owns only the
 * GLFW input callbacks and the per-window state they read. */

void yetty_yplatform_teardown_window_callbacks(GLFWwindow *window)
{
    struct yetty_yplatform_os_event_state *st = state_of(window);
    glfwSetWindowUserPointer(window, NULL);
    free(st);
}
