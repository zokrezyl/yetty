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
#include <yetty/yevent/event.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <GLFW/glfw3.h>
#include <stdlib.h>

struct yetty_yplatform_glfw_window_manager {
    struct yetty_yplatform_window_manager base;
    /* Borrowed — owned by the caller. */
    GLFWwindow *window;
    struct yetty_ycore_xthread_event_pipe *output_pipe;
};

static void glfw_window_manager_destroy(struct yetty_yplatform_window_manager *self)
{
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
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
        return;
    }
    struct yetty_yplatform_glfw_window_manager *m =
        container_of(self, struct yetty_yplatform_glfw_window_manager, base);
    struct yetty_yui_event ev = {.type = YETTY_YCORE_WINDOW_DRAG_BY,
                                 .window_drag = {.dx = dx, .dy = dy}};
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
        return;
    }
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
    case YETTY_YCORE_WINDOW_CLOSE:
        /* Mirror the OS-bar close button: ask GLFW to wind down. The
         * main loop's glfwWindowShouldClose check ends the run, after
         * which the existing shutdown path takes over. */
        glfwSetWindowShouldClose(m->window, GLFW_TRUE);
        break;
    case YETTY_YCORE_WINDOW_DRAG_BY: {
        int x, y;
        glfwGetWindowPos(m->window, &x, &y);
        glfwSetWindowPos(m->window, x + event->window_drag.dx, y + event->window_drag.dy);
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
    .handle_event = glfw_window_manager_handle_event,
};

struct yetty_yplatform_window_manager_ptr_result yetty_yplatform_window_manager_create(
    void *os_window, struct yetty_ycore_xthread_event_pipe *output_pipe)
{
    if (!os_window || !output_pipe) {
        return YETTY_ERR(yetty_yplatform_window_manager_ptr,
                         "window_manager_create: os_window and output_pipe required");
    }
    struct yetty_yplatform_glfw_window_manager *m = calloc(1, sizeof(*m));
    if (!m) {
        return YETTY_ERR(yetty_yplatform_window_manager_ptr,
                         "window_manager_create: alloc failed");
    }
    m->base.ops = &glfw_window_manager_ops;
    m->window = os_window;
    m->output_pipe = output_pipe;
    return YETTY_OK(yetty_yplatform_window_manager_ptr, &m->base);
}
