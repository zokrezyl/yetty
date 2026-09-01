#ifndef YETTY_YPLATFORM_MOVE_RESIZE_H
#define YETTY_YPLATFORM_MOVE_RESIZE_H

/*
 * Wayland-native interactive window move / resize. The implementation
 * lives in src/yetty/yplatform/move-resize/wayland.c and is the only file
 * in the codebase that touches GLFW's private headers (staged from the
 * upstream source tarball into the 3rdparty prebuilt under
 * include-private/). The helpers exist because Wayland's protocol forbids
 * client-driven absolute positioning / sizing — only xdg_toplevel.move
 * (seat, serial) and xdg_toplevel.resize(seat, serial, edge) are honored,
 * and the toplevel they need lives behind GLFW objects that aren't
 * exposed in glfw3native.h.
 *
 * Call from the main thread (the window-chrome's handle_event runs there
 * — calls into wl_proxy_marshal are otherwise not thread-safe with GLFW's
 * own usage). No-ops on X11; the per-pixel glfwSetWindowPos/Size paths
 * through WINDOW_DRAG_BY / WINDOW_RESIZE_BY handle X11 correctly.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct GLFWwindow;

/* Returns 1 if the compositor took the gesture over (Wayland-native path
 * fired xdg_toplevel.move / .resize), 0 otherwise (null.c on
 * macOS/Windows/mobile/web, or the runtime-X11 branch on a Wayland-capable
 * GLFW build). The desktop chrome subclass uses the return value to
 * synthesize a MOUSE_UP into the render pipe after a compositor takeover —
 * the compositor eats the real MOUSE_UP, so without a synthetic one chrome
 * (and yui's tabbar-model) would stay in a stale `resizing`/`dragging`
 * state and misinterpret every subsequent MOUSE_MOVE. */
int yetty_yplatform_wayland_begin_interactive_move(struct GLFWwindow *window);

/* `edge` is a yetty_ycore_resize_edge value (values match xdg-shell's
 * xdg_toplevel.resize_edge wire enum: 1=top, 2=bottom, 4=left, 8=right,
 * combine for corners). 0 (NONE) is a no-op. Return semantics match
 * yetty_yplatform_wayland_begin_interactive_move above. */
int yetty_yplatform_wayland_begin_interactive_resize(struct GLFWwindow *window, unsigned int edge);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_MOVE_RESIZE_H */
