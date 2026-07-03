/*
 * yframework/yframework.h — generic GPU/event/RPC services layer.
 *
 * yframework sits between the platform (window, surface, config, pipes — a
 * yplatform:platform object) and an app (yetty terminal, an analyzer/test
 * harness, etc.). Given a populated platform object, yframework requests the
 * WebGPU adapter+device+queue, picks the surface format and present mode,
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
 * Build order:  platform -> yframework -> app.
 * Teardown:     app -> yframework -> platform.
 */

#ifndef YETTY_YFRAMEWORK_YFRAMEWORK_H
#define YETTY_YFRAMEWORK_YFRAMEWORK_H

#include <yetty/ycore/result.h>
#include <yetty/yetty/yetty.h>
#include <webgpu/webgpu.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yconfig_config;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yclass_object;
struct yetty_yevent_event_loop;
struct yetty_yplatform_wgpu;
struct yetty_yvnc_server;
struct yetty_yctl_server;
struct yetty_ydraw_target;
struct yetty_yfigure_registry;
struct yetty_context;
/* Opaque holder for per-kind factory args bundles (ymgui pipeline cache,
 * future: yrdawn/ygui/ygrid bundles). Defined in yframework.c so the
 * header doesn't pull in any kind-specific types. */
struct yetty_yframework_factory_state;

struct yetty_yframework {
    /* Borrowed from the platform object; not owned. yframework_destroy leaves
     * these alone — the platform's teardown frees them. */
    struct yetty_yconfig_config *config;
    struct yetty_ycore_xthread_event_pipe *platform_input_pipe;
    struct yetty_ycore_xthread_event_pipe *output_pipe;
    /* yplatform:clipboard yclass object (or NULL). Borrowed from the platform. */
    struct yetty_yclass_object *clipboard;
    /* yplatform:window_chrome yclass object (or NULL). Borrowed from the platform. */
    struct yetty_yclass_object *window_chrome;

    /* Adapter + device + queue + surface_format + allocator + msdf_generator,
     * plus the embedded yplatform_gpu_context with instance/surface/dims/x11. */
    struct yetty_yframework_gpu_context gpu;

    /* Selected from surface capabilities + `rendering/present-mode`
     * config key. Re-used on RESIZE so wgpuSurfaceConfigure stays
     * consistent. */
    WGPUPresentMode present_mode;

    /* Owned. Destroyed in reverse-creation order by yframework_destroy. */
    struct yetty_yevent_event_loop *event_loop;
    struct yetty_yplatform_wgpu *wgpu;    /* coroutine-aware wgpu await */
    struct yetty_yvnc_server *vnc_server; /* NULL when vnc config off */
    struct yetty_yctl_server *rpc_server; /* NULL when rpc/port unset */
    struct yetty_ydraw_target *render_target;

    /* Per-kind factory args bundles (e.g. ymgui's lazy-pipeline cache).
     * The struct is private to yframework.c; consumers only see this
     * pointer + the register_figure_factories entry point. */
    struct yetty_yframework_factory_state *factory_state;

    /* Shared per-frame animation clock. Stamped once at the top of each
     * render frame by yetty_yframework_frame_tick(), so every layer, figure
     * and effect shader rendered in that frame writes the identical value
     * into its own time uniform. Effects therefore stay phase-coherent
     * across all shaders instead of each ticking from its own creation
     * origin. Value is seconds since framework creation (zero-based). */
    double frame_origin_sec; /* monotonic time captured at create        */
    double frame_time_sec;   /* now - frame_origin_sec, updated per frame */
    double frame_delta_sec;  /* seconds since the previous frame tick     */
};

YETTY_YRESULT_DECLARE(yetty_yframework_ptr, struct yetty_yframework *);

/* Advance the shared per-frame animation clock. Call exactly once per frame,
 * before any layer/figure renders, so all time uniforms sampled during the
 * frame observe the same value. Safe to call with a NULL rt (no-op). */
void yetty_yframework_frame_tick(struct yetty_yframework *rt);

/* Build the runtime from a populated yplatform:platform object. On success the
 * returned pointer owns everything in the "owned" block above plus the
 * service objects it created (adapter, device, allocator, ...). */
struct yetty_yframework_ptr_result yetty_yframework_create(struct yetty_yclass_object *platform);

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
 * When `surface` is non-NULL, also dumps the surface's supported texture
 * formats (preferred first) and present modes — useful for tracking down
 * per-platform asymmetries (e.g. Metal returning a *Srgb format first
 * while Vulkan returns *Unorm).
 *
 * Safe to call any time after the adapter is up. Errors: NULL adapter,
 * adapter-info-fetch failure (the WebGPU call surface). */
struct yetty_ycore_void_result yetty_yframework_log_gpu_info(WGPUAdapter adapter,
                                                             WGPUSurface surface);

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

/* Per-host accessor for the yrdawn factory args. Host installs its
 * own emit_osc / request_render before calling
 * register_figure_factories. The pointer is owned by `framework`; do
 * not free. Errors: NULL framework, this build has no yrdawn server
 * compiled in (webasm — distinguishable from a runtime failure by the
 * error msg). */
struct yetty_yrdawn_factory_args;
YETTY_YRESULT_DECLARE(yetty_yrdawn_factory_args_ptr, struct yetty_yrdawn_factory_args *);
struct yetty_yrdawn_factory_args_ptr_result yetty_yframework_factory_args_yrdawn(
    struct yetty_yframework *framework);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFRAMEWORK_YFRAMEWORK_H */
