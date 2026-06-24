/*
 * yplatform/ywindow/glfw.c — GLFW desktop window subclass
 * (Linux / macOS / Windows).
 *
 * Reimplements the old hand-written yplatform/window/default.c functions as
 * overrides of the yplatform:window virtual contract. The GLFWwindow* is
 * private state of this subclass — stored in its own data slice, never on the
 * base and never handed out through a public accessor.
 */

#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>
#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/yclass/class.h>

/* WebGPU surface creator (webgpu-surface/default.c). */
WGPUSurface yetty_yplatform_create_surface(WGPUInstance instance, GLFWwindow *window);

/* Own result wrapper + codegen accessor/downcast forward-decls. */
YETTY_YRESULT_DECLARE(yetty_yplatform_glfw_window_ptr, struct yetty_yplatform_glfw_window *);
struct yetty_yclass_ptr_result yetty_yplatform_glfw_window_class_get(void);
struct yetty_yplatform_glfw_window_ptr_result yetty_yplatform_glfw_window_from(
    struct yetty_yclass_object *obj);

/* Private subclass state: the native GLFW window, owned here. */
struct [[clang::annotate("class@yplatform:glfw_window")]] [[clang::annotate(
    "platform@glfw")]] [[clang::annotate("parent@yplatform:window")]] yetty_yplatform_glfw_window {
    struct GLFWwindow *handle;
};

/* Recover the private GLFWwindow*. Returns NULL if unset or on a data-access
 * failure; the override callers treat NULL as "no window". */
static GLFWwindow *glfw_window_handle(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_window_ptr_result data = yetty_yplatform_glfw_window_from(obj);
    if (!YETTY_IS_OK(data)) {
        yetty_ycore_error_destroy(data.error);
        return NULL;
    }
    return data.value->handle;
}

/* Map the last GLFW error code to a static reason string. The description
 * string from glfwGetError is transient and the result error stores a
 * non-owning pointer, so key on the stable error code instead. */
static const char *glfw_open_reason(void)
{
    switch (glfwGetError(NULL)) {
    case GLFW_NOT_INITIALIZED:
        return "window_open: GLFW not initialized";
    case GLFW_INVALID_ENUM:
        return "window_open: invalid enum";
    case GLFW_INVALID_VALUE:
        return "window_open: invalid value";
    case GLFW_API_UNAVAILABLE:
        return "window_open: required client API unavailable";
    case GLFW_VERSION_UNAVAILABLE:
        return "window_open: requested API version unavailable";
    case GLFW_FORMAT_UNAVAILABLE:
        return "window_open: requested pixel format unavailable";
    case GLFW_PLATFORM_ERROR:
        return "window_open: platform error (no display server?)";
    case GLFW_NO_ERROR:
        return "window_open: glfwCreateWindow returned NULL with no GLFW error set";
    default:
        return "window_open: glfwCreateWindow failed (unknown GLFW error)";
    }
}

[[clang::annotate("override@yplatform:glfw_window:window_open")]]
static struct yetty_ycore_void_result glfw_window_open(struct yetty_yclass_object *obj, int width,
                                                       int height, const char *title)
{
    struct yetty_yplatform_glfw_window_ptr_result data = yetty_yplatform_glfw_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "window_open: data_get");

    /* No OpenGL context — WebGPU draws into the window; undecorated, yetty
     * draws its own tab strip. */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, glfw_open_reason());
    }
    data.value->handle = window;
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_window:window_destroy")]]
static struct yetty_ycore_void_result glfw_window_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yplatform_glfw_window_ptr_result data = yetty_yplatform_glfw_window_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "window_destroy: data_get");
    if (data.value->handle) {
        glfwDestroyWindow(data.value->handle);
        data.value->handle = NULL;
    }
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_window:window_get_size")]]
static struct yetty_ycore_void_result glfw_window_get_size(struct yetty_yclass_object *obj,
                                                           int *width, int *height)
{
    GLFWwindow *window = glfw_window_handle(obj);
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "window_get_size: no GLFW window");
    }
    glfwGetWindowSize(window, width, height);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_window:window_get_framebuffer_size")]]
static struct yetty_ycore_void_result glfw_window_get_framebuffer_size(
    struct yetty_yclass_object *obj, int *width, int *height)
{
    GLFWwindow *window = glfw_window_handle(obj);
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "window_get_framebuffer_size: no GLFW window");
    }
    glfwGetFramebufferSize(window, width, height);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_window:window_get_content_scale")]]
static struct yetty_ycore_void_result glfw_window_get_content_scale(struct yetty_yclass_object *obj,
                                                                    float *xscale, float *yscale)
{
    GLFWwindow *window = glfw_window_handle(obj);
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "window_get_content_scale: no GLFW window");
    }
    glfwGetWindowContentScale(window, xscale, yscale);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_window:window_should_close")]]
static struct yetty_ycore_int_result glfw_window_should_close(struct yetty_yclass_object *obj)
{
    GLFWwindow *window = glfw_window_handle(obj);
    return YETTY_OK(yetty_ycore_int, window ? glfwWindowShouldClose(window) : 1);
}

[[clang::annotate("override@yplatform:glfw_window:window_set_title")]]
static struct yetty_ycore_void_result glfw_window_set_title(struct yetty_yclass_object *obj,
                                                            const char *title)
{
    GLFWwindow *window = glfw_window_handle(obj);
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "window_set_title: no GLFW window");
    }
    glfwSetWindowTitle(window, title);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yplatform:glfw_window:window_create_surface")]]
static struct yetty_yclass_void_ptr_result glfw_window_create_surface(
    struct yetty_yclass_object *obj, void *instance)
{
    GLFWwindow *window = glfw_window_handle(obj);
    if (!window) {
        return YETTY_ERR(yetty_yclass_void_ptr, "window_create_surface: no GLFW window");
    }
    WGPUSurface surface = yetty_yplatform_create_surface((WGPUInstance)instance, window);
    if (!surface) {
        return YETTY_ERR(yetty_yclass_void_ptr, "window_create_surface: surface creation failed");
    }
    return YETTY_OK(yetty_yclass_void_ptr, surface);
}

/* Internal platform-glue accessor (NOT part of the public window API and NOT in
 * the generated header): hand the private GLFWwindow* to the GLFW platform layer
 * that owns the OS event loop. The event-loop callbacks, the window_chrome
 * configure, and the X11 co-handle lookup all run on the same main thread that
 * created this window and legitimately need the native handle; everything that
 * crosses module / platform boundaries goes through the virtual methods above.
 * The glfw_platform forward-declares this and calls it directly. */
void *yetty_yplatform_glfw_window_native_handle(struct yetty_yclass_object *obj)
{
    return glfw_window_handle(obj);
}

#include "glfw.gen.c"
