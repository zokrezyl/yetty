/* GLFW window - Window creation and management */

#include <GLFW/glfw3.h>
#include <stddef.h>
#include <yetty/ycore/result.h>
#include <yetty/yplatform/window.h>

/* Map the last GLFW error code to a static reason string. The description
 * string from glfwGetError is transient (valid only until the next GLFW
 * error), and the result error stores a non-owning pointer, so we key on the
 * stable error code and return a string literal instead. */
static const char *glfw_create_window_reason(void)
{
    switch (glfwGetError(NULL)) {
    case GLFW_NOT_INITIALIZED:
        return "glfwCreateWindow: GLFW not initialized";
    case GLFW_INVALID_ENUM:
        return "glfwCreateWindow: invalid enum";
    case GLFW_INVALID_VALUE:
        return "glfwCreateWindow: invalid value";
    case GLFW_API_UNAVAILABLE:
        return "glfwCreateWindow: required client API unavailable";
    case GLFW_VERSION_UNAVAILABLE:
        return "glfwCreateWindow: requested API version unavailable";
    case GLFW_FORMAT_UNAVAILABLE:
        return "glfwCreateWindow: requested pixel format unavailable";
    case GLFW_PLATFORM_ERROR:
        return "glfwCreateWindow: platform error (no display server?)";
    case GLFW_NO_ERROR:
        return "glfwCreateWindow returned NULL with no GLFW error set";
    default:
        return "glfwCreateWindow failed (unknown GLFW error)";
    }
}

struct yetty_yplatform_glfwindow_ptr_result yetty_yplatform_create_window(int width, int height,
                                                                          const char *title)
{
    /* No OpenGL context - we use WebGPU */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    /* Browser-style UI: drop the OS title bar and draw our own tab strip on top.
     * Workspaces ARE the tabs; the strip is owned by the workspace renderer.
     * GLFW_DECORATED=FALSE removes the OS frame on X11, Wayland, macOS, and
     * Win32 alike. Window move/resize will be re-implemented inside the tab
     * strip event handler (drag empty area to move, edges to resize). */
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    GLFWwindow *window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        return YETTY_ERR(yetty_yplatform_glfwindow_ptr, glfw_create_window_reason());
    }
    return YETTY_OK(yetty_yplatform_glfwindow_ptr, window);
}

/* Best-effort teardown — NULL-tolerant, never fails. */
struct yetty_ycore_void_result yetty_yplatform_destroy_window(GLFWwindow *window)
{
    if (window) {
        glfwDestroyWindow(window);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_get_window_size(GLFWwindow *window, int *width,
                                                               int *height)
{
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "get_window_size: window is NULL");
    }
    glfwGetWindowSize(window, width, height);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_get_framebuffer_size(GLFWwindow *window, int *width,
                                                                    int *height)
{
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "get_framebuffer_size: window is NULL");
    }
    glfwGetFramebufferSize(window, width, height);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_get_content_scale(GLFWwindow *window, float *xscale,
                                                                 float *yscale)
{
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "get_content_scale: window is NULL");
    }
    glfwGetWindowContentScale(window, xscale, yscale);
    return YETTY_OK_VOID();
}

/* Carries the should-close flag as the result value. A NULL window is
 * treated as "should close" (value 1), matching the pre-Result behaviour. */
struct yetty_ycore_int_result yetty_yplatform_window_should_close(GLFWwindow *window)
{
    return YETTY_OK(yetty_ycore_int, window ? glfwWindowShouldClose(window) : 1);
}

struct yetty_ycore_void_result yetty_yplatform_set_window_title(GLFWwindow *window,
                                                                const char *title)
{
    if (!window) {
        return YETTY_ERR(yetty_ycore_void, "set_window_title: window is NULL");
    }
    glfwSetWindowTitle(window, title);
    return YETTY_OK_VOID();
}
