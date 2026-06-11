/*
 * window-manager.c — yclass class `yplatform:window_manager`.
 *
 * The render thread's typed API for telling the OS window to minimize /
 * toggle-maximize / close / move / resize, and for setting the mouse-cursor
 * shape. Producer slots write a typed event onto the shared output_pipe and
 * post an empty GLFW event so the main thread wakes; window_manager_handle_event
 * runs the actual GLFW call on the main thread (GLFW requires window-mutating
 * calls there).
 *
 * The output_pipe drain (currently inside the clipboard manager) calls
 * window_manager_handle_event for any YETTY_YCORE_WINDOW_* it pulls off the
 * pipe — pipes are FIFO with a single reader, so one drain serves both managers
 * by dispatching on event type.
 *
 * Thread-safety: the producer slots are safe from any thread (they only write
 * to the pipe). window_manager_handle_event must run on the main thread (it
 * touches GLFW directly).
 *
 * This is a yclass class so codegen emits the public header (window-manager.h),
 * the dispatch stubs, model.yaml, and the FFI / host-language binding surface.
 * The only hand-written file is this annotated .c; window-manager.gen.c is
 * #included at the foot. Every slot is `local@` — the window manager is an
 * in-process object bound to one GLFW window, never proxied over RPC.
 *
 * This TU deliberately does NOT include its own generated public header
 * `yetty/yplatform/window-manager.h` — that header is a downstream artifact
 * for other modules. The foundational types it would pull in (yclass
 * identity, Result, ycore types) are included directly below, and this TU
 * declares its own `yetty_yplatform_window_manager_ptr_result` (after the
 * class struct) plus forward decls for the generated class accessor and the
 * obj->body downcast the appended window-manager.gen.c defines.
 *
 * Lifecycle:
 *   yetty_yplatform_register();
 *   obj = yetty_yplatform_window_manager_create(NULL);
 *   yetty_yplatform_window_manager_configure(NULL, obj, glfw_window,
 *                                            output_pipe, input_pipe);
 *   ... yetty_yplatform_window_manager_drag_by(NULL, obj, dx, dy); ...
 *   yetty_yplatform_window_manager_destroy(NULL, obj);  // frees cursors + object
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yevent/event.h>
#include <yetty/yplatform/move-resize.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

#include <GLFW/glfw3.h>
#include <stdlib.h>

/* Implemented in src/yetty/yplatform/os-event-loop/default.c. The os event
 * loop owns the per-window state holding double-click timestamps; we ask it to
 * drop the pairing window when a render-side WINDOW_DRAG_BY / WINDOW_RESIZE_BY /
 * WINDOW_BEGIN_INTERACTIVE_MOVE has just moved the window — that press was a
 * drag, not a click, and shouldn't pair into a double-click on the next press. */
void yetty_yplatform_os_event_invalidate_click_pairing(GLFWwindow *window);

struct [[clang::annotate("class@yplatform:window_manager")]] yetty_yplatform_window_manager {
    /* All three borrowed — owned by the caller, set via configure(). */
    GLFWwindow *window;
    struct yetty_ycore_xthread_event_pipe *output_pipe;
    /* Used by handle_event(WINDOW_CLOSE) to post SHUTDOWN to the render thread —
     * same path the OS title-bar window_close_callback takes. Without it the
     * render thread keeps spinning yetty's event loop after the main thread
     * exits, and the join() at shutdown hangs. */
    struct yetty_ycore_xthread_event_pipe *input_pipe;

    /* Mouse-cursor cache. The cursor pointers are created lazily on first
     * request via glfwCreateStandardCursor and reused; reapplying the same
     * shape is a cheap no-op. applied_shape tracks the value currently bound to
     * the window so handle_event can skip redundant glfwSetCursor calls. */
    GLFWcursor *cursors[5]; /* indexed by enum yetty_ycore_cursor_shape */
    int applied_shape;

    /* Manual maximize bookkeeping for macOS. Cocoa's `-[NSWindow zoom:]` (which
     * glfwMaximizeWindow lowers to) is a silent no-op for borderless windows —
     * and yetty creates a borderless window so it can draw its own titlebar (see
     * yplatform/window/default.c with GLFW_DECORATED = GLFW_FALSE). The maximize
     * button + strip double-click therefore both did nothing on macOS while
     * working on X11/Wayland/Win32. We work around by saving the window's
     * geometry and resizing to the monitor's workarea ourselves; restore puts it
     * back. Active only on APPLE; other platforms keep using
     * glfwMaximizeWindow/Restore which the WM handles correctly even on
     * undecorated surfaces. */
    int macos_maximized; /* 1 while we've stretched to the workarea */
    int macos_saved_x;
    int macos_saved_y;
    int macos_saved_w;
    int macos_saved_h;
};

/* Result wrapper for the window-manager handle. Declared here (not pulled
 * from window-manager.h, which this TU does not include) so the appended
 * window-manager.gen.c — which defines yetty_yplatform_window_manager_from()
 * returning it — has the type in scope. The public window-manager.h
 * publishes the identical declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_yplatform_window_manager_ptr, struct yetty_yplatform_window_manager *);

/* Defined in the appended window-manager.gen.c (foot of this TU). Forward-
 * declared here because this TU does not include its own generated header —
 * the class accessor and the obj->body downcast are used by the helpers and
 * the object-keyed slots below. */
struct yetty_yclass_ptr_result yetty_yplatform_window_manager_class_get(void);
struct yetty_yplatform_window_manager_ptr_result yetty_yplatform_window_manager_from(
    struct yetty_yclass_object *obj);

/* Resolve the object's yetty_yplatform_window_manager slice, preserving the
 * class_get / object_data error chain. */
static struct yetty_yclass_void_ptr_result window_manager_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_manager_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "window_manager_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "window_manager_from_obj: object_data");
    return slice_r;
}

/* Push one typed event to the main thread + wake it up. Best-effort: a full or
 * broken pipe just drops the request (matches the pre-yclass behaviour). */
static void post_event(struct yetty_yplatform_window_manager *manager,
                       const struct yetty_yui_event *event)
{
    if (!manager->output_pipe) {
        return;
    }
    struct yetty_ycore_size_result write_result =
        manager->output_pipe->ops->write(manager->output_pipe, event, sizeof(*event));
    if (!YETTY_IS_OK(write_result) || write_result.value != sizeof(*event)) {
        return;
    }
    glfwPostEmptyEvent();
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

/* Bind the manager to its GLFW window and the render↔main event pipes. Call
 * once after create(), before any other slot. All three are borrowed. */
/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_configure")]]
[[clang::annotate("local@yplatform:window_manager_configure")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_configure(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, void *os_window,
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe)
{
    (void)ctx;
    if (!os_window || !output_pipe || !input_pipe) {
        return YETTY_ERR(yetty_ycore_void,
                         "window_manager configure: os_window, output_pipe, input_pipe required");
    }
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager configure: object");
    struct yetty_yplatform_window_manager *manager = data_r.value;
    manager->window = os_window;
    manager->output_pipe = output_pipe;
    manager->input_pipe = input_pipe;
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_destroy")]]
[[clang::annotate("local@yplatform:window_manager_destroy")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_destroy(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj)
{
    (void)ctx;
    /* Best-effort teardown: even if the slice can't be resolved we still free
     * the object. Stash (not swallow) any resolve error as the first error. */
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    struct yetty_yplatform_window_manager *manager =
        YETTY_IS_OK(data_r) ? (struct yetty_yplatform_window_manager *)data_r.value : NULL;
    struct yetty_ycore_void_result result =
        YETTY_IS_ERR(data_r) ? YETTY_ERR(yetty_ycore_void, "window_manager destroy: object", data_r)
                             : YETTY_OK_VOID();
    if (manager) {
        for (size_t i = 0; i < sizeof(manager->cursors) / sizeof(manager->cursors[0]); i++) {
            if (manager->cursors[i]) {
                glfwDestroyCursor(manager->cursors[i]);
            }
        }
    }
    struct yetty_ycore_void_result free_result = yetty_yclass_object_free(obj);
    if (YETTY_IS_OK(result) && YETTY_IS_ERR(free_result)) {
        return YETTY_ERR(yetty_ycore_void, "window_manager destroy: object_free", free_result);
    }
    if (YETTY_IS_ERR(free_result)) {
        yetty_ycore_error_destroy(free_result.error);
    }
    return result;
}

/*=============================================================================
 * Producer slots (any thread)
 *===========================================================================*/

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_iconify")]]
[[clang::annotate("local@yplatform:window_manager_iconify")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_iconify(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager iconify: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_ICONIFY};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_toggle_maximize")]]
[[clang::annotate("local@yplatform:window_manager_toggle_maximize")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_toggle_maximize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager toggle_maximize: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_request_close")]]
[[clang::annotate("local@yplatform:window_manager_request_close")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_request_close(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager request_close: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_CLOSE};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_drag_by")]]
[[clang::annotate("local@yplatform:window_manager_drag_by")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_drag_by(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             int dx, int dy)
{
    (void)ctx;
    if (dx == 0 && dy == 0) {
        ydebug("DRAGTRACE: drag_by called with zero delta — skipping");
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager drag_by: object");
    ydebug("DRAGTRACE: [render-thread] drag_by(dx=%d, dy=%d) -> posting WINDOW_DRAG_BY", dx, dy);
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_DRAG_BY,
                                    .window_drag = {.dx = dx, .dy = dy}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* Grow (or shrink, with negative deltas) the window's outer size by (dx, dy)
 * screen pixels. Top-left stays fixed — only the right/bottom edges move. The
 * tabbar's edge/corner resize handles call this. */
/* clang-format off: keep each annotation on one line — splitting the long
 * override string breaks the codegen parser. */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_resize_by")]] [[clang::annotate(
    "local@yplatform:window_manager_resize_by")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_resize_by(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               int dx, int dy, int edge)
{
    (void)ctx;
    if (dx == 0 && dy == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager resize_by: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_RESIZE_BY,
                                    .window_resize = {.dx = dx, .dy = dy, .edge = edge}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* Hand the move gesture to the compositor (Wayland) or fall back to per-pixel
 * drag_by (X11). Called once on the MOUSE_DOWN that starts a titlebar drag. */
/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_begin_interactive_move")]]
[[clang::annotate("local@yplatform:window_manager_begin_interactive_move")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_begin_interactive_move(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager begin_interactive_move: object");
    ydebug("WMOVETRACE: [render-thread] begin_interactive_move -> posting "
           "WINDOW_BEGIN_INTERACTIVE_MOVE");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* Same shape as begin_interactive_move, but for resize. `edge` is a
 * yetty_ycore_resize_edge bitmask telling the compositor which edge the user
 * grabbed; on X11 this is a no-op and per-pixel resize_by keeps driving
 * glfwSetWindowSize. */
/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_begin_interactive_resize")]]
[[clang::annotate("local@yplatform:window_manager_begin_interactive_resize")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_begin_interactive_resize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int edge)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r,
                        "window_manager begin_interactive_resize: object");
    ydebug("WMOVETRACE: [render-thread] begin_interactive_resize(edge=%d) -> "
           "posting WINDOW_BEGIN_INTERACTIVE_RESIZE",
           edge);
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE,
                                    .window_begin_resize = {.edge = edge}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* Set the OS mouse cursor shape. Shape is an enum yetty_ycore_cursor_shape
 * value. Posted to output_pipe and applied on the main thread (GLFW cursor
 * calls aren't safe off the main thread). Idempotent on the OS side. */
/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_set_cursor")]]
[[clang::annotate("local@yplatform:window_manager_set_cursor")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_set_cursor(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                int shape)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager set_cursor: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_SET_CURSOR, .set_cursor = {.shape = shape}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Main-thread side — apply one drained event by calling GLFW
 *===========================================================================*/

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_manager:window_manager_handle_event")]]
[[clang::annotate("local@yplatform:window_manager_handle_event")]]
/* clang-format on */
static struct yetty_ycore_void_result window_manager_handle_event(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_yui_event *event)
{
    (void)ctx;
    struct yetty_yclass_void_ptr_result data_r = window_manager_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_manager handle_event: object");
    struct yetty_yplatform_window_manager *manager = data_r.value;
    if (!manager->window || !event) {
        ydebug("window_manager: handle_event window=%p event=%p — skipping",
               (void *)manager->window, (const void *)event);
        return YETTY_OK_VOID();
    }
    ydebug("window_manager: handle_event type=%d", (int)event->type);
    switch (event->type) {
    case YETTY_YCORE_WINDOW_ICONIFY:
        glfwIconifyWindow(manager->window);
        break;
    case YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE:
#if defined(__APPLE__)
        /* Borderless NSWindows silently ignore `-[NSWindow zoom:]` (which is
         * what glfwMaximizeWindow lowers to on macOS), so we resize to the
         * monitor's workarea by hand and remember the pre-maximize geometry to
         * restore on toggle-off. The GLFW_MAXIMIZED attribute stays false
         * throughout — Cocoa doesn't consider us maximized. We track the state
         * in our own macos_maximized flag. */
        if (manager->macos_maximized) {
            glfwSetWindowPos(manager->window, manager->macos_saved_x, manager->macos_saved_y);
            glfwSetWindowSize(manager->window, manager->macos_saved_w, manager->macos_saved_h);
            manager->macos_maximized = 0;
            break;
        }
        glfwGetWindowPos(manager->window, &manager->macos_saved_x, &manager->macos_saved_y);
        glfwGetWindowSize(manager->window, &manager->macos_saved_w, &manager->macos_saved_h);
        /* Pick the monitor whose workarea contains the window's center; fall
         * back to the primary monitor for the (rare) case where the window
         * straddles every monitor. */
        int cx = manager->macos_saved_x + manager->macos_saved_w / 2;
        int cy = manager->macos_saved_y + manager->macos_saved_h / 2;
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
            glfwSetWindowPos(manager->window, wax, way);
            glfwSetWindowSize(manager->window, waw, wah);
            manager->macos_maximized = 1;
        }
#else
        if (glfwGetWindowAttrib(manager->window, GLFW_MAXIMIZED)) {
            glfwRestoreWindow(manager->window);
        } else {
            glfwMaximizeWindow(manager->window);
        }
#endif
        break;
    case YETTY_YCORE_WINDOW_CLOSE: {
        /* Mirror the OS-bar close callback exactly: post SHUTDOWN to the render
         * thread's input pipe. The render thread stops its event loop, sets
         * *running=0, posts an empty event; the main loop then exits naturally
         * on the running check. Setting glfwSetWindowShouldClose alone would
         * leave the render thread spinning and deadlock on join. */
        if (manager->input_pipe) {
            struct yetty_yui_event shutdown = {.type = YETTY_YCORE_SHUTDOWN};
            manager->input_pipe->ops->write(manager->input_pipe, &shutdown, sizeof(shutdown));
        }
        break;
    }
    case YETTY_YCORE_WINDOW_DRAG_BY: {
        /* On Wayland the compositor took over the drag at MOUSE_DOWN (see
         * WINDOW_BEGIN_INTERACTIVE_MOVE below) — we don't get any MOUSE_MOVE
         * events during the drag and glfwSetWindowPos is a no-op anyway. So
         * skip the absolute-positioning path here. */
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            yetty_yplatform_os_event_invalidate_click_pairing(manager->window);
            break;
        }
        /* Delta comes in framebuffer pixels (mouse coords are scaled to
         * framebuffer at the input layer so hit-tests against bar->width line
         * up). glfwSetWindowPos / glfwSetWindowSize take *screen* coords. On
         * HiDPI displays (macOS Retina = 2×) framebuffer is larger than the
         * window in screen coords, so we have to divide back down here or the
         * window moves/resizes by 2× too much. */
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(manager->window, &ww, &wh);
        glfwGetFramebufferSize(manager->window, &fw, &fh);
        double sx = (fw > 0 && ww > 0) ? (double)ww / (double)fw : 1.0;
        double sy = (fh > 0 && wh > 0) ? (double)wh / (double)fh : 1.0;

        int x, y;
        glfwGetWindowPos(manager->window, &x, &y);
        glfwSetWindowPos(manager->window, x + (int)(event->window_drag.dx * sx),
                         y + (int)(event->window_drag.dy * sy));
        /* Drag executed → the preceding MOUSE_DOWN dragged the window, so it
         * wasn't a clean click. Tell the OS event loop to drop the double-click
         * pairing window: a subsequent rapid press starts a fresh drag instead
         * of mis-firing as a double-click. */
        yetty_yplatform_os_event_invalidate_click_pairing(manager->window);
        break;
    }
    case YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE:
        ydebug("WMOVETRACE: [main-thread] received WINDOW_BEGIN_INTERACTIVE_MOVE, "
               "platform=%d (WAYLAND=%d, X11=%d)",
               glfwGetPlatform(), GLFW_PLATFORM_WAYLAND, GLFW_PLATFORM_X11);
        /* Wayland-only meaningful work. On X11 this is a no-op and the per-pixel
         * WINDOW_DRAG_BY path handles the move via glfwSetWindowPos as before.
         * The helper itself runtime-checks glfwGetPlatform() and returns early
         * on non-Wayland. */
        yetty_yplatform_wayland_begin_interactive_move(manager->window);
        /* After this point the compositor owns the drag — record that the press
         * wasn't a clean click so the next press doesn't mis-pair as a
         * double-click. */
        yetty_yplatform_os_event_invalidate_click_pairing(manager->window);
        break;
    case YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE:
        ydebug("WMOVETRACE: [main-thread] received WINDOW_BEGIN_INTERACTIVE_RESIZE "
               "edge=%d, platform=%d",
               event->window_begin_resize.edge, glfwGetPlatform());
        /* Same Wayland-vs-X11 split as the move helper. The wayland.c
         * implementation runtime-checks the platform and no-ops on X11; X11
         * keeps using per-pixel WINDOW_RESIZE_BY. */
        yetty_yplatform_wayland_begin_interactive_resize(
            manager->window, (unsigned int)event->window_begin_resize.edge);
        yetty_yplatform_os_event_invalidate_click_pairing(manager->window);
        break;
    case YETTY_YCORE_SET_CURSOR: {
        int shape = event->set_cursor.shape;
        if (shape < 0 || (size_t)shape >= sizeof(manager->cursors) / sizeof(manager->cursors[0])) {
            break;
        }
        if (shape == manager->applied_shape) {
            break; /* idempotent — already bound. */
        }
        /* Map enum yetty_ycore_cursor_shape onto GLFW standard cursors. Lazily
         * create on first use; the cached pointer is reused for subsequent
         * applies. NULL = restore default arrow. */
        if (!manager->cursors[shape] && shape != YETTY_YCORE_CURSOR_DEFAULT) {
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
            manager->cursors[shape] = glfwCreateStandardCursor(glfw_shape);
        }
        glfwSetCursor(manager->window, manager->cursors[shape]); /* NULL → default. */
        manager->applied_shape = shape;
        break;
    }
    case YETTY_YCORE_WINDOW_RESIZE_BY: {
        /* On Wayland the compositor took over the resize at MOUSE_DOWN (see
         * WINDOW_BEGIN_INTERACTIVE_RESIZE) — and glfwSetWindowSize is a no-op
         * there anyway. Skip the per-pixel path. */
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
            yetty_yplatform_os_event_invalidate_click_pairing(manager->window);
            break;
        }
        int ww = 0, wh = 0, fw = 0, fh = 0, wx = 0, wy = 0;
        glfwGetWindowSize(manager->window, &ww, &wh);
        glfwGetFramebufferSize(manager->window, &fw, &fh);
        glfwGetWindowPos(manager->window, &wx, &wy);
        double sx = (fw > 0 && ww > 0) ? (double)ww / (double)fw : 1.0;
        double sy = (fh > 0 && wh > 0) ? (double)wh / (double)fh : 1.0;
        /* Delta in framebuffer px → screen px. */
        int dx = (int)(event->window_resize.dx * sx);
        int dy = (int)(event->window_resize.dy * sy);
        int edge = event->window_resize.edge;

        /* Apply per edge from FRESH geometry. Right/bottom grow with the
         * top-left fixed; left/top move the origin too. Reading current x/y/w/h
         * each event (rather than accumulating) keeps the dragged edge tracking
         * the cursor — the caller anchors left/top so the window-relative cursor
         * resets after each move. edge==0 → legacy right+bottom growth. */
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
        /* Minimum size — keep the opposite (fixed) edge in place when clamping a
         * left/top drag, so the window doesn't jump. */
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
            glfwSetWindowPos(manager->window, nx, ny);
        }
        glfwSetWindowSize(manager->window, nw, nh);
        yetty_yplatform_os_event_invalidate_click_pairing(manager->window);
        break;
    }
    default:
        /* Not ours — silently ignore. The drain dispatches by event type, so
         * this branch only runs if a caller passes a foreign event by mistake. */
        break;
    }
    return YETTY_OK_VOID();
}

#include "window-manager.gen.c"
