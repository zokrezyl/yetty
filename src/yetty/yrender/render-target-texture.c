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
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>

/* Shader embedded via incbin_add_resources (stubs on Emscripten) */
extern const unsigned char gblend_shaderData[];
extern const unsigned int gblend_shaderSize;
extern const unsigned char gsolid_rects_shaderData[];
extern const unsigned int gsolid_rects_shaderSize;

#define MAX_BLEND_SOURCES 4

/* Per-layer binder cache entry. Each layer's resource_set tree concatenates
 * its own shader (e.g. text-grid + msdf-font, ypaint-grid + ypaint-prims),
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
    struct yetty_ypaint_core_target base; /* viewport stored here */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat format;
    struct yetty_ypaint_core_gpu_allocator *allocator;

    /* Owned texture (size from base.viewport.w/h) */
    WGPUTexture texture;
    WGPUTextureView view;

    /* Optional surface for present() - NULL for layer/terminal targets */
    WGPUSurface surface;

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

    /* Solid-color overlay pipeline (tab strip + future UI chrome).
     * Lazily created on first draw_solid_rects() call — many render
     * targets never paint chrome (per-layer / per-terminal targets), so
     * paying for shader + pipeline + buffers upfront would be wasted on
     * 99% of them. instance_buffer grows-only; we keep the largest size
     * we ever needed to avoid per-frame realloc churn on resize. */
    WGPUShaderModule solid_rects_shader;
    WGPURenderPipeline solid_rects_pipeline;
    WGPUBindGroupLayout solid_rects_bgl;
    WGPUBindGroup solid_rects_bind_group;
    WGPUBuffer solid_rects_uniform_buffer;
    WGPUBuffer solid_rects_instance_buffer;
    size_t solid_rects_instance_capacity;
};

/*=============================================================================
 * Destroy
 *===========================================================================*/

static void render_target_texture_destroy(struct yetty_ypaint_core_target *self)
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
    /* Solid-rects overlay teardown — symmetric with create_solid_rects_pipeline. */
    if (rt->solid_rects_bind_group) {
        wgpuBindGroupRelease(rt->solid_rects_bind_group);
        rt->solid_rects_bind_group = NULL;
    }
    if (rt->solid_rects_uniform_buffer) {
        wgpuBufferDestroy(rt->solid_rects_uniform_buffer);
        wgpuBufferRelease(rt->solid_rects_uniform_buffer);
        rt->solid_rects_uniform_buffer = NULL;
    }
    if (rt->solid_rects_instance_buffer) {
        wgpuBufferDestroy(rt->solid_rects_instance_buffer);
        wgpuBufferRelease(rt->solid_rects_instance_buffer);
        rt->solid_rects_instance_buffer = NULL;
    }
    if (rt->solid_rects_pipeline) {
        wgpuRenderPipelineRelease(rt->solid_rects_pipeline);
        rt->solid_rects_pipeline = NULL;
    }
    if (rt->solid_rects_bgl) {
        wgpuBindGroupLayoutRelease(rt->solid_rects_bgl);
        rt->solid_rects_bgl = NULL;
    }
    if (rt->solid_rects_shader) {
        wgpuShaderModuleRelease(rt->solid_rects_shader);
        rt->solid_rects_shader = NULL;
    }

    free(rt);
}

/*=============================================================================
 * Clear
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_clear(
    struct yetty_ypaint_core_target *self)
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
    struct yetty_ypaint_core_target *self, struct yetty_yrender_viewport viewport)
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

static WGPUTextureView render_target_texture_get_view(const struct yetty_ypaint_core_target *self)
{
    const struct yetty_yrender_render_target_texture *rt =
        (const struct yetty_yrender_render_target_texture *)self;
    return rt->view;
}

static WGPUTexture render_target_texture_get_texture(const struct yetty_ypaint_core_target *self)
{
    const struct yetty_yrender_render_target_texture *rt =
        (const struct yetty_yrender_render_target_texture *)self;
    return rt->texture;
}

/*=============================================================================
 * render_layer - render a terminal layer to this target
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_render_layer(
    struct yetty_ypaint_core_target *self, struct yetty_yrender_terminal_layer *layer)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    /* No per-layer dirty early-out here.
     *
     * The texture state at the start of this layer's pass depends on what
     * earlier layers in the same frame did:
     *   - layer 0 renders with LoadOp_Clear → wipes the entire attachment,
     *     so any non-dirty upper layer (ypaint, ymgui, …) skipping its draw
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

    const struct yetty_ypaint_core_gpu_resource_set *rs = rs_res.value;

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
        wgpuRenderPassEncoderSetViewport(pass, vp.x, vp.y, vp.w, vp.h, 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, (uint32_t)vp.x, (uint32_t)vp.y, (uint32_t)vp.w,
                                            (uint32_t)vp.h);

        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    ytime_start(rt_submit_queue);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    ytime_report(rt_submit_queue);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    ytime_report(rt_gpu);

    /* Clear dirty flag */
    layer->dirty = 0;

    ydebug("render_target_texture: rendered layer");
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
    struct yetty_ypaint_core_target *self, struct yetty_ypaint_core_target **sources, size_t count)
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

static struct yetty_ycore_void_result render_target_texture_present(
    struct yetty_ypaint_core_target *self)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    if (!rt->surface) {
        return YETTY_ERR(yetty_ycore_void, "no surface configured for present");
    }

    /* Acquire surface texture — on X11/VNC (incl. VirtualGL) this can block
	 * waiting for the compositor/VNC-server to hand back a free swapchain
	 * image, so this is one of the prime suspects on slow remote displays. */
    ytime_start(present_acquire);
    WGPUSurfaceTexture surface_texture;
    wgpuSurfaceGetCurrentTexture(rt->surface, &surface_texture);
    ytime_report(present_acquire);

    if (surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return YETTY_ERR(yetty_ycore_void, "surface not ready");
    }

    WGPUTextureView surface_view = wgpuTextureCreateView(surface_texture.texture, NULL);
    if (!surface_view) {
        return YETTY_ERR(yetty_ycore_void, "failed to create surface view");
    }

    /* Create blend pipeline if needed (reuse for present) */
    if (!rt->blend_pipeline) {
        struct yetty_ycore_void_result res = create_blend_pipeline(rt);
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
    wgpuQueueWriteBuffer(rt->queue, rt->uniform_buffer, 0, &uniforms, sizeof(uniforms));

    /* Create bind group with this target's texture as source */
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

    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(rt->device, &bg_desc);
    if (!bind_group) {
        wgpuTextureViewRelease(surface_view);
        return YETTY_ERR(yetty_ycore_void, "failed to create bind group");
    }

    /* Create encoder */
    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(rt->device, &enc_desc);
    if (!encoder) {
        wgpuBindGroupRelease(bind_group);
        wgpuTextureViewRelease(surface_view);
        return YETTY_ERR(yetty_ycore_void, "failed to create encoder");
    }

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

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bind_group);
        wgpuTextureViewRelease(surface_view);
        return YETTY_ERR(yetty_ycore_void, "failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, rt->blend_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    /* Submit the blit-to-surface command buffer */
    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    ytime_start(present_submit);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    ytime_report(present_submit);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bind_group);

    /* Hand texture to the window system. On X11/VNC with VirtualGL this is
	 * where the GPU->CPU readback happens and the image is shipped to the
	 * X server (and then to the VNC client over the network). Expect this
	 * to dominate on remote displays. */
#ifndef __EMSCRIPTEN__
    ytime_start(surface_present);
    wgpuSurfacePresent(rt->surface);
    ytime_report(surface_present);
#endif
    wgpuTextureViewRelease(surface_view);

    ydebug("render_target_texture: presented to surface");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * vtable and create
 *===========================================================================*/

static struct yetty_ycore_void_result render_target_texture_set_visual_zoom(
    struct yetty_ypaint_core_target *self, float scale, float offset_x, float offset_y)
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

/*=============================================================================
 * Solid-rects overlay — colored axis-aligned rects drawn on top of the
 * already-rendered workspace. The tabbar uses it for the Chrome-style strip,
 * but it's a general-purpose hook for any flat-fill UI chrome. Single
 * instanced draw call; one render pass with loadOp=Load so the workspace
 * pixels survive.
 *===========================================================================*/

/* Uniform buffer layout matches solid-rects.wgsl::Uniforms (16 B). */
struct solid_rects_uniforms {
    float target_w;
    float target_h;
    float _pad[2];
};

static struct yetty_ycore_void_result create_solid_rects_pipeline(
    struct yetty_yrender_render_target_texture *rt)
{
    /* Shader module — same incbin convention as blend.wgsl. On Emscripten
     * the shader lives in /data/shaders/ post-extract. */
    WGPUShaderSourceWGSL wgsl_src = {0};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
#ifdef __EMSCRIPTEN__
    struct yetty_yrender_shader_code code = {0};
    yetty_yrender_shader_code_load_file(&code, "/data/shaders/solid-rects.wgsl");
    wgsl_src.code = (WGPUStringView){.data = code.data, .length = code.size};
#else
    wgsl_src.code = (WGPUStringView){.data = (const char *)gsolid_rects_shaderData,
                                     .length = gsolid_rects_shaderSize};
#endif

    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_src;
    rt->solid_rects_shader = wgpuDeviceCreateShaderModule(rt->device, &shader_desc);
    if (!rt->solid_rects_shader) {
        return YETTY_ERR(yetty_ycore_void, "solid_rects: shader module create failed");
    }

    /* Bind group layout: one uniform (target size) bound at binding=0,
     * visible to vertex stage only. */
    WGPUBindGroupLayoutEntry bgl_entry = {0};
    bgl_entry.binding = 0;
    bgl_entry.visibility = WGPUShaderStage_Vertex;
    bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entry.buffer.minBindingSize = sizeof(struct solid_rects_uniforms);

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 1;
    bgl_desc.entries = &bgl_entry;
    rt->solid_rects_bgl = wgpuDeviceCreateBindGroupLayout(rt->device, &bgl_desc);
    if (!rt->solid_rects_bgl) {
        return YETTY_ERR(yetty_ycore_void, "solid_rects: bgl create failed");
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &rt->solid_rects_bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(rt->device, &pl_desc);
    if (!pl) {
        return YETTY_ERR(yetty_ycore_void, "solid_rects: pipeline layout create failed");
    }

    /* Instance attributes: pos(vec2), size(vec2), color(vec4), radii(vec4).
     * 48 B stride. Mirrors struct yetty_yrender_solid_rect one-for-one so a
     * caller's array doubles as the GPU buffer payload. */
    WGPUVertexAttribute attrs[4] = {0};
    attrs[0].format = WGPUVertexFormat_Float32x2;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x2;
    attrs[1].offset = 8;
    attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x4;
    attrs[2].offset = 16;
    attrs[2].shaderLocation = 2;
    attrs[3].format = WGPUVertexFormat_Float32x4;
    attrs[3].offset = 32;
    attrs[3].shaderLocation = 3;

    WGPUVertexBufferLayout vb_layout = {0};
    vb_layout.arrayStride = sizeof(struct yetty_yrender_solid_rect);
    vb_layout.stepMode = WGPUVertexStepMode_Instance;
    vb_layout.attributeCount = 4;
    vb_layout.attributes = attrs;

    /* Alpha blending: rects with a<1.0 alpha-over the underlying pixels so
     * tab cells can fade-tint instead of opaquely overwriting (e.g. inactive
     * tab = semi-transparent overlay on the strip background). */
    WGPUBlendState blend = {0};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState color_target = {0};
    color_target.format = rt->format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {0};
    fragment.module = rt->solid_rects_shader;
    fragment.entryPoint = (WGPUStringView){.data = "fs_main", .length = 7};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.layout = pl;
    rp_desc.vertex.module = rt->solid_rects_shader;
    rp_desc.vertex.entryPoint = (WGPUStringView){.data = "vs_main", .length = 7};
    rp_desc.vertex.bufferCount = 1;
    rp_desc.vertex.buffers = &vb_layout;
    rp_desc.fragment = &fragment;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;

    rt->solid_rects_pipeline = wgpuDeviceCreateRenderPipeline(rt->device, &rp_desc);
    wgpuPipelineLayoutRelease(pl);
    if (!rt->solid_rects_pipeline) {
        return YETTY_ERR(yetty_ycore_void, "solid_rects: pipeline create failed");
    }

    /* Uniform buffer + bind group — created once, contents rewritten per
     * draw via wgpuQueueWriteBuffer. */
    WGPUBufferDescriptor ub_desc = {0};
    ub_desc.label = (WGPUStringView){.data = "solid_rects uniforms", .length = 20};
    ub_desc.size = sizeof(struct solid_rects_uniforms);
    ub_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    rt->solid_rects_uniform_buffer = wgpuDeviceCreateBuffer(rt->device, &ub_desc);
    if (!rt->solid_rects_uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "solid_rects: uniform buffer create failed");
    }

    WGPUBindGroupEntry bg_entry = {0};
    bg_entry.binding = 0;
    bg_entry.buffer = rt->solid_rects_uniform_buffer;
    bg_entry.size = sizeof(struct solid_rects_uniforms);

    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = rt->solid_rects_bgl;
    bg_desc.entryCount = 1;
    bg_desc.entries = &bg_entry;
    rt->solid_rects_bind_group = wgpuDeviceCreateBindGroup(rt->device, &bg_desc);
    if (!rt->solid_rects_bind_group) {
        return YETTY_ERR(yetty_ycore_void, "solid_rects: bind group create failed");
    }

    return YETTY_OK_VOID();
}

/* Grow-only instance buffer. Drops & recreates when count exceeds capacity;
 * keeps the largest size ever needed so resize churn doesn't burn allocations
 * across frames. */
static struct yetty_ycore_void_result ensure_solid_rects_instance_buffer(
    struct yetty_yrender_render_target_texture *rt, size_t count)
{
    if (count <= rt->solid_rects_instance_capacity) {
        return YETTY_OK_VOID();
    }
    /* Round up to power of two to dampen growth cost on incremental adds. */
    size_t new_cap = rt->solid_rects_instance_capacity ? rt->solid_rects_instance_capacity * 2 : 8;
    while (new_cap < count) {
        new_cap *= 2;
    }

    if (rt->solid_rects_instance_buffer) {
        wgpuBufferDestroy(rt->solid_rects_instance_buffer);
        wgpuBufferRelease(rt->solid_rects_instance_buffer);
        rt->solid_rects_instance_buffer = NULL;
    }

    WGPUBufferDescriptor buf_desc = {0};
    buf_desc.label = (WGPUStringView){.data = "solid_rects instances", .length = 21};
    buf_desc.size = new_cap * sizeof(struct yetty_yrender_solid_rect);
    buf_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    rt->solid_rects_instance_buffer = wgpuDeviceCreateBuffer(rt->device, &buf_desc);
    if (!rt->solid_rects_instance_buffer) {
        rt->solid_rects_instance_capacity = 0;
        return YETTY_ERR(yetty_ycore_void, "solid_rects: instance buffer create failed");
    }
    rt->solid_rects_instance_capacity = new_cap;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result render_target_texture_draw_solid_rects(
    struct yetty_ypaint_core_target *self, const struct yetty_yrender_solid_rect *rects,
    size_t count)
{
    struct yetty_yrender_render_target_texture *rt =
        (struct yetty_yrender_render_target_texture *)self;

    if (count == 0) {
        return YETTY_OK_VOID();
    }
    if (!rects) {
        return YETTY_ERR(yetty_ycore_void, "draw_solid_rects: rects is NULL");
    }
    if (!rt->view) {
        return YETTY_ERR(yetty_ycore_void, "draw_solid_rects: target has no view");
    }

    /* Lazy first-call pipeline setup. */
    if (!rt->solid_rects_pipeline) {
        struct yetty_ycore_void_result cr = create_solid_rects_pipeline(rt);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "draw_solid_rects: pipeline create failed");
    }

    struct yetty_ycore_void_result br = ensure_solid_rects_instance_buffer(rt, count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "draw_solid_rects: instance buffer ensure failed");

    /* Push uniforms + instances. wgpuQueueWriteBuffer is the simple route;
     * the data set is tiny (kB-class) and per-frame, no mapping needed.
     *
     * We size NDC against the actual texture dimensions, not base.viewport:
     * pane_render() temporarily overwrites the target's viewport with the
     * pane's bounds as a scissor, and never restores it. By the time the
     * tabbar overlay runs, base.viewport reflects whichever pane was
     * rendered last, not the full target. The texture size, by contrast,
     * is invariant to the per-pane traffic. */
    uint32_t tex_w = rt->texture ? wgpuTextureGetWidth(rt->texture) : (uint32_t)rt->base.viewport.w;
    uint32_t tex_h =
        rt->texture ? wgpuTextureGetHeight(rt->texture) : (uint32_t)rt->base.viewport.h;
    struct solid_rects_uniforms u = {.target_w = (float)tex_w, .target_h = (float)tex_h};
    wgpuQueueWriteBuffer(rt->queue, rt->solid_rects_uniform_buffer, 0, &u, sizeof(u));
    wgpuQueueWriteBuffer(rt->queue, rt->solid_rects_instance_buffer, 0, rects,
                         count * sizeof(*rects));

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(rt->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "draw_solid_rects: encoder create failed");
    }

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = rt->view;
    color_attachment.loadOp = WGPULoadOp_Load; /* preserve workspace pixels */
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "draw_solid_rects: begin pass failed");
    }

    wgpuRenderPassEncoderSetPipeline(pass, rt->solid_rects_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, rt->solid_rects_bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, rt->solid_rects_instance_buffer, 0,
                                         count * sizeof(*rects));
    /* 6 verts per quad, `count` instances. */
    wgpuRenderPassEncoderDraw(pass, 6, (uint32_t)count, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(rt->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

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
    .draw_solid_rects = render_target_texture_draw_solid_rects,
};

struct yetty_yrender_target_ptr_result yetty_yrender_target_texture_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format,
    struct yetty_ypaint_core_gpu_allocator *allocator, WGPUSurface surface,
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
    return YETTY_OK(yetty_yrender_target_ptr, &rt->base);
}
