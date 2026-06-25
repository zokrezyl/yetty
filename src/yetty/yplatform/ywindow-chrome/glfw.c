/*
 * ywindow-chrome/glfw.c — GLFW desktop window-chrome subclass
 * (Linux / macOS / Windows).
 *
 * The platform half of yplatform:window_chrome: it overrides handle_event to
 * perform the real GLFW window-mutating calls on the main thread, and owns the
 * native window handle, the mouse-cursor cache, and the macOS manual-maximize
 * bookkeeping. The producer slots (iconify / drag_by / set_cursor / …) live in
 * the platform-independent base (window-chrome.c) and just post events; this
 * subclass applies them.
 *
 * Bind order: create() → window_chrome_configure(output_pipe) [base] →
 * yetty_yplatform_glfw_window_chrome_attach(os_window, input_pipe) [here].
 */

#include <GLFW/glfw3.h>
#include <stdlib.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/move-resize.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

/* os-event-loop/default.c: drop the double-click pairing window after a
 * render-side drag/resize moved the window (that press was a drag, not a click). */
void yetty_yplatform_os_event_invalidate_click_pairing(GLFWwindow *window);

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_glfw_window_chrome_ptr,
                      struct yetty_yplatform_glfw_window_chrome *);
struct yetty_yclass_ptr_result yetty_yplatform_glfw_window_chrome_class_get(void);
struct yetty_yplatform_glfw_window_chrome_ptr_result yetty_yplatform_glfw_window_chrome_from(
    struct yetty_yclass_object *obj);

/* Private subclass state. */
struct YETTY_ANNOTATE("class@yplatform:glfw_window_chrome") YETTY_ANNOTATE("platform@glfw")
    YETTY_ANNOTATE("parent@yplatform:window_chrome") yetty_yplatform_glfw_window_chrome {
    GLFWwindow *window; /* borrowed — owned by the caller. */
    /* Used by handle_event(WINDOW_CLOSE) to post SHUTDOWN to the render thread. */
    struct yetty_ycore_xthread_event_pipe *input_pipe;
    /* Mouse-cursor cache, indexed by enum yetty_ycore_cursor_shape; created lazily
     * and reused. applied_shape tracks what is currently bound to skip redundant
     * glfwSetCursor calls. */
    GLFWcursor *cursors[5];
    int applied_shape;
    /* Manual maximize bookkeeping for macOS borderless windows. */
    int macos_maximized;
    int macos_saved_x;
    int macos_saved_y;
    int macos_saved_w;
    int macos_saved_h;
};

static struct yetty_yplatform_glfw_window_chrome *glfw_window_chrome_data(
    struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_window_chrome_ptr_result data =
        yetty_yplatform_glfw_window_chrome_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value;
}

/* Bind the native window + main→render response pipe. Call once after create()
 * and the base window_chrome_configure(). Both borrowed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_yplatform_glfw_window_chrome_attach(
    struct yetty_yclass_object *obj, void *os_window,
    struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    if (!os_window || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void,
                         "glfw_window_chrome_attach: os_window and input_pipe required");
    }
    struct yetty_yplatform_glfw_window_chrome_ptr_result data =
        yetty_yplatform_glfw_window_chrome_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "glfw_window_chrome_attach: data_get");
    data.value->window = os_window;
    data.value->input_pipe = input_pipe;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@yplatform:glfw_window_chrome:window_chrome_destroy")
static struct yetty_ycore_void_result glfw_window_chrome_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_window_chrome *chrome = glfw_window_chrome_data(obj);
    if (chrome) {
        for (size_t i = 0; i < sizeof(chrome->cursors) / sizeof(chrome->cursors[0]); i++) {
            if (chrome->cursors[i]) {
                glfwDestroyCursor(chrome->cursors[i]);
            }
        }
    }
    return yetty_yclass_object_free(obj);
}

YETTY_ANNOTATE("override@yplatform:glfw_window_chrome:window_chrome_handle_event")
static struct yetty_ycore_void_result glfw_window_chrome_handle_event(
    struct yetty_yclass_object *obj, const struct yetty_yui_event *event)
{
    struct yetty_yplatform_glfw_window_chrome *chrome = glfw_window_chrome_data(obj);
    if (!chrome || !chrome->window || !event) {
        return YETTY_OK_VOID();
    }
    switch (event->type) {
    case YETTY_YCORE_WINDOW_ICONIFY:
        glfwIconifyWindow(chrome->window);
        break;
    case YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE:
#if defined(__APPLE__)
        /* Borderless NSWindows ignore glfwMaximizeWindow; resize to the monitor
         * workarea by hand and remember the pre-maximize geometry to restore. */
        if (chrome->macos_maximized) {
            glfwSetWindowPos(chrome->window, chrome->macos_saved_x, chrome->macos_saved_y);
            glfwSetWindowSize(chrome->window, chrome->macos_saved_w, chrome->macos_saved_h);
            chrome->macos_maximized = 0;
            break;
        }
        glfwGetWindowPos(chrome->window, &chrome->macos_saved_x, &chrome->macos_saved_y);
        glfwGetWindowSize(chrome->window, &chrome->macos_saved_w, &chrome->macos_saved_h);
        int cx = chrome->macos_saved_x + chrome->macos_saved_w / 2;
        int cy = chrome->macos_saved_y + chrome->macos_saved_h / 2;
        GLFWmonitor *target = NULL;
        int monitor_count = 0;
        GLFWmonitor **monitors = glfwGetMonitors(&monitor_count);
        for (int i = 0; i < monitor_count; i++) {
            int wax, way, waw, wah;
            glfwGetMonitorWorkarea(monitors[i], &wax, &way, &waw, &wah);
            if (cx >= wax && cx < wax + waw && cy >= way && cy < way + wah) {
                target = monitors[i];
                break;
            }
        }
        if (!target) {
            target = glfwGetPrimaryMonitor();
        }
        if (target) {
            int wax, way, waw, wah;
            glfwGetMonitorWorkarea(target, &wax, &way, &waw, &wah);
            glfwSetWindowPos(chrome->window, wax, way);
            glfwSetWindowSize(chrome->window, waw, wah);
            chrome->macos_maximized = 1;
        }
#else
        if (glfwGetWindowAttrib(chrome->window, GLFW_MAXIMIZED)) {
            glfwRestoreWindow(chrome->window);
        } else {
            glfwMaximizeWindow(chrome->window);
        }
#endif
        break;
    case YETTY_YCORE_WINDOW_CLOSE:
        /* Mirror the OS-bar close callback: post SHUTDOWN to the render thread so
         * its loop stops and the main loop exits on the running check. */
        if (chrome->input_pipe) {
            struct yetty_yui_event shutdown = {.type = YETTY_YCORE_SHUTDOWN};
            chrome->input_pipe->ops->write(chrome->input_pipe, &shutdown, sizeof(shutdown));
        }
        break;
    case YETTY_YCORE_WINDOW_DRAG_BY: {
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            yetty_yplatform_os_event_invalidate_click_pairing(chrome->window);
            break;
        }
        /* Deltas arrive in framebuffer pixels; glfwSetWindowPos takes screen
         * coords, so scale back down on HiDPI. */
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(chrome->window, &ww, &wh);
        glfwGetFramebufferSize(chrome->window, &fw, &fh);
        double sx = (fw > 0 && ww > 0) ? (double)ww / (double)fw : 1.0;
        double sy = (fh > 0 && wh > 0) ? (double)wh / (double)fh : 1.0;
        int x, y;
        glfwGetWindowPos(chrome->window, &x, &y);
        glfwSetWindowPos(chrome->window, x + (int)(event->window_drag.dx * sx),
                         y + (int)(event->window_drag.dy * sy));
        yetty_yplatform_os_event_invalidate_click_pairing(chrome->window);
        break;
    }
    case YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE:
        yetty_yplatform_wayland_begin_interactive_move(chrome->window);
        yetty_yplatform_os_event_invalidate_click_pairing(chrome->window);
        break;
    case YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE:
        yetty_yplatform_wayland_begin_interactive_resize(
            chrome->window, (unsigned int)event->window_begin_resize.edge);
        yetty_yplatform_os_event_invalidate_click_pairing(chrome->window);
        break;
    case YETTY_YCORE_SET_CURSOR: {
        int shape = event->set_cursor.shape;
        if (shape < 0 || (size_t)shape >= sizeof(chrome->cursors) / sizeof(chrome->cursors[0])) {
            break;
        }
        if (shape == chrome->applied_shape) {
            break;
        }
        if (!chrome->cursors[shape] && shape != YETTY_YCORE_CURSOR_DEFAULT) {
            int glfw_shape = GLFW_ARROW_CURSOR;
            switch (shape) {
            case YETTY_YCORE_CURSOR_HRESIZE:
                glfw_shape = GLFW_HRESIZE_CURSOR;
                break;
            case YETTY_YCORE_CURSOR_VRESIZE:
                glfw_shape = GLFW_VRESIZE_CURSOR;
                break;
            case YETTY_YCORE_CURSOR_IBEAM:
                glfw_shape = GLFW_IBEAM_CURSOR;
                break;
            case YETTY_YCORE_CURSOR_HAND:
                glfw_shape = GLFW_HAND_CURSOR;
                break;
            default:
                break;
            }
            chrome->cursors[shape] = glfwCreateStandardCursor(glfw_shape);
        }
        glfwSetCursor(chrome->window, chrome->cursors[shape]); /* NULL → default. */
        chrome->applied_shape = shape;
        break;
    }
    case YETTY_YCORE_WINDOW_RESIZE_BY: {
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            yetty_yplatform_os_event_invalidate_click_pairing(chrome->window);
            break;
        }
        int ww = 0, wh = 0, fw = 0, fh = 0, wx = 0, wy = 0;
        glfwGetWindowSize(chrome->window, &ww, &wh);
        glfwGetFramebufferSize(chrome->window, &fw, &fh);
        glfwGetWindowPos(chrome->window, &wx, &wy);
        double sx = (fw > 0 && ww > 0) ? (double)ww / (double)fw : 1.0;
        double sy = (fh > 0 && wh > 0) ? (double)wh / (double)fh : 1.0;
        int dx = (int)(event->window_resize.dx * sx);
        int dy = (int)(event->window_resize.dy * sy);
        int edge = event->window_resize.edge;
        int nx = wx, ny = wy, nw = ww, nh = wh;
        if (edge == 0 || (edge & YETTY_YCORE_RESIZE_EDGE_RIGHT)) {
            nw = ww + dx;
        }
        if (edge & YETTY_YCORE_RESIZE_EDGE_LEFT) {
            nx = wx + dx;
            nw = ww - dx;
        }
        if (edge == 0 || (edge & YETTY_YCORE_RESIZE_EDGE_BOTTOM)) {
            nh = wh + dy;
        }
        if (edge & YETTY_YCORE_RESIZE_EDGE_TOP) {
            ny = wy + dy;
            nh = wh - dy;
        }
        if (nw < 200) {
            if (edge & YETTY_YCORE_RESIZE_EDGE_LEFT) {
                nx -= (200 - nw);
            }
            nw = 200;
        }
        if (nh < 100) {
            if (edge & YETTY_YCORE_RESIZE_EDGE_TOP) {
                ny -= (100 - nh);
            }
            nh = 100;
        }
        if (nx != wx || ny != wy) {
            glfwSetWindowPos(chrome->window, nx, ny);
        }
        glfwSetWindowSize(chrome->window, nw, nh);
        yetty_yplatform_os_event_invalidate_click_pairing(chrome->window);
        break;
    }
    default:
        break;
    }
    return YETTY_OK_VOID();
}

#include "glfw.gen.c"
