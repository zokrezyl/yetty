/*
 * yruntime/yruntime.h — generic GPU/event/RPC services layer.
 *
 * yruntime sits between yinit (platform: window, surface, config, pipes)
 * and an app (yetty terminal, an analyzer/test harness, etc.). Given a
 * fully-populated `yetty_yinit_runtime`, yruntime requests the WebGPU
 * adapter+device+queue, picks the surface format and present mode,
 * configures the surface, builds the GPU allocator, the MSDF generator,
 * the optional VNC and RPC servers, the event loop, the coroutine-aware
 * wgpu await machinery, and the render target.
 *
 * The result is a single `yetty_yruntime` struct the app borrows. The
 * app's job is just the app-specific bits (terminals + tabbar for yetty;
 * a single test view for an analyzer); the entire WebGPU bring-up is
 * shared.
 *
 * Lifetime rule: the app's lifetime is strictly nested inside yruntime's.
 * Build order:  yinit -> yruntime -> app.
 * Teardown:     app -> yruntime -> yinit.
 */

#ifndef YETTY_YRUNTIME_YRUNTIME_H
#define YETTY_YRUNTIME_YRUNTIME_H

#include <yetty/ycore/result.h>
#include <yetty/yetty/yetty.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yinit_runtime;
struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;
struct yetty_platform_clipboard_manager;
struct yetty_yplatform_window_manager;
struct yetty_yevent_event_loop;
struct yetty_yplatform_wgpu;
struct yetty_yvnc_server;
struct yetty_yrpc_server;
struct yetty_ydraw_target;

struct yetty_yruntime {
    /* Borrowed from yinit_runtime; not owned. yruntime_destroy leaves
     * these alone — yinit's teardown frees them. */
    struct yetty_yconfig_config             *config;
    struct yetty_ycore_xthread_event_pipe   *platform_input_pipe;
    struct yetty_ycore_xthread_event_pipe   *output_pipe;
    struct yetty_platform_clipboard_manager *clipboard_manager;
    struct yetty_yplatform_window_manager   *window_manager;

    /* Adapter + device + queue + surface_format + allocator + msdf_generator,
     * plus the embedded yinit_gpu_context with instance/surface/dims/x11. */
    struct yetty_yruntime_gpu_context        gpu;

    /* Selected from surface capabilities + `rendering/present-mode`
     * config key. Re-used on RESIZE so wgpuSurfaceConfigure stays
     * consistent. */
    WGPUPresentMode                          present_mode;

    /* Owned. Destroyed in reverse-creation order by yruntime_destroy. */
    struct yetty_yevent_event_loop  *event_loop;
    struct yetty_yplatform_wgpu     *wgpu;          /* coroutine-aware wgpu await */
    struct yetty_yvnc_server        *vnc_server;    /* NULL when vnc config off */
    struct yetty_yrpc_server        *rpc_server;    /* NULL when rpc/port unset */
    struct yetty_ydraw_target       *render_target;
};

YETTY_YRESULT_DECLARE(yetty_yruntime_ptr, struct yetty_yruntime *);

/* Build the runtime from a fully-populated yinit_runtime. On success the
 * returned pointer owns everything in the "owned" block above plus the
 * service objects it created (adapter, device, allocator, ...). */
struct yetty_yruntime_ptr_result yetty_yruntime_create(
    const struct yetty_yinit_runtime *yinit_rt);

/* Tear down in reverse-creation order:
 *   render_target -> wgpu -> event_loop -> vnc -> rpc -> msdf -> allocator
 *   -> surface unconfigure -> queue -> device -> adapter.
 *
 * The caller (app) MUST have destroyed itself first — render_target may
 * still hold pending readbacks whose callbacks dereference wgpu+event_loop,
 * so those two must outlive every consumer of GPU resources.
 */
struct yetty_ycore_void_result yetty_yruntime_destroy(struct yetty_yruntime *rt);

/* Dump adapter info (vendor, backend, type, IDs, key limits) via yinfo.
 * Safe to call any time after the adapter is up. */
void yetty_yruntime_log_gpu_info(WGPUAdapter adapter);

/* Reconfigure surface after a window resize. Called from the app's RESIZE
 * handler — yruntime keeps the present_mode the initial capability scan
 * chose, so the swapchain doesn't silently flip back to Fifo. */
struct yetty_ycore_void_result yetty_yruntime_reconfigure_surface(
    struct yetty_yruntime *rt, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRUNTIME_YRUNTIME_H */
