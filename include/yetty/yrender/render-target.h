#ifndef YETTY_YRENDER_TARGET_H
#define YETTY_YRENDER_TARGET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_core_target;
struct yetty_yrender_terminal_layer;
struct yetty_ydraw_core_gpu_allocator;
struct yetty_yetty_gpu_context;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yrender_target_ptr, struct yetty_ydraw_core_target *);

/* Viewport - position and size */
struct yetty_yrender_viewport {
    float x, y, w, h;
};

/*=============================================================================
 * Render target - unified abstraction for rendering
 *
 * Used at multiple levels:
 * - Layer level: render_layer() renders a terminal layer to texture
 * - Terminal level: blend() composites layer targets together
 * - Yetty level: blend() composites terminal targets with viewport/scissor
 * - Final level: present() outputs to surface/vnc/ymux
 *===========================================================================*/

struct yetty_yrender_target_ops {
    void (*destroy)(struct yetty_ydraw_core_target *self);

    /* Clear the target to a solid color */
    struct yetty_ycore_void_result (*clear)(struct yetty_ydraw_core_target *self);

    /* Render single terminal layer to this target */
    struct yetty_ycore_void_result (*render_layer)(struct yetty_ydraw_core_target *self,
                                                   struct yetty_yrender_terminal_layer *layer);

    /* Blend multiple source targets into this target */
    struct yetty_ycore_void_result (*blend)(struct yetty_ydraw_core_target *self,
                                            struct yetty_ydraw_core_target **sources,
                                            size_t count);

    /* Present this target's content to final destination (surface/vnc/ymux) */
    struct yetty_ycore_void_result (*present)(struct yetty_ydraw_core_target *self);

    /* Get texture view for blending */
    WGPUTextureView (*get_view)(const struct yetty_ydraw_core_target *self);

    /* Get texture for VNC/other use */
    WGPUTexture (*get_texture)(const struct yetty_ydraw_core_target *self);

    /* Resize/reposition the target */
    struct yetty_ycore_void_result (*resize)(struct yetty_ydraw_core_target *self,
                                             struct yetty_yrender_viewport viewport);

    /* Apply a non-intrusive visual zoom to the next blend() into this target.
	 * scale = 1.0 disables zoom; scale > 1.0 zooms in.
	 * offset_{x,y} are in source pixels within the target. Optional op — may
	 * be NULL for targets that don't composite layers. */
    struct yetty_ycore_void_result (*set_visual_zoom)(struct yetty_ydraw_core_target *self,
                                                      float scale, float offset_x, float offset_y);

    /* Hint that the next present() must produce a full-window repaint even
	 * if the GPU content is unchanged. Called by the event loop on X11
	 * Expose / window-refresh so damage-aware targets (X11-tile) re-ship
	 * every tile — otherwise their delta would be empty and the window
	 * stays broken after being uncovered. Optional — NULL means "the
	 * target's present() already repaints everything unconditionally". */
    void (*refresh_full)(struct yetty_ydraw_core_target *self);

    /* Optional: return true if the target currently can't accept a new
	 * render (e.g. an async readback is still in flight). When true, the
	 * render loop skips the WHOLE pipeline — clear + workspace_render +
	 * present — because otherwise the layer renders would submit GPU work
	 * that feeds a present() we'd only drop anyway. The target is
	 * responsible for asking for a catch-up render once it's free again.
	 * NULL means "always ready". */
    bool (*is_busy)(const struct yetty_ydraw_core_target *self);

    /* Called when the render loop skipped a pipeline because is_busy() was
	 * true. The target must remember to fire a catch-up render once it
	 * becomes idle — without this the skip is silent and the display
	 * lags behind by one event. NULL iff is_busy is NULL. */
    void (*notify_render_skipped)(struct yetty_ydraw_core_target *self);
};

/* Render target base - embed as first member in subclasses */
struct yetty_ydraw_core_target {
    const struct yetty_yrender_target_ops *ops;
    struct yetty_yrender_viewport viewport;
};

/*=============================================================================
 * Texture render target - renders to GPU texture
 *
 * Used for:
 * - Layer targets (render_layer) - surface=NULL
 * - Terminal compositing target (blend layers) - surface=NULL
 * - Big yetty texture (blend terminals + present) - surface provided
 *
 * If surface is provided, present() blits to it.
 * If surface is NULL, present() returns error.
 *===========================================================================*/

struct yetty_yrender_target_ptr_result yetty_yrender_target_texture_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ydraw_core_gpu_allocator *allocator,
    WGPUSurface surface, /* NULL for layer/terminal targets */
    struct yetty_yrender_viewport viewport);

/*=============================================================================
 * VNC render target - renders to texture and sends to VNC clients
 *
 * Wraps a texture render target:
 * - clear/render_layer/blend delegate to inner texture target
 * - present() sends frame to VNC server, optionally also to surface (mirror)
 *
 * For headless mode: surface=NULL, only sends to VNC
 * For mirror mode: surface provided, sends to VNC AND presents to surface
 *===========================================================================*/

struct yetty_yvnc_server;

struct yetty_yrender_target_ptr_result yetty_yrender_target_vnc_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ydraw_core_gpu_allocator *allocator,
    WGPUSurface surface, /* NULL for headless, non-NULL for mirror */
    struct yetty_yvnc_server *vnc_server, struct yetty_yrender_viewport viewport);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRENDER_TARGET_H */
