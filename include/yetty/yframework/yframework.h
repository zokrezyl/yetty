/*
 * yframework/yframework.h — generic GPU/event/RPC services layer.
 *
 * yframework sits between yinit (platform: window, surface, config, pipes)
 * and an app (yetty terminal, an analyzer/test harness, etc.). Given a
 * fully-populated `yetty_yinit_runtime`, yframework requests the WebGPU
 * adapter+device+queue, picks the surface format and present mode,
 * configures the surface, builds the GPU allocator, the MSDF generator,
 * the optional VNC and RPC servers, the event loop, the coroutine-aware
 * wgpu await machinery, and the render target.
 *
 * The result is a single `yetty_yframework` struct the app borrows. The
 * app's job is just the app-specific bits (terminals + tabbar for yetty;
 * a single test view for an analyzer); the entire WebGPU bring-up is
 * shared.
 *
 * Lifetime rule: the app's lifetime is strictly nested inside yframework's.
 * Build order:  yinit -> yframework -> app.
 * Teardown:     app -> yframework -> yinit.
 */

#ifndef YETTY_YFRAMEWORK_YFRAMEWORK_H
#define YETTY_YFRAMEWORK_YFRAMEWORK_H

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
struct yetty_yfigure_registry;
struct yetty_context;
/* Opaque holder for per-kind factory args bundles (ymgui pipeline cache,
 * future: yrdawn/ygui/ygrid bundles). Defined in yframework.c so the
 * header doesn't pull in any kind-specific types. */
struct yetty_yframework_factory_state;

struct yetty_yframework {
    /* Borrowed from yinit_runtime; not owned. yframework_destroy leaves
     * these alone — yinit's teardown frees them. */
    struct yetty_yconfig_config *config;
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe;
    struct yetty_ycore_xthread_event_pipe *output_pipe;
    struct yetty_platform_clipboard_manager *clipboard_manager;
    struct yetty_yplatform_window_manager *window_manager;

    /* Adapter + device + queue + surface_format + allocator + msdf_generator,
     * plus the embedded yinit_gpu_context with instance/surface/dims/x11. */
    struct yetty_yframework_gpu_context gpu;

    /* Selected from surface capabilities + `rendering/present-mode`
     * config key. Re-used on RESIZE so wgpuSurfaceConfigure stays
     * consistent. */
    WGPUPresentMode present_mode;

    /* Owned. Destroyed in reverse-creation order by yframework_destroy. */
    struct yetty_yevent_event_loop *event_loop;
    struct yetty_yplatform_wgpu *wgpu;    /* coroutine-aware wgpu await */
    struct yetty_yvnc_server *vnc_server; /* NULL when vnc config off */
    struct yetty_yrpc_server *rpc_server; /* NULL when rpc/port unset */
    struct yetty_ydraw_target *render_target;

    /* Per-kind factory args bundles (e.g. ymgui's lazy-pipeline cache).
     * The struct is private to yframework.c; consumers only see this
     * pointer + the register_figure_factories entry point. */
    struct yetty_yframework_factory_state *factory_state;
};

YETTY_YRESULT_DECLARE(yetty_yframework_ptr, struct yetty_yframework *);

/* Build the runtime from a fully-populated yinit_runtime. On success the
 * returned pointer owns everything in the "owned" block above plus the
 * service objects it created (adapter, device, allocator, ...). */
struct yetty_yframework_ptr_result yetty_yframework_create(
    const struct yetty_yinit_runtime *yinit_rt);

/* Tear down in reverse-creation order:
 *   render_target -> wgpu -> event_loop -> vnc -> rpc -> msdf -> allocator
 *   -> surface unconfigure -> queue -> device -> adapter.
 *
 * The caller (app) MUST have destroyed itself first — render_target may
 * still hold pending readbacks whose callbacks dereference wgpu+event_loop,
 * so those two must outlive every consumer of GPU resources.
 */
struct yetty_ycore_void_result yetty_yframework_destroy(struct yetty_yframework *rt);

/* Dump adapter info (vendor, backend, type, IDs, key limits) via yinfo.
 * Safe to call any time after the adapter is up. */
void yetty_yframework_log_gpu_info(WGPUAdapter adapter);

/* Reconfigure surface after a window resize. Called from the app's RESIZE
 * handler — yframework keeps the present_mode the initial capability scan
 * chose, so the swapchain doesn't silently flip back to Fifo. */
struct yetty_ycore_void_result yetty_yframework_reconfigure_surface(struct yetty_yframework *rt,
                                                                    uint32_t width,
                                                                    uint32_t height);

/* Register every figure kind the framework ships (ymgui today; yrdawn /
 * ygui / ygrid sub-kinds as they migrate) onto the given registry.
 *
 * Hosts (yterm, yui, …) own their own root containers but share the
 * framework's set of factories — they each create a registry, hand it
 * here, and from that point on the registry resolves any kind the
 * framework knows about. The args bundles live on `framework->factory_state`,
 * so a single yframework can populate any number of registries.
 *
 * `context` is the yetty_context the framework propagates to factories
 * at mint time. Borrowed; the caller must keep it alive at least until
 * every figure minted via this registry is destroyed. */
struct yetty_ycore_void_result yetty_yframework_register_figure_factories(
    struct yetty_yframework *framework, struct yetty_yfigure_registry *registry,
    const struct yetty_context *context);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFRAMEWORK_YFRAMEWORK_H */
