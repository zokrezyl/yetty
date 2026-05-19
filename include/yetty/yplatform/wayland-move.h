#ifndef YETTY_YPLATFORM_WAYLAND_MOVE_H
#define YETTY_YPLATFORM_WAYLAND_MOVE_H

/*
 * Wayland-native interactive window move. The implementation lives in
 * src/yetty/yplatform/wayland-move/default.c and is the only file in the
 * codebase that touches GLFW's private headers (staged from the upstream
 * source tarball into the 3rdparty prebuilt under include-private/). The
 * helper exists because Wayland's protocol forbids client-driven absolute
 * positioning — only xdg_toplevel.move(seat, serial) is honored, and that
 * lives behind GLFW objects that aren't exposed in glfw3native.h.
 *
 * Call from the main thread (the window-manager's handle_event runs there
 * — calls into wl_proxy_marshal are otherwise not thread-safe with GLFW's
 * own usage). No-ops on X11; the per-pixel glfwSetWindowPos path through
 * WINDOW_DRAG_BY handles X11 correctly.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct GLFWwindow;

void yetty_yplatform_wayland_begin_interactive_move(struct GLFWwindow *window);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_WAYLAND_MOVE_H */
