/*
 * yplatform/gpu-context.h — platform-supplied GPU bring-up parameters.
 *
 * The platform layer (yplatform:platform subclasses) brings up the WebGPU
 * instance + surface and captures the window geometry, then hands these to
 * yframework, which performs the complex GPU setup (adapter / device / queue /
 * allocator / MSDF / render target). This POD is that handoff slice — the
 * *inputs* to the framework's GPU setup. It is held on the platform object and
 * read back through the platform's accessors; yframework copies it by value
 * into its own yetty_yframework_gpu_context.
 */

#ifndef YETTY_YPLATFORM_GPU_CONTEXT_H
#define YETTY_YPLATFORM_GPU_CONTEXT_H

#include <webgpu/webgpu.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_gpu_context {
    WGPUInstance instance;
    WGPUSurface surface;
    uint32_t surface_width;
    uint32_t surface_height;

    /* HiDPI scale = framebuffer_size / window_size, captured at startup
     * (default 1.0 on non-HiDPI). Pure read-only knob: anything that
     * loads a size in CSS-pixel-ish units from the user's config (font
     * size, padding) multiplies by this to render at framebuffer
     * resolution. Rendering, hit-tests, render-target dimensions etc.
     * are already in framebuffer pixels and ignore this field. */
    float content_scale;

    /* Optional X11 native handles. Populated by the platform layer on
     * Linux/X11 (opaque here to keep Xlib out of this header); NULL / 0 on
     * every other platform. yframework reads these only when picking the
     * X11-tile render target — see yetty/yframework/yframework.c. */
    void *x11_display;        /* Display * */
    unsigned long x11_window; /* Window (XID) */
};

/* Convert a logical (CSS-pixel-ish) dimension to framebuffer pixels for
 * the given platform GPU context. The contract on `content_scale` (see
 * the comment above) is that every hardcoded chrome dimension — tab
 * strip height, titlebar buttons, splitter thickness, font sizes — is
 * authored in logical units and multiplied through this helper at the
 * use site so HiDPI / Retina displays render at the correct physical
 * size. NULL ctx or non-positive scale falls back to 1.0f, matching the
 * "platform without HiDPI" path. */
static inline float yetty_dp_to_px(const struct yetty_yplatform_gpu_context *gpu, float logical_px)
{
    float s = (gpu && gpu->content_scale > 0.0f) ? gpu->content_scale : 1.0f;
    return logical_px * s;
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_GPU_CONTEXT_H */
