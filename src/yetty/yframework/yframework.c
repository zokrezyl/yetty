/*
 * yframework.c — lifts the WebGPU / event-loop / VNC / RPC / render-target
 * bring-up out of the yetty terminal so the same bring-up serves any app
 * that wants a window + GPU + event loop. The actual logic is the same
 * one yetty/yetty.c carried as the static init_webgpu(); the deltas are:
 *
 *   - inputs come from a yplatform:platform object (not an app-context bundle),
 *   - the result is an owned struct yetty_yframework (not stamped onto a
 *     yetty struct),
 *   - the event_loop + wgpu await machinery + RPC server live here too,
 *     since they were already needed by VNC/render-target bring-up and
 *     are equally generic.
 */

#include <yetty/yframework/yframework.h>

#include <yetty/yplatform/gpu-context.h>
#include <yetty/yplatform/time.h>
#include <yetty/yplatform/yplatform/platform.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/render-target.h>
#ifdef YETTY_HAS_X11_TILE
#include <yetty/yrender/render-target-x11-tile.h>
#endif
#include <yetty/ywebgpu/utils.h>
#include <yetty/ywebgpu/limits.h>
#include <yetty/ywebgpu/request.h>
#include <yetty/webgpu/error.h>
#include <yetty/yplatform/ywebgpu.h>
#include <yetty/ymsdf/generator.h>
#include <yetty/yvnc/vnc-server.h>
#include <yetty/yctl/rpc-server.h>
#include <yetty/yconfig/config.h>
#include <yetty/ytrace/ytrace.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif
#include <yetty/yfigure/registry.h>
#include <yetty/yshadertoy/figure.h>
#ifdef YETTY_HAS_YMGUI
#include <yetty/ymgui/figure.h>
#endif
#ifndef __EMSCRIPTEN__
#include <yetty/yrdawn/figure.h>
#define YETTY_HAS_YRDAWN_SERVER 1
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

/*===========================================================================
 * Figure-kind factory args
 *
 * Each figure kind exports its own `register_factory(registry, &args)`
 * call. The args bundle (e.g. ymgui's lazy pipeline cache) must outlive
 * every figure minted from any registry it was registered on. yframework
 * is the natural owner — it sits below every host (yterm, yui, …) and
 * outlives them all. The bundle is kept opaque to yframework.h so the
 * header doesn't acquire kind-specific dependencies.
 *=========================================================================*/

struct yetty_yframework_factory_state {
#ifdef YETTY_HAS_YMGUI
    struct yetty_ymgui_factory_args ymgui_args;
#endif
#ifdef YETTY_HAS_YRDAWN_SERVER
    /* yrdawn — per-host outbound callbacks (emit_osc / request_render)
     * are NOT set by yframework itself: the host (terminal, yui, the
     * compositor tool) installs them via
     * yetty_yframework_factory_args_yrdawn() before calling
     * register_figure_factories. */
    struct yetty_yrdawn_factory_args yrdawn_args;
#endif
    /* Future kinds: ygui_args, ygrid_args ... */
    int _unused; /* keep the struct non-empty when no kinds compile in */
};

struct yetty_ycore_void_result yetty_yframework_log_gpu_info(WGPUAdapter adapter,
                                                             WGPUSurface surface)
{
    if (!adapter) {
        return YETTY_ERR(yetty_ycore_void, "log_gpu_info: adapter is NULL");
    }

    char *desc = yetty_ywebgpu_get_webgpu_description(adapter, surface);
    if (!desc) {
        return YETTY_ERR(yetty_ycore_void,
                         "log_gpu_info: yetty_ywebgpu_get_webgpu_description failed");
    }
    yinfo("WebGPU adapter description:\n%s", desc);
    free(desc);
    return YETTY_OK_VOID();
}

void yetty_yframework_frame_tick(struct yetty_yframework *rt)
{
    if (!rt) {
        return;
    }
    double previous_sec = rt->frame_time_sec;
    rt->frame_time_sec = yetty_yplatform_ytime_monotonic_sec() - rt->frame_origin_sec;
    rt->frame_delta_sec = rt->frame_time_sec - previous_sec;
}

/* ------------------------------------------------------------------ *
 * Adapter / backend selection
 *
 * The default is unchanged: Dawn auto-picks the OS-best backend (D3D12 on
 * Windows, Metal on macOS, Vulkan on Linux) and the highest-performance
 * HARDWARE adapter. That is exactly what every real GPU wants and it is
 * left alone. Two opt-in adjustments sit on top, neither of which touches
 * the real-GPU path:
 *
 *   1. $YETTY_WEBGPU_BACKEND=<d3d11|d3d12|vulkan|metal|opengl|opengles>
 *      forces a specific backend. Handy on VMs whose virtual GPU only
 *      accelerates an older API. Unset / "auto" => Dawn auto-picks.
 *   2. If Dawn returns a CPU/software adapter (its WARP fallback) — which
 *      happens on VMware/Hyper-V whose virtual GPU advertises D3D12 but
 *      can't create a real D3D12 device — retry once on D3D11, which those
 *      GPUs DO accelerate. A real GPU never returns a CPU adapter, so this
 *      branch is inert on real hardware.
 * ------------------------------------------------------------------ */

/* ASCII case-insensitive equality (no locale/ctype dependency). */
static int yframework_ci_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
    }
    return *a == *b;
}

/* Map $YETTY_WEBGPU_BACKEND to a WGPUBackendType. Returns
 * WGPUBackendType_Undefined when unset or unrecognised — which tells Dawn to
 * auto-pick, i.e. the default behaviour on every real GPU. */
static WGPUBackendType yframework_env_backend(void)
{
    const char *b = getenv("YETTY_WEBGPU_BACKEND");
    if (!b || !*b || yframework_ci_eq(b, "auto")) {
        return WGPUBackendType_Undefined;
    }
    if (yframework_ci_eq(b, "d3d11") || yframework_ci_eq(b, "dx11")) {
        return WGPUBackendType_D3D11;
    }
    if (yframework_ci_eq(b, "d3d12") || yframework_ci_eq(b, "dx12")) {
        return WGPUBackendType_D3D12;
    }
    if (yframework_ci_eq(b, "vulkan") || yframework_ci_eq(b, "vk")) {
        return WGPUBackendType_Vulkan;
    }
    if (yframework_ci_eq(b, "metal")) {
        return WGPUBackendType_Metal;
    }
    if (yframework_ci_eq(b, "opengl") || yframework_ci_eq(b, "gl")) {
        return WGPUBackendType_OpenGL;
    }
    if (yframework_ci_eq(b, "opengles") || yframework_ci_eq(b, "gles")) {
        return WGPUBackendType_OpenGLES;
    }
    ywarn("yframework: unknown YETTY_WEBGPU_BACKEND='%s'; letting Dawn auto-pick", b);
    return WGPUBackendType_Undefined;
}

/* Non-zero if `adapter` is a software rasterizer (Dawn's WARP fallback).
 *
 * Two signals, because Dawn's D3D12 WARP adapter is inconsistent: it reports
 * adapterType as IntegratedGPU (not CPU!), but always carries Microsoft's
 * software-renderer PCI vendor ID 0x1414. No real GPU uses 0x1414 (NVIDIA
 * 0x10DE, AMD 0x1002, Intel 0x8086, VMware 0x15AD, Qualcomm 0x17CB…), so this
 * never false-positives on real hardware. */
#define YFRAMEWORK_VENDOR_MICROSOFT_SOFTWARE 0x1414u

static int yframework_adapter_is_software(WGPUAdapter adapter)
{
    if (!adapter) {
        return 0;
    }
    WGPUAdapterInfo info = {0};
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
        return 0; /* unknown → assume it's fine, don't second-guess */
    }
    int is_software = (info.adapterType == WGPUAdapterType_CPU) ||
                      (info.vendorID == YFRAMEWORK_VENDOR_MICROSOFT_SOFTWARE);
    wgpuAdapterInfoFreeMembers(info);
    return is_software;
}

/* Non-zero if `adapter` is specifically WARP (vendor 0x1414). Unlike
 * yframework_adapter_is_software this stays zero for bundled lavapipe
 * (Mesa, vendor 0x10005, adapterType CPU) — a software adapter we *want*:
 * its LLVM JIT is much faster than WARP for yetty's workload, so the
 * recovery chain must not try to recover from it. */
static int yframework_adapter_is_warp(WGPUAdapter adapter)
{
    if (!adapter) {
        return 0;
    }
    WGPUAdapterInfo info = {0};
    if (wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success) {
        return 0;
    }
    int is_warp = (info.vendorID == YFRAMEWORK_VENDOR_MICROSOFT_SOFTWARE);
    wgpuAdapterInfoFreeMembers(info);
    return is_warp;
}


/* Request an adapter synchronously. Native Dawn fires the callback inline
 * during the request; the emscripten path spins the loop. Returns NULL on
 * failure (e.g. a forced backend that isn't compiled in). */
static WGPUAdapter yframework_request_adapter(WGPUInstance instance,
                                              const WGPURequestAdapterOptions *opts)
{
    WGPUAdapter adapter = NULL;
    int ready = 0;
    WGPURequestAdapterCallbackInfo cb = {0};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = yetty_ywebgpu_adapter_request_callback;
    cb.userdata1 = &adapter;
    cb.userdata2 = &ready;
    wgpuInstanceRequestAdapter(instance, opts, cb);
#ifdef __EMSCRIPTEN__
    while (!ready) {
        emscripten_sleep(0);
    }
#endif
    (void)ready;
    return adapter;
}

/* Lifted verbatim from yetty/yetty.c init_webgpu(): request adapter +
 * device + queue, pick surface format and present mode from caps + config,
 * configure the surface, build the GPU allocator and MSDF generator,
 * spin up VNC if requested, and create the render target (vnc / x11-tile
 * / texture, chosen the same way the terminal used to). All side effects
 * land on `rt`. */
static struct yetty_ycore_void_result init_gpu(struct yetty_yframework *rt, WGPUInstance instance,
                                               WGPUSurface surface)
{
    if (!instance) {
        return YETTY_ERR(yetty_ycore_void, "yframework: no WebGPU instance");
    }
    ydebug("yframework: instance=%p surface=%p", (void *)instance, (void *)surface);

    /* Adapter. Default (Undefined backend) = Dawn auto-picks the OS-best
     * backend + best hardware adapter — untouched for real GPUs. */
    WGPUBackendType forced_backend = yframework_env_backend();
    WGPURequestAdapterOptions adapter_opts = {0};
    adapter_opts.compatibleSurface = surface; /* NULL OK for headless */
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_opts.backendType = forced_backend;
    if (forced_backend != WGPUBackendType_Undefined) {
        yinfo("yframework: YETTY_WEBGPU_BACKEND=%s forces the WebGPU backend",
              getenv("YETTY_WEBGPU_BACKEND"));
    }
    /* Dawn's GL physical devices only exist at the Compatibility feature
     * level (PhysicalDeviceGL::SupportsFeatureLevel); the zero-initialized
     * featureLevel means Core, which silently filters GL out of adapter
     * discovery. Requesting Compatibility is required, not optional, for
     * the GL backends. */
    if (forced_backend == WGPUBackendType_OpenGL ||
        forced_backend == WGPUBackendType_OpenGLES) {
        adapter_opts.featureLevel = WGPUFeatureLevel_Compatibility;
    }

    ydebug("yframework: requesting adapter");
    rt->gpu.adapter = yframework_request_adapter(instance, &adapter_opts);

    /* If a forced backend yields nothing (e.g. not compiled in / unavailable),
     * don't brick startup — fall back to auto-select. */
    if (!rt->gpu.adapter && forced_backend != WGPUBackendType_Undefined) {
        ywarn("yframework: forced backend produced no adapter; falling back to auto-select");
        forced_backend = WGPUBackendType_Undefined;
        adapter_opts.backendType = WGPUBackendType_Undefined;
        rt->gpu.adapter = yframework_request_adapter(instance, &adapter_opts);
    }
    if (!rt->gpu.adapter) {
        return YETTY_ERR(yetty_ycore_void, "yframework: adapter request failed");
    }

    /* Auto-recover from a software (WARP) fallback on Windows: VMware/Hyper-V
     * virtual GPUs advertise D3D12 but can't create a real D3D12 device, so
     * Dawn hands back the CPU WARP rasterizer (very slow). Those GPUs DO
     * accelerate D3D11 — retry once there and keep it only if it's real
     * hardware. Guarded on getting a CPU adapter AND no user-forced backend,
     * so real GPUs (which never return CPU) never enter here. */
#if defined(_WIN32)
    if (forced_backend == WGPUBackendType_Undefined &&
        yframework_adapter_is_software(rt->gpu.adapter)) {
        ywarn("yframework: WebGPU selected a software (WARP) adapter — the GPU has no "
              "usable D3D12 device; retrying on D3D11 for hardware acceleration");
        WGPURequestAdapterOptions d3d11_opts = adapter_opts;
        d3d11_opts.backendType = WGPUBackendType_D3D11;
        WGPUAdapter d3d11_adapter = yframework_request_adapter(instance, &d3d11_opts);
        if (d3d11_adapter && !yframework_adapter_is_software(d3d11_adapter)) {
            wgpuAdapterRelease(rt->gpu.adapter); /* drop WARP; use the D3D11 GPU */
            rt->gpu.adapter = d3d11_adapter;
            yinfo("yframework: switched to a hardware D3D11 adapter");
        } else {
            if (d3d11_adapter) {
                wgpuAdapterRelease(d3d11_adapter); /* still software → try Vulkan */
            }
            /* No hardware D3D11 either. Last stop before settling for WARP:
             * Vulkan. Stock Windows has no Vulkan driver at all, but yetty
             * bundles Mesa lavapipe (registered with the loader by
             * yetty_yplatform_vulkan_register_bundled_driver at instance
             * creation), whose LLVM JIT renders yetty much faster than
             * WARP. A hardware Vulkan ICD, if one exists, outranks
             * lavapipe in this request too. */
            WGPURequestAdapterOptions vk_opts = adapter_opts;
            vk_opts.backendType = WGPUBackendType_Vulkan;
            WGPUAdapter vk_adapter = yframework_request_adapter(instance, &vk_opts);
            if (vk_adapter && !yframework_adapter_is_warp(vk_adapter)) {
                wgpuAdapterRelease(rt->gpu.adapter); /* drop WARP; use Vulkan */
                rt->gpu.adapter = vk_adapter;
                yinfo("yframework: switched to a Vulkan adapter (bundled lavapipe "
                      "or a hardware ICD) instead of WARP");
            } else {
                if (vk_adapter) {
                    wgpuAdapterRelease(vk_adapter);
                }
                ywarn("yframework: no Vulkan adapter available either; staying on "
                      "WARP (rendering stays CPU-bound)");
            }
        }
    }
#endif

    /* Best-effort: GPU info logging is diagnostic-only — degraded
     * GPUs may not surface a description string but the rest of init
     * can still proceed. Pass the surface so the dump includes the
     * supported-formats / present-modes list (their preferred-first
     * ordering is platform-specific and load-bearing for chrome
     * colors — see Surface format pick below). */
    {
        struct yetty_ycore_void_result _gi =
            yetty_yframework_log_gpu_info(rt->gpu.adapter, surface);
        if (YETTY_IS_ERR(_gi)) {
            ywarn("yframework: log_gpu_info: %s", _gi.error.msg ? _gi.error.msg : "(no msg)");
            yetty_ycore_error_destroy(_gi.error);
        }
    }

    /* Device + queue. */
    WGPULimits limits;
    yetty_ywebgpu_fill_default_limits(rt->gpu.adapter, rt->config, &limits);

    WGPUStringView device_label = {.data = "yetty device", .length = 12};
    WGPUStringView queue_label = {.data = "default queue", .length = 13};

    WGPUDeviceDescriptor device_desc = {0};
    device_desc.label = device_label;
    device_desc.requiredLimits = &limits;
    device_desc.defaultQueue.label = queue_label;
    device_desc.uncapturedErrorCallbackInfo = yetty_ywebgpu_get_error_callback_info();
    device_desc.deviceLostCallbackInfo = yetty_ywebgpu_get_device_lost_callback_info();

#ifndef __EMSCRIPTEN__
    static const char *const dawn_device_enabled_toggles[] = {
        "use_user_defined_labels_in_backend",
        "disable_symbol_renaming",
        "enable_immediate_error_handling",
    };
    WGPUDawnTogglesDescriptor dawn_device_toggles = {0};
    if (getenv("YETTY_DAWN_DEBUG")) {
        dawn_device_toggles.chain.sType = WGPUSType_DawnTogglesDescriptor;
        dawn_device_toggles.enabledToggleCount =
            sizeof(dawn_device_enabled_toggles) / sizeof(dawn_device_enabled_toggles[0]);
        dawn_device_toggles.enabledToggles = dawn_device_enabled_toggles;
        device_desc.nextInChain = &dawn_device_toggles.chain;
    }
#endif

    struct yetty_ywebgpu_device_request_state device_cb_data = {{0}, 0};
    WGPURequestDeviceCallbackInfo device_cb = {0};
    device_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    device_cb.callback = yetty_ywebgpu_device_request_callback;
    device_cb.userdata1 = &rt->gpu.device;
    device_cb.userdata2 = &device_cb_data;

    ydebug("yframework: requesting device");
    wgpuAdapterRequestDevice(rt->gpu.adapter, &device_desc, device_cb);

#ifdef __EMSCRIPTEN__
    while (!device_cb_data.ready) {
        emscripten_sleep(0);
    }
#endif
    if (!rt->gpu.device) {
        yerror("yframework: device request failed: %s",
               device_cb_data.error_msg[0] ? device_cb_data.error_msg : "(no message)");
        return YETTY_ERR(yetty_ycore_void, "yframework: device request failed");
    }
    rt->gpu.queue = wgpuDeviceGetQueue(rt->gpu.device);

    /* Surface format + present mode (auto picks Mailbox > FifoRelaxed > Fifo;
     * config rendering/present-mode overrides). */
    rt->present_mode = WGPUPresentMode_Fifo;
    if (surface) {
        WGPUSurfaceCapabilities caps = {0};
        wgpuSurfaceGetCapabilities(surface, rt->gpu.adapter, &caps);
        if (caps.formatCount > 0) {
            /* Prefer a UNORM (linear-byte) BGRA/RGBA surface over the *Srgb
             * variants. Dawn-on-Metal reports an sRGB-tagged format first on
             * macOS, which makes the GPU treat every byte the shader writes
             * as already-encoded sRGB and decode it back to linear before
             * scan-out. Our shaders write straight color bytes (no manual
             * gamma encode) — that mismatch crushes dark values to black
             * (see the chrome looking entirely black on macOS while Linux,
             * which returns BGRA8Unorm first, renders the intended palette).
             * Pick a UNORM format when offered; only fall back to the
             * first-reported (potentially *Srgb) when nothing linear is
             * available. */
            rt->gpu.surface_format = caps.formats[0];
            for (size_t i = 0; i < caps.formatCount; i++) {
                if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm ||
                    caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
                    rt->gpu.surface_format = caps.formats[i];
                    break;
                }
            }
        }
        const char *want =
            rt->config->ops->get_string(rt->config, "rendering/present-mode", "auto");

        int have_fifo = 0, have_fifo_relaxed = 0, have_mailbox = 0, have_immediate = 0;
        for (size_t i = 0; i < caps.presentModeCount; i++) {
            switch (caps.presentModes[i]) {
            case WGPUPresentMode_Fifo:
                have_fifo = 1;
                break;
            case WGPUPresentMode_FifoRelaxed:
                have_fifo_relaxed = 1;
                break;
            case WGPUPresentMode_Mailbox:
                have_mailbox = 1;
                break;
            case WGPUPresentMode_Immediate:
                have_immediate = 1;
                break;
            default:
                break;
            }
        }

        const char *picked_name = "fifo";
        if (strcmp(want, "mailbox") == 0 && have_mailbox) {
            rt->present_mode = WGPUPresentMode_Mailbox;
            picked_name = "mailbox (user)";
        } else if (strcmp(want, "fifo-relaxed") == 0 && have_fifo_relaxed) {
            rt->present_mode = WGPUPresentMode_FifoRelaxed;
            picked_name = "fifo-relaxed (user)";
        } else if (strcmp(want, "immediate") == 0 && have_immediate) {
            rt->present_mode = WGPUPresentMode_Immediate;
            picked_name = "immediate (user)";
        } else if (strcmp(want, "fifo") == 0) {
            rt->present_mode = WGPUPresentMode_Fifo;
            picked_name = "fifo (user)";
        } else {
            if (have_mailbox) {
                rt->present_mode = WGPUPresentMode_Mailbox;
                picked_name = "mailbox (auto)";
            } else if (have_fifo_relaxed) {
                rt->present_mode = WGPUPresentMode_FifoRelaxed;
                picked_name = "fifo-relaxed (auto)";
            } else {
                rt->present_mode = WGPUPresentMode_Fifo;
                picked_name = "fifo (auto — only mode available)";
            }
        }
        yinfo("present mode: %s (avail: fifo=%d fifo-relaxed=%d mailbox=%d immediate=%d)",
              picked_name, have_fifo, have_fifo_relaxed, have_mailbox, have_immediate);
#ifdef __ANDROID__
        __android_log_print(4, "yhello",
                            "PROBE present mode: %s (avail fifo=%d fifo-relaxed=%d mailbox=%d "
                            "immediate=%d)",
                            picked_name, have_fifo, have_fifo_relaxed, have_mailbox,
                            have_immediate);
#endif
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    } else {
        rt->gpu.surface_format = WGPUTextureFormat_BGRA8Unorm;
    }

    if (surface) {
        WGPUSurfaceConfiguration sc = {0};
        sc.device = rt->gpu.device;
        sc.format = rt->gpu.surface_format;
        sc.usage = WGPUTextureUsage_RenderAttachment;
        sc.width = rt->gpu.app_gpu_context.surface_width;
        sc.height = rt->gpu.app_gpu_context.surface_height;
        sc.presentMode = rt->present_mode;
        wgpuSurfaceConfigure(surface, &sc);
        ydebug("yframework: surface configured %ux%u present_mode=%d", sc.width, sc.height,
               (int)sc.presentMode);
    }

    /* GPU allocator. */
    struct yetty_yrender_gpu_allocator_result alloc_res =
        yetty_yrender_gpu_allocator_create(rt->gpu.device);
    if (!YETTY_IS_OK(alloc_res)) {
        return YETTY_ERR(yetty_ycore_void, "yframework: gpu_allocator_create failed", alloc_res);
    }
    rt->gpu.allocator = alloc_res.value;

    /* MSDF generator. */
    {
        const char *shaders_dir = rt->config->ops->get_string(rt->config, "paths/shaders", "");
        struct yetty_ymsdf_generator_ptr_result gres = yetty_ymsdf_generator_create_from_config(
            rt->config, rt->gpu.device, instance, shaders_dir);
        if (YETTY_IS_ERR(gres)) {
            return YETTY_ERR(yetty_ycore_void, "yframework: msdf_generator create failed", gres);
        }
        rt->gpu.msdf_generator = gres.value;
        yinfo("ymsdf: generator = %s", gres.value->ops->name(gres.value));
    }

    /* VNC server (optional). vnc/server or vnc/headless → TCP listener;
     * vnc/record-file alone → record-only encode pipeline, no listener. */
    const char *vnc_server_str = rt->config->ops->get_string(rt->config, "vnc/server", NULL);
    const char *vnc_headless_str = rt->config->ops->get_string(rt->config, "vnc/headless", NULL);
    const char *vnc_record_str = rt->config->ops->get_string(rt->config, "vnc/record-file", NULL);
    int vnc_listen_enabled = (vnc_server_str && strcmp(vnc_server_str, "true") == 0) ||
                             (vnc_headless_str && strcmp(vnc_headless_str, "true") == 0);
    int vnc_record_enabled = vnc_record_str && vnc_record_str[0];
    int vnc_enabled = vnc_listen_enabled || vnc_record_enabled;

    if (vnc_enabled) {
        struct yetty_vnc_server_ptr_result vnc_res =
            yetty_yvnc_server_create(instance, rt->gpu.device, rt->gpu.queue, rt->event_loop,
                                     rt->wgpu, rt->platform_input_pipe, rt->config);
        if (!YETTY_IS_OK(vnc_res)) {
            return YETTY_ERR(yetty_ycore_void, "yframework: vnc_server_create failed", vnc_res);
        }
        rt->vnc_server = vnc_res.value;

        if (vnc_listen_enabled) {
            int vnc_port = rt->config->ops->get_int(rt->config, "vnc/port", 5900);
            struct yetty_ycore_void_result start_res =
                yetty_yvnc_server_start(rt->vnc_server, (uint16_t)vnc_port);
            if (!YETTY_IS_OK(start_res)) {
                return YETTY_ERR(yetty_ycore_void, "yframework: vnc_server_start failed",
                                 start_res);
            }
            yinfo("VNC server started on port %d", vnc_port);
        } else {
            struct yetty_ycore_void_result act_res =
                yetty_yvnc_server_start_record_only(rt->vnc_server);
            if (!YETTY_IS_OK(act_res)) {
                return YETTY_ERR(yetty_ycore_void, "yframework: vnc record-only activate failed",
                                 act_res);
            }
            yinfo("VNC record mode: %s", vnc_record_str);
        }
    }

    /* Render target — vnc / x11-tile / texture, picked the same way the
     * terminal used to. set_wgpu must only fire on the texture target;
     * it casts to render_target_texture and writes through a raw offset. */
    struct yetty_yrender_viewport vp = {
        .x = 0,
        .y = 0,
        .w = (float)rt->gpu.app_gpu_context.surface_width,
        .h = (float)rt->gpu.app_gpu_context.surface_height,
    };

    struct yetty_yrender_target_ptr_result target_res;
    bool x11_tile_available = false;
#ifdef YETTY_HAS_X11_TILE
    x11_tile_available =
        rt->gpu.app_gpu_context.x11_display != NULL && rt->gpu.app_gpu_context.x11_window != 0UL;
#endif
    bool target_is_texture = false;

    if (vnc_enabled) {
        target_res =
            yetty_yrender_target_vnc_create(rt->gpu.device, rt->gpu.queue, rt->gpu.surface_format,
                                            rt->gpu.allocator, surface, rt->vnc_server, vp);
#ifdef YETTY_HAS_X11_TILE
    } else if (x11_tile_available) {
        yinfo("render target: X11-tile (XShmPutImage per dirty tile)");
        target_res = yetty_yrender_target_x11_tile_create(
            rt->gpu.device, rt->gpu.queue, rt->gpu.surface_format, rt->gpu.allocator, rt->wgpu,
            rt->event_loop, rt->gpu.app_gpu_context.x11_display, rt->gpu.app_gpu_context.x11_window,
            vp);
        if (!YETTY_IS_OK(target_res)) {
            ywarn("yframework: x11-tile target failed (%s); falling back to texture",
                  target_res.error.msg);
            yetty_ycore_error_destroy(target_res.error);
            target_res = yetty_yrender_target_texture_create(rt->gpu.device, rt->gpu.queue,
                                                             rt->gpu.surface_format,
                                                             rt->gpu.allocator, surface, vp);
            target_is_texture = true;
        }
#endif
    } else {
        target_res = yetty_yrender_target_texture_create(
            rt->gpu.device, rt->gpu.queue, rt->gpu.surface_format, rt->gpu.allocator, surface, vp);
        target_is_texture = true;
    }
    if (!YETTY_IS_OK(target_res)) {
        return YETTY_ERR(yetty_ycore_void, "yframework: render_target create failed", target_res);
    }
    rt->render_target = target_res.value;
    if (surface && target_is_texture) {
        yetty_yrender_target_texture_set_wgpu(rt->render_target, rt->wgpu);
        /* Same target gets the event loop so its present coro can
         * fire a catch-up RENDER after a notify_render_skipped — fixes
         * the input-to-paint lag when a present is in flight. */
        yetty_yrender_target_texture_set_event_loop(rt->render_target, rt->event_loop);
    }
    ydebug("yframework: render target created %.0fx%.0f vnc=%d", vp.w, vp.h, vnc_enabled);
    return YETTY_OK_VOID();
}

struct yetty_yframework_ptr_result yetty_yframework_create(struct yetty_yclass_object *platform)
{
    if (!platform) {
        return YETTY_ERR(yetty_yframework_ptr, "yframework_create: platform is NULL");
    }

    /* Fetch all borrowed platform state up front (before allocating rt) so the
     * obj→data downcast errors propagate cleanly without leaking rt. */
    struct yetty_yplatform_gpu_context_const_ptr_result gpu_ctx_res =
        yetty_yplatform_platform_gpu_context(platform);
    YETTY_RETURN_IF_ERR(yetty_yframework_ptr, gpu_ctx_res,
                        "yframework_create: platform GPU context");
    const struct yetty_yplatform_gpu_context *gpu = gpu_ctx_res.value;
    if (!gpu) {
        return YETTY_ERR(yetty_yframework_ptr, "yframework_create: platform GPU context unset");
    }
    struct yetty_yconfig_config_ptr_result config_res = yetty_yplatform_platform_config(platform);
    YETTY_RETURN_IF_ERR(yetty_yframework_ptr, config_res, "yframework_create: platform config");
    struct yetty_ycore_xthread_event_pipe_ptr_result input_pipe_res =
        yetty_yplatform_platform_input_pipe(platform);
    YETTY_RETURN_IF_ERR(yetty_yframework_ptr, input_pipe_res, "yframework_create: platform pipe");
    struct yetty_yclass_object_ptr_result clipboard_res =
        yetty_yplatform_platform_clipboard(platform);
    YETTY_RETURN_IF_ERR(yetty_yframework_ptr, clipboard_res,
                        "yframework_create: platform clipboard");
    struct yetty_yclass_object_ptr_result window_chrome_res =
        yetty_yplatform_platform_window_chrome(platform);
    YETTY_RETURN_IF_ERR(yetty_yframework_ptr, window_chrome_res,
                        "yframework_create: window chrome");

    struct yetty_yframework *rt = calloc(1, sizeof(struct yetty_yframework));
    if (!rt) {
        return YETTY_ERR(yetty_yframework_ptr, "yframework_create: alloc failed");
    }

    rt->factory_state = calloc(1, sizeof(struct yetty_yframework_factory_state));
    if (!rt->factory_state) {
        free(rt);
        return YETTY_ERR(yetty_yframework_ptr, "yframework_create: factory_state alloc failed");
    }

    /* Wire the borrowed services and copy the platform GPU slice by value. */
    rt->config = config_res.value;
    rt->platform_input_pipe = input_pipe_res.value;
    rt->output_pipe = NULL;
    rt->clipboard = clipboard_res.value;
    rt->window_chrome = window_chrome_res.value;

    /* Anchor the shared per-frame animation clock (zero-based from here). */
    rt->frame_origin_sec = yetty_yplatform_ytime_monotonic_sec();
    rt->frame_time_sec = 0.0;
    rt->frame_delta_sec = 0.0;

    rt->gpu.app_gpu_context = *gpu;

    /* Event loop — VNC + render-target callbacks need it. */
    struct yetty_ycore_event_loop_result el_res =
        yetty_ycore_event_loop_create(rt->platform_input_pipe);
    if (!YETTY_IS_OK(el_res)) {
        (void)yetty_yframework_destroy(rt);
        return YETTY_ERR(yetty_yframework_ptr, "yframework: event_loop_create failed", el_res);
    }
    rt->event_loop = el_res.value;

    /* Coroutine-aware wgpu await machinery — VNC + texture render target
     * use it to yield instead of blocking on async wgpu work. */
    struct yplatform_wgpu_ptr_result wgpu_res =
        yetty_yplatform_wgpu_create(gpu->instance, rt->event_loop);
    if (!YETTY_IS_OK(wgpu_res)) {
        (void)yetty_yframework_destroy(rt);
        return YETTY_ERR(yetty_yframework_ptr, "yframework: wgpu_create failed", wgpu_res);
    }
    rt->wgpu = wgpu_res.value;

    struct yetty_ycore_void_result gpu_res = init_gpu(rt, gpu->instance, gpu->surface);
    if (!YETTY_IS_OK(gpu_res)) {
        (void)yetty_yframework_destroy(rt);
        return YETTY_ERR(yetty_yframework_ptr, "yframework: gpu init failed", gpu_res);
    }

    /* RPC server (optional). Created here so any app that wants the
     * generic key/mouse/resize/shutdown injection just toggles
     * `rpc/port` in its config. Apps can register more handlers on
     * rt->rpc_server with yetty_yctl_server_register_handler. */
    const char *rpc_port_str =
        rt->config->ops->get_string(rt->config, YETTY_YCONFIG_KEY_RPC_PORT, NULL);
    if (rpc_port_str) {
        const char *rpc_host =
            rt->config->ops->get_string(rt->config, YETTY_YCONFIG_KEY_RPC_HOST, "127.0.0.1");
        int rpc_port = atoi(rpc_port_str);
        struct yetty_rpc_server_ptr_result rpc_res = yetty_yctl_server_create(rt->event_loop);
        if (YETTY_IS_OK(rpc_res)) {
            rt->rpc_server = rpc_res.value;
            struct yetty_ycore_void_result sr =
                yetty_yctl_server_start(rt->rpc_server, rpc_host, rpc_port);
            if (YETTY_IS_OK(sr)) {
                yinfo("yetty: RPC server listening on %s:%d", rpc_host, rpc_port);
            } else {
                yerror("yframework: rpc_server_start failed: %s", sr.error.msg);
                yetty_ycore_error_destroy(sr.error);
                (void)yetty_yctl_server_destroy(rt->rpc_server);
                rt->rpc_server = NULL;
            }
        } else {
            yerror("yframework: rpc_server_create failed: %s", rpc_res.error.msg);
            yetty_ycore_error_destroy(rpc_res.error);
        }
    }

    return YETTY_OK(yetty_yframework_ptr, rt);
}

struct yetty_ycore_void_result yetty_yframework_destroy(struct yetty_yframework *rt)
{
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (!rt) {
        return YETTY_OK_VOID();
    }

    if (rt->rpc_server) {
        struct yetty_ycore_void_result r = yetty_yctl_server_destroy(rt->rpc_server);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
        rt->rpc_server = NULL;
    }

    /* Tear down the per-kind factory bundles BEFORE the GPU goes away
     * — ymgui_factory_args holds an owned pipeline (textures, shader
     * module) whose destroy issues wgpu calls. */
    if (rt->factory_state) {
#ifdef YETTY_HAS_YMGUI
        struct yetty_ycore_void_result r =
            yetty_ymgui_factory_args_release(&rt->factory_state->ymgui_args);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
#endif
#ifdef YETTY_HAS_YRDAWN_SERVER
        struct yetty_ycore_void_result yr =
            yetty_yrdawn_factory_args_release(&rt->factory_state->yrdawn_args);
        if (YETTY_IS_ERR(yr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = yr;
            } else {
                yetty_ycore_error_destroy(yr.error);
            }
        }
#endif
        free(rt->factory_state);
        rt->factory_state = NULL;
    }

    /* render_target before wgpu/event_loop: buffer-map callbacks the
     * release path fires with status=Aborted dereference wgpu->loop. */
    if (rt->render_target && rt->render_target->ops && rt->render_target->ops->destroy) {
        rt->render_target->ops->destroy(rt->render_target);
        rt->render_target = NULL;
    }

    if (rt->wgpu) {
        yetty_yplatform_wgpu_destroy(rt->wgpu);
        rt->wgpu = NULL;
    }

    if (rt->event_loop && rt->event_loop->ops && rt->event_loop->ops->destroy) {
        rt->event_loop->ops->destroy(rt->event_loop);
        rt->event_loop = NULL;
    }

    if (rt->vnc_server) {
        struct yetty_ycore_void_result vsr = yetty_yvnc_server_stop(rt->vnc_server);
        if (YETTY_IS_ERR(vsr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = vsr;
            } else {
                yetty_ycore_error_destroy(vsr.error);
            }
        }
        struct yetty_ycore_void_result vdr = yetty_yvnc_server_destroy(rt->vnc_server);
        if (YETTY_IS_ERR(vdr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = vdr;
            } else {
                yetty_ycore_error_destroy(vdr.error);
            }
        }
        rt->vnc_server = NULL;
    }

    if (rt->gpu.msdf_generator) {
        rt->gpu.msdf_generator->ops->destroy(rt->gpu.msdf_generator);
        rt->gpu.msdf_generator = NULL;
    }

    if (rt->gpu.allocator) {
        rt->gpu.allocator->ops->destroy(rt->gpu.allocator);
        rt->gpu.allocator = NULL;
    }

    WGPUSurface surface = rt->gpu.app_gpu_context.surface;
    if (surface && rt->gpu.device) {
        wgpuSurfaceUnconfigure(surface);
#ifndef __EMSCRIPTEN__
        wgpuDeviceTick(rt->gpu.device);
#endif
        wgpuSurfaceRelease(surface);
        rt->gpu.app_gpu_context.surface = NULL;
    }

    if (rt->gpu.queue) {
        wgpuQueueRelease(rt->gpu.queue);
    }
    if (rt->gpu.device) {
        wgpuDeviceRelease(rt->gpu.device);
    }
    if (rt->gpu.adapter) {
        wgpuAdapterRelease(rt->gpu.adapter);
    }

    free(rt);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "yframework_destroy: subsystem teardown failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yframework_register_figure_factories(
    struct yetty_yframework *framework, struct yetty_yfigure_registry *registry,
    const struct yetty_context *context)
{
    if (!framework || !registry || !context) {
        return YETTY_ERR(yetty_ycore_void, "yframework_register_figure_factories: NULL arg");
    }
    if (!framework->factory_state) {
        return YETTY_ERR(yetty_ycore_void,
                         "yframework_register_figure_factories: factory_state missing");
    }

#ifdef YETTY_HAS_YMGUI
    /* ymgui — the args bundle's context is the host's context for this
     * registration. A single yframework can register the same bundle on
     * multiple hosts' registries; the last context wins, which is fine
     * because every host shares the same yframework so the GPU /
     * runtime members the factory actually reads are identical. */
    framework->factory_state->ymgui_args.context = context;
    struct yetty_ycore_void_result mr =
        yetty_ymgui_register_factory(registry, &framework->factory_state->ymgui_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr,
                        "yframework_register_figure_factories: ymgui register");
    /* ymgui's yclass-RPC discovery hooks — installed here (rather than by
     * a load-time constructor) under the same guard that gates ymgui's
     * linkage, so a host that serves remote objects can resolve and
     * dispatch ymgui classes over the wire. Idempotent. */
    struct yetty_ycore_void_result mreg = yetty_ymgui_register();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mreg,
                        "yframework_register_figure_factories: ymgui hooks");
#endif

#ifdef YETTY_HAS_YRDAWN_SERVER
    framework->factory_state->yrdawn_args.context = context;
    struct yetty_ycore_void_result rr =
        yetty_yrdawn_register_factory(registry, &framework->factory_state->yrdawn_args);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                        "yframework_register_figure_factories: yrdawn register");
    /* yrdawn's yclass-RPC discovery hooks — see the ymgui note above.
     * Guarded by YETTY_HAS_YRDAWN_SERVER, the same gate as its linkage. */
    struct yetty_ycore_void_result rreg = yetty_yrdawn_register();
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rreg,
                        "yframework_register_figure_factories: yrdawn hooks");
#endif

    /* yshadertoy — always available (no feature gate). Own factory +
     * renderer; the registry hands the host context to the factory at
     * mint time, so there's no args bundle to thread through. */
    struct yetty_ycore_void_result sr = yetty_yshadertoy_register_factory(registry);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr,
                        "yframework_register_figure_factories: yshadertoy register");

#if !defined(YETTY_HAS_YMGUI) && !defined(YETTY_HAS_YRDAWN_SERVER)
    (void)context;
#endif

    return YETTY_OK_VOID();
}

struct yetty_yrdawn_factory_args_ptr_result yetty_yframework_factory_args_yrdawn(
    struct yetty_yframework *framework)
{
#ifdef YETTY_HAS_YRDAWN_SERVER
    if (!framework) {
        return YETTY_ERR(yetty_yrdawn_factory_args_ptr, "factory_args_yrdawn: NULL framework");
    }
    if (!framework->factory_state) {
        return YETTY_ERR(yetty_yrdawn_factory_args_ptr,
                         "factory_args_yrdawn: framework has no factory_state");
    }
    return YETTY_OK(yetty_yrdawn_factory_args_ptr, &framework->factory_state->yrdawn_args);
#else
    (void)framework;
    return YETTY_ERR(yetty_yrdawn_factory_args_ptr,
                     "factory_args_yrdawn: yrdawn server not compiled into this build");
#endif
}

struct yetty_ycore_void_result yetty_yframework_reconfigure_surface(struct yetty_yframework *rt,
                                                                    uint32_t width, uint32_t height)
{
    if (!rt) {
        return YETTY_ERR(yetty_ycore_void, "yframework_reconfigure_surface: rt is NULL");
    }
    if (width == 0 || height == 0) {
        return YETTY_OK_VOID();
    }

    WGPUSurface surface = rt->gpu.app_gpu_context.surface;
    if (surface && rt->gpu.device) {
        WGPUSurfaceConfiguration sc = {0};
        sc.device = rt->gpu.device;
        sc.format = rt->gpu.surface_format;
        sc.usage = WGPUTextureUsage_RenderAttachment;
        sc.width = width;
        sc.height = height;
        sc.presentMode = rt->present_mode;
        wgpuSurfaceConfigure(surface, &sc);
    }
    rt->gpu.app_gpu_context.surface_width = width;
    rt->gpu.app_gpu_context.surface_height = height;
    return YETTY_OK_VOID();
}
