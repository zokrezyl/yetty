/*
 * ywindow-chrome/window-chrome.c — base yclass class `yplatform:window_chrome`.
 *
 * The render thread's typed, platform-independent API for telling the OS window
 * to minimize / toggle-maximize / close / move / resize, and for setting the
 * mouse-cursor shape. Each producer slot writes a typed event onto the shared
 * output_pipe; the main thread drains the pipe and applies each event through
 * window_chrome_handle_event — which is a virtual whose real, platform-specific
 * implementation lives in a subclass (yplatform:glfw_window_chrome on desktop).
 *
 * This base carries NO native handle and NO platform headers, so it compiles
 * into the platform-independent yetty_yplatform_core: any module that drives the
 * window chrome (ychrome's gesture handlers, yui's tabbar) links the producer
 * stubs from core regardless of whether the platform's concrete window-chrome
 * implementation is in the link. The default handle_event is a no-op so a
 * headless / platformless build degrades gracefully.
 *
 * Thread-safety: the producer slots are safe from any thread (they only write to
 * the pipe). window_chrome_handle_event must run on the main thread (the
 * subclass override touches the native window directly).
 *
 * yclass: the only hand-written file is this annotated .c; window-chrome.gen.c is
 * #included at the foot. Every slot is `local@` — the window chrome is an
 * in-process object bound to one OS window, never proxied over RPC.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytrace/ytrace.h>

struct [[clang::annotate("class@yplatform:window_chrome")]] yetty_yplatform_window_chrome {
    /* Borrowed render→main marshalling bus — owned by the caller, set via
     * configure(). The producer slots write typed events here. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;
};

/* Result wrapper for the window-chrome handle. Declared here (not pulled from the
 * generated header, which this TU does not include) so the appended
 * window-chrome.gen.c — which defines yetty_yplatform_window_chrome_from()
 * returning it — has the type in scope. */
YETTY_YRESULT_DECLARE(yetty_yplatform_window_chrome_ptr, struct yetty_yplatform_window_chrome *);

/* Defined in the appended window-chrome.gen.c. Forward-declared because this TU
 * does not include its own generated header. */
struct yetty_yclass_ptr_result yetty_yplatform_window_chrome_class_get(void);
struct yetty_yplatform_window_chrome_ptr_result yetty_yplatform_window_chrome_from(
    struct yetty_yclass_object *obj);

/* Resolve the object's base slice, preserving the class_get / object_data error
 * chain. */
static struct yetty_yclass_void_ptr_result window_chrome_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yplatform_window_chrome_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "window_chrome_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "window_chrome_from_obj: object_data");
    return slice_r;
}

/* Push one typed event to the main thread. Best-effort: a full or broken pipe
 * just drops the request. Waking the main thread (e.g. glfwPostEmptyEvent) is the
 * platform layer's job — it owns the OS event loop that drains output_pipe. */
static void post_event(struct yetty_yplatform_window_chrome *chrome,
                       const struct yetty_yui_event *event)
{
    if (!chrome->output_pipe) {
        return;
    }
    struct yetty_ycore_size_result write_result =
        chrome->output_pipe->ops->write(chrome->output_pipe, event, sizeof(*event));
    (void)write_result;
}

/*=============================================================================
 * Lifecycle
 *===========================================================================*/

/* Bind the chrome to its render→main bus. The platform subclass binds the native
 * window + response pipe separately (e.g. glfw_window_chrome_attach), so each
 * class writes only its own data slice. output_pipe is borrowed. */
/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_configure")]]
[[clang::annotate("local@yplatform:window_chrome_configure")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *output_pipe)
{
    if (!output_pipe) {
        return YETTY_ERR(yetty_ycore_void, "window_chrome configure: output_pipe required");
    }
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome configure: object");
    struct yetty_yplatform_window_chrome *chrome = data_r.value;
    chrome->output_pipe = output_pipe;
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_destroy")]]
[[clang::annotate("local@yplatform:window_chrome_destroy")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_destroy(struct yetty_yclass_object *obj)
{
    return yetty_yclass_object_free(obj);
}

/*=============================================================================
 * Producer slots (any thread) — post a typed intent to the main thread.
 *===========================================================================*/

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_iconify")]]
[[clang::annotate("local@yplatform:window_chrome_iconify")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_iconify(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome iconify: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_ICONIFY};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_toggle_maximize")]]
[[clang::annotate("local@yplatform:window_chrome_toggle_maximize")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_toggle_maximize(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome toggle_maximize: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_TOGGLE_MAXIMIZE};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_request_close")]]
[[clang::annotate("local@yplatform:window_chrome_request_close")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_request_close(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome request_close: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_CLOSE};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_drag_by")]]
[[clang::annotate("local@yplatform:window_chrome_drag_by")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_drag_by(struct yetty_yclass_object *obj, int dx,
                                                            int dy)
{
    if (dx == 0 && dy == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome drag_by: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_DRAG_BY,
                                    .window_drag = {.dx = dx, .dy = dy}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_resize_by")]] [[clang::annotate(
    "local@yplatform:window_chrome_resize_by")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_resize_by(struct yetty_yclass_object *obj,
                                                              int dx, int dy, int edge)
{
    if (dx == 0 && dy == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome resize_by: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_RESIZE_BY,
                                    .window_resize = {.dx = dx, .dy = dy, .edge = edge}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_begin_interactive_move")]]
[[clang::annotate("local@yplatform:window_chrome_begin_interactive_move")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_begin_interactive_move(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome begin_interactive_move: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_MOVE};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_begin_interactive_resize")]]
[[clang::annotate("local@yplatform:window_chrome_begin_interactive_resize")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_begin_interactive_resize(
    struct yetty_yclass_object *obj, int edge)
{
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome begin_interactive_resize: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_WINDOW_BEGIN_INTERACTIVE_RESIZE,
                                    .window_begin_resize = {.edge = edge}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_set_cursor")]]
[[clang::annotate("local@yplatform:window_chrome_set_cursor")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_set_cursor(struct yetty_yclass_object *obj,
                                                               int shape)
{
    struct yetty_yclass_void_ptr_result data_r = window_chrome_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_r, "window_chrome set_cursor: object");
    struct yetty_yui_event event = {.type = YETTY_YCORE_SET_CURSOR, .set_cursor = {.shape = shape}};
    post_event(data_r.value, &event);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Main-thread side — apply one drained event. Default no-op; a platform subclass
 * overrides this to perform the real native window calls.
 *===========================================================================*/

/* clang-format off */
[[clang::annotate("virtual@yplatform:window_chrome:window_chrome_handle_event")]]
[[clang::annotate("local@yplatform:window_chrome_handle_event")]]
/* clang-format on */
static struct yetty_ycore_void_result window_chrome_handle_event(
    struct yetty_yclass_object *obj, const struct yetty_yui_event *event)
{
    (void)obj;
    (void)event;
    /* No native window in the base; a headless / platformless build drops the
     * event. The desktop subclass (glfw_window_chrome) overrides this. */
    return YETTY_OK_VOID();
}

#include "window-chrome.gen.c"
