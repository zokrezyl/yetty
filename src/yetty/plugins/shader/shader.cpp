#include "shader.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/shader-manager.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>

namespace yetty {

//-----------------------------------------------------------------------------
// ShaderPlugin
//-----------------------------------------------------------------------------

ShaderPlugin::~ShaderPlugin() {
    (void)dispose();
}

Result<PluginPtr> ShaderPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new ShaderPlugin(std::move(engine)));
    if (auto res = static_cast<ShaderPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init ShaderPlugin", res);
    }
    return Ok(p);
}

Result<void> ShaderPlugin::init() noexcept {
    // No shared resources - each layer compiles its own shader
    _initialized = true;
    return Ok();
}

Result<void> ShaderPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose ShaderPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> ShaderPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<ShaderLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize Shader layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

//-----------------------------------------------------------------------------
// ShaderLayer
//-----------------------------------------------------------------------------

ShaderLayer::ShaderLayer() = default;

ShaderLayer::~ShaderLayer() {
    (void)dispose();
}

Result<void> ShaderLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("ShaderLayer: empty payload");
    }

    _payload = payload;
    _compiled = false;
    _failed = false;

    spdlog::debug("ShaderLayer: initialized with {} bytes of shader code", payload.size());
    return Ok();
}

Result<void> ShaderLayer::dispose() {
    if (_bind_group) {
        wgpuBindGroupRelease(_bind_group);
        _bind_group = nullptr;
    }
    if (_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(_bindGroupLayout);
        _bindGroupLayout = nullptr;
    }
    if (_pipeline) {
        wgpuRenderPipelineRelease(_pipeline);
        _pipeline = nullptr;
    }
    if (_uniform_buffer) {
        wgpuBufferRelease(_uniform_buffer);
        _uniform_buffer = nullptr;
    }
    _compiled = false;
    return Ok();
}

Result<void> ShaderLayer::render(WebGPUContext& ctx) {
    if (_failed) return Err<void>("ShaderLayer already failed");
    if (!_visible) return Ok();

    // Get render context set by owner
    const auto& rc = _render_context;

    // First time: compile shader
    if (!_compiled) {
        auto result = compileShader(ctx, rc.targetFormat, _payload);
        if (!result) {
            _failed = true;
            return Err<void>("ShaderLayer: Failed to compile shader", result);
        }
        _compiled = true;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("ShaderLayer: pipeline not initialized");
    }

    // Get global bind group from ShaderManager
    auto* parent = getParent();
    if (!parent || !parent->getEngine()) {
        return Err<void>("ShaderLayer: no engine reference");
    }
    auto shaderMgr = parent->getEngine()->shaderManager();
    if (!shaderMgr) {
        return Err<void>("ShaderLayer: no shader manager");
    }
    WGPUBindGroup globalBindGroup = shaderMgr->getGlobalBindGroup();
    if (!globalBindGroup) {
        return Err<void>("ShaderLayer: no global bind group");
    }

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    // For Relative layers, adjust position when viewing scrollback
    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen (not an error)
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();
        }
    }

    // Update per-plugin uniform buffer (group 1)
    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    // Per-plugin uniforms (smaller now - time comes from global)
    struct PluginUniforms {
        float resolution[2];  // Plugin size in pixels
        float param;
        float zoom;
        float rect[4];        // Position in NDC
        float mouse[4];       // Local mouse state
    } uniforms;

    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.param = _param;
    uniforms.zoom = _zoom;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = _mouse_x;
    uniforms.mouse[1] = _mouse_y;
    uniforms.mouse[2] = _mouse_grabbed ? 1.0f : 0.0f;
    uniforms.mouse[3] = _mouse_down ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        return Err<void>("ShaderLayer: Failed to create command encoder");
    }

    // Render pass
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
        return Err<void>("ShaderLayer: Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup, 0, nullptr);  // Global uniforms
    wgpuRenderPassEncoderSetBindGroup(pass, 1, _bind_group, 0, nullptr);       // Per-plugin uniforms
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("ShaderLayer: Failed to finish command encoder");
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

bool ShaderLayer::renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (_failed || !_visible) return false;

    const auto& rc = _render_context;

    // Compile shader on first use
    if (!_compiled) {
        auto result = compileShader(ctx, rc.targetFormat, _payload);
        if (!result) {
            _failed = true;
            return false;
        }
        _compiled = true;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return false;
    }

    // Get global bind group from ShaderManager
    auto* parent = getParent();
    if (!parent || !parent->getEngine()) return false;
    auto shaderMgr = parent->getEngine()->shaderManager();
    if (!shaderMgr) return false;
    WGPUBindGroup globalBindGroup = shaderMgr->getGlobalBindGroup();
    if (!globalBindGroup) return false;

    // Calculate pixel position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return false;
        }
    }

    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    // Update per-plugin uniforms
    struct PluginUniforms {
        float resolution[2];
        float param;
        float zoom;
        float rect[4];
        float mouse[4];
    } uniforms;

    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.param = _param;
    uniforms.zoom = _zoom;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = _mouse_x;
    uniforms.mouse[1] = _mouse_y;
    uniforms.mouse[2] = _mouse_grabbed ? 1.0f : 0.0f;
    uniforms.mouse[3] = _mouse_down ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

    // Draw - no encoder, no pass creation, no submit!
    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, _bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    return true;
}

bool ShaderLayer::onMouseMove(float localX, float localY) {
    _mouse_x = localX / static_cast<float>(_pixel_width);
    _mouse_y = localY / static_cast<float>(_pixel_height);
    spdlog::debug("ShaderLayer::onMouseMove: local=({},{}) normalized=({},{})",
                  localX, localY, _mouse_x, _mouse_y);
    return true;
}

bool ShaderLayer::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        _mouse_down = pressed;
        _mouse_grabbed = pressed;
        spdlog::debug("ShaderLayer::onMouseButton: button={} pressed={} grabbed={}",
                      button, pressed, _mouse_grabbed);
        return true;
    }
    if (button == -1) {
        _mouse_grabbed = false;
        spdlog::debug("ShaderLayer::onMouseButton: focus lost");
        return false;
    }
    return false;
}

bool ShaderLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    bool ctrlPressed = (mods & 0x0002) != 0;

    if (ctrlPressed) {
        _zoom += yoffset * 0.1f;
        _zoom = std::max(0.1f, std::min(5.0f, _zoom));
        spdlog::debug("ShaderLayer::onMouseScroll: CTRL+scroll _zoom={}", _zoom);
    } else {
        _param += yoffset * 0.1f;
        _param = std::max(0.0f, std::min(1.0f, _param));
        spdlog::debug("ShaderLayer::onMouseScroll: scroll _param={}", _param);
    }
    return true;
}

const char* ShaderLayer::getVertexShader() {
    // Uses two bind groups:
    // @group(0) = Global uniforms (time, mouse, screen) - from ShaderManager
    // @group(1) = Per-plugin uniforms (resolution, rect, etc.)
    return R"(
// Global uniforms from ShaderManager (updated once per frame)
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

// Per-plugin uniforms (updated per-plugin per-frame)
struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 1.0)
    );

    let pos = positions[vertexIndex];
    let ndcX = plugin.rect.x + pos.x * plugin.rect.z;
    let ndcY = plugin.rect.y - pos.y * plugin.rect.w;

    var output: VertexOutput;
    output.position = vec4<f32>(ndcX, ndcY, 0.0, 1.0);
    output.uv = pos;
    return output;
}
)";
}

std::string ShaderLayer::wrapFragmentShader(const std::string& userCode) {
    std::ostringstream ss;
    ss << R"(
// Global uniforms from ShaderManager (updated once per frame)
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

// Per-plugin uniforms (updated per-plugin per-frame)
struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;

// ShaderToy-compatible accessors
fn iTime() -> f32 { return global.iTime; }
fn iTimeRelative() -> f32 { return global.iTimeRelative; }
fn iTimeDelta() -> f32 { return global.iTimeDelta; }
fn iFrame() -> u32 { return global.iFrame; }
fn iResolution() -> vec2<f32> { return plugin.resolution; }
fn iScreenResolution() -> vec2<f32> { return global.iScreenResolution; }
fn iMouse() -> vec4<f32> { return plugin.mouse; }  // Local mouse (normalized)
fn iMouseGlobal() -> vec4<f32> { return global.iMouse; }  // Screen mouse
fn iParam() -> f32 { return plugin.param; }
fn iZoom() -> f32 { return plugin.zoom; }
fn iGrabbed() -> bool { return plugin.mouse.z > 0.5; }
fn iMouseDown() -> bool { return plugin.mouse.w > 0.5; }

)" << userCode << R"(

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = uv * plugin.resolution;
    var col = mainImage(fragCoord);

    let border = 3.0;
    let res = plugin.resolution;
    let onBorder = fragCoord.x < border || fragCoord.x > res.x - border ||
                   fragCoord.y < border || fragCoord.y > res.y - border;

    if (onBorder) {
        if (iGrabbed()) {
            col = vec4<f32>(0.2, 0.9, 0.3, 1.0);
        } else {
            col = vec4<f32>(0.4, 0.4, 0.4, 1.0);
        }
    }

    return col;
}
)";
    return ss.str();
}

Result<void> ShaderLayer::compileShader(WebGPUContext& ctx,
                                            WGPUTextureFormat targetFormat,
                                            const std::string& fragmentCode) {
    // Get global bind group layout from ShaderManager
    auto* parent = getParent();
    if (!parent || !parent->getEngine()) {
        return Err<void>("ShaderLayer: no engine reference for shader compilation");
    }
    auto shaderMgr = parent->getEngine()->shaderManager();
    if (!shaderMgr) {
        return Err<void>("ShaderLayer: no shader manager for shader compilation");
    }
    WGPUBindGroupLayout globalBGL = shaderMgr->getGlobalBindGroupLayout();
    if (!globalBGL) {
        return Err<void>("ShaderLayer: no global bind group layout");
    }

    // Create per-plugin uniform buffer (smaller now - no time)
    // PluginUniforms: resolution(2) + param + zoom + rect(4) + mouse(4) = 12 floats = 48 bytes
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(ctx.getDevice(), &bufDesc);
    if (!_uniform_buffer) {
        return Err<void>("Failed to create uniform buffer");
    }

    // Compile vertex shader
    std::string vertCode = getVertexShader();
    WGPUShaderSourceWGSL wgslDescVert = {};
    wgslDescVert.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDescVert.code = { .data = vertCode.c_str(), .length = vertCode.size() };

    WGPUShaderModuleDescriptor shaderDescVert = {};
    shaderDescVert.nextInChain = &wgslDescVert.chain;
    WGPUShaderModule vertModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDescVert);

    // Compile fragment shader
    std::string fragCode = wrapFragmentShader(fragmentCode);
    spdlog::debug("ShaderLayer: compiling fragment shader");

    WGPUShaderSourceWGSL wgslDescFrag = {};
    wgslDescFrag.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDescFrag.code = { .data = fragCode.c_str(), .length = fragCode.size() };

    WGPUShaderModuleDescriptor shaderDescFrag = {};
    shaderDescFrag.nextInChain = &wgslDescFrag.chain;
    WGPUShaderModule fragModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDescFrag);

    if (!vertModule || !fragModule) {
        if (vertModule) wgpuShaderModuleRelease(vertModule);
        if (fragModule) wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create shader modules");
    }

    // Create per-plugin bind group layout (group 1)
    WGPUBindGroupLayoutEntry bindingEntry = {};
    bindingEntry.binding = 0;
    bindingEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bindingEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bindingEntry;
    _bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
    if (!_bindGroupLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create per-plugin bind group layout");
    }

    // Pipeline layout with TWO bind group layouts: [global, per-plugin]
    WGPUBindGroupLayout layouts[2] = { globalBGL, _bindGroupLayout };
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.getDevice(), &plDesc);
    if (!pipelineLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create pipeline layout");
    }

    // Create per-plugin bind group (group 1)
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = _uniform_buffer;
    bgEntry.size = 48;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    _bind_group = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    if (!_bind_group) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        wgpuPipelineLayoutRelease(pipelineLayout);
        return Err<void>("Failed to create per-plugin bind group");
    }

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = vertModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUFragmentState fragState = {};
    fragState.module = fragModule;
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
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(ctx.getDevice(), &pipelineDesc);

    wgpuShaderModuleRelease(vertModule);
    wgpuShaderModuleRelease(fragModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) {
        return Err<void>("Failed to create render pipeline");
    }

    spdlog::debug("ShaderLayer: pipeline created with global + per-plugin uniforms");
    return Ok();
}

} // namespace yetty

// C exports for dynamic loading
extern "C" {
    const char* name() { return "shader"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::ShaderPlugin::create(std::move(engine));
    }
}
