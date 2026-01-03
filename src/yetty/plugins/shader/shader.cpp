#include "shader.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
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
    _time = 0.0f;

    spdlog::debug("ShaderLayer: initialized with {} bytes of shader code", payload.size());
    return Ok();
}

Result<void> ShaderLayer::dispose() {
    if (_bind_group) {
        wgpuBindGroupRelease(_bind_group);
        _bind_group = nullptr;
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

    // Update time from deltaTime
    _time += static_cast<float>(rc.deltaTime);

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

    // Update uniform buffer
    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    struct Uniforms {
        float time;
        float param;
        float zoom;
        float _pad1;
        float resolution[2];
        float _pad2[2];
        float rect[4];
        float mouse[4];
    } uniforms;

    uniforms.time = _time;
    uniforms.param = _param;
    uniforms.zoom = _zoom;
    uniforms._pad1 = 0.0f;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms._pad2[0] = 0.0f;
    uniforms._pad2[1] = 0.0f;
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
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
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
    return R"(
struct Uniforms {
    time: f32,
    param: f32,
    zoom: f32,
    _pad1: f32,
    resolution: vec2<f32>,
    _pad2: vec2<f32>,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

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
    let ndcX = u.rect.x + pos.x * u.rect.z;
    let ndcY = u.rect.y - pos.y * u.rect.w;

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
struct Uniforms {
    time: f32,
    param: f32,
    zoom: f32,
    _pad1: f32,
    resolution: vec2<f32>,
    _pad2: vec2<f32>,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

fn iTime() -> f32 { return u.time; }
fn iResolution() -> vec2<f32> { return u.resolution; }
fn iMouse() -> vec4<f32> { return u.mouse; }
fn iParam() -> f32 { return u.param; }
fn iZoom() -> f32 { return u.zoom; }
fn iGrabbed() -> bool { return u.mouse.z > 0.5; }
fn iMouseDown() -> bool { return u.mouse.w > 0.5; }

)" << userCode << R"(

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = uv * u.resolution;
    var col = mainImage(fragCoord);

    let border = 3.0;
    let res = u.resolution;
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
    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 64;
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

    // Bind group layout
    WGPUBindGroupLayoutEntry bindingEntry = {};
    bindingEntry.binding = 0;
    bindingEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bindingEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bindingEntry;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
    if (!bgl) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.getDevice(), &plDesc);
    if (!pipelineLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        wgpuBindGroupLayoutRelease(bgl);
        return Err<void>("Failed to create pipeline layout");
    }

    // Bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = _uniform_buffer;
    bgEntry.size = 64;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    _bind_group = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    if (!_bind_group) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        wgpuBindGroupLayoutRelease(bgl);
        wgpuPipelineLayoutRelease(pipelineLayout);
        return Err<void>("Failed to create bind group");
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
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) {
        return Err<void>("Failed to create render pipeline");
    }

    spdlog::debug("ShaderLayer: pipeline created successfully");
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
