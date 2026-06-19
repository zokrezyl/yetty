/*
 * move-resize/null.c — stub used on builds where GLFW's private headers
 * aren't staged (macOS / Windows / mobile / web). The function is the
 * same shape as the real one in wayland.c so the window-chrome can call
 * it unconditionally; on these platforms there's nothing to do because
 * either the OS lets glfwSetWindowPos move the window (Windows), the
 * underlying platform isn't Wayland at all (macOS / mobile / web), or we
 * don't even draw chrome (mobile / web).
 */

#include <yetty/yplatform/move-resize.h>

void yetty_yplatform_wayland_begin_interactive_move(struct GLFWwindow *window)
{
    (void)window;
}

void yetty_yplatform_wayland_begin_interactive_resize(struct GLFWwindow *window, unsigned int edge)
{
    (void)window;
    (void)edge;
}
