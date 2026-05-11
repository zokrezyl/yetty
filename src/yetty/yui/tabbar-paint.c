/*
 * tabbar-paint.c — GPU pipeline for the tabbar's SDF rectangle overlay.
 *
 * See tabbar-paint.h. Lives in yui so the rendering is owned by the same
 * module that decides what to draw; yrender stays a pure texture/surface
 * plumbing layer.
 *
 * One pipeline, one bind group, one instance buffer. Each draw() submits
 * a single instanced quad render pass that paints `count` rects in one
 * GPU pass. instance_buffer grows-only; we keep the largest size ever
 * needed to avoid realloc churn on per-frame resize.
 */

#include <stdlib.h>
#include <webgpu/webgpu.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include "tabbar-paint.h"

/* Shader bytes embedded via incbin_add_resources (see yui CMakeLists). */
extern const unsigned char gtabbar_shaderData[];
extern const unsigned int gtabbar_shaderSize;

/* Uniform layout: must match tabbar.wgsl::Uniforms (16 B). */
struct tabbar_paint_uniforms {
    float target_w;
    float target_h;
    float _pad[2];
};

struct yetty_yui_tabbar_paint {
    /* Borrowed (owned by yetty). */
    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat format;

    WGPUShaderModule shader;
    WGPURenderPipeline pipeline;
    WGPUBindGroupLayout bgl;
    WGPUBindGroup bind_group;
    WGPUBuffer uniform_buffer;
    WGPUBuffer instance_buffer;
    size_t instance_capacity;
};

/*---------------------------------------------------------------------------
 * Create
 *--------------------------------------------------------------------------*/

static struct yetty_ycore_void_result build_pipeline(struct yetty_yui_tabbar_paint *paint)
{
    WGPUShaderSourceWGSL wgsl_src = {0};
    wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl_src.code = (WGPUStringView){.data = (const char *)gtabbar_shaderData,
                                     .length = gtabbar_shaderSize};

    WGPUShaderModuleDescriptor shader_desc = {0};
    shader_desc.nextInChain = (WGPUChainedStruct *)&wgsl_src;
    paint->shader = wgpuDeviceCreateShaderModule(paint->device, &shader_desc);
    if (!paint->shader) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: shader module create failed");
    }

    /* Bind group layout: one uniform (target size) at binding 0, vertex stage. */
    WGPUBindGroupLayoutEntry bgl_entry = {0};
    bgl_entry.binding = 0;
    bgl_entry.visibility = WGPUShaderStage_Vertex;
    bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entry.buffer.minBindingSize = sizeof(struct tabbar_paint_uniforms);

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 1;
    bgl_desc.entries = &bgl_entry;
    paint->bgl = wgpuDeviceCreateBindGroupLayout(paint->device, &bgl_desc);
    if (!paint->bgl) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: bgl create failed");
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &paint->bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(paint->device, &pl_desc);
    if (!pl) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: pipeline layout create failed");
    }

    /* Instance attributes — must mirror yetty_yui_tabbar_rect:
     *   loc 0 vec2 pos     @ 0
     *   loc 1 vec2 size    @ 8
     *   loc 2 vec4 color   @ 16
     *   loc 3 vec4 radii   @ 32
     *   loc 4 f32  rotation@ 48
     *   stride 52 B */
    WGPUVertexAttribute attrs[5] = {0};
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
    attrs[4].format = WGPUVertexFormat_Float32;
    attrs[4].offset = 48;
    attrs[4].shaderLocation = 4;

    WGPUVertexBufferLayout vb_layout = {0};
    vb_layout.arrayStride = sizeof(struct yetty_yui_tabbar_rect);
    vb_layout.stepMode = WGPUVertexStepMode_Instance;
    vb_layout.attributeCount = 5;
    vb_layout.attributes = attrs;

    /* Straight alpha-over blending: SDF antialiasing produces alpha<1 at
     * edges and rotated rects, so we need real blending to avoid harsh
     * jaggies. */
    WGPUBlendState blend = {0};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState color_target = {0};
    color_target.format = paint->format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = {0};
    fragment.module = paint->shader;
    fragment.entryPoint = (WGPUStringView){.data = "fs_main", .length = 7};
    fragment.targetCount = 1;
    fragment.targets = &color_target;

    WGPURenderPipelineDescriptor rp_desc = {0};
    rp_desc.layout = pl;
    rp_desc.vertex.module = paint->shader;
    rp_desc.vertex.entryPoint = (WGPUStringView){.data = "vs_main", .length = 7};
    rp_desc.vertex.bufferCount = 1;
    rp_desc.vertex.buffers = &vb_layout;
    rp_desc.fragment = &fragment;
    rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
    rp_desc.primitive.cullMode = WGPUCullMode_None;
    rp_desc.multisample.count = 1;
    rp_desc.multisample.mask = ~0u;

    paint->pipeline = wgpuDeviceCreateRenderPipeline(paint->device, &rp_desc);
    wgpuPipelineLayoutRelease(pl);
    if (!paint->pipeline) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: pipeline create failed");
    }

    /* Uniform buffer + bind group — fixed shape, contents rewritten per draw. */
    WGPUBufferDescriptor ub_desc = {0};
    ub_desc.label = (WGPUStringView){.data = "tabbar_paint uniforms", .length = 21};
    ub_desc.size = sizeof(struct tabbar_paint_uniforms);
    ub_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    paint->uniform_buffer = wgpuDeviceCreateBuffer(paint->device, &ub_desc);
    if (!paint->uniform_buffer) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: uniform buffer create failed");
    }

    WGPUBindGroupEntry bg_entry = {0};
    bg_entry.binding = 0;
    bg_entry.buffer = paint->uniform_buffer;
    bg_entry.size = sizeof(struct tabbar_paint_uniforms);

    WGPUBindGroupDescriptor bg_desc = {0};
    bg_desc.layout = paint->bgl;
    bg_desc.entryCount = 1;
    bg_desc.entries = &bg_entry;
    paint->bind_group = wgpuDeviceCreateBindGroup(paint->device, &bg_desc);
    if (!paint->bind_group) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: bind group create failed");
    }

    return YETTY_OK_VOID();
}

struct yetty_yui_tabbar_paint_ptr_result yetty_yui_tabbar_paint_create(
    WGPUDevice device, WGPUQueue queue, WGPUTextureFormat format)
{
    if (!device || !queue) {
        return YETTY_ERR(yetty_yui_tabbar_paint_ptr,
                         "tabbar_paint_create: device/queue required");
    }
    struct yetty_yui_tabbar_paint *paint = calloc(1, sizeof(*paint));
    if (!paint) {
        return YETTY_ERR(yetty_yui_tabbar_paint_ptr, "tabbar_paint_create: alloc failed");
    }
    paint->device = device;
    paint->queue = queue;
    paint->format = format;

    struct yetty_ycore_void_result br = build_pipeline(paint);
    if (YETTY_IS_ERR(br)) {
        yetty_yui_tabbar_paint_destroy(paint);
        return YETTY_ERR(yetty_yui_tabbar_paint_ptr, "tabbar_paint_create: pipeline failed", br);
    }
    return YETTY_OK(yetty_yui_tabbar_paint_ptr, paint);
}

/*---------------------------------------------------------------------------
 * Destroy
 *--------------------------------------------------------------------------*/

void yetty_yui_tabbar_paint_destroy(struct yetty_yui_tabbar_paint *paint)
{
    if (!paint) {
        return;
    }
    if (paint->bind_group) {
        wgpuBindGroupRelease(paint->bind_group);
    }
    if (paint->uniform_buffer) {
        wgpuBufferDestroy(paint->uniform_buffer);
        wgpuBufferRelease(paint->uniform_buffer);
    }
    if (paint->instance_buffer) {
        wgpuBufferDestroy(paint->instance_buffer);
        wgpuBufferRelease(paint->instance_buffer);
    }
    if (paint->pipeline) {
        wgpuRenderPipelineRelease(paint->pipeline);
    }
    if (paint->bgl) {
        wgpuBindGroupLayoutRelease(paint->bgl);
    }
    if (paint->shader) {
        wgpuShaderModuleRelease(paint->shader);
    }
    free(paint);
}

/*---------------------------------------------------------------------------
 * Draw
 *--------------------------------------------------------------------------*/

/* Grow-only instance buffer. Drops & recreates when count exceeds capacity;
 * keeps the largest size ever needed so add/close-tab churn doesn't burn
 * allocations across frames. */
static struct yetty_ycore_void_result ensure_instance_buffer(
    struct yetty_yui_tabbar_paint *paint, size_t count)
{
    if (count <= paint->instance_capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = paint->instance_capacity ? paint->instance_capacity * 2 : 16;
    while (new_cap < count) {
        new_cap *= 2;
    }

    if (paint->instance_buffer) {
        wgpuBufferDestroy(paint->instance_buffer);
        wgpuBufferRelease(paint->instance_buffer);
        paint->instance_buffer = NULL;
    }

    WGPUBufferDescriptor buf_desc = {0};
    buf_desc.label = (WGPUStringView){.data = "tabbar_paint instances", .length = 22};
    buf_desc.size = new_cap * sizeof(struct yetty_yui_tabbar_rect);
    buf_desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    paint->instance_buffer = wgpuDeviceCreateBuffer(paint->device, &buf_desc);
    if (!paint->instance_buffer) {
        paint->instance_capacity = 0;
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint: instance buffer create failed");
    }
    paint->instance_capacity = new_cap;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tabbar_paint_draw(
    struct yetty_yui_tabbar_paint *paint, WGPUTextureView view, uint32_t target_w,
    uint32_t target_h, const struct yetty_yui_tabbar_rect *rects, size_t count)
{
    if (!paint) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint_draw: NULL paint");
    }
    if (count == 0) {
        return YETTY_OK_VOID();
    }
    if (!rects) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint_draw: rects NULL");
    }
    if (!view) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint_draw: view NULL");
    }

    struct yetty_ycore_void_result br = ensure_instance_buffer(paint, count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "tabbar_paint_draw: instance buffer ensure failed");

    /* Push uniforms + instances. wgpuQueueWriteBuffer is the simple route;
     * the data set is tiny (kB-class) and per-frame, no map/unmap needed. */
    struct tabbar_paint_uniforms u = {.target_w = (float)target_w, .target_h = (float)target_h};
    wgpuQueueWriteBuffer(paint->queue, paint->uniform_buffer, 0, &u, sizeof(u));
    wgpuQueueWriteBuffer(paint->queue, paint->instance_buffer, 0, rects,
                         count * sizeof(*rects));

    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(paint->device, &enc_desc);
    if (!encoder) {
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint_draw: encoder create failed");
    }

    WGPURenderPassColorAttachment color_attachment = {0};
    color_attachment.view = view;
    color_attachment.loadOp = WGPULoadOp_Load; /* preserve workspace pixels */
    color_attachment.storeOp = WGPUStoreOp_Store;
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_desc = {0};
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "tabbar_paint_draw: begin pass failed");
    }

    wgpuRenderPassEncoderSetPipeline(pass, paint->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, paint->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, paint->instance_buffer, 0,
                                         count * sizeof(*rects));
    /* 6 vertices per rect (two triangles), `count` instances. */
    wgpuRenderPassEncoderDraw(pass, 6, (uint32_t)count, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(paint->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    return YETTY_OK_VOID();
}
