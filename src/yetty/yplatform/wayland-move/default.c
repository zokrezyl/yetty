/*
 * wayland-move/default.c — protocol-correct interactive window move on
 * Wayland.
 *
 * Wayland deliberately hides window position from clients: glfwSetWindowPos
 * is a no-op there, so yetty's per-pixel WINDOW_DRAG_BY path doesn't move
 * the window. The compositor will only accept a window move via the
 * `xdg_toplevel.move(seat, serial)` request, which it answers by grabbing
 * the pointer and dragging the window itself until the user releases the
 * button.
 *
 * GLFW 3.4 owns the wl_surface / xdg_surface / xdg_toplevel objects and
 * doesn't expose them through its public native API. But the symbols are
 * there in libglfw3.a (`_glfw` is a BSS global; the per-window state lives
 * inline in `_GLFWwindow->wl.xdg.toplevel`). All we need is the matching
 * struct layout — which is exactly the private GLFW source headers we
 * stage into the 3rdparty tarball under include-private/ (see
 * build-tools/3rdparty/glfw/_build.sh and build-tools/yetty/libs/glfw.cmake).
 *
 * This file is the entire Wayland-native move implementation: replicate
 * GLFW's own pattern from src/wl_window.c::pointerHandleButton:1565.
 */

/* The private headers are written against GLFW's own #define flags. We
 * stage them alongside a static lib built with both backends enabled, so
 * mirror that here — without these the wl_platform.h / x11_platform.h
 * sections in platform.h are skipped and `window->wl.xdg.toplevel`
 * wouldn't be in scope. */
#define _GLFW_WAYLAND
#define _GLFW_X11

#include "internal.h"
#include "xdg-shell-client-protocol.h"

#include <GLFW/glfw3.h>
#include <yetty/ytrace/ytrace.h>

void yetty_yplatform_wayland_begin_interactive_move(GLFWwindow *handle)
{
    if (!handle) {
        return;
    }
    /* Only the Wayland backend has an xdg_toplevel — and the runtime
     * platform may not be Wayland even on a Linux build (X11 fallback).
     * The X11 path uses per-pixel glfwSetWindowPos via WINDOW_DRAG_BY,
     * so just no-op here. */
    if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) {
        ydebug("wayland-move: not Wayland (platform=%d), no-op", glfwGetPlatform());
        return;
    }

    /* Cast the public GLFWwindow* to the private _GLFWwindow*. They
     * point to the same memory — GLFW does this exact cast everywhere in
     * its own source (`_GLFWwindow* window = (_GLFWwindow*) handle;`). */
    _GLFWwindow *window = (_GLFWwindow *)handle;

    if (!_glfw.wl.seat) {
        ydebug("wayland-move: no wl_seat — bailing");
        return;
    }

    /* GLFW has two CSD paths on Wayland:
     *   (a) direct xdg-shell — window->wl.xdg.toplevel is set.
     *   (b) libdecor (used when libdecor.so is loadable, which is the
     *       default on GNOME / Mutter setups) — the real xdg_toplevel
     *       lives inside a libdecor_frame and window->wl.xdg.toplevel
     *       stays NULL. The toplevel is retrieved via
     *       libdecor_frame_get_xdg_toplevel(frame), a function GLFW
     *       dlopen-loads and stores in _glfw.wl.libdecor.
     *
     * Pick whichever is non-NULL. Without the libdecor fallback the
     * helper silently bailed on every drag attempt under GNOME. */
    struct xdg_toplevel *toplevel = window->wl.xdg.toplevel;
    if (!toplevel && window->wl.libdecor.frame &&
        _glfw.wl.libdecor.libdecor_frame_get_xdg_toplevel_) {
        toplevel = libdecor_frame_get_xdg_toplevel(window->wl.libdecor.frame);
        ydebug("wayland-move: via libdecor frame=%p -> toplevel=%p",
               (void *)window->wl.libdecor.frame, (void *)toplevel);
    }
    if (!toplevel) {
        ydebug("wayland-move: no xdg_toplevel reachable "
               "(wl.xdg.toplevel=%p, wl.libdecor.frame=%p) — bailing",
               (void *)window->wl.xdg.toplevel,
               (void *)window->wl.libdecor.frame);
        return;
    }

    /* Same protocol call GLFW runs in src/wl_window.c:1565 to drag its
     * own fallback titlebar. _glfw.wl.serial is the last button-press
     * serial — GLFW updates it on every wl_pointer.button (see
     * pointerHandleButton:1547), so by the time the render thread has
     * decided "this is a titlebar drag" and posted us back through the
     * output_pipe, the value is still the one from THIS press (no other
     * button events have happened — the user is holding the mouse). */
    ydebug("wayland-move: xdg_toplevel_move(toplevel=%p, seat=%p, serial=%u)",
           (void *)toplevel, (void *)_glfw.wl.seat, (unsigned)_glfw.wl.serial);
    xdg_toplevel_move(toplevel, _glfw.wl.seat, _glfw.wl.serial);
}
