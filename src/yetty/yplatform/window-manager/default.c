/*
 * GLFW window manager — render→main marshaling for window control.
 *
 * GLFW requires window-mutating calls (iconify, maximize, set_pos,
 * set_should_close) on the main thread. The render thread parks
 * requests on the output_pipe via the producer ops; the drain on the
 * main thread calls handle_event() which runs the actual GLFW call
 * inline.
 *
 * Producer ops match the clipboard manager's shape so the two are easy
 * to read side by side: write a typed event, post an empty GLFW event
 * to break the main thread out of glfwWaitEvents.
 */

#include <yetty/yplatform/window-manager.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/move-resize.h>
#include <yetty/yevent/event.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <GLFW/glfw3.h>
#include <stdlib.h>

/* Implemented in src/yetty/yplatform/os-event-loop/default.c. The os
 * event loop owns the per-window state holding double-click timestamps;
 * we ask it to drop the pairing window when a render-side WINDOW_DRAG_BY
 * / WINDOW_RESIZE_BY / WINDOW_BEGIN_INTERACTIVE_MOVE has just moved the
 * window — that press was a drag, not a click, and shouldn't pair into
 * a double-click on the next press. */
void yetty_yplatform_os_event_invalidate_click_pairing(GLFWwindow *window);

struct yetty_yplatform_glfw_window_manager {
    struct yetty_yplatform_window_manager base;
    /* Borrowed — owned by the caller. */
    GLFWwindow *window;
    struct yetty_ycore_xthread_event_pipe *output_pipe;
    /* Used by handle_event(WINDOW_CLOSE) to post SHUTDOWN to the render
     * thread — same path the OS title-bar window_close_callback takes.
     * Without it the render thread keeps spinning yetty's event loop
     * after the main thread exits, and the join() at shutdown hangs. */
    struct yetty_ycore_xthread_event_pipe *input_pipe;
    /* Mouse-cursor cache. The cursor pointers are created lazily on
     * first request via glfwCreateStandardCursor and reused; reapplying
     * the same shape is a cheap no-op. `applied_shape` tracks the
     * value currently bound to the window so handle_event can skip
     * redundant glfwSetCursor calls. */
    GLFWcursor *cursors[5]; /* indexed by enum yetty_ycore_cursor_shape */
    int applied_shape;
};

static void glfw_window_manager_destroy(struct yetty_yplatform_window_manager *self)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    for (size_t i = 0; i < sizeof(m->cursors) / sizeof(m->cursors[0]); i++) {
        if (m->cursors[i]) {
            glfwDestroyCursor(m->cursors[i]);
        }
    }
    free(m);
}

/* Push one typed event to the main thread + wake it up. */
static void post_event(struct yetty_yplatform_glfw_window_manager *m,
                       const struct yetty_yui_event *ev)
{
    if (!m->output_pipe) {
        return;
    }
    struct yetty_ycore_size_result wr =
        m->output_pipe->ops->write(m->output_pipe, ev, sizeof(*ev));
    if (!YETTY_IS_OK(wr) || wr.value != sizeof(*ev)) {
        return;
    }
    glfwPostEmptyEvent();
}

/*=============================================================================
 * Producer ops (any thread)
 *===========================================================================*/

static void glfw_window_manager_iconify(struct yetty_yplatform_window_manager *self)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_ICONIFY};
    post_event(m, &ev);
}

static void glfw_window_manager_toggle_maximize(struct yetty_yplatform_window_manager *self)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE};
    post_event(m, &ev);
}

static void glfw_window_manager_request_close(struct yetty_yplatform_window_manager *self)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_CLOSE};
    post_event(m, &ev);
}

static void glfw_window_manager_drag_by(struct yetty_yplatform_window_manager *self, int dx, int dy)
{
    if (dx == 0 && dy == 0) {
        ydebug("DRAGTRACE: drag_by called with zero delta — skipping");
        return;
    }
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    ydebug("DRAGTRACE: [render-thread] drag_by(dx=%d, dy=%d) -> posting WINDOW_DRAG_BY", dx, dy);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_DRAG_BY,
                                 .window_drag = {.dx = dx, .dy = dy}};
    post_event(m, &ev);
}

static void glfw_window_manager_set_cursor(struct yetty_yplatform_window_manager *self, int shape)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_SET_CURSOR,
                                 .set_cursor = {.shape = shape}};
    post_event(m, &ev);
}

static void glfw_window_manager_resize_by(struct yetty_yplatform_window_manager *self, int dx,
                                          int dy)
{
    if (dx == 0 && dy == 0) {
        return;
    }
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_RESIZE_BY,
                                 .window_resize = {.dx = dx, .dy = dy}};
    post_event(m, &ev);
}

static void glfw_window_manager_begin_interactive_move(
    struct yetty_yplatform_window_manager *self)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    ydebug("WMOVETRACE: [render-thread] begin_interactive_move -> posting WINDOW_BEGIN_INTERACTIVE_MOVE");
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE};
    post_event(m, &ev);
}

static void glfw_window_manager_begin_interactive_resize(
    struct yetty_yplatform_window_manager *self, int edge)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    ydebug("WMOVETRACE: [render-thread] begin_interactive_resize(edge=%d) -> "
           "posting WINDOW_BEGIN_INTERACTIVE_RESIZE", edge);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE,
                                 .window_begin_resize = {.edge = edge}};
    post_event(m, &ev);
}

/*=============================================================================
 * Main-thread side — apply one event by calling GLFW
 *===========================================================================*/

static void glfw_window_manager_handle_event(struct yetty_yplatform_window_manager *self,
                                             const struct yetty_yui_event *event)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    if (!m->window || !event) {
        ydebug("window_manager: handle_event window=%p event=%p — skipping",
               (void *)(m ? m->window : NULL), (const void *)event);
        return;
    }
    ydebug("window_manager: handle_event type=%d", (int)event->type);
    switch (event->type) {
    case YETTY_YCORE_WINDOW_ICONIFY:
        glfwIconifyWindow(m->window);
        break;
    case YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE:
        if (glfwGetWindowAttrib(m->window, GLFW_MAXIMIZED)) {
            glfwRestoreWindow(m->window);
        } else {
            glfwMaximizeWindow(m->window);
        }
        break;
    case YETTY_YCORE_WINDOW_CLOSE: {
        /* Mirror the OS-bar close callback exactly: post SHUTDOWN to the
         * render thread's input pipe. The render thread stops its event
         * loop, sets *running=0, posts an empty event; the main loop
         * then exits naturally on the running check. Setting
         * glfwSetWindowShouldClose alone would leave the render thread
         * spinning and deadlock on join. */
        if (m->input_pipe) {
            struct yetty_yui_event ev = {.type = YETTY_YCORE_SHUTDOWN};
            m->input_pipe->ops->write(m->input_pipe, &ev, sizeof(ev));
        }
        break;
    }
    case YETTY_YCORE_WINDOW_DRAG_BY: {
        /* On Wayland the compositor took over the drag at MOUSE_DOWN
         * (see WINDOW_BEGIN_INTERACTIVE_MOVE below) — we don't get any
         * MOUSE_MOVE events during the drag and glfwSetWindowPos is a
         * no-op anyway. So skip the absolute-positioning path here. */
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            yetty_yplatform_os_event_invalidate_click_pairing(m->window);
            break;
        }
        /* Delta comes in framebuffer pixels (mouse coords are scaled to
         * framebuffer at the input layer so hit-tests against bar->width
         * line up). glfwSetWindowPos / glfwSetWindowSize take *screen*
         * coords. On HiDPI displays (macOS Retina = 2×) framebuffer is
         * larger than the window in screen coords, so we have to divide
         * back down here or the window moves/resizes by 2× too much. */
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(m->window, &ww, &wh);
        glfwGetFramebufferSize(m->window, &fw, &fh);
        double sx = (fw > 0 && ww > 0) ? (double)ww / (double)fw : 1.0;
        double sy = (fh > 0 && wh > 0) ? (double)wh / (double)fh : 1.0;

        int x, y;
        glfwGetWindowPos(m->window, &x, &y);
        glfwSetWindowPos(m->window, x + (int)(event->window_drag.dx * sx),
                         y + (int)(event->window_drag.dy * sy));
        /* Drag executed → the preceding MOUSE_DOWN dragged the window,
         * so it wasn't a clean click. Tell the OS event loop to drop the
         * double-click pairing window: a subsequent rapid press starts a
         * fresh drag instead of mis-firing as a double-click. */
        yetty_yplatform_os_event_invalidate_click_pairing(m->window);
        break;
    }
    case YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE:
        ydebug("WMOVETRACE: [main-thread] received WINDOW_BEGIN_INTERACTIVE_MOVE, "
               "platform=%d (WAYLAND=%d, X11=%d)",
               glfwGetPlatform(), GLFW_PLATFORM_WAYLAND, GLFW_PLATFORM_X11);
        /* Wayland-only meaningful work. On X11 this is a no-op and the
         * per-pixel WINDOW_DRAG_BY path handles the move via
         * glfwSetWindowPos as before. The helper itself runtime-checks
         * glfwGetPlatform() and returns early on non-Wayland. */
        yetty_yplatform_wayland_begin_interactive_move(m->window);
        /* After this point the compositor owns the drag — record that
         * the press wasn't a clean click so the next press doesn't
         * mis-pair as a double-click. */
        yetty_yplatform_os_event_invalidate_click_pairing(m->window);
        break;
    case YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE:
        ydebug("WMOVETRACE: [main-thread] received WINDOW_BEGIN_INTERACTIVE_RESIZE "
               "edge=%d, platform=%d", event->window_begin_resize.edge, glfwGetPlatform());
        /* Same Wayland-vs-X11 split as the move helper. The wayland.c
         * implementation runtime-checks the platform and no-ops on X11;
         * X11 keeps using per-pixel WINDOW_RESIZE_BY. */
        yetty_yplatform_wayland_begin_interactive_resize(
            m->window, (unsigned int)event->window_begin_resize.edge);
        yetty_yplatform_os_event_invalidate_click_pairing(m->window);
        break;
    case YETTY_YCORE_SET_CURSOR: {
        int shape = event->set_cursor.shape;
        if (shape < 0 ||
            (size_t)shape >= sizeof(m->cursors) / sizeof(m->cursors[0])) {
            break;
        }
        if (shape == m->applied_shape) {
            break; /* idempotent — already bound. */
        }
        /* Map enum yetty_ycore_cursor_shape onto GLFW standard cursors.
         * Lazily create on first use; the cached pointer is reused for
         * subsequent applies. NULL = restore default arrow. */
        if (!m->cursors[shape] && shape != YETTY_YCORE_CURSOR_DEFAULT) {
            int glfw_shape = GLFW_ARROW_CURSOR;
            switch (shape) {
            case YETTY_YCORE_CURSOR_HRESIZE: glfw_shape = GLFW_HRESIZE_CURSOR; break;
            case YETTY_YCORE_CURSOR_VRESIZE: glfw_shape = GLFW_VRESIZE_CURSOR; break;
            case YETTY_YCORE_CURSOR_IBEAM:   glfw_shape = GLFW_IBEAM_CURSOR;   break;
            case YETTY_YCORE_CURSOR_HAND:    glfw_shape = GLFW_HAND_CURSOR;    break;
            default: break;
            }
            m->cursors[shape] = glfwCreateStandardCursor(glfw_shape);
        }
        glfwSetCursor(m->window, m->cursors[shape]); /* NULL → default. */
        m->applied_shape = shape;
        break;
    }
    case YETTY_YCORE_WINDOW_RESIZE_BY: {
        /* On Wayland the compositor took over the resize at MOUSE_DOWN
         * (see WINDOW_BEGIN_INTERACTIVE_RESIZE) — and glfwSetWindowSize is
         * a no-op there anyway. Skip the per-pixel path. */
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            yetty_yplatform_os_event_invalidate_click_pairing(m->window);
            break;
        }
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(m->window, &ww, &wh);
        glfwGetFramebufferSize(m->window, &fw, &fh);
        double sx = (fw > 0 && ww > 0) ? (double)ww / (double)fw : 1.0;
        double sy = (fh > 0 && wh > 0) ? (double)wh / (double)fh : 1.0;

        int nw = ww + (int)(event->window_resize.dx * sx);
        int nh = wh + (int)(event->window_resize.dy * sy);
        /* Minimum size — anything smaller than a couple of cells makes
         * the terminal grid degenerate and risks divide-by-zero or
         * negative-bound math downstream. Screen-coord minimums. */
        if (nw < 200) nw = 200;
        if (nh < 100) nh = 100;
        glfwSetWindowSize(m->window, nw, nh);
        yetty_yplatform_os_event_invalidate_click_pairing(m->window);
        break;
    }
    default:
        /* Not ours — silently ignore. The drain dispatches by event
         * type, so this branch only runs if a caller passes a foreign
         * event by mistake. */
        break;
    }
}

static const struct yetty_yplatform_window_manager_ops glfw_window_manager_ops = {
    .destroy = glfw_window_manager_destroy,
    .iconify = glfw_window_manager_iconify,
    .toggle_maximize = glfw_window_manager_toggle_maximize,
    .request_close = glfw_window_manager_request_close,
    .drag_by = glfw_window_manager_drag_by,
    .resize_by = glfw_window_manager_resize_by,
    .begin_interactive_move = glfw_window_manager_begin_interactive_move,
    .begin_interactive_resize = glfw_window_manager_begin_interactive_resize,
    .set_cursor = glfw_window_manager_set_cursor,
    .handle_event = glfw_window_manager_handle_event,
};

struct yetty_yplatform_window_manager_ptr_result yetty_yplatform_window_manager_create(
    void *os_window, struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    if (!os_window || !output_pipe || !input_pipe) {
        return YETTY_ERR(yetty_yplatform_window_manager_ptr,
                         "window_manager_create: os_window, output_pipe, input_pipe required");
    }
    struct yetty_yplatform_glfw_window_manager *m = calloc(1, sizeof(*m));
    if (!m) {
        return YETTY_ERR(yetty_yplatform_window_manager_ptr,
                         "window_manager_create: alloc failed");
    }
    m->base.ops = &glfw_window_manager_ops;
    m->window = os_window;
    m->output_pipe = output_pipe;
    m->input_pipe = input_pipe;
    return YETTY_OK(yetty_yplatform_window_manager_ptr, &m->base);
}
