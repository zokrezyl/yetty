#include "image.h"
#include "../../renderer/webgpu-context.h"
#include "../../renderer/wgpu-compat.h"
#include <iostream>
#include <cstring>

#include <stb_image.h>

namespace yetty {

//-----------------------------------------------------------------------------
// ImagePlugin
//-----------------------------------------------------------------------------

ImagePlugin::ImagePlugin() = default;
ImagePlugin::~ImagePlugin() { (void)dispose(); }

Result<PluginPtr> ImagePlugin::create() {
    return Ok<PluginPtr>(std::make_shared<ImagePlugin>());
}

Result<void> ImagePlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    _initialized = true;
    return Ok();
}

Result<void> ImagePlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose ImagePlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> ImagePlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<ImageLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init ImageLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> ImagePlugin::renderAll(WebGPUContext& ctx,
                                     WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset, uint32_t termRows,
                                     bool isAltScreen) {
    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

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

        if (auto res = layer->render(ctx, targetView, targetFormat,
                                      screenWidth, screenHeight,
                                      pixelX, pixelY, pixelW, pixelH); !res) {
            return Err<void>("Failed to render ImageLayer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// ImageLayer
//-----------------------------------------------------------------------------

ImageLayer::ImageLayer() = default;

ImageLayer::~ImageLayer() { (void)dispose(); }

Result<void> ImageLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("ImageLayer: empty payload");
    }

    _payload = payload;
    (void)dispose();

    auto result = loadImage(payload);
    if (!result) {
        return result;
    }

    std::cout << "ImageLayer: loaded " << _image_width << "x" << _image_height
              << " (" << _image_channels << " channels)" << std::endl;
    return Ok();
}

Result<void> ImageLayer::loadImage(const std::string& data) {
    if (_image_data) {
        stbi_image_free(_image_data);
        _image_data = nullptr;
    }

    _image_data = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>(data.data()),
        static_cast<int>(data.size()),
        &_image_width, &_image_height, &_image_channels, 4);

    if (!_image_data) {
        return Err<void>(std::string("Failed to load image: ") + stbi_failure_reason());
    }

    _image_channels = 4;
    return Ok();
}

Result<void> ImageLayer::dispose() {
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_sampler) { wgpuSamplerRelease(_sampler); _sampler = nullptr; }
    if (_texture_view) { wgpuTextureViewRelease(_texture_view); _texture_view = nullptr; }
    if (_texture) { wgpuTextureRelease(_texture); _texture = nullptr; }
    if (_image_data) { stbi_image_free(_image_data); _image_data = nullptr; }
    _gpu_initialized = false;
    return Ok();
}

Result<void> ImageLayer::render(WebGPUContext& ctx,
                                 WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float pixelX, float pixelY, float pixelW, float pixelH) {
    if (_failed) return Err<void>("ImageLayer already failed");
    if (!_image_data) return Err<void>("ImageLayer has no image data");

    if (!_gpu_initialized) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to create pipeline", result);
        }
        _gpu_initialized = true;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("ImageLayer pipeline not initialized");
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

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("Failed to create command encoder");

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // v27: required for 2D textures

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
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
    return Ok();
}

Result<void> ImageLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = _image_width;
    texDesc.size.height = _image_height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    _texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!_texture) return Err<void>("Failed to create texture");

    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = _texture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = _image_width * 4;
    layout.rowsPerImage = _image_height;
    WGPUExtent3D extent = {(uint32_t)_image_width, (uint32_t)_image_height, 1};
    wgpuQueueWriteTexture(ctx.getQueue(), &dst, _image_data,
                          _image_width * _image_height * 4, &layout, &extent);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    _texture_view = wgpuTextureCreateView(_texture, &viewDesc);
    if (!_texture_view) return Err<void>("Failed to create texture view");

    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    _sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!_sampler) return Err<void>("Failed to create sampler");

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

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

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);
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
    bgE[0].binding = 0; bgE[0].buffer = _uniform_buffer; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = _sampler;
    bgE[2].binding = 2; bgE[2].textureView = _texture_view;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl; bgDesc.entryCount = 3; bgDesc.entries = bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");
    WGPUFragmentState fragState = {};
    fragState.module = shaderModule; fragState.entryPoint = WGPU_STR("fs_main");
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

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) return Err<void>("Failed to create render pipeline");

    std::cout << "ImageLayer: pipeline created" << std::endl;
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* image_plugin_name() { return "image"; }
    yetty::Result<yetty::PluginPtr> image_plugin_create() { return yetty::ImagePlugin::create(); }
}
