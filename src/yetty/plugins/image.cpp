#include "image.h"
#include "../renderer/webgpu-context.h"
#include <iostream>
#include <cstring>

#include <stb_image.h>

namespace yetty {

//-----------------------------------------------------------------------------
// ImagePlugin
//-----------------------------------------------------------------------------

ImagePlugin::ImagePlugin() = default;
ImagePlugin::~ImagePlugin() { dispose(); }

Result<PluginPtr> ImagePlugin::create() {
    return Ok<PluginPtr>(std::make_shared<ImagePlugin>());
}

Result<void> ImagePlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    initialized_ = true;
    return Ok();
}

void ImagePlugin::dispose() {
    Plugin::dispose();
    initialized_ = false;
}

Result<PluginLayerPtr> ImagePlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<ImageLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init ImageLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

void ImagePlugin::renderAll(WebGPUContext& ctx,
                             WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                             uint32_t screenWidth, uint32_t screenHeight,
                             float cellWidth, float cellHeight,
                             int scrollOffset, uint32_t termRows) {
    for (auto& layerBase : layers_) {
        if (!layerBase->isVisible()) continue;

        auto layer = std::static_pointer_cast<ImageLayer>(layerBase);

        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;
            }
        }

        layer->render(ctx, targetView, targetFormat,
                      screenWidth, screenHeight,
                      pixelX, pixelY, pixelW, pixelH);
    }
}

//-----------------------------------------------------------------------------
// ImageLayer
//-----------------------------------------------------------------------------

ImageLayer::ImageLayer() = default;

ImageLayer::~ImageLayer() { dispose(); }

Result<void> ImageLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("ImageLayer: empty payload");
    }

    payload_ = payload;
    dispose();

    auto result = loadImage(payload);
    if (!result) {
        return result;
    }

    std::cout << "ImageLayer: loaded " << imageWidth_ << "x" << imageHeight_
              << " (" << imageChannels_ << " channels)" << std::endl;
    return Ok();
}

Result<void> ImageLayer::loadImage(const std::string& data) {
    if (imageData_) {
        stbi_image_free(imageData_);
        imageData_ = nullptr;
    }

    imageData_ = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(data.data()),
        static_cast<int>(data.size()),
        &imageWidth_, &imageHeight_, &imageChannels_, 4);

    if (!imageData_) {
        return Err<void>(std::string("Failed to load image: ") + stbi_failure_reason());
    }

    imageChannels_ = 4;
    return Ok();
}

void ImageLayer::dispose() {
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }
    if (textureView_) { wgpuTextureViewRelease(textureView_); textureView_ = nullptr; }
    if (texture_) { wgpuTextureRelease(texture_); texture_ = nullptr; }
    if (imageData_) { stbi_image_free(imageData_); imageData_ = nullptr; }
    gpuInitialized_ = false;
}

void ImageLayer::render(WebGPUContext& ctx,
                         WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                         uint32_t screenWidth, uint32_t screenHeight,
                         float pixelX, float pixelY, float pixelW, float pixelH) {
    if (failed_ || !imageData_) return;

    if (!gpuInitialized_) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            std::cerr << "ImageLayer: " << error_msg(result) << std::endl;
            failed_ = true;
            return;
        }
        gpuInitialized_ = true;
    }

    if (!pipeline_ || !uniformBuffer_ || !bindGroup_) {
        failed_ = true;
        return;
    }

    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    struct Uniforms { float rect[4]; } uniforms;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return;

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) { wgpuCommandEncoderRelease(encoder); return; }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (cmdBuffer) {
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
    }
    wgpuCommandEncoderRelease(encoder);
}

Result<void> ImageLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = imageWidth_;
    texDesc.size.height = imageHeight_;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    texture_ = wgpuDeviceCreateTexture(device, &texDesc);
    if (!texture_) return Err<void>("Failed to create texture");

    WGPUImageCopyTexture dst = {};
    dst.texture = texture_;
    WGPUTextureDataLayout layout = {};
    layout.bytesPerRow = imageWidth_ * 4;
    layout.rowsPerImage = imageHeight_;
    WGPUExtent3D extent = {(uint32_t)imageWidth_, (uint32_t)imageHeight_, 1};
    wgpuQueueWriteTexture(ctx.getQueue(), &dst, imageData_,
                          imageWidth_ * imageHeight_ * 4, &layout, &extent);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
    if (!textureView_) return Err<void>("Failed to create texture view");

    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler_) return Err<void>("Failed to create sampler");

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

    const char* shaderCode = R"(
struct Uniforms { rect: vec4<f32>, }
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
struct VertexOutput { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32>, }
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var p = array<vec2<f32>,6>(vec2(0.,0.),vec2(1.,0.),vec2(1.,1.),vec2(0.,0.),vec2(1.,1.),vec2(0.,1.));
    let pos = p[vi];
    var o: VertexOutput;
    o.position = vec4(u.rect.x + pos.x * u.rect.z, u.rect.y - pos.y * u.rect.w, 0., 1.);
    o.uv = pos;
    return o;
}
@fragment fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    return textureSample(tex, texSampler, uv);
}
)";

    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = shaderCode;
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0; entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].binding = 1; entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[2].binding = 2; entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 3; bglDesc.entries = entries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bgl) { wgpuShaderModuleRelease(shaderModule); return Err<void>("Failed to create bgl"); }

    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1; plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    WGPUBindGroupEntry bgE[3] = {};
    bgE[0].binding = 0; bgE[0].buffer = uniformBuffer_; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = sampler_;
    bgE[2].binding = 2; bgE[2].textureView = textureView_;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl; bgDesc.entryCount = 3; bgDesc.entries = bgE;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "vs_main";
    WGPUFragmentState fragState = {};
    fragState.module = shaderModule; fragState.entryPoint = "fs_main";
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat; colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;
    fragState.targetCount = 1; fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1; pipelineDesc.multisample.mask = ~0u;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) return Err<void>("Failed to create render pipeline");

    std::cout << "ImageLayer: pipeline created" << std::endl;
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* image_plugin_name() { return "image"; }
    yetty::Result<yetty::PluginPtr> image_plugin_create() { return yetty::ImagePlugin::create(); }
}
