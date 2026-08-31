/*
 * ywindow/macos.m — macOS-specific post-creation tweaks for the GLFW window.
 *
 * GLFW opens the window with hints (undecorated + resizable). On macOS that
 * translates to an NSWindow with NSWindowStyleMaskResizable but no
 * NSWindowStyleMaskTitled, and the OS still activates its native
 * edge-resize behavior — a mousedown within the borderline gets absorbed
 * by AppKit's modal resize-tracking loop instead of flowing to GLFW's
 * mouse callbacks. yetty draws its own resize edges (ychrome / tabbar
 * model) so the OS one is unwanted: it hides the click from the render
 * thread, and it also blocks Metal drawable presentation for the duration
 * of the drag (the render thread's presents queue up and only flush on
 * release — the "resize catches up after release" symptom).
 *
 * Fix: clear NSWindowStyleMaskResizable after glfwCreateWindow. The window
 * is still resizable programmatically (glfwSetWindowSize / setFrame both
 * work regardless of the style bit); we just tell AppKit "don't handle
 * the edge drag yourself".
 */
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>

void yetty_yplatform_glfw_window_configure_macos(struct GLFWwindow *window)
{
    if (!window) {
        return;
    }
    NSWindow *ns_window = glfwGetCocoaWindow(window);
    if (!ns_window) {
        return;
    }
    NSWindowStyleMask mask = [ns_window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [ns_window setStyleMask:mask];
}
