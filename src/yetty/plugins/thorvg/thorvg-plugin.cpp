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
    if (auto res = static_cast<ThorvgPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init ThorvgPlugin", res);
    }
    return Ok(p);
}

Result<void> ThorvgPlugin::init() noexcept {
    // Initialize ThorVG engine with WebGPU support
    auto result = tvg::Initializer::init(0);  // Single-threaded for now
    if (result != tvg::Result::Success) {
        return Err<void>("Failed to initialize ThorVG engine");
    }
    
    uint32_t major, minor, micro;
    const char* version = tvg::Initializer::version(&major, &minor, &micro);
    spdlog::info("ThorvgPlugin: initialized ThorVG {} (WebGPU renderer)", version ? version : "unknown");
    
    _initialized = true;
    return Ok();
}

Result<void> ThorvgPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose ThorvgPlugin", res);
    }
    
    if (_initialized) {
        tvg::Initializer::term();
    }
    
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> ThorvgPlugin::createLayer(const std::string& payload) {
    spdlog::info("ThorvgPlugin::createLayer called with payload size={}", payload.size());
    auto layer = std::make_shared<ThorvgLayer>();
    auto result = layer->init(payload);
    if (!result) {
        spdlog::error("ThorvgPlugin::createLayer: init failed: {}", result.error().message());
        return Err<PluginLayerPtr>("Failed to init ThorvgLayer", result);
    }
    spdlog::info("ThorvgPlugin::createLayer: layer created successfully");
    return Ok<PluginLayerPtr>(layer);
}

//-----------------------------------------------------------------------------
// ThorvgLayer
//-----------------------------------------------------------------------------

ThorvgLayer::ThorvgLayer() = default;

ThorvgLayer::~ThorvgLayer() { (void)dispose(); }

Result<void> ThorvgLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("ThorvgLayer: empty payload");
    }

    _payload = payload;
    
    std::string content = payload;
    std::string mimeType;
    
    spdlog::info("ThorvgLayer::init: payload size={}", payload.size());
    
    // Check for type prefix (format: "type\n<content>")
    size_t newlinePos = payload.find('\n');
    if (newlinePos != std::string::npos && newlinePos < 20) {
        std::string prefix = payload.substr(0, newlinePos);
        spdlog::info("ThorvgLayer::init: detected prefix='{}' at pos {}", prefix, newlinePos);
        if (prefix == "svg" || prefix == "lottie" || prefix == "yaml") {
            mimeType = prefix;
            content = payload.substr(newlinePos + 1);
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
        spdlog::info("ThorvgLayer::init: auto-detected mimeType='{}'", mimeType);
    }
    
    spdlog::info("ThorvgLayer::init: mimeType='{}', content size={}", mimeType, content.size());
    
    // Handle YAML vector graphics by converting to SVG
    if (mimeType == "yaml") {
        auto result = yamlToSvg(content);
        if (!result) {
            return Err<void>("Failed to convert YAML to SVG", result);
        }
        content = *result;
        mimeType = "svg";
        spdlog::info("ThorvgLayer::init: converted YAML to SVG, size={}", content.size());
    }

    auto result = loadContent(content, mimeType);
    if (!result) {
        spdlog::error("ThorvgLayer::init: loadContent failed: {}", result.error().message());
        return result;
    }

    spdlog::info("ThorvgLayer: loaded {}x{} content (animated: {})", 
                 _content_width, _content_height, _is_animated);
    return Ok();
}

Result<void> ThorvgLayer::loadContent(const std::string& data, const std::string& mimeType) {
    // WgCanvas will be created when WebGPU context is available (in initWebGPU)
    // For now, just parse content to get dimensions and animation info
    
    spdlog::info("ThorvgLayer::loadContent: mimeType='{}', data size={}", mimeType, data.size());
    
    // Default size
    _content_width = 256;
    _content_height = 256;

    tvg::Picture* picture = nullptr;

    // Check if this is an animation (Lottie)
    if (mimeType == "lottie" || mimeType == "lottie+json" || mimeType == "lot") {
        spdlog::info("ThorvgLayer::loadContent: loading as Lottie animation");
        _animation.reset(tvg::Animation::gen());
        if (!_animation) {
            return Err<void>("Failed to create ThorVG Animation");
        }

        picture = _animation->picture();
        if (!picture) {
            return Err<void>("Failed to get picture from Animation");
        }

        auto result = picture->load(data.c_str(), static_cast<uint32_t>(data.size()), 
                                    "lottie", nullptr, true);
        spdlog::info("ThorvgLayer::loadContent: Lottie load result={}", static_cast<int>(result));
        if (result != tvg::Result::Success) {
            _animation.reset();
            return Err<void>("Failed to load Lottie animation");
        }

        _is_animated = true;
        _total_frames = _animation->totalFrame();
        _duration = _animation->duration();
        _current_frame = 0.0f;
        
        spdlog::debug("ThorvgLayer: Lottie animation loaded - {} frames, {}s duration", 
                     _total_frames, _duration);
    } else {
        // Static picture (SVG, PNG, etc.)
        // Use Animation wrapper for ownership management (Picture has protected destructor)
        spdlog::info("ThorvgLayer::loadContent: loading as static content (mimeType='{}')", mimeType);
        _animation.reset(tvg::Animation::gen());
        if (!_animation) {
            return Err<void>("Failed to create ThorVG Animation for static content");
        }

        picture = _animation->picture();
        if (!picture) {
            return Err<void>("Failed to get picture from Animation");
        }

        // ThorVG auto-detects format from content when mime is nullptr
        // For SVG, pass nullptr to let ThorVG detect the format from the XML/SVG content
        const char* mime = nullptr;  // Let ThorVG auto-detect
        spdlog::info("ThorvgLayer::loadContent: calling picture->load() with mime=nullptr (auto-detect), data size={}", 
                     data.size());
        auto result = picture->load(data.c_str(), static_cast<uint32_t>(data.size()), 
                                    mime, nullptr, true);
        spdlog::info("ThorvgLayer::loadContent: picture->load result={}", static_cast<int>(result));
        if (result != tvg::Result::Success) {
            _animation.reset();
            return Err<void>("Failed to load static content into ThorVG Picture (result=" + std::to_string(static_cast<int>(result)) + ")");
        }

        _is_animated = false;
    }

    // Get content size
    float w = 0, h = 0;
    picture->size(&w, &h);
    
    if (w > 0 && h > 0) {
        _content_width = static_cast<uint32_t>(w);
        _content_height = static_cast<uint32_t>(h);
    }

    _picture = picture;
    _content_dirty = true;

    return Ok();
}

Result<void> ThorvgLayer::initWebGPU(WebGPUContext& ctx) {
    WGPUDevice device = ctx.getDevice();
    WGPUInstance instance = ctx.getInstance();
    
    // Create render texture that ThorVG will render to
    // ThorVG's WgCanvas internal blit pipeline uses BGRA8Unorm format
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = _content_width;
    texDesc.size.height = _content_height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_BGRA8Unorm;  // Must match ThorVG's internal blit pipeline
    // ThorVG needs RenderAttachment for drawing, we need TextureBinding for compositing
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;

    _render_texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!_render_texture) return Err<void>("Failed to create ThorVG render texture");

    // Create texture view for our composite shader
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    _render_texture_view = wgpuTextureCreateView(_render_texture, &viewDesc);
    if (!_render_texture_view) return Err<void>("Failed to create ThorVG texture view");

    // Create ThorVG WgCanvas and set the texture as target
    _canvas.reset(tvg::WgCanvas::gen());
    if (!_canvas) {
        return Err<void>("Failed to create ThorVG WgCanvas");
    }

    // Set the render target - type=1 means texture target (not surface)
    auto result = _canvas->target(device, instance, _render_texture, 
                                   _content_width, _content_height,
                                   tvg::ColorSpace::ABGR8888S, 1);
    if (result != tvg::Result::Success) {
        return Err<void>("Failed to set ThorVG WgCanvas target");
    }

    // Push the picture to canvas
    // Note: Canvas::push() takes ownership reference
    if (_picture) {
        result = _canvas->push(_picture);
        if (result != tvg::Result::Success) {
            return Err<void>("Failed to push picture to ThorVG canvas");
        }
    }

    spdlog::debug("ThorvgLayer: WebGPU canvas initialized {}x{}", _content_width, _content_height);
    return Ok();
}

Result<void> ThorvgLayer::createCompositePipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create sampler for compositing
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    _sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!_sampler) return Err<void>("Failed to create sampler");

    // Create uniform buffer for rect transform
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

    // Composite shader - renders ThorVG output texture as a quad
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
    let col = textureSample(tex, texSampler, uv);
    // DEBUG: Mix texture with magenta to see if quad is drawing
    return mix(vec4(1.0, 0.0, 1.0, 1.0), col, col.a);
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
    bgE[0].binding = 0; bgE[0].buffer = _uniform_buffer; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = _sampler;
    bgE[2].binding = 2; bgE[2].textureView = _render_texture_view;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl; bgDesc.entryCount = 3; bgDesc.entries = bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

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

    _composite_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_composite_pipeline) return Err<void>("Failed to create composite pipeline");

    spdlog::debug("ThorvgLayer: composite pipeline created");
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
        spdlog::error("ThorvgLayer WebGPU error (type={}): {}", static_cast<int>(type), data->message);
    }
}

Result<void> ThorvgLayer::renderThorvgFrame(WGPUDevice device) {
    if (!_canvas || !_picture) {
        spdlog::warn("ThorvgLayer::renderThorvgFrame: no canvas or picture");
        return Ok();  // Nothing to render is not an error
    }

    spdlog::info("ThorvgLayer::renderThorvgFrame: rendering frame {}, animated={}", 
                 _current_frame, _is_animated);

    // Update animation frame if animated
    if (_is_animated && _animation) {
        auto frameResult = _animation->frame(_current_frame);
        spdlog::info("ThorvgLayer::renderThorvgFrame: animation->frame({}) result={}", 
                     _current_frame, static_cast<int>(frameResult));
    }
    
    // Render using ThorVG's WebGPU canvas
    auto updateResult = _canvas->update();
    spdlog::info("ThorvgLayer::renderThorvgFrame: canvas->update result={}", static_cast<int>(updateResult));
    if (updateResult != tvg::Result::Success) {
        return Err<void>("ThorVG canvas update failed (result=" + std::to_string(static_cast<int>(updateResult)) + ")");
    }
    
    // draw(true) clears the buffer before rendering
    auto drawResult = _canvas->draw(true);
    spdlog::info("ThorvgLayer::renderThorvgFrame: canvas->draw result={}", static_cast<int>(drawResult));
    if (drawResult != tvg::Result::Success) {
        return Err<void>("ThorVG canvas draw failed (result=" + std::to_string(static_cast<int>(drawResult)) + ")");
    }
    
    // sync() submits WebGPU commands
    auto syncResult = _canvas->sync();
    spdlog::info("ThorvgLayer::renderThorvgFrame: canvas->sync result={}", static_cast<int>(syncResult));
    if (syncResult != tvg::Result::Success) {
        return Err<void>("ThorVG canvas sync failed (result=" + std::to_string(static_cast<int>(syncResult)) + ")");
    }
    
    _content_dirty = false;
    spdlog::info("ThorvgLayer::renderThorvgFrame: completed successfully");
    return Ok();
}

void ThorvgLayer::setFrame(float frame) {
    if (!_is_animated) return;
    
    _current_frame = frame;
    if (_current_frame >= _total_frames) {
        _current_frame = _loop ? 0.0f : _total_frames - 1;
    }
    if (_current_frame < 0) {
        _current_frame = 0;
    }
    
    _content_dirty = true;
}

//-----------------------------------------------------------------------------
// YAML to SVG Conversion
// Converts simple YAML vector graphics definitions to SVG format
//-----------------------------------------------------------------------------

Result<std::string> ThorvgLayer::yamlToSvg(const std::string& yamlContent) {
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
        
        spdlog::debug("ThorvgLayer: converted YAML to SVG ({} bytes)", svg.str().size());
        return Ok(svg.str());
        
    } catch (const YAML::Exception& e) {
        return Err<std::string>(std::string("YAML parse error: ") + e.what());
    } catch (const std::exception& e) {
        return Err<std::string>(std::string("Error converting YAML to SVG: ") + e.what());
    }
}

Result<void> ThorvgLayer::dispose() {
    // Release composite resources
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_composite_pipeline) { wgpuRenderPipelineRelease(_composite_pipeline); _composite_pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_sampler) { wgpuSamplerRelease(_sampler); _sampler = nullptr; }
    if (_render_texture_view) { wgpuTextureViewRelease(_render_texture_view); _render_texture_view = nullptr; }
    
    // Release ThorVG canvas before destroying the texture it targets
    _canvas.reset();
    
    if (_render_texture) { wgpuTextureRelease(_render_texture); _render_texture = nullptr; }
    
    _animation.reset();
    _picture = nullptr;
    
    _gpu_initialized = false;
    _content_dirty = true;
    return Ok();
}

Result<void> ThorvgLayer::render(WebGPUContext& ctx) {
    if (_failed) return Err<void>("ThorvgLayer already failed");
    if (!_visible) return Ok();
    if (!_animation) return Err<void>("ThorvgLayer has no content");

    const auto& rc = _render_context;

    // Update animation if playing
    if (_is_animated && _playing && _animation && _duration > 0) {
        double dt = rc.deltaTime > 0 ? rc.deltaTime : 0.016;  // Default to ~60fps
        _accumulated_time += dt;
        
        float fps = _total_frames / _duration;
        float targetFrame = static_cast<float>(_accumulated_time * fps);
        
        if (targetFrame >= _total_frames) {
            if (_loop) {
                _accumulated_time = std::fmod(_accumulated_time, static_cast<double>(_duration));
                targetFrame = std::fmod(targetFrame, _total_frames);
            } else {
                targetFrame = _total_frames - 1;
                _playing = false;
            }
        }
        
        if (std::abs(targetFrame - _current_frame) >= 0.5f) {
            _current_frame = targetFrame;
            _content_dirty = true;
        }
    }

    // Initialize GPU resources on first use
    if (!_gpu_initialized) {
        auto result = initWebGPU(ctx);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to init WebGPU", result);
        }
        
        result = createCompositePipeline(ctx, rc.targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to create pipeline", result);
        }
        _gpu_initialized = true;
        _content_dirty = true;  // Need initial render
    }

    // Render ThorVG content if dirty
    if (_content_dirty) {
        auto renderResult = renderThorvgFrame(ctx.getDevice());
        if (!renderResult) {
            _failed = true;
            return Err<void>("ThorvgLayer render failed", renderResult);
        }
    }

    if (!_composite_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("ThorvgLayer pipeline not initialized");
    }

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    // Adjust for scroll offset
    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
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
    bool rectChanged = (ndcX != _last_rect[0] || ndcY != _last_rect[1] ||
                        ndcW != _last_rect[2] || ndcH != _last_rect[3]);
    if (rectChanged) {
        struct Uniforms { float rect[4]; } uniforms;
        uniforms.rect[0] = ndcX;
        uniforms.rect[1] = ndcY;
        uniforms.rect[2] = ndcW;
        uniforms.rect[3] = ndcH;
        wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));
        _last_rect[0] = ndcX;
        _last_rect[1] = ndcY;
        _last_rect[2] = ndcW;
        _last_rect[3] = ndcH;
    }

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("ThorvgLayer: Failed to create command encoder");

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
        return Err<void>("ThorvgLayer: Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _composite_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("ThorvgLayer: Failed to finish command encoder");
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

bool ThorvgLayer::renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (_failed) {
        spdlog::warn("ThorvgLayer::renderToPass: layer already failed");
        return false;
    }
    if (!_visible) {
        return false;  // Silent skip, normal
    }
    if (!_animation) {
        spdlog::warn("ThorvgLayer::renderToPass: no animation/picture loaded");
        return false;
    }

    const auto& rc = _render_context;

    // Update animation if playing
    if (_is_animated && _playing && _animation && _duration > 0) {
        double dt = rc.deltaTime > 0 ? rc.deltaTime : 0.016;  // Default to ~60fps
        _accumulated_time += dt;
        
        float fps = _total_frames / _duration;
        float targetFrame = static_cast<float>(_accumulated_time * fps);
        
        if (targetFrame >= _total_frames) {
            if (_loop) {
                _accumulated_time = std::fmod(_accumulated_time, static_cast<double>(_duration));
                targetFrame = std::fmod(targetFrame, _total_frames);
            } else {
                targetFrame = _total_frames - 1;
                _playing = false;
            }
        }
        
        if (std::abs(targetFrame - _current_frame) >= 0.5f) {
            _current_frame = targetFrame;
            _content_dirty = true;
        }
    }

    // Initialize GPU resources on first use
    if (!_gpu_initialized) {
        spdlog::info("ThorvgLayer::renderToPass: initializing WebGPU resources");
        auto result = initWebGPU(ctx);
        if (!result) {
            spdlog::error("ThorvgLayer::renderToPass: initWebGPU failed: {}", result.error().message());
            _failed = true;
            return false;
        }
        
        spdlog::info("ThorvgLayer::renderToPass: creating composite pipeline, targetFormat={}", 
                     static_cast<int>(rc.targetFormat));
        result = createCompositePipeline(ctx, rc.targetFormat);
        if (!result) {
            spdlog::error("ThorvgLayer::renderToPass: createCompositePipeline failed: {}", result.error().message());
            _failed = true;
            return false;
        }
        _gpu_initialized = true;
        _content_dirty = true;
        spdlog::info("ThorvgLayer::renderToPass: GPU resources initialized");
    }

    // Render ThorVG content if dirty (animation frame changed)
    if (_content_dirty) {
        spdlog::info("ThorvgLayer::renderToPass: rendering ThorVG frame (dirty)");
        auto renderResult = renderThorvgFrame(ctx.getDevice());
        if (!renderResult) {
            spdlog::error("ThorvgLayer::renderToPass: {}", renderResult.error().message());
            _failed = true;
            return false;
        }
        spdlog::info("ThorvgLayer::renderToPass: ThorVG frame rendered successfully");
    }

    if (!_composite_pipeline || !_uniform_buffer || !_bind_group) {
        spdlog::warn("ThorvgLayer::renderToPass: pipeline/buffer/bindgroup null - pipeline={}, buffer={}, bindgroup={}",
                     (void*)_composite_pipeline, (void*)_uniform_buffer, (void*)_bind_group);
        _failed = true;
        return false;
    }

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;
    
    spdlog::info("ThorvgLayer::renderToPass: cell pos ({},{}) size ({},{}) cells", _x, _y, _width_cells, _height_cells);
    spdlog::info("ThorvgLayer::renderToPass: pixel pos ({},{}) size ({},{})", pixelX, pixelY, pixelW, pixelH);

    // Adjust for scroll offset
    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
        spdlog::info("ThorvgLayer::renderToPass: adjusted for scroll, new pixelY={}", pixelY);
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        spdlog::info("ThorvgLayer::renderToPass: termRows={}, screenPixelHeight={}, pixelY={}, pixelH={}", 
                     rc.termRows, screenPixelHeight, pixelY, pixelH);
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            spdlog::info("ThorvgLayer::renderToPass: skipped - off-screen");
            return false;
        }
    }

    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    // Update uniform buffer if rect changed
    bool rectChanged = (ndcX != _last_rect[0] || ndcY != _last_rect[1] ||
                        ndcW != _last_rect[2] || ndcH != _last_rect[3]);
    if (rectChanged) {
        struct Uniforms { float rect[4]; } uniforms;
        uniforms.rect[0] = ndcX;
        uniforms.rect[1] = ndcY;
        uniforms.rect[2] = ndcW;
        uniforms.rect[3] = ndcH;
        wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));
        _last_rect[0] = ndcX;
        _last_rect[1] = ndcY;
        _last_rect[2] = ndcW;
        _last_rect[3] = ndcH;
    }

    // Draw ThorVG rendered content into existing pass
    spdlog::info("ThorvgLayer::renderToPass: drawing composite - NDC rect ({}, {}, {}, {})", 
                 ndcX, ndcY, ndcW, ndcH);
    spdlog::info("ThorvgLayer::renderToPass: pixel position ({}, {}) size ({}, {})",
                 pixelX, pixelY, pixelW, pixelH);
    spdlog::info("ThorvgLayer::renderToPass: screen size ({}, {}), cell size ({}, {})",
                 rc.screenWidth, rc.screenHeight, rc.cellWidth, rc.cellHeight);
    
    wgpuRenderPassEncoderSetPipeline(pass, _composite_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    spdlog::info("ThorvgLayer::renderToPass: composite draw issued");
    return true;
}

} // namespace yetty

extern "C" {
    const char* name() { return "thorvg"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::ThorvgPlugin::create(std::move(engine));
    }
}
