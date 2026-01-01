#include "musical-score.h"
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace yetty {

//-----------------------------------------------------------------------------
// MusicalScorePlugin
//-----------------------------------------------------------------------------

MusicalScorePlugin::MusicalScorePlugin() = default;

MusicalScorePlugin::~MusicalScorePlugin() {
    (void)dispose();
}

Result<PluginPtr> MusicalScorePlugin::create() {
    return Ok<PluginPtr>(std::make_shared<MusicalScorePlugin>());
}

Result<void> MusicalScorePlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    _initialized = true;
    return Ok();
}

Result<void> MusicalScorePlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose MusicalScorePlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> MusicalScorePlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<MusicalScoreLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize MusicalScore layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> MusicalScorePlugin::renderAll(WebGPUContext& ctx,
                                     WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset, uint32_t termRows,
                                     bool isAltScreen) {
    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<MusicalScoreLayer>(layerBase);

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
            return Err<void>("Failed to render MusicalScore layer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// MusicalScoreLayer
//-----------------------------------------------------------------------------

MusicalScoreLayer::MusicalScoreLayer() = default;

MusicalScoreLayer::~MusicalScoreLayer() {
    (void)dispose();
}

Result<void> MusicalScoreLayer::init(const std::string& payload) {
    _payload = payload;

    // Parse payload: "sheetWidth,numStaves"
    if (!payload.empty()) {
        int width = 800, staves = 4;
        if (sscanf(payload.c_str(), "%d,%d", &width, &staves) >= 1) {
            _sheet_width = std::max(100, width);
            if (sscanf(payload.c_str(), "%d,%d", &width, &staves) >= 2) {
                _num_staves = std::clamp(staves, 1, MAX_STAVES);
            }
        }
    }

    spdlog::info("MusicalScoreLayer: initialized ({}px wide, {} staves)",
                 _sheet_width, _num_staves);
    return Ok();
}

Result<void> MusicalScoreLayer::dispose() {
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    _gpu_initialized = false;
    return Ok();
}

Result<void> MusicalScoreLayer::update(double deltaTime) {
    (void)deltaTime;
    return Ok();
}

bool MusicalScoreLayer::onMouseMove(float localX, float localY) {
    (void)localX;
    (void)localY;
    return true;
}

bool MusicalScoreLayer::onMouseButton(int button, bool pressed) {
    (void)button;
    (void)pressed;
    return true;
}

bool MusicalScoreLayer::onKey(int key, int scancode, int action, int mods) {
    (void)key;
    (void)scancode;
    (void)action;
    (void)mods;
    // Consume all keys when focused
    return true;
}

bool MusicalScoreLayer::onChar(unsigned int codepoint) {
    (void)codepoint;
    // Consume all chars when focused
    return true;
}

Result<void> MusicalScoreLayer::render(WebGPUContext& ctx,
                                 WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float pixelX, float pixelY, float pixelW, float pixelH) {
    if (_failed) return Err<void>("MusicalScoreLayer already failed");

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
        return Err<void>("MusicalScoreLayer pipeline not initialized");
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    struct Uniforms {
        float rect[4];        // 16 bytes, offset 0
        float resolution[2];  // 8 bytes, offset 16
        float numStaves;      // 4 bytes, offset 24
        float _pad1;          // 4 bytes, offset 28 (total 32)
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.numStaves = static_cast<float>(_num_staves);
    uniforms._pad1 = 0;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    // Render
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("Failed to create command encoder");

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

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

Result<void> MusicalScoreLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Uniform buffer (32 bytes)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 32;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

    // Shader
    const char* shaderCode = R"(
struct Uniforms {
    rect: vec4<f32>,           // 16 bytes, offset 0
    resolution: vec2<f32>,     // 8 bytes, offset 16
    numStaves: f32,            // 4 bytes, offset 24
    _pad1: f32,                // 4 bytes, offset 28 (total 32)
}

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var p = array<vec2<f32>, 6>(
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    let pos = p[vi];
    var o: VertexOutput;
    o.position = vec4(u.rect.x + pos.x * u.rect.z, u.rect.y - pos.y * u.rect.w, 0.0, 1.0);
    o.uv = pos;
    return o;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let uv = in.uv;
    let fragCoord = uv * u.resolution;

    // Paper background (cream/off-white)
    var color = vec3<f32>(0.98, 0.96, 0.92);

    let numStaves = i32(u.numStaves);
    let linesPerStaff = 5;

    // Calculate staff dimensions
    let margin = 0.08;  // Top/bottom margin as fraction
    let staffSpacing = (1.0 - 2.0 * margin) / f32(numStaves);
    let staffHeight = staffSpacing * 0.5;  // Staff takes 50% of its allocated space
    let lineSpacing = staffHeight / f32(linesPerStaff - 1);

    // Line thickness (in UV space, ~1-2 pixels)
    let lineThickness = 1.5 / u.resolution.y;

    // Draw staff lines
    for (var s = 0; s < numStaves; s = s + 1) {
        let staffTop = margin + f32(s) * staffSpacing + (staffSpacing - staffHeight) * 0.5;

        for (var line = 0; line < linesPerStaff; line = line + 1) {
            let lineY = staffTop + f32(line) * lineSpacing;
            let dist = abs(uv.y - lineY);

            if (dist < lineThickness) {
                // Anti-aliased line
                let alpha = 1.0 - smoothstep(0.0, lineThickness, dist);
                color = mix(color, vec3<f32>(0.1, 0.1, 0.1), alpha);
            }
        }
    }

    // Left margin line (barline at start)
    let leftMargin = 0.03;
    let barlineThickness = 2.0 / u.resolution.x;
    if (abs(uv.x - leftMargin) < barlineThickness) {
        // Only draw within staff regions
        for (var s = 0; s < numStaves; s = s + 1) {
            let staffTop = margin + f32(s) * staffSpacing + (staffSpacing - staffHeight) * 0.5;
            let staffBottom = staffTop + staffHeight;
            if (uv.y >= staffTop && uv.y <= staffBottom) {
                let alpha = 1.0 - smoothstep(0.0, barlineThickness, abs(uv.x - leftMargin));
                color = mix(color, vec3<f32>(0.1, 0.1, 0.1), alpha);
            }
        }
    }

    // Right margin line (barline at end)
    let rightMargin = 0.97;
    if (abs(uv.x - rightMargin) < barlineThickness) {
        for (var s = 0; s < numStaves; s = s + 1) {
            let staffTop = margin + f32(s) * staffSpacing + (staffSpacing - staffHeight) * 0.5;
            let staffBottom = staffTop + staffHeight;
            if (uv.y >= staffTop && uv.y <= staffBottom) {
                let alpha = 1.0 - smoothstep(0.0, barlineThickness, abs(uv.x - rightMargin));
                color = mix(color, vec3<f32>(0.1, 0.1, 0.1), alpha);
            }
        }
    }

    // Subtle border
    let border = 1.0 / min(u.resolution.x, u.resolution.y);
    if (uv.x < border || uv.x > 1.0 - border || uv.y < border || uv.y > 1.0 - border) {
        color = vec3<f32>(0.7, 0.7, 0.7);
    }

    return vec4<f32>(color, 1.0);
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
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &entry;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bgl) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Bind group
    WGPUBindGroupEntry bgE = {};
    bgE.binding = 0;
    bgE.buffer = _uniform_buffer;
    bgE.size = 32;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Render pipeline
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

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) return Err<void>("Failed to create render pipeline");

    spdlog::info("MusicalScoreLayer: pipeline created");
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "musical-score"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::MusicalScorePlugin::create(); }
}
