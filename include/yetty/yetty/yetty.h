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
struct yetty_yruntime;

/* App GPU context - platform-owned GPU objects */
struct yetty_yinit_gpu_context {
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
     * every other platform. yruntime reads these only when picking the
     * X11-tile render target — see yetty/yruntime/yruntime.c. */
    void *x11_display;        /* Display * */
    unsigned long x11_window; /* Window (XID) */
};

/* App context - passed from platform main to yetty_create */
struct yetty_yplatform_window_manager;

struct yetty_yetty_app_context {
    struct yetty_yinit_gpu_context app_gpu_context;
    struct yetty_yconfig_config *config;
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe;
    struct yetty_platform_clipboard_manager *clipboard_manager;
    /* Owned by glfw.c (main thread). Producer ops are thread-safe; the
     * tabbar calls them on its render-thread mouse-down/drag handlers
     * to ask the OS window for iconify / maximize-toggle / close /
     * drag_by. NULL in headless mode. */
    struct yetty_yplatform_window_manager *window_manager;
    struct yetty_yplatform_pty_factory *pty_factory;
    /* Generic GPU/event/RPC services layer below the yetty app. Holds
     * adapter+device+queue+allocator+msdf, the event loop, the wgpu
     * await machinery, the render target, plus optional VNC+RPC
     * servers. Created by ymain via yetty_yruntime_create, lifetime
     * outlives this yetty instance. yetty borrows everything through
     * this pointer. */
    struct yetty_yruntime *runtime;
};

/* Yetty GPU context - yetty-owned GPU objects */
struct yetty_yruntime_gpu_context {
    struct yetty_yinit_gpu_context app_gpu_context;
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
    struct yetty_yruntime_gpu_context gpu_context;
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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YETTY_YETTY_H */
