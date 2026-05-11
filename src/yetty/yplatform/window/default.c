/* GLFW window - Window creation and management */

#include <GLFW/glfw3.h>
#include <stddef.h>

GLFWwindow *yetty_yplatform_create_window(int width, int height, const char *title)
{
    /* No OpenGL context - we use WebGPU */
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    /* Chrome-like UI: drop the OS title bar and draw our own tab strip on top.
     * Workspaces ARE the tabs; the strip is owned by the workspace renderer.
     * GLFW_DECORATED=FALSE removes the OS frame on X11, Wayland, macOS, and
     * Win32 alike. Window move/resize will be re-implemented inside the tab
     * strip event handler (drag empty area to move, edges to resize). */
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    return glfwCreateWindow(width, height, title, NULL, NULL);
}

void yetty_yplatform_destroy_window(GLFWwindow *window)
{
    if (window) {
        glfwDestroyWindow(window);
    }
}

void yetty_yplatform_get_window_size(GLFWwindow *window, int *width, int *height)
{
    if (window) {
        glfwGetWindowSize(window, width, height);
    } else {
        *width = 0;
        *height = 0;
    }
}

void yetty_yplatform_get_framebuffer_size(GLFWwindow *window, int *width, int *height)
{
    if (window) {
        glfwGetFramebufferSize(window, width, height);
    } else {
        *width = 0;
        *height = 0;
    }
}

void yetty_yplatform_get_content_scale(GLFWwindow *window, float *xscale, float *yscale)
{
    if (window) {
        glfwGetWindowContentScale(window, xscale, yscale);
    } else {
        *xscale = 1.0f;
        *yscale = 1.0f;
    }
}

int yetty_yplatform_window_should_close(GLFWwindow *window)
{
    return window ? glfwWindowShouldClose(window) : 1;
}

void yetty_yplatform_set_window_title(GLFWwindow *window, const char *title)
{
    if (window) {
        glfwSetWindowTitle(window, title);
    }
}
