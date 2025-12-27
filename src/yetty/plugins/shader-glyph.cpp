#include "shader-glyph.h"
#include "../renderer/webgpu-context.h"
#include <spdlog/spdlog.h>

namespace yetty {

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

void ShaderGlyphLayer::dispose() {
    // No per-layer resources to dispose (uses shared pipeline)
}

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
        // Emoticons block (smileys, etc.)
        {0x1F600, 0x1F64F},
        // Miscellaneous Symbols and Pictographs
        {0x1F300, 0x1F5FF},
        // Transport and Map Symbols
        {0x1F680, 0x1F6FF},
        // Supplemental Symbols and Pictographs
        {0x1F900, 0x1F9FF},
    };
}

Result<void> ShaderGlyphPlugin::init(WebGPUContext* ctx) {
    if (!ctx) {
        return Err<void>("ShaderGlyphPlugin::init: null context");
    }

    if (auto res = initPipeline(ctx->getDevice(), ctx->getSurfaceFormat()); !res) {
        return Err<void>("ShaderGlyphPlugin: failed to init pipeline", res);
    }

    _initialized = true;
    spdlog::info("ShaderGlyphPlugin: initialized");
    return Ok();
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
    if (!_initialized || !_pipeline) {
        return Ok();  // Not yet initialized, skip
    }

    // Ensure pipeline is created for this format
    if (_targetFormat != targetFormat) {
        dispose();
        if (auto res = initPipeline(ctx.getDevice(), targetFormat); !res) {
            return Err<void>("ShaderGlyphPlugin: failed to reinit pipeline", res);
        }
    }

    // Render each visible layer with its own command buffer
    // Each layer needs a separate submission because uniform buffers are updated
    for (auto& layer : _layers) {
        if (!layer->isVisible()) {
            continue;
        }

        // Calculate pixel position
        float pixelX = layer->getCol() * cellWidth;
        float pixelY = layer->getRow() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        // Apply scroll offset for relative positioning
        if (scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        // Skip if off-screen
        float screenPixelHeight = static_cast<float>(screenHeight);
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            continue;
        }

        // Convert pixel coords to NDC
        float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
        float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
        float ndcW = (pixelW / screenWidth) * 2.0f;
        float ndcH = (pixelH / screenHeight) * 2.0f;

        // Update uniform buffer
        struct alignas(16) Uniforms {
            float time;
            float _pad0[3];
            float _pad1[4];
            float rect[4];
            float resolution[2];
            float _pad2[2];
        } uniforms;

        uniforms.time = layer->getTime();
        uniforms._pad0[0] = uniforms._pad0[1] = uniforms._pad0[2] = 0.0f;
        uniforms._pad1[0] = uniforms._pad1[1] = uniforms._pad1[2] = uniforms._pad1[3] = 0.0f;
        uniforms.rect[0] = ndcX;
        uniforms.rect[1] = ndcY;
        uniforms.rect[2] = ndcW;
        uniforms.rect[3] = ndcH;
        uniforms.resolution[0] = pixelW;
        uniforms.resolution[1] = pixelH;
        uniforms._pad2[0] = uniforms._pad2[1] = 0.0f;

        wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

        // Create command encoder
        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
        if (!encoder) continue;

        // Begin render pass
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        if (!pass) {
            wgpuCommandEncoderRelease(encoder);
            continue;
        }

        wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, _bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Submit
        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        if (cmdBuffer) {
            wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
            wgpuCommandBufferRelease(cmdBuffer);
            spdlog::debug("ShaderGlyphPlugin: draw submitted for ({},{})",
                         layer->getCol(), layer->getRow());
        }
        wgpuCommandEncoderRelease(encoder);
    }

    return Ok();
}

void ShaderGlyphPlugin::update(double deltaTime) {
    for (auto& layer : _layers) {
        layer->update(deltaTime);
    }
}

void ShaderGlyphPlugin::dispose() {
    clearLayers();

    if (_bindGroup) {
        wgpuBindGroupRelease(_bindGroup);
        _bindGroup = nullptr;
    }
    if (_uniformBuffer) {
        wgpuBufferRelease(_uniformBuffer);
        _uniformBuffer = nullptr;
    }
    if (_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(_bindGroupLayout);
        _bindGroupLayout = nullptr;
    }
    if (_pipeline) {
        wgpuRenderPipelineRelease(_pipeline);
        _pipeline = nullptr;
    }

    _initialized = false;
    _targetFormat = WGPUTextureFormat_Undefined;
}

Result<void> ShaderGlyphPlugin::renderLayer(WebGPUContext& ctx,
                                             WGPUTextureView targetView,
                                             WGPUTextureFormat targetFormat,
                                             uint32_t screenWidth, uint32_t screenHeight,
                                             float pixelX, float pixelY,
                                             float pixelW, float pixelH,
                                             float time, uint32_t codepoint) {
    (void)targetFormat;
    (void)codepoint;

    if (!_pipeline || !_uniformBuffer || !_bindGroup) {
        return Err<void>("ShaderGlyphPlugin: pipeline not initialized");
    }

    // Convert pixel coords to NDC
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    // Update uniform buffer
    // WGSL struct layout (aligned to 16 bytes for vec3/vec4):
    // time: f32 at offset 0
    // _pad1: vec3<f32> at offset 16 (aligned to 16)
    // rect: vec4<f32> at offset 32 (aligned to 16)
    // resolution: vec2<f32> at offset 48 (aligned to 8)
    // _pad2: vec2<f32> at offset 56 (aligned to 8)
    // Total: 64 bytes
    struct alignas(16) Uniforms {
        float time;           // offset 0
        float _pad0[3];       // offset 4, padding to 16
        float _pad1[4];       // offset 16, represents vec3 + alignment padding
        float rect[4];        // offset 32
        float resolution[2];  // offset 48
        float _pad2[2];       // offset 56
    } uniforms;

    static_assert(sizeof(Uniforms) == 64, "Uniforms must be 64 bytes");

    uniforms.time = time;
    uniforms._pad0[0] = uniforms._pad0[1] = uniforms._pad0[2] = 0.0f;
    uniforms._pad1[0] = uniforms._pad1[1] = uniforms._pad1[2] = uniforms._pad1[3] = 0.0f;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms._pad2[0] = uniforms._pad2[1] = 0.0f;

    spdlog::debug("    Uniforms: rect({},{},{},{}) at offset 32",
                  uniforms.rect[0], uniforms.rect[1], uniforms.rect[2], uniforms.rect[3]);

    // Debug: print raw bytes of uniforms struct
    static bool printedOnce = false;
    if (!printedOnce) {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&uniforms);
        spdlog::info("Uniforms layout (first 48 bytes): ");
        for (int i = 0; i < 48; i += 4) {
            float val;
            std::memcpy(&val, bytes + i, 4);
            spdlog::info("  offset {}: {}", i, val);
        }
        printedOnce = true;
    }

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

    if (!targetView) {
        spdlog::error("ShaderGlyphPlugin: targetView is null!");
        return Err<void>("ShaderGlyphPlugin: targetView is null");
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        return Err<void>("ShaderGlyphPlugin: failed to create command encoder");
    }

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("ShaderGlyphPlugin: failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (cmdBuffer) {
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        spdlog::debug("ShaderGlyphPlugin: draw submitted successfully");
    } else {
        spdlog::error("ShaderGlyphPlugin: failed to finish command buffer!");
    }
    wgpuCommandEncoderRelease(encoder);

    return Ok();
}

Result<void> ShaderGlyphPlugin::initPipeline(WGPUDevice device, WGPUTextureFormat targetFormat) {
    _targetFormat = targetFormat;

    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 64;  // WGSL aligned struct size = 64 bytes
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniformBuffer) {
        return Err<void>("ShaderGlyphPlugin: failed to create uniform buffer");
    }

    // Compile shaders
    const char* vertCode = getVertexShader();
    WGPUShaderModuleWGSLDescriptor wgslDescVert = {};
    wgslDescVert.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDescVert.code = vertCode;

    WGPUShaderModuleDescriptor shaderDescVert = {};
    shaderDescVert.nextInChain = &wgslDescVert.chain;
    WGPUShaderModule vertModule = wgpuDeviceCreateShaderModule(device, &shaderDescVert);

    const char* fragCode = getFragmentShader();
    WGPUShaderModuleWGSLDescriptor wgslDescFrag = {};
    wgslDescFrag.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDescFrag.code = fragCode;

    WGPUShaderModuleDescriptor shaderDescFrag = {};
    shaderDescFrag.nextInChain = &wgslDescFrag.chain;
    WGPUShaderModule fragModule = wgpuDeviceCreateShaderModule(device, &shaderDescFrag);

    if (!vertModule || !fragModule) {
        if (vertModule) wgpuShaderModuleRelease(vertModule);
        if (fragModule) wgpuShaderModuleRelease(fragModule);
        return Err<void>("ShaderGlyphPlugin: failed to create shader modules");
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &entry;
    _bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!_bindGroupLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("ShaderGlyphPlugin: failed to create bind group layout");
    }

    // Bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = _uniformBuffer;
    bgEntry.size = 64;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    _bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (!_bindGroup) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("ShaderGlyphPlugin: failed to create bind group");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
    if (!pipelineLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("ShaderGlyphPlugin: failed to create pipeline layout");
    }

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = vertModule;
    pipelineDesc.vertex.entryPoint = "vs_main";

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat;
    colorTarget.blend = &blend;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState = {};
    fragState.module = fragModule;
    fragState.entryPoint = "fs_main";
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(vertModule);
    wgpuShaderModuleRelease(fragModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) {
        return Err<void>("ShaderGlyphPlugin: failed to create render pipeline");
    }

    spdlog::debug("ShaderGlyphPlugin: pipeline created for format {}", (int)_targetFormat);
    return Ok();
}

const char* ShaderGlyphPlugin::getVertexShader() {
    return R"(
struct Uniforms {
    time: f32,
    _pad1: vec3<f32>,
    rect: vec4<f32>,
    resolution: vec2<f32>,
    _pad2: vec2<f32>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    // Generate unit quad vertices [0,1] and transform to NDC using rect
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 1.0)
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

const char* ShaderGlyphPlugin::getFragmentShader() {
    return R"(
struct Uniforms {
    time: f32,
    _pad1: vec3<f32>,
    rect: vec4<f32>,
    resolution: vec2<f32>,
    _pad2: vec2<f32>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

// SDF circle
fn sdCircle(p: vec2<f32>, r: f32) -> f32 {
    return length(p) - r;
}

// SDF arc for the smile
fn sdArc(p: vec2<f32>, r: f32, a1: f32, a2: f32) -> f32 {
    let angle = atan2(p.y, p.x);
    if (angle >= a1 && angle <= a2) {
        return abs(length(p) - r);
    }
    // Distance to endpoints
    let p1 = vec2<f32>(cos(a1), sin(a1)) * r;
    let p2 = vec2<f32>(cos(a2), sin(a2)) * r;
    return min(length(p - p1), length(p - p2));
}

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    // Center UV at (0,0), scale to [-1,1]
    let p = (uv - 0.5) * 2.0;

    // Animate
    let t = u.time;
    let blink = step(0.9, fract(t * 0.5));  // Blink every 2 seconds

    // Face (yellow circle)
    let face = sdCircle(p, 0.85);
    let faceColor = vec3<f32>(1.0, 0.85, 0.2);  // Yellow

    // Eyes
    let eyeY = 0.2;
    let eyeX = 0.3;
    let eyeRadius = 0.12;
    let eyeHeight = mix(eyeRadius, 0.02, blink);  // Flatten when blinking

    let leftEyeP = vec2<f32>(p.x + eyeX, (p.y - eyeY) / (eyeHeight / eyeRadius));
    let rightEyeP = vec2<f32>(p.x - eyeX, (p.y - eyeY) / (eyeHeight / eyeRadius));
    let leftEye = sdCircle(leftEyeP, eyeRadius);
    let rightEye = sdCircle(rightEyeP, eyeRadius);

    // Smile (arc)
    let smileP = vec2<f32>(p.x, p.y + 0.15);
    let smileRadius = 0.5;
    let smile = sdArc(smileP, smileRadius, -2.5, -0.64) - 0.06;

    // Compose
    var color = vec3<f32>(0.0);
    var alpha = 0.0;

    // Face fill
    if (face < 0.0) {
        color = faceColor;
        alpha = 1.0;
    }

    // Face outline
    if (abs(face) < 0.05) {
        color = vec3<f32>(0.8, 0.6, 0.1);
        alpha = 1.0;
    }

    // Eyes (black)
    if (leftEye < 0.0 || rightEye < 0.0) {
        color = vec3<f32>(0.1, 0.1, 0.1);
        alpha = 1.0;
    }

    // Smile (dark)
    if (smile < 0.0 && face < 0.0) {
        color = vec3<f32>(0.6, 0.3, 0.1);
        alpha = 1.0;
    }

    return vec4<f32>(color, alpha);
}
)";
}

} // namespace yetty
