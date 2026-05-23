/*
 * pipeline.c — build / destroy the shared WebGPU pipeline used by every
 * ymgui figure on a compositor.
 *
 * GPU layout (mirrors the old ymgui-layer):
 *   Bind group 0:
 *     binding 0 = uniform buffer (32 B: display_size, frame_top, pad)
 *     binding 1 = atlas texture view (R8 today)
 *     binding 2 = sampler
 *   Vertex layout (ImDrawVert, 20 B):
 *     loc 0 = vec2 pos (frame-local pixels)
 *     loc 1 = vec2 uv
 *     loc 2 = unorm8x4 col
 *   Topology = triangles, premultiplied-alpha-ish blend
 *     (color: src.a / 1-src.a, alpha: 1 / 1-src.a)
 *
 * Reads the WGSL source from `paths/shaders`/ymgui-layer.wgsl. The
 * old ymgui-layer still ships the same file — we share the asset.
 */
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ymgui_pipeline_ptr_result yetty_ymgui_pipeline_create(
    const struct yetty_context *context)
{
    if (!context)
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: NULL context");
    if (!context->runtime || !context->runtime->gpu.device ||
        !context->runtime->gpu.queue)
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: gpu context incomplete");

    struct yetty_yconfig_config *cfg = context->runtime->config;
    const char *shaders_dir = cfg->ops->get_string(cfg, "paths/shaders", "");
    char shader_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/ymgui-layer.wgsl",
             shaders_dir);
    struct yetty_ycore_buffer_result shader_res =
        yetty_ycore_read_file(shader_path);
    YETTY_RETURN_IF_ERR(yetty_ymgui_pipeline_ptr, shader_res,
                        "ymgui_pipeline_create: read_file(ymgui-layer.wgsl)");

    struct yetty_ymgui_pipeline *p = calloc(1, sizeof(*p));
    if (!p) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: oom");
    }
    p->device = context->runtime->gpu.device;
    p->queue = context->runtime->gpu.queue;
    p->target_format = context->runtime->gpu.surface_format;

    WGPUShaderSourceWGSL wgsl = {0};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgsl.code = (WGPUStringView){(const char *)shader_res.value.data,
                                 shader_res.value.size};
    WGPUShaderModuleDescriptor sm_desc = {0};
    sm_desc.nextInChain = &wgsl.chain;
    p->shader_module = wgpuDeviceCreateShaderModule(p->device, &sm_desc);
    free(shader_res.value.data);
    if (!p->shader_module) {
        struct yetty_ycore_void_result d = yetty_ymgui_pipeline_destroy(p);
        if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: shader module creation failed");
    }

    WGPUBindGroupLayoutEntry bgl_entries[3] = {0};
    bgl_entries[0].binding = 0;
    bgl_entries[0].visibility =
        WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entries[0].buffer.minBindingSize = 32;
    bgl_entries[1].binding = 1;
    bgl_entries[1].visibility = WGPUShaderStage_Fragment;
    bgl_entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bgl_entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    bgl_entries[2].binding = 2;
    bgl_entries[2].visibility = WGPUShaderStage_Fragment;
    bgl_entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bgl_desc = {0};
    bgl_desc.entryCount = 3;
    bgl_desc.entries = bgl_entries;
    p->bind_group_layout = wgpuDeviceCreateBindGroupLayout(p->device, &bgl_desc);
    if (!p->bind_group_layout) {
        struct yetty_ycore_void_result d = yetty_ymgui_pipeline_destroy(p);
        if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: bind group layout failed");
    }

    WGPUPipelineLayoutDescriptor pl_desc = {0};
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &p->bind_group_layout;
    p->pipeline_layout = wgpuDeviceCreatePipelineLayout(p->device, &pl_desc);
    if (!p->pipeline_layout) {
        struct yetty_ycore_void_result d = yetty_ymgui_pipeline_destroy(p);
        if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: pipeline layout failed");
    }

    WGPUVertexAttribute vattrs[3] = {0};
    vattrs[0].format = WGPUVertexFormat_Float32x2;
    vattrs[0].offset = 0;
    vattrs[0].shaderLocation = 0;
    vattrs[1].format = WGPUVertexFormat_Float32x2;
    vattrs[1].offset = 8;
    vattrs[1].shaderLocation = 1;
    vattrs[2].format = WGPUVertexFormat_Unorm8x4;
    vattrs[2].offset = 16;
    vattrs[2].shaderLocation = 2;
    WGPUVertexBufferLayout vbl = {0};
    vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.arrayStride = 20;
    vbl.attributeCount = 3;
    vbl.attributes = vattrs;

    WGPUBlendComponent blend_color = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_SrcAlpha,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
    };
    WGPUBlendComponent blend_alpha = {
        .operation = WGPUBlendOperation_Add,
        .srcFactor = WGPUBlendFactor_One,
        .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha,
    };
    WGPUBlendState blend = {.color = blend_color, .alpha = blend_alpha};
    WGPUColorTargetState color_target = {0};
    color_target.format = p->target_format;
    color_target.blend = &blend;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fs = {0};
    fs.module = p->shader_module;
    fs.entryPoint = (WGPUStringView){"fs_main", 7};
    fs.targetCount = 1;
    fs.targets = &color_target;

    WGPURenderPipelineDescriptor rpd = {0};
    rpd.layout = p->pipeline_layout;
    rpd.vertex.module = p->shader_module;
    rpd.vertex.entryPoint = (WGPUStringView){"vs_main", 7};
    rpd.vertex.bufferCount = 1;
    rpd.vertex.buffers = &vbl;
    rpd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rpd.primitive.frontFace = WGPUFrontFace_CCW;
    rpd.primitive.cullMode = WGPUCullMode_None;
    rpd.fragment = &fs;
    rpd.multisample.count = 1;
    rpd.multisample.mask = 0xFFFFFFFFu;

    p->pipeline = wgpuDeviceCreateRenderPipeline(p->device, &rpd);
    if (!p->pipeline) {
        struct yetty_ycore_void_result d = yetty_ymgui_pipeline_destroy(p);
        if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: render pipeline failed");
    }

    WGPUSamplerDescriptor sd = {0};
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = WGPUFilterMode_Linear;
    sd.minFilter = WGPUFilterMode_Linear;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.maxAnisotropy = 1;
    p->sampler = wgpuDeviceCreateSampler(p->device, &sd);
    if (!p->sampler) {
        struct yetty_ycore_void_result d = yetty_ymgui_pipeline_destroy(p);
        if (YETTY_IS_ERR(d)) yetty_ycore_error_destroy(d.error);
        return YETTY_ERR(yetty_ymgui_pipeline_ptr,
                         "ymgui_pipeline_create: sampler create failed");
    }

    ydebug("ymgui_pipeline_create: compiled");
    return YETTY_OK(yetty_ymgui_pipeline_ptr, p);
}

struct yetty_ycore_void_result yetty_ymgui_pipeline_destroy(
    struct yetty_ymgui_pipeline *p)
{
    if (!p) return YETTY_OK_VOID();
    if (p->sampler) wgpuSamplerRelease(p->sampler);
    if (p->pipeline) wgpuRenderPipelineRelease(p->pipeline);
    if (p->pipeline_layout) wgpuPipelineLayoutRelease(p->pipeline_layout);
    if (p->bind_group_layout) wgpuBindGroupLayoutRelease(p->bind_group_layout);
    if (p->shader_module) wgpuShaderModuleRelease(p->shader_module);
    free(p);
    return YETTY_OK_VOID();
}
