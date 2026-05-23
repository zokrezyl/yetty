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
struct yetty_yplatform_window_manager;
struct yetty_yevent_event_loop;
struct yetty_ydraw_gpu_allocator;
struct yetty_ymsdf_generator;
struct yetty_yframework;

/* Platform-supplied GPU bindings created by yinit. Embedded by value in
 * yetty_yframework_gpu_context so consumers can pass &gpu.app_gpu_context
 * by pointer to anything that wants the platform-level slice. */
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
     * every other platform. yframework reads these only when picking the
     * X11-tile render target — see yetty/yframework/yframework.c. */
    void *x11_display;        /* Display * */
    unsigned long x11_window; /* Window (XID) */
};

/* Runtime-owned GPU objects, built on top of the platform slice above.
 * Created by yetty_yframework_create; lives on struct yetty_yframework. */
struct yetty_yframework_gpu_context {
    struct yetty_yinit_gpu_context app_gpu_context;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat surface_format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Polymorphic MSDF CDB generator (cpu | gpu). Selected from the
     * `msdf/generator` config key by yetty_yframework_create after the
     * WGPU device is up. Shared by every consumer that materialises a
     * font on the fly (today: ydraw-canvas blob-font materialisation). */
    struct yetty_ymsdf_generator *msdf_generator;
};

/* The context propagated down the terminal hierarchy.
 *   runtime     — generic GPU/event/RPC services layer that ymain handed
 *                 us via yetty_yframework_create. Source of truth for
 *                 adapter/device/queue/allocator/msdf, render target,
 *                 wgpu await machinery, optional VNC + RPC servers,
 *                 config, platform input pipe, clipboard + window
 *                 managers.
 *   pty_factory — yetty-specific, supplied by ymain alongside the
 *                 runtime; consumed by terminal creation.
 *   event_loop  — convenience alias of runtime->event_loop. Hot paths
 *                 use it instead of digging through runtime each time.
 */
struct yetty_context {
    struct yetty_yframework              *runtime;
    struct yetty_yplatform_pty_factory *pty_factory;
    struct yetty_yevent_event_loop     *event_loop;
};

/* Result type */
YETTY_YRESULT_DECLARE(yetty_yetty_yetty, struct yetty_yetty_yetty *);

/* Create yetty instance. Both inputs are required and borrowed:
 *
 *   runtime     — outlives the returned yetty (caller tears yetty down
 *                 first, then yetty_yframework_destroy).
 *   pty_factory — destroyed by the caller after yetty_destroy.
 */
struct yetty_yetty_yetty_result yetty_create(struct yetty_yframework *runtime,
                                             struct yetty_yplatform_pty_factory *pty_factory);

/* Destroy yetty instance */
struct yetty_ycore_void_result yetty_destroy(struct yetty_yetty_yetty *yetty);

/* Run yetty (main loop integration) */
struct yetty_ycore_void_result yetty_run(struct yetty_yetty_yetty *yetty);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YETTY_YETTY_H */
