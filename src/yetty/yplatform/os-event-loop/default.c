/* glfw-event-loop.c - OS event loop using GLFW */

#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/clipboard-manager.h>
#include <yetty/yevent/event.h>
#include <yetty/ytrace/ytrace.h>
#include <GLFW/glfw3.h>

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
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
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
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
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
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
    struct yetty_yui_event event = {0};
    double x, y;

    if (!pipe) {
        return;
    }

    glfwGetCursorPos(window, &x, &y);
    scale_cursor_to_framebuffer(window, &x, &y);

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
}

static void cursor_pos_callback(GLFWwindow *window, double x, double y)
{
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
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
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
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
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
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
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
    struct yetty_yui_event event = {0};

    if (!pipe) {
        return;
    }

    event.type = YETTY_YCORE_SHUTDOWN;
    pipe->ops->write(pipe, &event, sizeof(event));
}

static void window_refresh_callback(GLFWwindow *window)
{
    struct yetty_ycore_xthread_event_pipe *pipe = glfwGetWindowUserPointer(window);
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

void yetty_yplatform_setup_window_callbacks(GLFWwindow *window)
{
    glfwSetKeyCallback(window, key_callback);
    glfwSetCharCallback(window, char_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetWindowCloseCallback(window, window_close_callback);
    glfwSetWindowRefreshCallback(window, window_refresh_callback);
}

void yetty_yplatform_run_os_event_loop(GLFWwindow *window, int *running,
                                       struct yetty_platform_clipboard_manager *cm)
{
    while (*running && !glfwWindowShouldClose(window)) {
        glfwWaitEvents();
        /* After each event burst, drain anything the render thread has
         * pushed onto the clipboard pipe. GLFW clipboard calls have to
         * happen on this thread; the render thread parks requests on
         * the pipe and wakes us via glfwPostEmptyEvent. */
        if (cm && cm->ops->drain) {
            cm->ops->drain(cm);
        }
    }
}
