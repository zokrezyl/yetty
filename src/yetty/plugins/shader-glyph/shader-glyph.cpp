#include "shader-glyph.h"
#include "../../renderer/webgpu-context.h"
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <cstring>

namespace yetty {

// Shadertoy-compatible uniform struct (must match WGSL)
// Note: Individual padding fields to avoid WGSL alignment issues with arrays/vec3
struct alignas(16) GlyphUniforms {
    float iTime;           // offset 0
    float iTimeDelta;      // offset 4
    uint32_t iFrame;       // offset 8
    uint32_t _pad0;        // offset 12
    float iResolution[2];  // offset 16
    float iMouse[2];       // offset 24
    uint32_t iCodepoint;   // offset 32
    uint32_t _pad1a;       // offset 36
    uint32_t _pad1b;       // offset 40
    uint32_t _pad1c;       // offset 44
    float rect[4];         // offset 48 (NDC position)
};
static_assert(sizeof(GlyphUniforms) == 64, "GlyphUniforms must be 64 bytes");

//-----------------------------------------------------------------------------
// ShaderGlyphLayer
//-----------------------------------------------------------------------------

ShaderGlyphLayer::~ShaderGlyphLayer() {
    dispose();
}

Result<void> ShaderGlyphLayer::init(uint32_t codepoint) {
    _codepoint = codepoint;
    _time = 0.0f;
    spdlog::debug("ShaderGlyphLayer: initialized for U+{:04X}", codepoint);
    return Ok();
}

void ShaderGlyphLayer::update(double deltaTime) {
    _time += static_cast<float>(deltaTime);
}

Result<void> ShaderGlyphLayer::render(WebGPUContext& ctx,
                                       WGPUTextureView targetView,
                                       WGPUTextureFormat targetFormat,
                                       uint32_t screenWidth, uint32_t screenHeight,
                                       float pixelX, float pixelY,
                                       float pixelW, float pixelH) {
    if (!_parent) {
        return Err<void>("ShaderGlyphLayer: no parent plugin");
    }
    return _parent->renderLayer(ctx, targetView, targetFormat,
                                 screenWidth, screenHeight,
                                 pixelX, pixelY, pixelW, pixelH,
                                 _time, _codepoint);
}

void ShaderGlyphLayer::dispose() {}

//-----------------------------------------------------------------------------
// ShaderGlyphPlugin
//-----------------------------------------------------------------------------

ShaderGlyphPlugin::~ShaderGlyphPlugin() {
    dispose();
}

Result<CustomGlyphPluginPtr> ShaderGlyphPlugin::create() {
    return Ok<CustomGlyphPluginPtr>(std::make_shared<ShaderGlyphPlugin>());
}

std::vector<CodepointRange> ShaderGlyphPlugin::getCodepointRanges() const {
    return {
        {0x231B, 0x231B},    // Hourglass
        {0x23F3, 0x23F3},    // Hourglass flowing
        {0x2728, 0x2728},    // Sparkles
        {0x2764, 0x2764},    // Red heart
        {0x2B50, 0x2B50},    // Star
        {0x1F300, 0x1F5FF},  // Misc Symbols and Pictographs (includes fire, gems, etc)
        {0x1F600, 0x1F64F},  // Emoticons
        {0x1F680, 0x1F6FF},  // Transport and Map
        {0x1F900, 0x1F9FF},  // Supplemental Symbols
    };
}

Result<void> ShaderGlyphPlugin::init(WebGPUContext* ctx) {
    if (!ctx) {
        return Err<void>("ShaderGlyphPlugin::init: null context");
    }

    _device = ctx->getDevice();
    _targetFormat = ctx->getSurfaceFormat();

    // Set shader path relative to source directory
    _shaderPath = std::string(CMAKE_SOURCE_DIR) + "/src/yetty/plugins/shader-glyph/shaders/";

    if (auto res = loadMapping(); !res) {
        spdlog::warn("ShaderGlyphPlugin: failed to load mapping, using defaults");
    }

    if (auto res = initSharedResources(_device, _targetFormat); !res) {
        return Err<void>("ShaderGlyphPlugin: failed to init shared resources", res);
    }

    _initialized = true;
    spdlog::info("ShaderGlyphPlugin: initialized with {} codepoint mappings", _codepointToShader.size());
    return Ok();
}

Result<void> ShaderGlyphPlugin::loadMapping() {
    std::string mappingPath = _shaderPath + "mapping.yaml";

    try {
        YAML::Node config = YAML::LoadFile(mappingPath);

        if (config["default"]) {
            _defaultShader = config["default"].as<std::string>();
        }

        if (config["shaders"]) {
            for (const auto& entry : config["shaders"]) {
                std::string file = entry["file"].as<std::string>();

                if (entry["codepoint"]) {
                    uint32_t cp = entry["codepoint"].as<uint32_t>();
                    _codepointToShader[cp] = file;
                    spdlog::debug("Mapped U+{:04X} -> {}", cp, file);
                }

                if (entry["range"]) {
                    auto range = entry["range"];
                    uint32_t start = range[0].as<uint32_t>();
                    uint32_t end = range[1].as<uint32_t>();
                    for (uint32_t cp = start; cp <= end; cp++) {
                        _codepointToShader[cp] = file;
                    }
                    spdlog::debug("Mapped U+{:04X}-U+{:04X} -> {}", start, end, file);
                }
            }
        }
    } catch (const std::exception& e) {
        return Err<void>(std::string("Failed to load mapping: ") + e.what());
    }

    return Ok();
}

Result<void> ShaderGlyphPlugin::initSharedResources(WGPUDevice device, WGPUTextureFormat targetFormat) {
    (void)targetFormat;

    // Uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(GlyphUniforms);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniformBuffer) {
        return Err<void>("Failed to create uniform buffer");
    }

    // Compile shared vertex shader
    const char* vertCode = getVertexShader();
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = vertCode;

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    _vertModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!_vertModule) {
        return Err<void>("Failed to create vertex shader module");
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.minBindingSize = sizeof(GlyphUniforms);

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &entry;
    _bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!_bindGroupLayout) {
        return Err<void>("Failed to create bind group layout");
    }

    // Bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = _uniformBuffer;
    bgEntry.size = sizeof(GlyphUniforms);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    _bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (!_bindGroup) {
        return Err<void>("Failed to create bind group");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &_bindGroupLayout;
    _pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
    if (!_pipelineLayout) {
        return Err<void>("Failed to create pipeline layout");
    }

    return Ok();
}

std::string ShaderGlyphPlugin::loadShaderFile(const std::string& filename) const {
    std::string fullPath = _shaderPath + filename;
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        spdlog::error("Failed to open shader file: {}", fullPath);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string ShaderGlyphPlugin::getShaderForCodepoint(uint32_t codepoint) const {
    auto it = _codepointToShader.find(codepoint);
    if (it != _codepointToShader.end()) {
        return it->second;
    }
    return _defaultShader;
}

Result<WGPURenderPipeline> ShaderGlyphPlugin::loadShaderPipeline(const std::string& shaderFile) {
    // Load fragment shader source
    std::string fragSource = loadShaderFile(shaderFile);
    if (fragSource.empty()) {
        return Err<WGPURenderPipeline>("Failed to load shader: " + shaderFile);
    }

    // Prepend uniforms struct and global variable declarations
    // Note: Use individual u32 padding fields to avoid WGSL alignment issues
    std::string fullFragSource = R"(
struct GlyphUniforms {
    iTime: f32,
    iTimeDelta: f32,
    iFrame: u32,
    _pad0: u32,
    iResolution: vec2<f32>,
    iMouse: vec2<f32>,
    iCodepoint: u32,
    _pad1a: u32,
    _pad1b: u32,
    _pad1c: u32,
    rect: vec4<f32>,
}

@group(0) @binding(0) var<uniform> u: GlyphUniforms;

// Shadertoy-compatible globals (set by entry point)
var<private> iTime: f32;
var<private> iResolution: vec2<f32>;
var<private> iMouse: vec2<f32>;

)" + fragSource + R"(

// Entry point wrapper
@fragment
fn fs_entry(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    iTime = u.iTime;
    iResolution = u.iResolution;
    iMouse = u.iMouse;
    return fs_main(uv);
}
)";

    // Compile fragment shader
    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDesc.code = fullFragSource.c_str();

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule fragModule = wgpuDeviceCreateShaderModule(_device, &shaderDesc);
    if (!fragModule) {
        spdlog::error("Failed to compile shader: {} - fragModule is null", shaderFile);
        return Err<WGPURenderPipeline>("Failed to compile shader: " + shaderFile);
    }
    spdlog::debug("Compiled fragment shader: {} (ptr={})", shaderFile, (void*)fragModule);

    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = _pipelineLayout;
    pipelineDesc.vertex.module = _vertModule;
    pipelineDesc.vertex.entryPoint = "vs_main";

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = _targetFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState = {};
    fragState.module = fragModule;
    fragState.entryPoint = "fs_entry";
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(_device, &pipelineDesc);
    wgpuShaderModuleRelease(fragModule);

    if (!pipeline) {
        spdlog::error("Pipeline creation returned null for: {}", shaderFile);
        return Err<WGPURenderPipeline>("Failed to create pipeline for: " + shaderFile);
    }

    spdlog::info("Loaded shader pipeline: {} (ptr={})", shaderFile, (void*)pipeline);
    return Ok(pipeline);
}

WGPURenderPipeline ShaderGlyphPlugin::getPipelineForCodepoint(uint32_t codepoint) {
    std::string shaderFile = getShaderForCodepoint(codepoint);

    auto it = _pipelineCache.find(shaderFile);
    if (it != _pipelineCache.end()) {
        return it->second;
    }

    // Load and cache pipeline
    auto result = loadShaderPipeline(shaderFile);
    if (!result) {
        spdlog::error("Failed to load pipeline for {}: {}", shaderFile, error_msg(result));
        // Try default shader as fallback
        if (shaderFile != _defaultShader) {
            result = loadShaderPipeline(_defaultShader);
            if (result) {
                _pipelineCache[_defaultShader] = *result;
            }
        }
    }

    if (result) {
        _pipelineCache[shaderFile] = *result;
        return *result;
    }

    return nullptr;
}

Result<CustomGlyphLayerPtr> ShaderGlyphPlugin::createLayer(uint32_t codepoint) {
    auto layer = std::make_shared<ShaderGlyphLayer>();
    layer->setParent(this);

    if (auto res = layer->init(codepoint); !res) {
        return Err<CustomGlyphLayerPtr>("Failed to init ShaderGlyphLayer", res);
    }

    return Ok<CustomGlyphLayerPtr>(layer);
}

Result<void> ShaderGlyphPlugin::renderAll(WebGPUContext& ctx,
                                           WGPUTextureView targetView,
                                           WGPUTextureFormat targetFormat,
                                           uint32_t screenWidth, uint32_t screenHeight,
                                           float cellWidth, float cellHeight,
                                           int scrollOffset) {
    if (!_initialized) {
        return Ok();
    }

    for (auto& layer : _layers) {
        if (!layer->isVisible()) continue;

        float pixelX = layer->getCol() * cellWidth;
        float pixelY = layer->getRow() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        if (scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        if (pixelY + pixelH <= 0 || pixelY >= screenHeight) continue;

        if (auto res = renderLayer(ctx, targetView, targetFormat,
                                    screenWidth, screenHeight,
                                    pixelX, pixelY, pixelW, pixelH,
                                    layer->getTime(), layer->getCodepoint()); !res) {
            spdlog::error("Failed to render layer: {}", error_msg(res));
        }
    }

    return Ok();
}

Result<void> ShaderGlyphPlugin::renderLayer(WebGPUContext& ctx,
                                             WGPUTextureView targetView,
                                             WGPUTextureFormat targetFormat,
                                             uint32_t screenWidth, uint32_t screenHeight,
                                             float pixelX, float pixelY,
                                             float pixelW, float pixelH,
                                             float time, uint32_t codepoint) {
    (void)targetFormat;

    WGPURenderPipeline pipeline = getPipelineForCodepoint(codepoint);
    if (!pipeline) {
        return Err<void>("No pipeline for codepoint");
    }

    // Convert to NDC
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    // Update uniforms
    GlyphUniforms uniforms = {};
    uniforms.iTime = time;
    uniforms.iTimeDelta = 0.016f;
    uniforms.iFrame = static_cast<uint32_t>(time * 60);
    uniforms.iResolution[0] = pixelW;
    uniforms.iResolution[1] = pixelH;
    uniforms.iMouse[0] = -1.0f;
    uniforms.iMouse[1] = -1.0f;
    uniforms.iCodepoint = codepoint;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Render
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encDesc);

    WGPURenderPassColorAttachment colorAtt = {};
    colorAtt.view = targetView;
    colorAtt.loadOp = WGPULoadOp_Load;
    colorAtt.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);

    return Ok();
}

void ShaderGlyphPlugin::update(double deltaTime) {
    for (auto& layer : _layers) {
        layer->update(deltaTime);
    }
}

void ShaderGlyphPlugin::dispose() {
    clearLayers();

    for (auto& [name, pipeline] : _pipelineCache) {
        if (pipeline) wgpuRenderPipelineRelease(pipeline);
    }
    _pipelineCache.clear();

    if (_bindGroup) { wgpuBindGroupRelease(_bindGroup); _bindGroup = nullptr; }
    if (_uniformBuffer) { wgpuBufferRelease(_uniformBuffer); _uniformBuffer = nullptr; }
    if (_pipelineLayout) { wgpuPipelineLayoutRelease(_pipelineLayout); _pipelineLayout = nullptr; }
    if (_bindGroupLayout) { wgpuBindGroupLayoutRelease(_bindGroupLayout); _bindGroupLayout = nullptr; }
    if (_vertModule) { wgpuShaderModuleRelease(_vertModule); _vertModule = nullptr; }

    _initialized = false;
}

const char* ShaderGlyphPlugin::getVertexShader() {
    return R"(
struct GlyphUniforms {
    iTime: f32,
    iTimeDelta: f32,
    iFrame: u32,
    _pad0: u32,
    iResolution: vec2<f32>,
    iMouse: vec2<f32>,
    iCodepoint: u32,
    _pad1a: u32,
    _pad1b: u32,
    _pad1c: u32,
    rect: vec4<f32>,
}

@group(0) @binding(0) var<uniform> u: GlyphUniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 1.0), vec2<f32>(0.0, 1.0)
    );

    let pos = positions[vertexIndex];
    let ndcX = u.rect.x + pos.x * u.rect.z;
    let ndcY = u.rect.y - pos.y * u.rect.w;

    var output: VertexOutput;
    output.position = vec4<f32>(ndcX, ndcY, 0.0, 1.0);
    output.uv = pos;
    return output;
}
)";
}

} // namespace yetty
