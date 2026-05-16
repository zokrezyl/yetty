#ifndef YETTY_YETTY_YETTY_H
#define YETTY_YETTY_YETTY_H

#include <yetty/ycore/result.h>
#include <webgpu/webgpu.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct yetty_yetty_yetty;
struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;
struct yetty_platform_clipboard_manager;
struct yetty_yplatform_pty_factory;
struct yetty_yevent_event_loop;
struct yetty_ydraw_gpu_allocator;
struct yetty_ymsdf_generator;

/* App GPU context - platform-owned GPU objects */
struct yetty_yetty_app_gpu_context {
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
     * every other platform. yetty uses these only when the X11-tile render
     * target is selected — see yetty_log_gpu_info / initWebGPU. */
    void *x11_display;        /* Display * */
    unsigned long x11_window; /* Window (XID) */
};

/* App context - passed from platform main to yetty_create */
struct yetty_yplatform_window_manager;

struct yetty_yetty_app_context {
    struct yetty_yetty_app_gpu_context app_gpu_context;
    struct yetty_yconfig_config *config;
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe;
    struct yetty_platform_clipboard_manager *clipboard_manager;
    /* Owned by glfw.c (main thread). Producer ops are thread-safe; the
     * tabbar calls them on its render-thread mouse-down/drag handlers
     * to ask the OS window for iconify / maximize-toggle / close /
     * drag_by. NULL in headless mode. */
    struct yetty_yplatform_window_manager *window_manager;
    struct yetty_yplatform_pty_factory *pty_factory;
};

/* Yetty GPU context - yetty-owned GPU objects */
struct yetty_yetty_gpu_context {
    struct yetty_yetty_app_gpu_context app_gpu_context;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Polymorphic MSDF CDB generator (cpu | gpu). Selected from the
     * `msdf/generator` config key in yetty_create after the WGPU device
     * is up. Shared by every consumer that materialises a font on the
     * fly (today: ydraw-canvas blob-font materialisation). */
    struct yetty_ymsdf_generator *msdf_generator;
};

/* Yetty context - passed down the hierarchy to terminals */
struct yetty_context {
    struct yetty_yetty_app_context app_context;
    struct yetty_yetty_gpu_context gpu_context;
    struct yetty_yevent_event_loop *event_loop;
};

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yetty_yetty, struct yetty_yetty_yetty *);

/* Create yetty instance */
struct yetty_yetty_yetty_result yetty_create(const struct yetty_yetty_app_context *app_context);

/* Destroy yetty instance */
struct yetty_ycore_void_result yetty_destroy(struct yetty_yetty_yetty *yetty);

/* Run yetty (main loop integration) */
struct yetty_ycore_void_result yetty_run(struct yetty_yetty_yetty *yetty);

/* Dump WebGPU adapter info (vendor, backend, adapter type, IDs, key limits)
 * via yinfo. Safe to call any time after the adapter is available — used at
 * startup and can be re-invoked for diagnostics (e.g. when a GPU error
 * occurs or on demand via a debug command). */
void yetty_log_gpu_info(WGPUAdapter adapter);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YETTY_YETTY_H */
