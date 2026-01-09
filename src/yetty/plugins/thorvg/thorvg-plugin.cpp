#include "thorvg-plugin.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>
#include <cstring>
#include <cmath>
#include <sstream>

namespace yetty {

//-----------------------------------------------------------------------------
// ThorvgPlugin
//-----------------------------------------------------------------------------

ThorvgPlugin::~ThorvgPlugin() { (void)dispose(); }

Result<PluginPtr> ThorvgPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new ThorvgPlugin(std::move(engine)));
    if (auto res = static_cast<ThorvgPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init ThorvgPlugin", res);
    }
    return Ok(p);
}

Result<void> ThorvgPlugin::pluginInit() noexcept {
    // Initialize ThorVG engine with WebGPU support
    auto result = tvg::Initializer::init(0);  // Single-threaded for now
    if (result != tvg::Result::Success) {
        spdlog::error("ThorvgPlugin: tvg::Initializer::init failed with result={}", static_cast<int>(result));
        return Err<void>("Failed to initialize ThorVG engine");
    }

    uint32_t major, minor, micro;
    const char* version = tvg::Initializer::version(&major, &minor, &micro);
    spdlog::info("ThorvgPlugin: initialized ThorVG {} (WebGPU renderer)", version ? version : "unknown");

    initialized_ = true;
    return Ok();
}

Result<void> ThorvgPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose ThorvgPlugin", res);
    }

    if (initialized_) {
        tvg::Initializer::term();
    }

    initialized_ = false;
    return Ok();
}

Result<WidgetPtr> ThorvgPlugin::createWidget(const std::string& payload) {
    spdlog::info("ThorvgPlugin::createWidget called with payload size={}", payload.size());
    return Lottie::create(payload);
}

//-----------------------------------------------------------------------------
// Lottie
//-----------------------------------------------------------------------------

Lottie::~Lottie() { (void)dispose(); }

Result<void> Lottie::init() {
    if (payload_.empty()) {
        return Err<void>("Lottie: empty payload");
    }

    std::string content = payload_;
    std::string mimeType;

    spdlog::info("Lottie::init: payload size={}", payload_.size());

    // Check for type prefix (format: "type\n<content>")
    size_t newlinePos = payload_.find('\n');
    if (newlinePos != std::string::npos && newlinePos < 20) {
        std::string prefix = payload_.substr(0, newlinePos);
        spdlog::info("Lottie::init: detected prefix='{}' at pos {}", prefix, newlinePos);
        if (prefix == "svg" || prefix == "lottie" || prefix == "yaml") {
            mimeType = prefix;
            content = payload_.substr(newlinePos + 1);
        }
    }

    // If no prefix, auto-detect from content
    if (mimeType.empty()) {
        size_t start = content.find_first_not_of(" \t\n\r");
        if (start != std::string::npos && content[start] == '{') {
            mimeType = "lottie";
        } else if (content.find("<svg") != std::string::npos ||
                   (content.find("<?xml") != std::string::npos && content.find("<svg") != std::string::npos)) {
            mimeType = "svg";
        }
        spdlog::info("Lottie::init: auto-detected mimeType='{}'", mimeType);
    }

    spdlog::info("Lottie::init: mimeType='{}', content size={}", mimeType, content.size());

    // Handle YAML vector graphics by converting to SVG
    if (mimeType == "yaml") {
        auto result = yamlToSvg(content);
        if (!result) {
            return Err<void>("Failed to convert YAML to SVG", result);
        }
        content = *result;
        mimeType = "svg";
        spdlog::info("Lottie::init: converted YAML to SVG, size={}", content.size());
    }

    auto result = loadContent(content, mimeType);
    if (!result) {
        spdlog::error("Lottie::init: loadContent failed: {}", result.error().message());
        return result;
    }

    spdlog::info("Lottie: loaded {}x{} content (animated: {})",
                 contentWidth_, contentHeight_, isAnimated_);
    return Ok();
}

Result<void> Lottie::loadContent(const std::string& data, const std::string& mimeType) {
    // WgCanvas will be created when WebGPU context is available (in initWebGPU)
    // For now, just parse content to get dimensions and animation info

    spdlog::info("Lottie::loadContent: mimeType='{}', data size={}", mimeType, data.size());

    // Default size
    contentWidth_ = 256;
    contentHeight_ = 256;

    tvg::Picture* picture = nullptr;

    // Check if this is an animation (Lottie)
    if (mimeType == "lottie" || mimeType == "lottie+json" || mimeType == "lot") {
        spdlog::info("Lottie::loadContent: loading as Lottie animation");
        animation_.reset(tvg::Animation::gen());
        if (!animation_) {
            return Err<void>("Failed to create ThorVG Animation");
        }

        picture = animation_->picture();
        if (!picture) {
            return Err<void>("Failed to get picture from Animation");
        }

        auto result = picture->load(data.c_str(), static_cast<uint32_t>(data.size()),
                                    "lottie", nullptr, true);
        spdlog::info("Lottie::loadContent: Lottie load result={}", static_cast<int>(result));
        if (result != tvg::Result::Success) {
            animation_.reset();
            return Err<void>("Failed to load Lottie animation");
        }

        isAnimated_ = true;
        totalFrames_ = animation_->totalFrame();
        duration_ = animation_->duration();
        currentFrame_ = 0.0f;

        spdlog::debug("Lottie: Lottie animation loaded - {} frames, {}s duration",
                     totalFrames_, duration_);
    } else {
        // Static picture (SVG, PNG, etc.)
        // Use Animation wrapper for ownership management (Picture has protected destructor)
        spdlog::info("Lottie::loadContent: loading as static content (mimeType='{}')", mimeType);
        animation_.reset(tvg::Animation::gen());
        if (!animation_) {
            return Err<void>("Failed to create ThorVG Animation for static content");
        }

        picture = animation_->picture();
        if (!picture) {
            return Err<void>("Failed to get picture from Animation");
        }

        // ThorVG auto-detects format from content when mime is nullptr
        // For SVG, pass nullptr to let ThorVG detect the format from the XML/SVG content
        const char* mime = nullptr;  // Let ThorVG auto-detect
        spdlog::info("Lottie::loadContent: calling picture->load() with mime=nullptr (auto-detect), data size={}",
                     data.size());
        auto result = picture->load(data.c_str(), static_cast<uint32_t>(data.size()),
                                    mime, nullptr, true);
        spdlog::info("Lottie::loadContent: picture->load result={}", static_cast<int>(result));
        if (result != tvg::Result::Success) {
            animation_.reset();
            return Err<void>("Failed to load static content into ThorVG Picture (result=" + std::to_string(static_cast<int>(result)) + ")");
        }

        isAnimated_ = false;
    }

    // Get content size
    float w = 0, h = 0;
    picture->size(&w, &h);

    if (w > 0 && h > 0) {
        contentWidth_ = static_cast<uint32_t>(w);
        contentHeight_ = static_cast<uint32_t>(h);
    }

    picture_ = picture;
    contentDirty_ = true;

    return Ok();
}

Result<void> Lottie::initWebGPU(WebGPUContext& ctx) {
    WGPUDevice device = ctx.getDevice();
    WGPUInstance instance = ctx.getInstance();

    // Create render texture that ThorVG will render to
    // ThorVG's WgCanvas internal blit pipeline uses BGRA8Unorm format
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = contentWidth_;
    texDesc.size.height = contentHeight_;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_BGRA8Unorm;  // Must match ThorVG's internal blit pipeline
    // ThorVG needs RenderAttachment for drawing, we need TextureBinding for compositing
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;

    renderTexture_ = wgpuDeviceCreateTexture(device, &texDesc);
    if (!renderTexture_) return Err<void>("Failed to create ThorVG render texture");

    // Create texture view for our composite shader
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    renderTextureView_ = wgpuTextureCreateView(renderTexture_, &viewDesc);
    if (!renderTextureView_) return Err<void>("Failed to create ThorVG texture view");

    // Create ThorVG WgCanvas and set the texture as target
    canvas_.reset(tvg::WgCanvas::gen());
    if (!canvas_) {
        return Err<void>("Failed to create ThorVG WgCanvas");
    }

    // Set the render target - type=1 means texture target (not surface)
    auto result = canvas_->target(device, instance, renderTexture_,
                                   contentWidth_, contentHeight_,
                                   tvg::ColorSpace::ABGR8888S, 1);
    if (result != tvg::Result::Success) {
        return Err<void>("Failed to set ThorVG WgCanvas target");
    }

    // Push the picture to canvas
    // Note: Canvas::push() takes ownership reference
    if (picture_) {
        result = canvas_->push(picture_);
        if (result != tvg::Result::Success) {
            return Err<void>("Failed to push picture to ThorVG canvas");
        }
    }

    spdlog::debug("Lottie: WebGPU canvas initialized {}x{}", contentWidth_, contentHeight_);
    return Ok();
}

Result<void> Lottie::createCompositePipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create sampler for compositing
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler_) return Err<void>("Failed to create sampler");

    // Create uniform buffer for rect transform
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

    // Composite shader - renders ThorVG output texture as a quad
    // ThorVG WebGPU outputs ABGR into a BGRA texture, so swizzle to RGBA
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
    let c = textureSample(tex, texSampler, uv);
    // ThorVG writes ABGR; BGRA view yields c = (B=A, G=B, R=G, A=R). Swizzle back to RGBA.
    return vec4(c.a, c.r, c.g, c.b);
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    // Bind group layout
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

    // Bind group
    WGPUBindGroupEntry bgE[3] = {};
    bgE[0].binding = 0; bgE[0].buffer = uniformBuffer_; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = sampler_;
    bgE[2].binding = 2; bgE[2].textureView = renderTextureView_;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl; bgDesc.entryCount = 3; bgDesc.entries = bgE;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = WGPU_STR("fs_main");

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    compositePipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!compositePipeline_) return Err<void>("Failed to create composite pipeline");

    spdlog::debug("Lottie: composite pipeline created");
    return Ok();
}

// Error scope callback data for ThorVG rendering
struct ThorvgErrorData {
    bool hasError = false;
    std::string message;
};

static void thorvgErrorCallback(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                                 WGPUStringView message, void* userdata1, void* userdata2) {
    (void)userdata2;
    ThorvgErrorData* data = static_cast<ThorvgErrorData*>(userdata1);
    if (status != WGPUPopErrorScopeStatus_Success || type != WGPUErrorType_NoError) {
        data->hasError = true;
        data->message = message.data ? std::string(message.data, message.length) : "unknown WebGPU error";
        spdlog::error("Lottie WebGPU error (type={}): {}", static_cast<int>(type), data->message);
    }
}

Result<void> Lottie::renderThorvgFrame(WGPUDevice device) {
    if (!canvas_ || !picture_) {
        spdlog::warn("Lottie::renderThorvgFrame: no canvas or picture");
        return Ok();  // Nothing to render is not an error
    }

    spdlog::info("Lottie::renderThorvgFrame: rendering frame {}, animated={}",
                 currentFrame_, isAnimated_);

    // Update animation frame if animated
    if (isAnimated_ && animation_) {
        auto frameResult = animation_->frame(currentFrame_);
        spdlog::info("Lottie::renderThorvgFrame: animation->frame({}) result={}",
                     currentFrame_, static_cast<int>(frameResult));
    }

    // Render using ThorVG's WebGPU canvas
    auto updateResult = canvas_->update();
    spdlog::info("Lottie::renderThorvgFrame: canvas->update result={}", static_cast<int>(updateResult));
    if (updateResult != tvg::Result::Success) {
        return Err<void>("ThorVG canvas update failed (result=" + std::to_string(static_cast<int>(updateResult)) + ")");
    }

    // draw(true) clears the buffer before rendering
    auto drawResult = canvas_->draw(true);
    spdlog::info("Lottie::renderThorvgFrame: canvas->draw result={}", static_cast<int>(drawResult));
    if (drawResult != tvg::Result::Success) {
        return Err<void>("ThorVG canvas draw failed (result=" + std::to_string(static_cast<int>(drawResult)) + ")");
    }

    // sync() submits WebGPU commands
    auto syncResult = canvas_->sync();
    spdlog::info("Lottie::renderThorvgFrame: canvas->sync result={}", static_cast<int>(syncResult));
    if (syncResult != tvg::Result::Success) {
        return Err<void>("ThorVG canvas sync failed (result=" + std::to_string(static_cast<int>(syncResult)) + ")");
    }

    contentDirty_ = false;

    // Recreate texture view to ensure fresh GPU state after ThorVG render
    if (renderTextureView_) {
        wgpuTextureViewRelease(renderTextureView_);
    }
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    renderTextureView_ = wgpuTextureCreateView(renderTexture_, &viewDesc);

    // Recreate bind group with fresh texture view
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
    }
    WGPUBindGroupEntry bgE[3] = {};
    bgE[0].binding = 0; bgE[0].buffer = uniformBuffer_; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = sampler_;
    bgE[2].binding = 2; bgE[2].textureView = renderTextureView_;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = wgpuRenderPipelineGetBindGroupLayout(compositePipeline_, 0);
    bgDesc.entryCount = 3;
    bgDesc.entries = bgE;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

    spdlog::info("Lottie::renderThorvgFrame: completed successfully, recreated view/bindgroup");
    return Ok();
}

void Lottie::setFrame(float frame) {
    if (!isAnimated_) return;

    currentFrame_ = frame;
    if (currentFrame_ >= totalFrames_) {
        currentFrame_ = loop_ ? 0.0f : totalFrames_ - 1;
    }
    if (currentFrame_ < 0) {
        currentFrame_ = 0;
    }

    contentDirty_ = true;
}

//-----------------------------------------------------------------------------
// YAML to SVG Conversion
// Converts simple YAML vector graphics definitions to SVG format
//-----------------------------------------------------------------------------

Result<std::string> Lottie::yamlToSvg(const std::string& yamlContent) {
    try {
        YAML::Node root = YAML::Load(yamlContent);

        // Default canvas size
        int width = 800;
        int height = 600;
        std::string bgColor = "none";

        // Check for canvas settings at root
        if (root["canvas"]) {
            if (root["canvas"]["width"]) width = root["canvas"]["width"].as<int>();
            if (root["canvas"]["height"]) height = root["canvas"]["height"].as<int>();
            if (root["canvas"]["background"]) bgColor = root["canvas"]["background"].as<std::string>();
        }

        std::ostringstream svg;
        svg << R"(<?xml version="1.0" encoding="UTF-8"?>)" << "\n";
        svg << R"(<svg xmlns="http://www.w3.org/2000/svg" width=")" << width
            << R"(" height=")" << height << R"(" viewBox="0 0 )" << width << " " << height << R"(">)" << "\n";

        // Background
        if (bgColor != "none") {
            svg << R"(  <rect width="100%" height="100%" fill=")" << bgColor << R"("/>)" << "\n";
        }

        // Process shapes - find 'body' or 'shapes' array
        YAML::Node shapes;
        if (root["body"]) {
            shapes = root["body"];
        } else if (root["shapes"]) {
            shapes = root["shapes"];
        } else if (root.IsSequence()) {
            // Root is directly a sequence of shapes
            shapes = root;
        }

        if (shapes && shapes.IsSequence()) {
            for (const auto& shape : shapes) {
                // Circle
                if (shape["circle"]) {
                    auto c = shape["circle"];
                    float cx = c["cx"] ? c["cx"].as<float>() : (c["x"] ? c["x"].as<float>() : 0);
                    float cy = c["cy"] ? c["cy"].as<float>() : (c["y"] ? c["y"].as<float>() : 0);
                    float r = c["r"] ? c["r"].as<float>() : (c["radius"] ? c["radius"].as<float>() : 10);
                    std::string fill = c["fill"] ? c["fill"].as<std::string>() : "#000000";
                    std::string stroke = c["stroke"] ? c["stroke"].as<std::string>() : "none";
                    float strokeWidth = c["stroke-width"] ? c["stroke-width"].as<float>() : 1;
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <circle cx=")" << cx << R"(" cy=")" << cy
                        << R"(" r=")" << r << R"(" fill=")" << fill
                        << R"(" stroke=")" << stroke << R"(" stroke-width=")" << strokeWidth
                        << R"(" opacity=")" << opacity << R"("/>)" << "\n";
                }
                // Rectangle
                else if (shape["rect"]) {
                    auto c = shape["rect"];
                    float x = c["x"] ? c["x"].as<float>() : 0;
                    float y = c["y"] ? c["y"].as<float>() : 0;
                    float w = c["width"] ? c["width"].as<float>() : (c["w"] ? c["w"].as<float>() : 50);
                    float h = c["height"] ? c["height"].as<float>() : (c["h"] ? c["h"].as<float>() : 50);
                    float rx = c["rx"] ? c["rx"].as<float>() : (c["round"] ? c["round"].as<float>() : 0);
                    float ry = c["ry"] ? c["ry"].as<float>() : rx;
                    std::string fill = c["fill"] ? c["fill"].as<std::string>() : "#000000";
                    std::string stroke = c["stroke"] ? c["stroke"].as<std::string>() : "none";
                    float strokeWidth = c["stroke-width"] ? c["stroke-width"].as<float>() : 1;
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <rect x=")" << x << R"(" y=")" << y
                        << R"(" width=")" << w << R"(" height=")" << h
                        << R"(" rx=")" << rx << R"(" ry=")" << ry
                        << R"(" fill=")" << fill << R"(" stroke=")" << stroke
                        << R"(" stroke-width=")" << strokeWidth
                        << R"(" opacity=")" << opacity << R"("/>)" << "\n";
                }
                // Line
                else if (shape["line"]) {
                    auto c = shape["line"];
                    float x1 = c["x1"] ? c["x1"].as<float>() : 0;
                    float y1 = c["y1"] ? c["y1"].as<float>() : 0;
                    float x2 = c["x2"] ? c["x2"].as<float>() : 100;
                    float y2 = c["y2"] ? c["y2"].as<float>() : 100;
                    std::string stroke = c["stroke"] ? c["stroke"].as<std::string>() : "#000000";
                    float strokeWidth = c["stroke-width"] ? c["stroke-width"].as<float>() : 1;
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <line x1=")" << x1 << R"(" y1=")" << y1
                        << R"(" x2=")" << x2 << R"(" y2=")" << y2
                        << R"(" stroke=")" << stroke << R"(" stroke-width=")" << strokeWidth
                        << R"(" opacity=")" << opacity << R"("/>)" << "\n";
                }
                // Ellipse
                else if (shape["ellipse"]) {
                    auto c = shape["ellipse"];
                    float cx = c["cx"] ? c["cx"].as<float>() : (c["x"] ? c["x"].as<float>() : 0);
                    float cy = c["cy"] ? c["cy"].as<float>() : (c["y"] ? c["y"].as<float>() : 0);
                    float rx = c["rx"] ? c["rx"].as<float>() : 20;
                    float ry = c["ry"] ? c["ry"].as<float>() : 10;
                    std::string fill = c["fill"] ? c["fill"].as<std::string>() : "#000000";
                    std::string stroke = c["stroke"] ? c["stroke"].as<std::string>() : "none";
                    float strokeWidth = c["stroke-width"] ? c["stroke-width"].as<float>() : 1;
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <ellipse cx=")" << cx << R"(" cy=")" << cy
                        << R"(" rx=")" << rx << R"(" ry=")" << ry
                        << R"(" fill=")" << fill << R"(" stroke=")" << stroke
                        << R"(" stroke-width=")" << strokeWidth
                        << R"(" opacity=")" << opacity << R"("/>)" << "\n";
                }
                // Polygon
                else if (shape["polygon"]) {
                    auto c = shape["polygon"];
                    std::string points;
                    if (c["points"]) {
                        if (c["points"].IsSequence()) {
                            for (size_t i = 0; i < c["points"].size(); i += 2) {
                                if (i > 0) points += " ";
                                points += std::to_string(c["points"][i].as<float>()) + "," +
                                         std::to_string(c["points"][i+1].as<float>());
                            }
                        } else {
                            points = c["points"].as<std::string>();
                        }
                    }
                    std::string fill = c["fill"] ? c["fill"].as<std::string>() : "#000000";
                    std::string stroke = c["stroke"] ? c["stroke"].as<std::string>() : "none";
                    float strokeWidth = c["stroke-width"] ? c["stroke-width"].as<float>() : 1;
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <polygon points=")" << points
                        << R"(" fill=")" << fill << R"(" stroke=")" << stroke
                        << R"(" stroke-width=")" << strokeWidth
                        << R"(" opacity=")" << opacity << R"("/>)" << "\n";
                }
                // Path
                else if (shape["path"]) {
                    auto c = shape["path"];
                    std::string d = c["d"] ? c["d"].as<std::string>() : "";
                    std::string fill = c["fill"] ? c["fill"].as<std::string>() : "none";
                    std::string stroke = c["stroke"] ? c["stroke"].as<std::string>() : "#000000";
                    float strokeWidth = c["stroke-width"] ? c["stroke-width"].as<float>() : 1;
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <path d=")" << d
                        << R"(" fill=")" << fill << R"(" stroke=")" << stroke
                        << R"(" stroke-width=")" << strokeWidth
                        << R"(" opacity=")" << opacity << R"("/>)" << "\n";
                }
                // Text
                else if (shape["text"]) {
                    auto c = shape["text"];
                    float x = c["x"] ? c["x"].as<float>() : 0;
                    float y = c["y"] ? c["y"].as<float>() : 0;
                    std::string content = c["content"] ? c["content"].as<std::string>() : "";
                    std::string fill = c["fill"] ? c["fill"].as<std::string>() : "#000000";
                    float fontSize = c["font-size"] ? c["font-size"].as<float>() : 16;
                    std::string fontFamily = c["font-family"] ? c["font-family"].as<std::string>() : "sans-serif";
                    std::string anchor = c["text-anchor"] ? c["text-anchor"].as<std::string>() : "start";
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <text x=")" << x << R"(" y=")" << y
                        << R"(" fill=")" << fill << R"(" font-size=")" << fontSize
                        << R"(" font-family=")" << fontFamily << R"(" text-anchor=")" << anchor
                        << R"(" opacity=")" << opacity << R"(">)" << content << R"(</text>)" << "\n";
                }
                // Group
                else if (shape["group"] || shape["g"]) {
                    auto c = shape["group"] ? shape["group"] : shape["g"];
                    std::string transform = c["transform"] ? c["transform"].as<std::string>() : "";
                    float opacity = c["opacity"] ? c["opacity"].as<float>() : 1.0f;

                    svg << R"(  <g)";
                    if (!transform.empty()) svg << R"( transform=")" << transform << R"(")";
                    if (opacity < 1.0f) svg << R"( opacity=")" << opacity << R"(")";
                    svg << R"(>)" << "\n";
                    // TODO: recursively process children
                    svg << R"(  </g>)" << "\n";
                }
            }
        }

        svg << R"(</svg>)" << "\n";

        spdlog::debug("Lottie: converted YAML to SVG ({} bytes)", svg.str().size());
        return Ok(svg.str());

    } catch (const YAML::Exception& e) {
        return Err<std::string>(std::string("YAML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Err<std::string>(std::string("Error converting YAML to SVG: ") + e.what());
    }
}

Result<void> Lottie::dispose() {
    // Release composite resources
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (compositePipeline_) { wgpuRenderPipelineRelease(compositePipeline_); compositePipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }
    if (renderTextureView_) { wgpuTextureViewRelease(renderTextureView_); renderTextureView_ = nullptr; }

    // Release ThorVG canvas before destroying the texture it targets
    canvas_.reset();

    if (renderTexture_) { wgpuTextureRelease(renderTexture_); renderTexture_ = nullptr; }

    animation_.reset();
    picture_ = nullptr;

    gpuInitialized_ = false;
    contentDirty_ = true;
    return Ok();
}

Result<void> Lottie::render(WebGPUContext& ctx) {
    if (failed_) return Err<void>("Lottie already failed");
    if (!visible_) return Ok();
    if (!animation_) return Err<void>("Lottie has no content");

    const auto& rc = renderCtx_;

    // Update animation if playing
    if (isAnimated_ && playing_ && animation_ && duration_ > 0) {
        double dt = rc.deltaTime > 0 ? rc.deltaTime : 0.016;  // Default to ~60fps
        accumulatedTime_ += dt;

        float fps = totalFrames_ / duration_;
        float targetFrame = static_cast<float>(accumulatedTime_ * fps);

        if (targetFrame >= totalFrames_) {
            if (loop_) {
                accumulatedTime_ = std::fmod(accumulatedTime_, static_cast<double>(duration_));
                targetFrame = std::fmod(targetFrame, totalFrames_);
            } else {
                targetFrame = totalFrames_ - 1;
                playing_ = false;
            }
        }

        if (std::abs(targetFrame - currentFrame_) >= 0.5f) {
            currentFrame_ = targetFrame;
            contentDirty_ = true;
        }
    }

    // Initialize GPU resources on first use
    if (!gpuInitialized_) {
        auto result = initWebGPU(ctx);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to init WebGPU", result);
        }

        result = createCompositePipeline(ctx, rc.targetFormat);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to create pipeline", result);
        }
        gpuInitialized_ = true;
        contentDirty_ = true;  // Need initial render
    }

    // Render ThorVG content if dirty
    if (contentDirty_) {
        auto renderResult = renderThorvgFrame(ctx.getDevice());
        if (!renderResult) {
            failed_ = true;
            return Err<void>("Lottie render failed", renderResult);
        }
    }

    if (!compositePipeline_ || !uniformBuffer_ || !bindGroup_) {
        failed_ = true;
        return Err<void>("Lottie pipeline not initialized");
    }

    // Calculate pixel position from cell position
    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    // Adjust for scroll offset
    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();
        }
    }

    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    // Update uniform buffer if rect changed
    bool rectChanged = (ndcX != lastRect_[0] || ndcY != lastRect_[1] ||
                        ndcW != lastRect_[2] || ndcH != lastRect_[3]);
    if (rectChanged) {
        struct Uniforms { float rect[4]; } uniforms;
        uniforms.rect[0] = ndcX;
        uniforms.rect[1] = ndcY;
        uniforms.rect[2] = ndcW;
        uniforms.rect[3] = ndcH;
        wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));
        lastRect_[0] = ndcX;
        lastRect_[1] = ndcY;
        lastRect_[2] = ndcW;
        lastRect_[3] = ndcH;
    }

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("Lottie: Failed to create command encoder");

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = rc.targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Lottie: Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, compositePipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Lottie: Failed to finish command encoder");
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

void Lottie::prepareFrame(WebGPUContext& ctx) {
    // This is called BEFORE the shared render pass begins
    // Here we render ThorVG content to our intermediate texture

    spdlog::info("Lottie::prepareFrame CALLED! failed={} visible={} animation={} gpu_init={} dirty={}",
                 failed_, visible_, (void*)animation_.get(), gpuInitialized_, contentDirty_);

    if (failed_ || !visible_ || !animation_) {
        return;
    }

    const auto& rc = renderCtx_;

    // Update animation if playing
    if (isAnimated_ && playing_ && animation_ && duration_ > 0) {
        double dt = rc.deltaTime > 0 ? rc.deltaTime : 0.016;  // Default to ~60fps
        accumulatedTime_ += dt;

        float fps = totalFrames_ / duration_;
        float targetFrame = static_cast<float>(accumulatedTime_ * fps);

        if (targetFrame >= totalFrames_) {
            if (loop_) {
                accumulatedTime_ = std::fmod(accumulatedTime_, static_cast<double>(duration_));
                targetFrame = std::fmod(targetFrame, totalFrames_);
            } else {
                targetFrame = totalFrames_ - 1;
                playing_ = false;
            }
        }

        if (std::abs(targetFrame - currentFrame_) >= 0.5f) {
            currentFrame_ = targetFrame;
            contentDirty_ = true;
        }
    }

    // Initialize GPU resources on first use
    if (!gpuInitialized_) {
        spdlog::info("Lottie::prepareFrame: initializing WebGPU resources");
        auto result = initWebGPU(ctx);
        if (!result) {
            spdlog::error("Lottie::prepareFrame: initWebGPU failed: {}", result.error().message());
            failed_ = true;
            return;
        }

        spdlog::info("Lottie::prepareFrame: creating composite pipeline, targetFormat={}",
                     static_cast<int>(rc.targetFormat));
        result = createCompositePipeline(ctx, rc.targetFormat);
        if (!result) {
            spdlog::error("Lottie::prepareFrame: createCompositePipeline failed: {}", result.error().message());
            failed_ = true;
            return;
        }
        gpuInitialized_ = true;
        contentDirty_ = true;
        spdlog::info("Lottie::prepareFrame: GPU resources initialized");
    }

    // Render ThorVG content to texture if dirty
    if (contentDirty_) {
        spdlog::info("Lottie::prepareFrame: rendering ThorVG frame to texture");
        auto renderResult = renderThorvgFrame(ctx.getDevice());
        if (!renderResult) {
            spdlog::error("Lottie::prepareFrame: {}", renderResult.error().message());
            failed_ = true;
            return;
        }
        spdlog::info("Lottie::prepareFrame: ThorVG frame rendered successfully");
    }
}

bool Lottie::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    // This is called INSIDE the shared render pass
    // We only blit our pre-rendered texture here - NO ThorVG rendering!

    if (failed_) {
        return false;
    }
    if (!visible_) {
        return false;
    }
    if (!animation_) {
        return false;
    }
    if (!gpuInitialized_ || !compositePipeline_ || !uniformBuffer_ || !bindGroup_) {
        // Not ready yet - prepareFrame() should have set this up
        return false;
    }

    const auto& rc = renderCtx_;

    // Calculate pixel position from cell position
    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    spdlog::info("Lottie::render: cell pos ({},{}) size ({},{}) cells", x_, y_, widthCells_, heightCells_);
    spdlog::info("Lottie::render: pixel pos ({},{}) size ({},{})", pixelX, pixelY, pixelW, pixelH);

    // Adjust for scroll offset
    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
        spdlog::info("Lottie::render: adjusted for scroll, new pixelY={}", pixelY);
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        spdlog::info("Lottie::render: termRows={}, screenPixelHeight={}, pixelY={}, pixelH={}",
                     rc.termRows, screenPixelHeight, pixelY, pixelH);
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            spdlog::info("Lottie::render: skipped - off-screen");
            return false;
        }
    }

    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    // Update uniform buffer if rect changed
    bool rectChanged = (ndcX != lastRect_[0] || ndcY != lastRect_[1] ||
                        ndcW != lastRect_[2] || ndcH != lastRect_[3]);
    if (rectChanged) {
        struct Uniforms { float rect[4]; } uniforms;
        uniforms.rect[0] = ndcX;
        uniforms.rect[1] = ndcY;
        uniforms.rect[2] = ndcW;
        uniforms.rect[3] = ndcH;
        wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));
        lastRect_[0] = ndcX;
        lastRect_[1] = ndcY;
        lastRect_[2] = ndcW;
        lastRect_[3] = ndcH;
    }

    // Draw ThorVG rendered content into existing pass
    spdlog::info("Lottie::render: drawing composite - NDC rect ({}, {}, {}, {})",
                 ndcX, ndcY, ndcW, ndcH);
    spdlog::info("Lottie::render: pixel position ({}, {}) size ({}, {})",
                 pixelX, pixelY, pixelW, pixelH);
    spdlog::info("Lottie::render: screen size ({}, {}), cell size ({}, {})",
                 rc.screenWidth, rc.screenHeight, rc.cellWidth, rc.cellHeight);

    wgpuRenderPassEncoderSetPipeline(pass, compositePipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    spdlog::info("Lottie::render: composite draw issued");
    return true;
}

} // namespace yetty

extern "C" {
    const char* name() { return "thorvg"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::ThorvgPlugin::create(std::move(engine));
    }
}
