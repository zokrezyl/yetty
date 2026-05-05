/*
 * ymesh-gen.c — concrete factory for the ymesh complex primitive.
 *
 * Unlike yimage / yplot, ymesh uses raw WebGPU directly (not the schema-
 * driven yetty_yrender_pipeline / gpu_resource_binder framework). The
 * reason is that ymesh genuinely needs:
 *   - a real vertex layout (vec3 position + vec3 normal in two buffers)
 *   - an index buffer (drawIndexed)
 *   - a depth attachment for correct rendering of overlapping geometry
 * none of which the existing fullscreen-quad framework expresses.
 *
 * Each instance owns its own:
 *   - position / normal vertex buffers, index buffer
 *   - 3D uniform buffer (MVP, model, normal-matrix, light, base color)
 *   - 3D bind group
 *   - offscreen RGBA8 color attachment + depth16 attachment + views
 *   - blit uniform buffer + blit bind group
 *
 * render() runs two passes:
 *   1) 3D pass: clear+depth-clear the offscreen target, drawIndexed.
 *   2) blit pass: fullscreen-triangle on the layer target with LoadOp=Load,
 *      sampling the offscreen color into the bounds rect.
 */

#include <yetty/ymesh/ymesh-gen.h>
#include <yetty/ymesh/ymesh-math.h>

#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <webgpu/webgpu.h>

/* WebGPU string-view literal helper — newer dawn replaced const char* fields
 * with WGPUStringView (.data + .length) for label / code / entryPoint. */
#define YMS_STR(s) ((WGPUStringView){.data = (s), .length = sizeof(s) - 1})

extern const unsigned char gymesh_3d_shaderData[];
extern const unsigned int  gymesh_3d_shaderSize;
extern const unsigned char gymesh_blit_shaderData[];
extern const unsigned int  gymesh_blit_shaderSize;

/* Cap the offscreen render texture so a huge bounds_w/h doesn't allocate
 * a multi-megabyte target. The blit pass scales with linear sampling. */
#define YMESH_OFFSCREEN_MAX 1024u
#define YMESH_OFFSCREEN_MIN 16u

/*=============================================================================
 * 3D-pass uniform layout (must match Uniforms struct in ymesh.wgsl).
 *===========================================================================*/

struct ymesh_3d_uniforms {
    float mvp[16];
    float model[16];
    float normal_matrix[16];
    float light_dir[4];
    float base_color[4];
};

/* Blit-pass uniforms. vec4 + vec2 + vec2 padding = 32 bytes; exceeds the
 * 16-byte uniform-buffer min alignment WebGPU requires. */
struct ymesh_blit_uniforms {
    float bounds[4];     /* x, y, w, h */
    float viewport[2];   /* w, h */
    float _pad[2];
};

/*=============================================================================
 * Factory
 *===========================================================================*/

struct yetty_ymesh_factory {
    struct yetty_ypaint_core_concrete_factory base;

    WGPUDevice device;
    WGPUQueue queue;
    WGPUTextureFormat target_format;

    WGPUShaderModule shader_3d;
    WGPUShaderModule shader_blit;

    WGPUBindGroupLayout bgl_3d;
    WGPUBindGroupLayout bgl_blit;
    WGPUPipelineLayout layout_3d;
    WGPUPipelineLayout layout_blit;

    WGPURenderPipeline pipeline_3d;
    WGPURenderPipeline pipeline_blit;

    WGPUSampler linear_sampler;

    int initialized;
};

static struct yetty_ymesh_factory *
ymesh_factory_from_base(struct yetty_ypaint_core_concrete_factory *base)
{
    return (struct yetty_ymesh_factory *)base;
}

/*=============================================================================
 * Per-instance state — kept in instance->instance_data.
 *===========================================================================*/

struct ymesh_instance_data {
    /* Mesh GPU buffers. */
    WGPUBuffer pos_vb;
    WGPUBuffer nrm_vb;
    WGPUBuffer index_buf;
    uint32_t   index_count;
    uint32_t   index_size;   /* 2 or 4; MVP always 4 */

    /* 3D-pass uniform buffer + bind group. */
    WGPUBuffer ub_3d;
    WGPUBindGroup bg_3d;

    /* Offscreen color + depth. */
    uint32_t off_w, off_h;
    WGPUTexture color_tex;
    WGPUTextureView color_view;
    WGPUTexture depth_tex;
    WGPUTextureView depth_view;

    /* Blit-pass uniform buffer + bind group. */
    WGPUBuffer ub_blit;
    WGPUBindGroup bg_blit;
};

/*=============================================================================
 * Helpers
 *===========================================================================*/

static WGPUBuffer ymesh_create_buffer_with_data(
    WGPUDevice device, WGPUQueue queue, WGPUBufferUsage usage,
    const void *data, size_t size)
{
    WGPUBufferDescriptor desc = {0};
    desc.usage = usage | WGPUBufferUsage_CopyDst;
    desc.size = (size + 3) & ~(size_t)3;  /* WebGPU requires 4-byte alignment */
    desc.mappedAtCreation = false;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(device, &desc);
    if (!buf)
        return NULL;
    if (data && size > 0)
        wgpuQueueWriteBuffer(queue, buf, 0, data, (size + 3) & ~(size_t)3);
    return buf;
}

static uint32_t ymesh_clamp_offscreen_dim(float v)
{
    if (v < (float)YMESH_OFFSCREEN_MIN) return YMESH_OFFSCREEN_MIN;
    if (v > (float)YMESH_OFFSCREEN_MAX) return YMESH_OFFSCREEN_MAX;
    return (uint32_t)v;
}

/*=============================================================================
 * Instance render
 *===========================================================================*/

static struct yetty_ycore_void_result
ymesh_compute_3d_uniforms(const float bbox_min[3], const float bbox_max[3],
                          float aspect, struct ymesh_3d_uniforms *out)
{
    /* Centre + radius from the bbox. */
    struct yetty_ymesh_vec3 center = {
        (bbox_min[0] + bbox_max[0]) * 0.5f,
        (bbox_min[1] + bbox_max[1]) * 0.5f,
        (bbox_min[2] + bbox_max[2]) * 0.5f,
    };
    float dx = bbox_max[0] - bbox_min[0];
    float dy = bbox_max[1] - bbox_min[1];
    float dz = bbox_max[2] - bbox_min[2];
    float radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    if (radius < 1e-4f)
        radius = 1.0f;

    /* Camera orbits the centre at ~3× radius, slight elevation. */
    float dist = radius * 3.0f;
    struct yetty_ymesh_vec3 eye = {
        center.x + dist * 0.7f,
        center.y + dist * 0.5f,
        center.z + dist * 0.7f,
    };
    struct yetty_ymesh_vec3 up = {0.0f, 1.0f, 0.0f};

    struct yetty_ymesh_mat4 model = yetty_ymesh_mat4_identity();
    struct yetty_ymesh_mat4 view = yetty_ymesh_mat4_lookat(eye, center, up);
    struct yetty_ymesh_mat4 proj = yetty_ymesh_mat4_perspective(
        45.0f * 3.14159265f / 180.0f, aspect, radius * 0.1f, dist + radius * 4.0f);
    struct yetty_ymesh_mat4 mvp = yetty_ymesh_mat4_mul(proj, yetty_ymesh_mat4_mul(view, model));
    /* model is identity → normal_matrix = identity too. */

    memcpy(out->mvp, mvp.m, sizeof(mvp.m));
    memcpy(out->model, model.m, sizeof(model.m));
    memcpy(out->normal_matrix, model.m, sizeof(model.m));
    out->light_dir[0] = -0.4f; out->light_dir[1] = 0.8f; out->light_dir[2] = -0.5f; out->light_dir[3] = 0.0f;
    out->base_color[0] = 0.85f; out->base_color[1] = 0.85f; out->base_color[2] = 0.95f; out->base_color[3] = 1.0f;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result
ymesh_instance_render(struct yetty_ypaint_core_complex_prim_instance *self,
                      struct yetty_ypaint_core_target *target, float x, float y)
{
    if (!self || !self->buffer_data || !self->factory)
        return YETTY_ERR(yetty_ycore_void, "ymesh: invalid instance");
    struct yetty_ymesh_factory *factory = ymesh_factory_from_base(self->factory);
    if (!factory->initialized)
        return YETTY_ERR(yetty_ycore_void, "ymesh: factory not initialized");
    struct ymesh_instance_data *inst = (struct ymesh_instance_data *)self->instance_data;
    if (!inst)
        return YETTY_ERR(yetty_ycore_void, "ymesh: instance data missing");

    /* Wire bounds (w/h come from wire; x/y are passed in by the canvas). */
    const uint32_t *data = (const uint32_t *)self->buffer_data;
    const uint32_t *payload = data + 2;
    float bw = *(const float *)&payload[2];
    float bh = *(const float *)&payload[3];

    /* --- update blit uniforms --- */
    struct ymesh_blit_uniforms bu = {0};
    bu.bounds[0] = x;
    bu.bounds[1] = y;
    bu.bounds[2] = bw;
    bu.bounds[3] = bh;
    bu.viewport[0] = target->viewport.w;
    bu.viewport[1] = target->viewport.h;
    wgpuQueueWriteBuffer(factory->queue, inst->ub_blit, 0, &bu, sizeof(bu));

    /* --- pass 1: 3D into offscreen --- */
    WGPUCommandEncoderDescriptor enc_desc = {0};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(factory->device, &enc_desc);
    if (!encoder)
        return YETTY_ERR(yetty_ycore_void, "ymesh: encoder create failed");

    WGPURenderPassColorAttachment off_color = {0};
    off_color.view = inst->color_view;
    off_color.loadOp = WGPULoadOp_Clear;
    off_color.storeOp = WGPUStoreOp_Store;
    off_color.clearValue = (WGPUColor){0.05, 0.05, 0.07, 0.0};
    off_color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDepthStencilAttachment off_depth = {0};
    off_depth.view = inst->depth_view;
    off_depth.depthLoadOp = WGPULoadOp_Clear;
    off_depth.depthStoreOp = WGPUStoreOp_Store;
    off_depth.depthClearValue = 1.0f;
    off_depth.depthReadOnly = false;
    off_depth.stencilLoadOp = WGPULoadOp_Undefined;
    off_depth.stencilStoreOp = WGPUStoreOp_Undefined;

    WGPURenderPassDescriptor pass3d = {0};
    pass3d.colorAttachmentCount = 1;
    pass3d.colorAttachments = &off_color;
    pass3d.depthStencilAttachment = &off_depth;

    WGPURenderPassEncoder p1 = wgpuCommandEncoderBeginRenderPass(encoder, &pass3d);
    if (!p1) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "ymesh: 3d pass begin failed");
    }
    wgpuRenderPassEncoderSetPipeline(p1, factory->pipeline_3d);
    wgpuRenderPassEncoderSetBindGroup(p1, 0, inst->bg_3d, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(p1, 0, inst->pos_vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(p1, 1, inst->nrm_vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(p1, inst->index_buf,
        inst->index_size == 2 ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32,
        0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(p1, inst->index_count, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(p1);
    wgpuRenderPassEncoderRelease(p1);

    /* --- pass 2: blit into layer target --- */
    WGPUTextureView layer_view = target->ops->get_view(target);
    if (!layer_view) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "ymesh: target get_view failed");
    }

    WGPURenderPassColorAttachment blit_color = {0};
    blit_color.view = layer_view;
    blit_color.loadOp = WGPULoadOp_Load;
    blit_color.storeOp = WGPUStoreOp_Store;
    blit_color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor pass_blit_desc = {0};
    pass_blit_desc.colorAttachmentCount = 1;
    pass_blit_desc.colorAttachments = &blit_color;

    WGPURenderPassEncoder p2 = wgpuCommandEncoderBeginRenderPass(encoder, &pass_blit_desc);
    if (!p2) {
        wgpuCommandEncoderRelease(encoder);
        return YETTY_ERR(yetty_ycore_void, "ymesh: blit pass begin failed");
    }
    wgpuRenderPassEncoderSetViewport(p2, 0.0f, 0.0f,
        target->viewport.w, target->viewport.h, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(p2, 0, 0,
        (uint32_t)target->viewport.w, (uint32_t)target->viewport.h);
    wgpuRenderPassEncoderSetPipeline(p2, factory->pipeline_blit);
    wgpuRenderPassEncoderSetBindGroup(p2, 0, inst->bg_blit, 0, NULL);
    wgpuRenderPassEncoderDraw(p2, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(p2);
    wgpuRenderPassEncoderRelease(p2);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(factory->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    ydebug("ymesh_render: at (%.1f,%.1f) size %.1fx%.1f indices=%u",
           x, y, bw, bh, inst->index_count);
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Pipeline / layout creation
 *===========================================================================*/

static WGPUShaderModule
ymesh_create_shader(WGPUDevice device, const char *label,
                    const unsigned char *src, unsigned int len)
{
    char *code = malloc(len + 1);
    if (!code)
        return NULL;
    memcpy(code, src, len);
    code[len] = '\0';

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code.data = code;
    wgsl.code.length = len;

    WGPUShaderModuleDescriptor desc = {0};
    desc.nextInChain = &wgsl.chain;
    desc.label.data = label;
    desc.label.length = label ? strlen(label) : 0;

    WGPUShaderModule mod = wgpuDeviceCreateShaderModule(device, &desc);
    free(code);
    return mod;
}

static struct yetty_ycore_void_result
ymesh_build_3d_pipeline(struct yetty_ymesh_factory *f)
{
    /* Bind group layout: one uniform buffer at binding 0, vertex+fragment vis. */
    WGPUBindGroupLayoutEntry bgl_entries[1] = {0};
    bgl_entries[0].binding = 0;
    bgl_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entries[0].buffer.hasDynamicOffset = false;
    bgl_entries[0].buffer.minBindingSize = sizeof(struct ymesh_3d_uniforms);

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.label = YMS_STR("ymesh_3d_bgl");
    bgl_desc.entryCount = 1;
    bgl_desc.entries = bgl_entries;
    f->bgl_3d = wgpuDeviceCreateBindGroupLayout(f->device, &bgl_desc);
    if (!f->bgl_3d)
        return YETTY_ERR(yetty_ycore_void, "ymesh: bgl_3d failed");

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.label = YMS_STR("ymesh_3d_layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &f->bgl_3d;
    f->layout_3d = wgpuDeviceCreatePipelineLayout(f->device, &pl_desc);
    if (!f->layout_3d)
        return YETTY_ERR(yetty_ycore_void, "ymesh: layout_3d failed");

    /* Vertex layout: two buffers (positions, normals), each vec3 f32. */
    WGPUVertexAttribute attr_pos = {0};
    attr_pos.format = WGPUVertexFormat_Float32x3;
    attr_pos.offset = 0;
    attr_pos.shaderLocation = 0;

    WGPUVertexAttribute attr_nrm = {0};
    attr_nrm.format = WGPUVertexFormat_Float32x3;
    attr_nrm.offset = 0;
    attr_nrm.shaderLocation = 1;

    WGPUVertexBufferLayout vb_layouts[2] = {0};
    vb_layouts[0].arrayStride = 3 * sizeof(float);
    vb_layouts[0].stepMode = WGPUVertexStepMode_Vertex;
    vb_layouts[0].attributeCount = 1;
    vb_layouts[0].attributes = &attr_pos;
    vb_layouts[1].arrayStride = 3 * sizeof(float);
    vb_layouts[1].stepMode = WGPUVertexStepMode_Vertex;
    vb_layouts[1].attributeCount = 1;
    vb_layouts[1].attributes = &attr_nrm;

    WGPUColorTargetState color_target = {0};
    color_target.format = WGPUTextureFormat_RGBA8Unorm;  /* offscreen color */
    color_target.writeMask = WGPUColorWriteMask_All;
    /* No blending — overwrite (offscreen is cleared each pass). */

    WGPUFragmentState frag = {0};
    frag.module = f->shader_3d;
    frag.entryPoint = YMS_STR("fs_main");
    frag.targetCount = 1;
    frag.targets = &color_target;

    WGPUDepthStencilState ds = {0};
    ds.format = WGPUTextureFormat_Depth16Unorm;
    ds.depthWriteEnabled = true;
    ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilFront.compare = WGPUCompareFunction_Always;
    ds.stencilFront.failOp = WGPUStencilOperation_Keep;
    ds.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    ds.stencilFront.passOp = WGPUStencilOperation_Keep;
    ds.stencilBack = ds.stencilFront;

    WGPURenderPipelineDescriptor rp = {0};
    rp.label = YMS_STR("ymesh_3d_pipeline");
    rp.layout = f->layout_3d;
    rp.vertex.module = f->shader_3d;
    rp.vertex.entryPoint = YMS_STR("vs_main");
    rp.vertex.bufferCount = 2;
    rp.vertex.buffers = vb_layouts;
    rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp.primitive.cullMode = WGPUCullMode_Back;
    rp.primitive.frontFace = WGPUFrontFace_CCW;
    rp.depthStencil = &ds;
    rp.multisample.count = 1;
    rp.multisample.mask = 0xFFFFFFFFu;
    rp.fragment = &frag;

    f->pipeline_3d = wgpuDeviceCreateRenderPipeline(f->device, &rp);
    if (!f->pipeline_3d)
        return YETTY_ERR(yetty_ycore_void, "ymesh: 3d pipeline create failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result
ymesh_build_blit_pipeline(struct yetty_ymesh_factory *f)
{
    WGPUBindGroupLayoutEntry bgl_entries[3] = {0};
    bgl_entries[0].binding = 0;
    bgl_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entries[0].buffer.minBindingSize = sizeof(struct ymesh_blit_uniforms);

    bgl_entries[1].binding = 1;
    bgl_entries[1].visibility = WGPUShaderStage_Fragment;
    bgl_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bgl_entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    bgl_entries[1].texture.multisampled = false;

    bgl_entries[2].binding = 2;
    bgl_entries[2].visibility = WGPUShaderStage_Fragment;
    bgl_entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.label = YMS_STR("ymesh_blit_bgl");
    bgl_desc.entryCount = 3;
    bgl_desc.entries = bgl_entries;
    f->bgl_blit = wgpuDeviceCreateBindGroupLayout(f->device, &bgl_desc);
    if (!f->bgl_blit)
        return YETTY_ERR(yetty_ycore_void, "ymesh: bgl_blit failed");

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.label = YMS_STR("ymesh_blit_layout");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &f->bgl_blit;
    f->layout_blit = wgpuDeviceCreatePipelineLayout(f->device, &pl_desc);
    if (!f->layout_blit)
        return YETTY_ERR(yetty_ycore_void, "ymesh: layout_blit failed");

    /* Composite over the layer target — premultiplied-alpha blend. */
    WGPUBlendState blend = {0};
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_One;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

    WGPUColorTargetState color_target = {0};
    color_target.format = f->target_format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState frag = {0};
    frag.module = f->shader_blit;
    frag.entryPoint = YMS_STR("fs_main");
    frag.targetCount = 1;
    frag.targets = &color_target;

    WGPURenderPipelineDescriptor rp = {0};
    rp.label = YMS_STR("ymesh_blit_pipeline");
    rp.layout = f->layout_blit;
    rp.vertex.module = f->shader_blit;
    rp.vertex.entryPoint = YMS_STR("vs_main");
    rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp.primitive.cullMode = WGPUCullMode_None;
    rp.primitive.frontFace = WGPUFrontFace_CCW;
    rp.multisample.count = 1;
    rp.multisample.mask = 0xFFFFFFFFu;
    rp.fragment = &frag;

    f->pipeline_blit = wgpuDeviceCreateRenderPipeline(f->device, &rp);
    if (!f->pipeline_blit)
        return YETTY_ERR(yetty_ycore_void, "ymesh: blit pipeline create failed");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result
ymesh_compile_pipeline(struct yetty_ypaint_core_concrete_factory *self,
                       WGPUDevice device, WGPUQueue queue,
                       WGPUTextureFormat target_format,
                       struct yetty_ypaint_core_gpu_allocator *allocator)
{
    (void)allocator;
    struct yetty_ymesh_factory *f = ymesh_factory_from_base(self);
    if (f->initialized)
        return YETTY_OK_VOID();

    f->device = device;
    f->queue = queue;
    f->target_format = target_format;

    f->shader_3d = ymesh_create_shader(device, "ymesh_3d",
        gymesh_3d_shaderData, gymesh_3d_shaderSize);
    if (!f->shader_3d)
        return YETTY_ERR(yetty_ycore_void, "ymesh: 3d shader compile failed");
    f->shader_blit = ymesh_create_shader(device, "ymesh_blit",
        gymesh_blit_shaderData, gymesh_blit_shaderSize);
    if (!f->shader_blit)
        return YETTY_ERR(yetty_ycore_void, "ymesh: blit shader compile failed");

    struct yetty_ycore_void_result r = ymesh_build_3d_pipeline(f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymesh: 3d pipeline build");
    r = ymesh_build_blit_pipeline(f);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ymesh: blit pipeline build");

    WGPUSamplerDescriptor smp_desc = {0};
    smp_desc.label = YMS_STR("ymesh_blit_sampler");
    smp_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    smp_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    smp_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    smp_desc.magFilter = WGPUFilterMode_Linear;
    smp_desc.minFilter = WGPUFilterMode_Linear;
    smp_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    smp_desc.lodMinClamp = 0.0f;
    smp_desc.lodMaxClamp = 1.0f;
    smp_desc.maxAnisotropy = 1;
    f->linear_sampler = wgpuDeviceCreateSampler(device, &smp_desc);
    if (!f->linear_sampler)
        return YETTY_ERR(yetty_ycore_void, "ymesh: sampler create failed");

    f->initialized = 1;
    yinfo("ymesh: pipelines compiled (3d + blit)");
    return YETTY_OK_VOID();
}

static WGPURenderPipeline
ymesh_get_pipeline(struct yetty_ypaint_core_concrete_factory *self)
{
    /* Returned only for informational use by the abstract factory. The
     * blit pipeline is the one that actually draws into the layer. */
    struct yetty_ymesh_factory *f = ymesh_factory_from_base(self);
    return f->pipeline_blit;
}

/*=============================================================================
 * Instance create / destroy
 *===========================================================================*/

static void ymesh_instance_data_destroy(struct ymesh_instance_data *d)
{
    if (!d)
        return;
    if (d->bg_blit)    wgpuBindGroupRelease(d->bg_blit);
    if (d->bg_3d)      wgpuBindGroupRelease(d->bg_3d);
    if (d->ub_blit)    wgpuBufferRelease(d->ub_blit);
    if (d->ub_3d)      wgpuBufferRelease(d->ub_3d);
    if (d->color_view) wgpuTextureViewRelease(d->color_view);
    if (d->color_tex)  wgpuTextureRelease(d->color_tex);
    if (d->depth_view) wgpuTextureViewRelease(d->depth_view);
    if (d->depth_tex)  wgpuTextureRelease(d->depth_tex);
    if (d->index_buf)  wgpuBufferRelease(d->index_buf);
    if (d->nrm_vb)     wgpuBufferRelease(d->nrm_vb);
    if (d->pos_vb)     wgpuBufferRelease(d->pos_vb);
    free(d);
}

static struct yetty_ypaint_core_complex_prim_instance_ptr_result
ymesh_create_instance(struct yetty_ypaint_core_concrete_factory *self,
                      const void *buffer_data, size_t size, uint32_t rolling_row)
{
    if (!buffer_data || size < sizeof(struct yetty_ypaint_core_complex_prim))
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: invalid buffer data");

    struct yetty_ymesh_factory *factory = ymesh_factory_from_base(self);
    if (!factory->initialized)
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: factory not initialized");

    /* Parse wire layout. */
    const uint32_t *data = (const uint32_t *)buffer_data;
    const uint32_t *payload = data + 2;

    float bx = *(const float *)&payload[0];
    float by = *(const float *)&payload[1];
    float bw = *(const float *)&payload[2];
    float bh = *(const float *)&payload[3];
    (void)bx; (void)by;

    float bbox_min[3], bbox_max[3];
    memcpy(bbox_min, &payload[4], 3 * sizeof(float));
    memcpy(bbox_max, &payload[7], 3 * sizeof(float));

    uint32_t vcount    = payload[10];
    uint32_t icount    = payload[11];
    uint32_t isize     = payload[12];
    if (isize != 4 && isize != 2)
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: bad index_size");
    if (vcount == 0 || icount == 0)
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: empty mesh");

    const float *positions = (const float *)(payload + 13);
    const float *normals   = positions + vcount * 3;
    const void  *indices   = (const void *)(normals + vcount * 3);

    /* Allocate instance + per-instance data. */
    struct yetty_ypaint_core_complex_prim_instance *inst =
        calloc(1, sizeof(struct yetty_ypaint_core_complex_prim_instance));
    if (!inst)
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "ymesh: inst alloc failed");
    struct ymesh_instance_data *d = calloc(1, sizeof(struct ymesh_instance_data));
    if (!d) {
        free(inst);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "ymesh: data alloc failed");
    }

    inst->buffer_data = malloc(size);
    if (!inst->buffer_data) {
        free(d);
        free(inst);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr, "ymesh: wire copy failed");
    }
    memcpy(inst->buffer_data, buffer_data, size);
    inst->buffer_size = size;
    inst->type = YETTY_YMESH_TYPE_ID;
    inst->factory = self;
    inst->rolling_row = rolling_row;
    inst->render = ymesh_instance_render;
    inst->instance_data = d;

    struct rectangle_result aabb_res =
        yetty_ypaint_core_complex_prim_aabb(buffer_data);
    if (YETTY_IS_OK(aabb_res))
        inst->bounds = aabb_res.value;

    /* GPU buffers — uploaded once, reused per frame. */
    d->index_count = icount;
    d->index_size = isize;
    d->pos_vb = ymesh_create_buffer_with_data(factory->device, factory->queue,
        WGPUBufferUsage_Vertex, positions, vcount * 3 * sizeof(float));
    d->nrm_vb = ymesh_create_buffer_with_data(factory->device, factory->queue,
        WGPUBufferUsage_Vertex, normals, vcount * 3 * sizeof(float));
    d->index_buf = ymesh_create_buffer_with_data(factory->device, factory->queue,
        WGPUBufferUsage_Index, indices, icount * isize);
    if (!d->pos_vb || !d->nrm_vb || !d->index_buf) {
        ymesh_instance_data_destroy(d);
        free(inst->buffer_data);
        free(inst);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: vertex/index buffer alloc failed");
    }

    /* Offscreen render textures. */
    d->off_w = ymesh_clamp_offscreen_dim(bw);
    d->off_h = ymesh_clamp_offscreen_dim(bh);

    WGPUTextureDescriptor color_desc = {0};
    color_desc.label = YMS_STR("ymesh_color");
    color_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    color_desc.dimension = WGPUTextureDimension_2D;
    color_desc.size.width = d->off_w;
    color_desc.size.height = d->off_h;
    color_desc.size.depthOrArrayLayers = 1;
    color_desc.format = WGPUTextureFormat_RGBA8Unorm;
    color_desc.mipLevelCount = 1;
    color_desc.sampleCount = 1;
    d->color_tex = wgpuDeviceCreateTexture(factory->device, &color_desc);

    WGPUTextureDescriptor depth_desc = color_desc;
    depth_desc.label = YMS_STR("ymesh_depth");
    depth_desc.usage = WGPUTextureUsage_RenderAttachment;
    depth_desc.format = WGPUTextureFormat_Depth16Unorm;
    d->depth_tex = wgpuDeviceCreateTexture(factory->device, &depth_desc);

    if (!d->color_tex || !d->depth_tex) {
        ymesh_instance_data_destroy(d);
        free(inst->buffer_data);
        free(inst);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: offscreen tex alloc failed");
    }
    d->color_view = wgpuTextureCreateView(d->color_tex, NULL);
    d->depth_view = wgpuTextureCreateView(d->depth_tex, NULL);

    /* 3D uniform buffer — populated once (static camera). */
    struct ymesh_3d_uniforms uniforms_3d = {0};
    float aspect = (float)d->off_w / (float)d->off_h;
    struct yetty_ycore_void_result mr =
        ymesh_compute_3d_uniforms(bbox_min, bbox_max, aspect, &uniforms_3d);
    (void)mr;
    d->ub_3d = ymesh_create_buffer_with_data(factory->device, factory->queue,
        WGPUBufferUsage_Uniform, &uniforms_3d, sizeof(uniforms_3d));

    /* Blit uniform buffer — refilled per frame with current bounds. */
    struct ymesh_blit_uniforms init_blit = {0};
    init_blit.bounds[2] = bw;
    init_blit.bounds[3] = bh;
    d->ub_blit = ymesh_create_buffer_with_data(factory->device, factory->queue,
        WGPUBufferUsage_Uniform, &init_blit, sizeof(init_blit));

    if (!d->ub_3d || !d->ub_blit) {
        ymesh_instance_data_destroy(d);
        free(inst->buffer_data);
        free(inst);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: uniform buffer alloc failed");
    }

    /* Bind groups. */
    WGPUBindGroupEntry bg3_entries[1] = {0};
    bg3_entries[0].binding = 0;
    bg3_entries[0].buffer = d->ub_3d;
    bg3_entries[0].offset = 0;
    bg3_entries[0].size = sizeof(struct ymesh_3d_uniforms);

    WGPUBindGroupDescriptor bg3_desc = {0};
    bg3_desc.label = YMS_STR("ymesh_3d_bg");
    bg3_desc.layout = factory->bgl_3d;
    bg3_desc.entryCount = 1;
    bg3_desc.entries = bg3_entries;
    d->bg_3d = wgpuDeviceCreateBindGroup(factory->device, &bg3_desc);

    WGPUBindGroupEntry bgb_entries[3] = {0};
    bgb_entries[0].binding = 0;
    bgb_entries[0].buffer = d->ub_blit;
    bgb_entries[0].offset = 0;
    bgb_entries[0].size = sizeof(struct ymesh_blit_uniforms);
    bgb_entries[1].binding = 1;
    bgb_entries[1].textureView = d->color_view;
    bgb_entries[2].binding = 2;
    bgb_entries[2].sampler = factory->linear_sampler;

    WGPUBindGroupDescriptor bgb_desc = {0};
    bgb_desc.label = YMS_STR("ymesh_blit_bg");
    bgb_desc.layout = factory->bgl_blit;
    bgb_desc.entryCount = 3;
    bgb_desc.entries = bgb_entries;
    d->bg_blit = wgpuDeviceCreateBindGroup(factory->device, &bgb_desc);

    if (!d->bg_3d || !d->bg_blit) {
        ymesh_instance_data_destroy(d);
        free(inst->buffer_data);
        free(inst);
        return YETTY_ERR(yetty_ypaint_core_complex_prim_instance_ptr,
                         "ymesh: bind group create failed");
    }

    yinfo("ymesh: instance created vcount=%u icount=%u off=%ux%u",
          vcount, icount, d->off_w, d->off_h);
    return YETTY_OK(yetty_ypaint_core_complex_prim_instance_ptr, inst);
}

static void
ymesh_destroy_instance(struct yetty_ypaint_core_concrete_factory *self,
                       struct yetty_ypaint_core_complex_prim_instance *instance)
{
    (void)self;
    if (!instance)
        return;
    ymesh_instance_data_destroy((struct ymesh_instance_data *)instance->instance_data);
    free(instance->buffer_data);
    free(instance);
}

static struct yetty_ypaint_core_gpu_resource_set *
ymesh_get_shared_rs(struct yetty_ypaint_core_concrete_factory *self)
{
    /* ymesh doesn't use the framework RS. The abstract factory tolerates
     * NULL — it only consults this for buffer-data introspection that
     * doesn't apply to our raw pipeline. */
    (void)self;
    return NULL;
}

/*=============================================================================
 * Factory create / destroy
 *===========================================================================*/

struct yetty_ypaint_core_concrete_factory *yetty_ymesh_factory_create(void)
{
    struct yetty_ymesh_factory *f = calloc(1, sizeof(struct yetty_ymesh_factory));
    if (!f)
        return NULL;
    f->base.type_id = YETTY_YMESH_TYPE_ID;
    f->base.compile_pipeline = ymesh_compile_pipeline;
    f->base.get_pipeline = ymesh_get_pipeline;
    f->base.create_instance = ymesh_create_instance;
    f->base.destroy_instance = ymesh_destroy_instance;
    f->base.get_shared_rs = ymesh_get_shared_rs;
    /* set_visual_zoom / set_cell_zoom optional — leave NULL; the abstract
     * factory's fan-out skips factories with NULL setters. */
    return &f->base;
}

void yetty_ymesh_factory_destroy(struct yetty_ypaint_core_concrete_factory *self)
{
    if (!self)
        return;
    struct yetty_ymesh_factory *f = ymesh_factory_from_base(self);

    if (f->linear_sampler) wgpuSamplerRelease(f->linear_sampler);
    if (f->pipeline_blit)  wgpuRenderPipelineRelease(f->pipeline_blit);
    if (f->pipeline_3d)    wgpuRenderPipelineRelease(f->pipeline_3d);
    if (f->layout_blit)    wgpuPipelineLayoutRelease(f->layout_blit);
    if (f->layout_3d)      wgpuPipelineLayoutRelease(f->layout_3d);
    if (f->bgl_blit)       wgpuBindGroupLayoutRelease(f->bgl_blit);
    if (f->bgl_3d)         wgpuBindGroupLayoutRelease(f->bgl_3d);
    if (f->shader_blit)    wgpuShaderModuleRelease(f->shader_blit);
    if (f->shader_3d)      wgpuShaderModuleRelease(f->shader_3d);
    free(f);
}
