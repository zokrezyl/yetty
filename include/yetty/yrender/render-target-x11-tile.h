/*
 * render-target-x11-tile.h - X11 render target that presents via per-tile
 * XShmPutImage instead of a WebGPU swap chain.
 *
 * Rationale: on a remote display (typically X11 + VNC), presenting a full
 * surface every frame forces the VNC server to ship the entire framebuffer
 * over the wire. Alacritty avoids this by only redrawing damaged cells and
 * letting X11 damage tracking propagate tiny deltas to the VNC server.
 *
 * This target does the same: it renders to an offscreen WebGPU texture,
 * feeds the texture through the GPU tile-diff engine, and on each frame
 * copies only the dirty tiles into an X shared-memory image, which is then
 * XShmPutImage'd rect-by-rect into the window. X's damage tracking sees
 * only the changed tiles, and so does VNC downstream.
 *
 * The target is an opt-in via YETTY_X11_TILE=1 (set by the platform init).
 * On a local display the standard surface path is still faster (zero-copy
 * dmabuf); the tile target only wins over a remote pipe.
 */

#ifndef YETTY_YRENDER_RENDER_TARGET_X11_TILE_H
#define YETTY_YRENDER_RENDER_TARGET_X11_TILE_H

#include <yetty/yrender/render-target.h>
#include <yetty/ycore/result.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_wgpu;
struct yetty_ypaint_core_gpu_allocator;
struct yetty_yevent_event_loop;

/*
 * Create the X11-tile render target.
 *
 * `x11_display` is an opaque `Display *` (void* here to keep Xlib out of the
 * public header). `x11_window` is the X11 `Window` ID as an unsigned long.
 * Both must come from the platform layer (glfwGetX11Display/Window on GLFW).
 *
 * `event_loop` is used to schedule a catch-up render when a present() is
 * dropped due to an in-flight readback (avoids the "one-character delay"
 * symptom on bursty input like nvim's initial draw).
 *
 * Returns an error if XShm is unavailable or initial setup fails. Callers
 * should fall back to the regular texture target on failure.
 */
struct yetty_yrender_target_ptr_result yetty_yrender_target_x11_tile_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ypaint_core_gpu_allocator *allocator, struct yetty_yplatform_wgpu *wgpu,
    struct yetty_yevent_event_loop *event_loop, void *x11_display, unsigned long x11_window,
    struct yetty_yrender_viewport viewport);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_RENDER_TARGET_X11_TILE_H */
