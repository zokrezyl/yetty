/*
 * render-target-texture.c - Texture render target implementation
 *
 * Renders to a GPU texture. Used for:
 * - Layer targets (render_layer)
 * - Terminal compositing (blend layers)
 * - Big yetty texture (blend terminals)
 */

#include <yetty/yrender/render-target.h>
#include <yetty/yrender/gpu-allocator.h>
#include <yetty/yrender/gpu-resource-binder.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yplatform/ywebgpu.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/yplatform/thread.h>
#include <yetty/yplatform/time.h>
#include <yetty/webgpu/error.h>
#include <yetty/ytrace/ytrace.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

/* Shader embedded via incbin_add_resources (stubs on Emscripten) */
extern const unsigned char gblend_shaderData[];
extern const unsigned int gblend_shaderSize;

#define MAX_BLEND_SOURCES 4

/* =============================================================================
 * Present watchdog
 *
 * wgpuSurfacePresent on Dawn/Wayland intermittently never returns on this
 * machine — the render thread parks inside Vulkan/Wayland Present and the
 * whole loop locks up with no error surfaced. The instrumentation around
 * the call shows "BEFORE wgpuSurfacePresent" with no matching AFTER, so we
 * know exactly where it's stuck, but the process keeps running, the window
 * goes black, and the user is left guessing.
 *
 * The watchdog is a single background thread that polls a monotonic
 * "in-Present since" timestamp set right before the wgpuSurfacePresent
 * call. If the value stays non-zero for more than WATCHDOG_LIMIT_MS, it
 * writes a clear FATAL line to stderr (including how long Present has
 * been stuck) and _Exit's the process. Better a loud crash than a silent
 * freeze.
 * =========================================================================== */

#define PRESENT_WATCHDOG_LIMIT_MS 5000

/* Heap-owned watchdog state. The owning rt holds the pointer; the thread
 * also keeps a copy of the pointer and outlives the rt (no join path), so
 * the struct is intentionally leaked at process exit — see
 * present_watchdog_start. */
struct yetty_yrender_present_watchdog {
    _Atomic uint64_t started_ms;
};

static uint64_t monotonic_ms(void)
{
    return (uint64_t)(yetty_yplatform_ytime_monotonic_sec() * 1000.0);
}

YETTY_EXTERNAL_CALLBACK
static int present_watchdog_thread(void *arg)
{
    struct yetty_yrender_present_watchdog *wd = arg;
    for (;;) {
        yetty_yplatform_ytime_sleep_ms(500);
        uint64_t started = atomic_load(&wd->started_ms);
        if (started == 0) {
            continue;
        }
        uint64_t elapsed_ms = monotonic_ms() - started;
        if (elapsed_ms >= PRESENT_WATCHDOG_LIMIT_MS) {
            fprintf(stderr,
                    "\n[FATAL] wgpuSurfacePresent stuck for %llums (>%dms threshold)\n"
                    "        Render thread is parked inside Dawn's Wayland Present.\n"
                    "        See ytrace log: last 'present_coro: BEFORE wgpuSurfacePresent'\n"
                    "        line tags the frame.\n"
                    "        Exiting.\n",
                    (unsigned long long)elapsed_ms, PRESENT_WATCHDOG_LIMIT_MS);
            fflush(stderr);
            _Exit(5);
        }
    }
    return 0;
}

static struct yetty_yrender_present_watchdog *present_watchdog_start(void)
{
    struct yetty_yrender_present_watchdog *wd = calloc(1, sizeof(*wd));
    if (!wd) {
        ywarn("present watchdog: alloc failed — running without watchdog");
        return NULL;
    }
    struct yetty_yplatform_ythread *t =
        yetty_yplatform_ythread_create(present_watchdog_thread, wd);
    if (!t) {
        ywarn("present watchdog: ythread_create failed — running without watchdog");
        free(wd);
        return NULL;
    }
    yinfo("present watchdog: started, limit=%d ms", PRESENT_WATCHDOG_LIMIT_MS);
    /* `wd` and `t` are deliberately leaked: the watchdog thread holds a
     * pointer to `wd` and runs for the rest of the process lifetime. */
    return wd;
}

/* Per-layer binder cache entry. Each layer's resource_set tree concatenates
 * its own shader (e.g. text-grid + msdf-font, ydraw-grid + ydraw-prims),
 * and merging trees from different layers redeclares functions like
 * median3. So when the same target serves multiple layers (direct
 * multi-layer render, no per-layer RTs), each layer needs its own binder.
 * Storage is a doubling-growth yetty_ycore_buffer used as a typed vector
 * (entries stored back-to-back, count = size / sizeof(entry)) — there's
 * no fixed cap, deeply nested split layouts grow it as panes are added. */
struct layer_binder_entry {
    struct yetty_yrender_terminal_layer *layer;
    struct yetty_yrender_gpu_resource_binder *binder;
};

struct yetty_yrender_render_target_texture {
    struct yetty_ydraw_target base; /* viewport stored here */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat format;
    struct yetty_ydraw_gpu_allocator *allocator;

    /* Owned texture (size from base.viewport.w/h) */
    WGPUTexture texture;
    WGPUTextureView view;

    /* Optional surface for present() - NULL for layer/terminal targets */
    WGPUSurface surface;

    /* Optional yplatform wgpu handle. When set, present() spawns a
     * "presenter" coroutine that yields via queue_done_await (the
     * wgpu-wait pool blocks in wgpuInstanceWaitAny on a helper thread,
     * libuv keeps running on the render thread). When the GPU work
     * has finished, the coro is resumed on the render thread and calls
     * wgpuSurfacePresent there — Wayland's surface backend is bound to
     * the render thread, so Present MUST happen on it. NULL → fall back
     * to inline synchronous Present (e.g. headless smoke tests). */
    struct yetty_yplatform_wgpu *wgpu;

    /* Set while the presenter coroutine is mid-await. The next call to
     * present() returns early instead of stacking another submit on
     * top of an unpresented swapchain image — overlapping presents on
     * the same surface deadlock under Wayland. The coro clears this
     * after wgpuSurfacePresent returns. is_busy() exposes this so the
     * RENDER event handler can skip the whole pipeline. */
    int present_in_flight;

    /* Per-target present watchdog. NULL when this rt has no surface
     * (layer and composite targets never call wgpuSurfacePresent).
     * Heap-owned; leaked on process exit — see present_watchdog_start. */
    struct yetty_yrender_present_watchdog *watchdog;

    /* Per-layer binder cache for render_layer. Lazily populated — first
     * call for a given layer creates and caches the binder. Stored as a
     * vector of `struct layer_binder_entry` inside the byte buffer. */
    struct yetty_ycore_buffer layer_binders;

    /* Blend pipeline resources (also used for present) */
    WGPUShaderModule blend_shader;
    WGPURenderPipeline blend_pipeline;
    WGPURenderPipeline present_pipeline; /* For presenting to surface */
    WGPUBindGroupLayout blend_layout;
    WGPUSampler sampler;
    WGPUBuffer uniform_buffer;
    WGPUTexture placeholder_texture;
    WGPUTextureView placeholder_view;

    /* Visual zoom state. scale=1.0 disables zoom. Offsets are in source
	 * pixels within this target. Read by blend()/present() and packed into
	 * the blend uniform buffer. */
    float visual_zoom_scale;
    float visual_zoom_offset_x;
    float visual_zoom_offset_y;
};

/*=============================================================================
 * Destroy
 *===========================================================================*/

static void render_target_texture_destroy(struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    if (rt->view) {
        wgpuTextureViewRelease(rt->view);
        rt->view = NULL;
    }
    if (rt->texture) {
        if (rt->allocator) {
            rt->allocator->ops->release_texture(rt->allocator, rt->texture);
        } else {
            wgpuTextureDestroy(rt->texture);
            wgpuTextureRelease(rt->texture);
        }
        rt->texture = NULL;
    }
    {
        struct layer_binder_entry *entries = (struct layer_binder_entry *)rt->layer_binders.data;
        size_t count = rt->layer_binders.size / sizeof(struct layer_binder_entry);
        for (size_t i = 0; i < count; i++) {
            if (entries[i].binder) {
                entries[i].binder->ops->destroy(entries[i].binder);
                entries[i].binder = NULL;
            }
        }
        yetty_ycore_buffer_destroy(&rt->layer_binders);
    }
    if (rt->blend_pipeline) {
        wgpuRenderPipelineRelease(rt->blend_pipeline);
        rt->blend_pipeline = NULL;
    }
    if (rt->blend_layout) {
        wgpuBindGroupLayoutRelease(rt->blend_layout);
        rt->blend_layout = NULL;
    }
    if (rt->blend_shader) {
        wgpuShaderModuleRelease(rt->blend_shader);
        rt->blend_shader = NULL;
    }
    if (rt->sampler) {
        wgpuSamplerRelease(rt->sampler);
        rt->sampler = NULL;
    }
    if (rt->uniform_buffer) {
        wgpuBufferDestroy(rt->uniform_buffer);
        wgpuBufferRelease(rt->uniform_buffer);
        rt->uniform_buffer = NULL;
    }
    if (rt->placeholder_view) {
        wgpuTextureViewRelease(rt->placeholder_view);
        rt->placeholder_view = NULL;
    }
    if (rt->placeholder_texture) {
        wgpuTextureDestroy(rt->placeholder_texture);
        wgpuTextureRelease(rt->placeholder_texture);
        rt->placeholder_texture = NULL;
    }

    free(rt);
}

/*=============================================================================
 * Clear
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_clear(
    struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(rt->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");
    }

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = rt->view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.0, 0.0, 0.0, 1.0};
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    return YETTY_OK_VOID();
}

/*=============================================================================
 * Resize
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_resize(
    struct yetty_ydraw_target *self, struct yetty_yrender_viewport viewport)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;
    uint32_t width = (uint32_t)viewport.w;
    uint32_t height = (uint32_t)viewport.h;

    /* Store viewport */
    rt->base.viewport = viewport;

    /* Only recreate texture if size changed */
    if (rt->texture) {
        uint32_t old_w = wgpuTextureGetWidth(rt->texture);
        uint32_t old_h = wgpuTextureGetHeight(rt->texture);
        if (old_w == width && old_h == height) {
            return YETTY_OK_VOID();
        }
    }

    /* Release old texture */
    if (rt->view) {
        wgpuTextureViewRelease(rt->view);
        rt->view = NULL;
    }
    if (rt->texture) {
        if (rt->allocator) {
            rt->allocator->ops->release_texture(rt->allocator, rt->texture);
        } else {
            wgpuTextureDestroy(rt->texture);
            wgpuTextureRelease(rt->texture);
        }
        rt->texture = NULL;
    }

    /* Create new texture */
    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.label = (WGPUStringView){.data = "render_target", .length = 13};
    tex_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                     WGPUTextureUsage_CopySrc;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size.width = width;
    tex_desc.size.height = height;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.format = rt->format;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;

    if (rt->allocator) {
        rt->texture = rt->allocator->ops->create_texture(rt->allocator, &tex_desc);
    } else {
        rt->texture = wgpuDeviceCreateTexture(rt->device, &tex_desc);
    }

    if (!rt->texture) {
        return YETTY_ERR(yetty_ycore_void, "failed to create texture");
    }

    rt->view = wgpuTextureCreateView(rt->texture, NULL);
    if (!rt->view) {
        if (rt->allocator) {
            rt->allocator->ops->release_texture(rt->allocator, rt->texture);
        } else {
            wgpuTextureDestroy(rt->texture);
            wgpuTextureRelease(rt->texture);
        }
        rt->texture = NULL;
        return YETTY_ERR(yetty_ycore_void, "failed to create view");
    }

    ydebug("render_target_texture: resized to %ux%u", width, height);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Accessors
 *===========================================================================*/

static WGPUTextureView render_target_texture_get_view(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_texture *rt =
        (const struct yetty_yrender_render_target_texture *)self;
    return rt->view;
}

static WGPUTexture render_target_texture_get_texture(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_texture *rt =
        (const struct yetty_yrender_render_target_texture *)self;
    return rt->texture;
}

/*=============================================================================
 * render_layer - render a terminal layer to this target
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_render_layer(
    struct yetty_ydraw_target *self, struct yetty_yrender_terminal_layer *layer)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    /* No per-layer dirty early-out here.
     *
     * The texture state at the start of this layer's pass depends on what
     * earlier layers in the same frame did:
     *   - layer 0 renders with LoadOp_Clear → wipes the entire attachment,
     *     so any non-dirty upper layer (ydraw, ymgui, …) skipping its draw
     *     would lose its previous-frame pixels — its content disappears.
     *   - layers above 0 with non-opaque pixels (alpha<1) would also leave
     *     ghosts of upper layers that didn't redraw, since LoadOp_Load
     *     keeps stale pixels under any transparent area.
     *
     * Frame-level gating already ensures this function only runs when
     * something requested a render. The cheap save-some-GPU optimisation
     * for non-dirty upper layers is not safe given the current compositing
     * model — terminal_render_frame skips empty layers, which is enough.
     * Anything more selective needs per-layer offscreen targets so each
     * layer's pixels are owned by it and not stomped by another layer's
     * pass. */

    /* Get gpu_resource_set from layer */
    struct yetty_yrender_gpu_resource_set_result rs_res = layer->ops->get_gpu_resource_set(layer);
    if (!YETTY_IS_OK(rs_res)) {
        return YETTY_ERR(yetty_ycore_void, rs_res.error.msg);
    }

    const struct yetty_ydraw_gpu_resource_set *rs = rs_res.value;

    /* Look up or create the per-layer binder. Multiple layers rendering
     * into the same target each have their own resource-tree shader, and
     * merging trees would redeclare common functions (e.g. median3). */
    struct yetty_yrender_gpu_resource_binder *binder = NULL;
    struct layer_binder_entry *entries = (struct layer_binder_entry *)rt->layer_binders.data;
    size_t entry_count = rt->layer_binders.size / sizeof(struct layer_binder_entry);
    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].layer == layer) {
            binder = entries[i].binder;
            break;
        }
    }
    if (!binder) {
        struct yetty_yrender_gpu_resource_binder_result binder_res =
            yetty_yrender_gpu_resource_binder_create(rt->device, rt->queue, rt->format,
                                                     rt->allocator);
        if (!YETTY_IS_OK(binder_res)) {
            return YETTY_ERR(yetty_ycore_void, binder_res.error.msg);
        }
        struct layer_binder_entry new_entry = {.layer = layer, .binder = binder_res.value};
        struct yetty_ycore_void_result wr =
            yetty_ycore_buffer_write(&rt->layer_binders, &new_entry, sizeof(new_entry));
        if (!YETTY_IS_OK(wr)) {
            binder_res.value->ops->destroy(binder_res.value);
            return YETTY_ERR(yetty_ycore_void, "render_layer: layer_binders grow failed", wr);
        }
        binder = binder_res.value;
        ydebug("render_target_texture: created binder for layer=%p (count=%zu)", (void *)layer,
               rt->layer_binders.size / sizeof(struct layer_binder_entry));
    }

    /* Submit to binder */
    ytime_start(rt_submit);
    struct yetty_ycore_void_result res = binder->ops->submit(binder, rs);
    ytime_report(rt_submit);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Finalize (compile shader if needed) */
    ytime_start(rt_finalize);
    res = binder->ops->finalize(binder);
    ytime_report(rt_finalize);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Update uniforms/buffers */
    ytime_start(rt_update);
    res = binder->ops->update(binder);
    ytime_report(rt_update);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Encode + draw + submit command buffer to GPU */
    ytime_start(rt_gpu);

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(rt->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");
    }

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = rt->view;
    /* Always Load. The single per-frame wipe is the global clear() in
     * yetty_event_handler; layer-pass loadOp is never Clear, so multiple
     * panes drawing into the shared big target can't stomp each other. */
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.0, 0.0, 0.0, 0.0};
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }

    WGPURenderPipeline pipeline = binder->ops->get_pipeline(binder);
    WGPUBuffer quad_vb = binder->ops->get_quad_vertex_buffer(binder);

    if (pipeline && quad_vb) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        binder->ops->bind(binder, pass, 0);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quad_vb, 0, WGPU_WHOLE_SIZE);

        /* Confine the layer's full-NDC quad to its pane's rect, otherwise
         * a pane's layer would draw across the whole texture and stomp
         * neighboring panes. pane_render() in yui/tile.c writes the pane
         * bounds into self->viewport before calling our render_layer. */
        struct yetty_yrender_viewport vp = self->viewport;
        ydebug("render_layer: GPU SetViewport=(%.1f,%.1f,%.1f,%.1f) layer=%p",
               vp.x, vp.y, vp.w, vp.h, (void *)layer);
        wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.w, vp.h, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)vp.x, (uint32_t)vp.y, (uint32_t)vp.w,
                                            (uint32_t)vp.h);

        /* instance_count defaults to 1 (single full-pane quad). Layers like
         * shader-glyph set it to the per-frame cell count for instanced
         * draws — each instance covers one cell, vertex shader fetches the
         * cell's position from a storage buffer indexed by instance_index.
         * Defensive: treat 0 as "draw once" so layers that don't set the
         * field still render. */
        uint32_t instance_count = rs->instance_count > 0 ? rs->instance_count : 1u;
        ydebug("render_layer: BEFORE Draw vertices=6 instances=%u layer=%p", instance_count,
               (void *)layer);
        wgpuRenderPassEncoderDraw(pass, 6, instance_count, 0, 0);
        ydebug("render_layer: AFTER Draw layer=%p", (void *)layer);
    }

    ydebug("render_layer: BEFORE RenderPassEncoderEnd layer=%p", (void *)layer);
    wgpuRenderPassEncoderEnd(pass);
    ydebug("render_layer: AFTER RenderPassEncoderEnd layer=%p", (void *)layer);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    ydebug("render_layer: BEFORE QueueSubmit layer=%p", (void *)layer);
    ytime_start(rt_submit_queue);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    ytime_report(rt_submit_queue);
    ydebug("render_layer: AFTER QueueSubmit layer=%p", (void *)layer);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    ytime_report(rt_gpu);

    /* Clear dirty flag */
    layer->dirty = 0;

    ydebug("render_target_texture: rendered layer instance_count_used=%u",
           rs->instance_count > 0 ? rs->instance_count : 1u);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * blend - blend multiple source targets into this target
 *===========================================================================*/

static struct yetty_ycore_void_result create_blend_pipeline(
    struct yetty_yrender_render_target_texture *rt)
{
    /* Shader module */
    WGPUShaderSourceWGSL wgsl_src = {0};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
#ifdef __EMSCRIPTEN__
    struct yetty_yrender_shader_code blend_code = {0};
    /* yetty_yplatform_extract_assets() decompresses the incbin'd blend
     * shader into /data/shaders/blender.wgsl (note: yetty_embed_assets
     * renames blend.wgsl → blender.wgsl, see shared.cmake). The old
     * /assets/shaders/* path was a leftover from the preload-file era
     * and 404s under the new incbin+extract model. */
    yetty_yrender_shader_code_load_file(&blend_code, "/data/shaders/blender.wgsl");
    wgsl_src.code = (WGPUStringView){.data = blend_code.data, .length = blend_code.size};
#else
    wgsl_src.code =
        (WGPUStringView){.data = (const char *)gblend_shaderData, .length = gblend_shaderSize};
#endif

    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_src;

    rt->blend_shader = wgpuDeviceCreateShaderModule(rt->device, &shader_desc);
    if (!rt->blend_shader) {
        return YETTY_ERR(yetty_ycore_void, "failed to create blend shader");
    }

    /* Bind group layout */
    WGPUBindGroupLayoutEntry entries[MAX_BLEND_SOURCES + 2] = {0};

    for (int i = 0; i < MAX_BLEND_SOURCES; i++) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }
    entries[MAX_BLEND_SOURCES].binding = MAX_BLEND_SOURCES;
    entries[MAX_BLEND_SOURCES].visibility = WGPUShaderStage_Fragment;
    entries[MAX_BLEND_SOURCES].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[MAX_BLEND_SOURCES + 1].binding = MAX_BLEND_SOURCES + 1;
    entries[MAX_BLEND_SOURCES + 1].visibility = WGPUShaderStage_Fragment;
    entries[MAX_BLEND_SOURCES + 1].buffer.type = WGPUBufferBindingType_Uniform;
    entries[MAX_BLEND_SOURCES + 1].buffer.minBindingSize = 32;

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = MAX_BLEND_SOURCES + 2;
    bgl_desc.entries = entries;

    rt->blend_layout = wgpuDeviceCreateBindGroupLayout(rt->device, &bgl_desc);
    if (!rt->blend_layout) {
        return YETTY_ERR(yetty_ycore_void, "failed to create blend layout");
    }

    /* Pipeline layout */
    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &rt->blend_layout;

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(rt->device, &pl_desc);
    if (!layout) {
        return YETTY_ERR(yetty_ycore_void, "failed to create pipeline layout");
    }

    /* Render pipeline */
    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.layout = layout;
    rp_desc.vertex.module = rt->blend_shader;
    rp_desc.vertex.entryPoint = (WGPUStringView){.data = "vs_main", .length = 7};

    WGPUColorTargetState color_target = {0};
    color_target.format = rt->format;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {0};
    fragment.module = rt->blend_shader;
    fragment.entryPoint = (WGPUStringView){.data = "fs_main", .length = 7};
    fragment.targetCount = 1;
    fragment.targets = &color_target;
    rp_desc.fragment = &fragment;

    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;

    rt->blend_pipeline = wgpuDeviceCreateRenderPipeline(rt->device, &rp_desc);
    wgpuPipelineLayoutRelease(layout);

    if (!rt->blend_pipeline) {
        return YETTY_ERR(yetty_ycore_void, "failed to create blend pipeline");
    }

    /* Sampler */
    WGPUSamplerDescriptor sampler_desc = {0};
    sampler_desc.minFilter = WGPUFilterMode_Linear;
    sampler_desc.magFilter = WGPUFilterMode_Linear;
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.maxAnisotropy = 1;

    rt->sampler = wgpuDeviceCreateSampler(rt->device, &sampler_desc);
    if (!rt->sampler) {
        return YETTY_ERR(yetty_ycore_void, "failed to create sampler");
    }

    /* Uniform buffer - BlendUniforms is 32 bytes:
	 *   u32 layer_count; u32 target_w; u32 target_h; u32 _pad;
	 *   f32 visual_zoom_scale; f32 visual_zoom_offset_x;
	 *   f32 visual_zoom_offset_y; f32 _pad2; */
    WGPUBufferDescriptor buf_desc = {0};
    buf_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    buf_desc.size = 32;

    rt->uniform_buffer = wgpuDeviceCreateBuffer(rt->device, &buf_desc);
    if (!rt->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "failed to create uniform buffer");
    }

    /* Placeholder texture */
    WGPUTextureDescriptor tex_desc = {0};
    tex_desc.usage = WGPUTextureUsage_TextureBinding;
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size.width = 1;
    tex_desc.size.height = 1;
    tex_desc.size.depthOrArrayLayers = 1;
    tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;

    rt->placeholder_texture = wgpuDeviceCreateTexture(rt->device, &tex_desc);
    if (!rt->placeholder_texture) {
        return YETTY_ERR(yetty_ycore_void, "failed to create placeholder texture");
    }

    rt->placeholder_view = wgpuTextureCreateView(rt->placeholder_texture, NULL);
    if (!rt->placeholder_view) {
        return YETTY_ERR(yetty_ycore_void, "failed to create placeholder view");
    }

    ydebug("render_target_texture: blend pipeline created");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result render_target_texture_blend(
    struct yetty_ydraw_target *self, struct yetty_ydraw_target **sources, size_t count)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    if (count == 0) {
        return YETTY_OK_VOID();
    }

    if (count > MAX_BLEND_SOURCES) {
        yerror("render_target_texture: too many sources (%zu > %d)", count, MAX_BLEND_SOURCES);
        count = MAX_BLEND_SOURCES;
    }

    /* Create blend pipeline on first use */
    if (!rt->blend_pipeline) {
        struct yetty_ycore_void_result res = create_blend_pipeline(rt);
        if (!YETTY_IS_OK(res)) {
            return res;
        }
    }

    /* Update uniforms. See BlendUniforms layout in blend.wgsl. */
    struct {
        uint32_t layer_count;
        uint32_t target_w;
        uint32_t target_h;
        uint32_t _pad;
        float zoom_scale;
        float zoom_offset_x;
        float zoom_offset_y;
        float _pad2;
    } uniforms = {
        .layer_count = (uint32_t)count,
        .target_w = (uint32_t)rt->base.viewport.w,
        .target_h = (uint32_t)rt->base.viewport.h,
        .zoom_scale = rt->visual_zoom_scale > 0.0f ? rt->visual_zoom_scale : 1.0f,
        .zoom_offset_x = rt->visual_zoom_offset_x,
        .zoom_offset_y = rt->visual_zoom_offset_y,
    };
    wgpuQueueWriteBuffer(rt->queue, rt->uniform_buffer, 0, &uniforms, sizeof(uniforms));

    /* Collect source views */
    WGPUTextureView source_views[MAX_BLEND_SOURCES];
    for (size_t i = 0; i < MAX_BLEND_SOURCES; i++) {
        if (i < count && sources[i] && sources[i]->ops->get_view) {
            source_views[i] = sources[i]->ops->get_view(sources[i]);
        } else {
            source_views[i] = rt->placeholder_view;
        }
    }

    /* Create bind group */
    WGPUBindGroupEntry bg_entries[MAX_BLEND_SOURCES + 2] = {0};
    for (int i = 0; i < MAX_BLEND_SOURCES; i++) {
        bg_entries[i].binding = i;
        bg_entries[i].textureView = source_views[i];
    }
    bg_entries[MAX_BLEND_SOURCES].binding = MAX_BLEND_SOURCES;
    bg_entries[MAX_BLEND_SOURCES].sampler = rt->sampler;
    bg_entries[MAX_BLEND_SOURCES + 1].binding = MAX_BLEND_SOURCES + 1;
    bg_entries[MAX_BLEND_SOURCES + 1].buffer = rt->uniform_buffer;
    bg_entries[MAX_BLEND_SOURCES + 1].size = 32;

    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = rt->blend_layout;
    bg_desc.entryCount = MAX_BLEND_SOURCES + 2;
    bg_desc.entries = bg_entries;

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(rt->device, &bg_desc);
    if (!bind_group) {
        return YETTY_ERR(yetty_ycore_void, "failed to create bind group");
    }

    /* Create encoder */
    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(rt->device, &enc_desc);
    if (!encoder) {
        wgpuBindGroupRelease(bind_group);
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");
    }

    /* Render pass - use Load to preserve existing content (for tiled rendering) */
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = rt->view;
    color_attachment.loadOp = WGPULoadOp_Load; /* Don't clear - preserve existing */
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bind_group);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, rt->blend_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);

    /* Set viewport and scissor from target's viewport */
    struct yetty_yrender_viewport vp = self->viewport;
    wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.w, vp.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)vp.x, (uint32_t)vp.y, (uint32_t)vp.w,
                                        (uint32_t)vp.h);

    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    /* Submit */
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bind_group);

    ydebug("render_target_texture[%p]: blended %zu sources at (%.0f,%.0f) %.0fx%.0f zoom=%.2f "
           "off=(%.1f,%.1f)",
           (void *)rt, count, vp.x, vp.y, vp.w, vp.h, rt->visual_zoom_scale,
           rt->visual_zoom_offset_x, rt->visual_zoom_offset_y);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * present - blit texture to surface (if surface was provided at creation)
 *===========================================================================*/

/* Args for the presenter coroutine spawned from present(). The coroutine
 * owns surface_view across the yield: releasing it earlier would leave
 * the render pass holding a dangling view by the time wgpuSurfacePresent
 * runs. The coroutine writes 0 back to rt->present_in_flight so the
 * RENDER handler stops skipping. */
struct present_coro_args {
    struct yetty_yrender_render_target_texture *rt;
    WGPUTextureView surface_view;
};

YETTY_EXTERNAL_CALLBACK
static void render_target_texture_present_coro(void *arg)
{
    struct present_coro_args *a = arg;
    struct yetty_yrender_render_target_texture *rt = a->rt;

    /* No yield yet — earlier queue_done_await between Submit and
     * Present triggered Dawn validation
     * "GetCurrentTexture was not called on [Surface] this frame prior to
     * calling Present". The wgpu-wait pool's wgpuInstanceWaitAny seems
     * to advance a surface-frame boundary inside Dawn. Until we have a
     * yield that doesn't pump surface events, this coro just calls
     * Present synchronously — same blocking semantics as before, just
     * routed through the coro machinery for instrumentation parity. */
    ydebug("present_coro: ENTER coro=%u surface=%p",
           yetty_yplatform_coro_id(yetty_yplatform_coro_current()), (void *)rt->surface);
    ydebug("present_coro: BEFORE wgpuSurfacePresent");
    /* Arm the watchdog. If wgpuSurfacePresent blocks for longer than
     * PRESENT_WATCHDOG_LIMIT_MS, the watchdog thread will write a
     * [FATAL] message naming this call and _Exit. */
    if (rt->watchdog) {
        atomic_store(&rt->watchdog->started_ms, monotonic_ms());
    }
    ytime_start(surface_present);
    wgpuSurfacePresent(rt->surface);
    ytime_report(surface_present);
    if (rt->watchdog) {
        atomic_store(&rt->watchdog->started_ms, 0);
    }
    YETTY_WGPU_CHECK("present_coro:SurfacePresent");
    ydebug("present_coro: AFTER  wgpuSurfacePresent");

    ydebug("present_coro: BEFORE wgpuTextureViewRelease view=%p", (void *)a->surface_view);
    wgpuTextureViewRelease(a->surface_view);
    YETTY_WGPU_CHECK("present_coro:TextureViewRelease");
    ydebug("present_coro: AFTER  wgpuTextureViewRelease");

    rt->present_in_flight = 0;
    free(a);
    ydebug("present_coro: EXIT");
}

static bool render_target_texture_is_busy(const struct yetty_ydraw_target *self)
{
    const struct yetty_yrender_render_target_texture *rt =
        (const struct yetty_yrender_render_target_texture *)self;
    return rt->present_in_flight != 0;
}

static struct yetty_ycore_void_result render_target_texture_present(
    struct yetty_ydraw_target *self)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    if (!rt->surface) {
        return YETTY_ERR(yetty_ycore_void, "no surface configured for present");
    }

    /* Skip if the previous presenter coro hasn't returned yet — the
     * RENDER handler should already gate on is_busy(), but double-check
     * here so a stray caller doesn't double-acquire the swapchain. */
    if (rt->present_in_flight) {
        ydebug("present: skipped, presenter coro still in flight");
        return YETTY_OK_VOID();
    }

    /* Acquire surface texture — on X11/VNC (incl. VirtualGL) this can block
	 * waiting for the compositor/VNC-server to hand back a free swapchain
	 * image, so this is one of the prime suspects on slow remote displays. */
    ydebug("present: BEFORE GetCurrentTexture surface=%p", (void *)rt->surface);
    ytime_start(present_acquire);
    WGPUSurfaceTexture surface_texture;
    wgpuSurfaceGetCurrentTexture(rt->surface, &surface_texture);
    ytime_report(present_acquire);
    ydebug("present: AFTER GetCurrentTexture status=%d tex=%p", (int)surface_texture.status,
           (void *)surface_texture.texture);

    YETTY_WGPU_CHECK("present:GetCurrentTexture");
    if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        fprintf(stderr,
                "\n[FATAL] wgpuSurfaceGetCurrentTexture returned non-Success status=%d "
                "(surface=%p). Likely surface lost or out-of-date — exiting.\n",
                (int)surface_texture.status, (void *)rt->surface);
        fflush(stderr);
        _Exit(4);
    }

    ydebug("present: BEFORE wgpuTextureCreateView tex=%p", (void *)surface_texture.texture);
    WGPUTextureView surface_view = wgpuTextureCreateView(surface_texture.texture, NULL);
    YETTY_WGPU_CHECK("present:CreateView");
    if (!surface_view) {
        fprintf(stderr, "\n[FATAL] wgpuTextureCreateView returned NULL — exiting.\n");
        fflush(stderr);
        _Exit(4);
    }
    ydebug("present: AFTER  wgpuTextureCreateView view=%p", (void *)surface_view);

    /* Create blend pipeline if needed (reuse for present) */
    if (!rt->blend_pipeline) {
        ydebug("present: lazy create_blend_pipeline");
        struct yetty_ycore_void_result res = create_blend_pipeline(rt);
        YETTY_WGPU_CHECK("present:create_blend_pipeline");
        if (!YETTY_IS_OK(res)) {
            wgpuTextureViewRelease(surface_view);
            return res;
        }
    }

    /* Update uniforms - single source (present path). Zoom is applied during
	 * blend(), so present blits 1:1. Match the 32-byte BlendUniforms layout. */
    struct {
        uint32_t layer_count;
        uint32_t target_w;
        uint32_t target_h;
        uint32_t _pad;
        float zoom_scale;
        float zoom_offset_x;
        float zoom_offset_y;
        float _pad2;
    } uniforms = {
        .layer_count = 1,
        .target_w = (uint32_t)rt->base.viewport.w,
        .target_h = (uint32_t)rt->base.viewport.h,
        .zoom_scale = 1.0f,
    };
    ydebug("present: BEFORE QueueWriteBuffer (uniforms %zu B)", sizeof(uniforms));
    wgpuQueueWriteBuffer(rt->queue, rt->uniform_buffer, 0, &uniforms, sizeof(uniforms));
    YETTY_WGPU_CHECK("present:QueueWriteBuffer(uniforms)");
    ydebug("present: AFTER  QueueWriteBuffer (uniforms)");

    /* Create bind group with this target's texture as source */
    ydebug("present: blit source rt->view=%p rt->texture=%p (the view ygrid/clear "
           "wrote to should match this)",
           (void *)rt->view, (void *)rt->texture);
    WGPUTextureView source_views[MAX_BLEND_SOURCES];
    source_views[0] = rt->view;
    for (int i = 1; i < MAX_BLEND_SOURCES; i++) {
        source_views[i] = rt->placeholder_view;
    }

    WGPUBindGroupEntry bg_entries[MAX_BLEND_SOURCES + 2] = {0};
    for (int i = 0; i < MAX_BLEND_SOURCES; i++) {
        bg_entries[i].binding = i;
        bg_entries[i].textureView = source_views[i];
    }
    bg_entries[MAX_BLEND_SOURCES].binding = MAX_BLEND_SOURCES;
    bg_entries[MAX_BLEND_SOURCES].sampler = rt->sampler;
    bg_entries[MAX_BLEND_SOURCES + 1].binding = MAX_BLEND_SOURCES + 1;
    bg_entries[MAX_BLEND_SOURCES + 1].buffer = rt->uniform_buffer;
    bg_entries[MAX_BLEND_SOURCES + 1].size = 32;

    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = rt->blend_layout;
    bg_desc.entryCount = MAX_BLEND_SOURCES + 2;
    bg_desc.entries = bg_entries;

    ydebug("present: BEFORE wgpuDeviceCreateBindGroup");
    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(rt->device, &bg_desc);
    YETTY_WGPU_CHECK("present:CreateBindGroup");
    if (!bind_group) {
        fprintf(stderr, "\n[FATAL] wgpuDeviceCreateBindGroup returned NULL — exiting.\n");
        fflush(stderr);
        _Exit(4);
    }
    ydebug("present: AFTER  wgpuDeviceCreateBindGroup bg=%p", (void *)bind_group);

    /* Create encoder */
    WGPUCommandEncoderDescriptor enc_desc = {0};
    ydebug("present: BEFORE wgpuDeviceCreateCommandEncoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(rt->device, &enc_desc);
    YETTY_WGPU_CHECK("present:CreateCommandEncoder");
    if (!encoder) {
        fprintf(stderr, "\n[FATAL] wgpuDeviceCreateCommandEncoder returned NULL — exiting.\n");
        fflush(stderr);
        _Exit(4);
    }
    ydebug("present: AFTER  wgpuDeviceCreateCommandEncoder enc=%p", (void *)encoder);

    /* Render pass to surface */
    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = surface_view;
    color_attachment.loadOp = WGPULoadOp_Clear;
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.clearValue = (WGPUColor){0.0, 0.0, 0.0, 1.0};
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    ydebug("present: BEFORE wgpuCommandEncoderBeginRenderPass");
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    YETTY_WGPU_CHECK("present:BeginRenderPass");
    if (!pass) {
        fprintf(stderr, "\n[FATAL] wgpuCommandEncoderBeginRenderPass returned NULL — exiting.\n");
        fflush(stderr);
        _Exit(4);
    }
    ydebug("present: AFTER  wgpuCommandEncoderBeginRenderPass pass=%p", (void *)pass);

    ydebug("present: BEFORE SetPipeline/SetBindGroup/Draw");
    wgpuRenderPassEncoderSetPipeline(pass, rt->blend_pipeline);
    YETTY_WGPU_CHECK("present:SetPipeline");
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    YETTY_WGPU_CHECK("present:SetBindGroup");
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    YETTY_WGPU_CHECK("present:Draw");
    ydebug("present: AFTER  SetPipeline/SetBindGroup/Draw");

    ydebug("present: BEFORE RenderPassEncoderEnd");
    wgpuRenderPassEncoderEnd(pass);
    YETTY_WGPU_CHECK("present:RenderPassEncoderEnd");
    ydebug("present: AFTER  RenderPassEncoderEnd");
    wgpuRenderPassEncoderRelease(pass);

    /* Submit the blit-to-surface command buffer */
    WGPUCommandBufferDescriptor cmd_desc = {0};
    ydebug("present: BEFORE CommandEncoderFinish");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    YETTY_WGPU_CHECK("present:CommandEncoderFinish");
    if (!cmd) {
        fprintf(stderr, "\n[FATAL] wgpuCommandEncoderFinish returned NULL — exiting.\n");
        fflush(stderr);
        _Exit(4);
    }
    ydebug("present: AFTER  CommandEncoderFinish cmd=%p", (void *)cmd);

    ydebug("present: BEFORE QueueSubmit (blit-to-surface)");
    ytime_start(present_submit);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    ytime_report(present_submit);
    YETTY_WGPU_CHECK("present:QueueSubmit(blit)");
    ydebug("present: AFTER  QueueSubmit (blit-to-surface)");
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bind_group);

#ifndef __EMSCRIPTEN__
    /* Hand the (potentially blocking) wgpuSurfacePresent off to a
     * presenter coroutine. The coro yields via queue_done_await — the
     * wgpu-wait pool blocks in wgpuInstanceWaitAny, libuv stays
     * responsive. When GPU work drains the coro resumes on this same
     * (render) thread and calls Present synchronously. Without a wgpu
     * handle (headless / pre-init) we just call Present inline. */
    if (rt->wgpu) {
        struct present_coro_args *args = malloc(sizeof(*args));
        if (!args) {
            wgpuTextureViewRelease(surface_view);
            return YETTY_ERR(yetty_ycore_void, "present: arg alloc failed");
        }
        args->rt = rt;
        args->surface_view = surface_view;
        struct yplatform_coro_ptr_result cr = yetty_yplatform_coro_spawn(
            render_target_texture_present_coro, args, 0, "presenter");
        if (!YETTY_IS_OK(cr)) {
            free(args);
            wgpuTextureViewRelease(surface_view);
            return YETTY_ERR(yetty_ycore_void, "present: coro spawn failed");
        }
        rt->present_in_flight = 1;
        ydebug("present: spawned presenter coro");
        yetty_yplatform_coro_resume(cr.value);
        if (yetty_yplatform_coro_is_finished(cr.value)) {
            yetty_yplatform_coro_destroy(cr.value);
        }
        /* If the coro yielded inside queue_done_await, the wgpu-wait
         * pool's done callback (post_to_loop'd onto the render thread)
         * resumes and destroys it once the GPU work completes. */
    } else {
        ydebug("present: BEFORE SurfacePresent (sync — no wgpu handle)");
        ytime_start(surface_present);
        wgpuSurfacePresent(rt->surface);
        ytime_report(surface_present);
        ydebug("present: AFTER SurfacePresent (sync)");
        wgpuTextureViewRelease(surface_view);
    }
#else
    wgpuTextureViewRelease(surface_view);
#endif

    ydebug("render_target_texture: presented to surface");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * vtable and create
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_set_visual_zoom(
    struct yetty_ydraw_target *self, float scale, float offset_x, float offset_y)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;
    if (!(scale > 0.0f)) {
        scale = 1.0f;
    }
    rt->visual_zoom_scale = scale;
    rt->visual_zoom_offset_x = offset_x;
    rt->visual_zoom_offset_y = offset_y;
    ydebug("render_target_texture[%p]: set_visual_zoom scale=%.2f off=(%.1f,%.1f)", (void *)rt,
           scale, offset_x, offset_y);
    return YETTY_OK_VOID();
}

static const struct yetty_yrender_target_ops render_target_texture_ops = {
    .destroy = render_target_texture_destroy,
    .clear = render_target_texture_clear,
    .render_layer = render_target_texture_render_layer,
    .blend = render_target_texture_blend,
    .present = render_target_texture_present,
    .get_view = render_target_texture_get_view,
    .get_texture = render_target_texture_get_texture,
    .resize = render_target_texture_resize,
    .set_visual_zoom = render_target_texture_set_visual_zoom,
    /* Reports present_in_flight so the RENDER handler skips a frame
     * while the presenter coro is mid-await. Without it we'd queue a
     * second render+submit on top of an unpresented swapchain image. */
    .is_busy = render_target_texture_is_busy,
};

struct yetty_yrender_target_ptr_result yetty_yrender_target_texture_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ydraw_gpu_allocator *allocator, WGPUSurface surface,
    struct yetty_yrender_viewport viewport)
{
    struct yetty_yrender_render_target_texture *rt = calloc(1, sizeof(*rt));
    if (!rt) {
        return YETTY_ERR(yetty_yrender_target_ptr, "failed to allocate render target");
    }

    rt->base.ops = &render_target_texture_ops;
    rt->device = device;
    rt->queue = queue;
    rt->format = format;
    rt->allocator = allocator;
    rt->surface = surface; /* NULL for layer/terminal targets */
    rt->visual_zoom_scale = 1.0f;

    /* Binders are created lazily per layer in render_layer(). */

    /* Create initial texture */
    struct yetty_ycore_void_result res = render_target_texture_resize(&rt->base, viewport);
    if (!YETTY_IS_OK(res)) {
        free(rt);
        return YETTY_ERR(yetty_yrender_target_ptr, res.error.msg);
    }

    ydebug("yetty_yrender_target_texture_create: %.0fx%.0f at (%.0f,%.0f) format=%d surface=%p",
           viewport.w, viewport.h, viewport.x, viewport.y, format, (void *)surface);

    /* Spin up a present watchdog for surface-bearing texture targets.
     * Non-surface targets (layer/composite) never call wgpuSurfacePresent
     * so they don't need one. */
    if (surface) {
        rt->watchdog = present_watchdog_start();
    }

    return YETTY_OK(yetty_yrender_target_ptr, &rt->base);
}

void yetty_yrender_target_texture_set_wgpu(struct yetty_ydraw_target *target,
                                           struct yetty_yplatform_wgpu *wgpu)
{
    if (!target) {
        return;
    }
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)target;
    rt->wgpu = wgpu;
    ydebug("render_target_texture: wgpu plumbed in (%p) — present() will use await path",
           (void *)wgpu);
}
