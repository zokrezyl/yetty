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
    if (auto res = static_cast<ShaderPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init ShaderPlugin", res);
    }
    return Ok(p);
}

Result<void> ShaderPlugin::pluginInit() noexcept {
    initialized_ = true;
    return Ok();
}

Result<void> ShaderPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose ShaderPlugin", res);
    }
    initialized_ = false;
    return Ok();
}

Result<WidgetPtr> ShaderPlugin::createWidget(const std::string& payload) {
    return Shader::create(payload);
}

//-----------------------------------------------------------------------------
// Shader
//-----------------------------------------------------------------------------

Shader::~Shader() {
    (void)dispose();
}

Result<void> Shader::init() {
    if (payload_.empty()) {
        return Err<void>("Shader: empty payload");
    }
    compiled_ = false;
    failed_ = false;
    spdlog::debug("Shader: initialized with {} bytes of shader code", payload_.size());
    return Ok();
}

Result<void> Shader::dispose() {
    if (bind_group_) {
        wgpuBindGroupRelease(bind_group_);
        bind_group_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (uniform_buffer_) {
        wgpuBufferRelease(uniform_buffer_);
        uniform_buffer_ = nullptr;
    }
    compiled_ = false;
    return Ok();
}

Result<void> Shader::render(WebGPUContext& ctx) {
    if (failed_) return Err<void>("Shader already failed");
    if (!visible_) return Ok();

    const auto& rc = renderCtx_;

    if (!compiled_) {
        auto result = compileShader(ctx, rc.targetFormat, payload_);
        if (!result) {
            failed_ = true;
            return Err<void>("Shader: Failed to compile shader", result);
        }
        compiled_ = true;
    }

    if (!pipeline_ || !uniform_buffer_ || !bind_group_) {
        failed_ = true;
        return Err<void>("Shader: pipeline not initialized");
    }

    auto* parent = getParent();
    if (!parent || !parent->getEngine()) {
        return Err<void>("Shader: no engine reference");
    }
    auto shaderMgr = parent->getEngine()->shaderManager();
    if (!shaderMgr) {
        return Err<void>("Shader: no shader manager");
    }
    WGPUBindGroup globalBindGroup = shaderMgr->getGlobalBindGroup();
    if (!globalBindGroup) {
        return Err<void>("Shader: no global bind group");
    }

    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

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

    struct PluginUniforms {
        float resolution[2];
        float param;
        float zoom;
        float rect[4];
        float mouse[4];
    } uniforms;

    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.param = param_;
    uniforms.zoom = zoom_;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = mouse_x_;
    uniforms.mouse[1] = mouse_y_;
    uniforms.mouse[2] = mouse_grabbed_ ? 1.0f : 0.0f;
    uniforms.mouse[3] = mouse_down_ ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), uniform_buffer_, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        return Err<void>("Shader: Failed to create command encoder");
    }

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
        return Err<void>("Shader: Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, bind_group_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Shader: Failed to finish command encoder");
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

bool Shader::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (failed_ || !visible_) return false;

    const auto& rc = renderCtx_;

    if (!compiled_) {
        auto result = compileShader(ctx, rc.targetFormat, payload_);
        if (!result) {
            failed_ = true;
            return false;
        }
        compiled_ = true;
    }

    if (!pipeline_ || !uniform_buffer_ || !bind_group_) {
        failed_ = true;
        return false;
    }

    auto* parent = getParent();
    if (!parent || !parent->getEngine()) return false;
    auto shaderMgr = parent->getEngine()->shaderManager();
    if (!shaderMgr) return false;
    WGPUBindGroup globalBindGroup = shaderMgr->getGlobalBindGroup();
    if (!globalBindGroup) return false;

    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

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

    struct PluginUniforms {
        float resolution[2];
        float param;
        float zoom;
        float rect[4];
        float mouse[4];
    } uniforms;

    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.param = param_;
    uniforms.zoom = zoom_;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = mouse_x_;
    uniforms.mouse[1] = mouse_y_;
    uniforms.mouse[2] = mouse_grabbed_ ? 1.0f : 0.0f;
    uniforms.mouse[3] = mouse_down_ ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), uniform_buffer_, 0, &uniforms, sizeof(uniforms));

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, bind_group_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    return true;
}

bool Shader::onMouseMove(float localX, float localY) {
    mouse_x_ = localX / static_cast<float>(pixelWidth_);
    mouse_y_ = localY / static_cast<float>(pixelHeight_);
    spdlog::debug("Shader::onMouseMove: local=({},{}) normalized=({},{})",
                  localX, localY, mouse_x_, mouse_y_);
    return true;
}

bool Shader::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        mouse_down_ = pressed;
        mouse_grabbed_ = pressed;
        spdlog::debug("Shader::onMouseButton: button={} pressed={} grabbed={}",
                      button, pressed, mouse_grabbed_);
        return true;
    }
    if (button == -1) {
        mouse_grabbed_ = false;
        spdlog::debug("Shader::onMouseButton: focus lost");
        return false;
    }
    return false;
}

bool Shader::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    bool ctrlPressed = (mods & 0x0002) != 0;

    if (ctrlPressed) {
        zoom_ += yoffset * 0.1f;
        zoom_ = std::max(0.1f, std::min(5.0f, zoom_));
        spdlog::debug("Shader::onMouseScroll: CTRL+scroll zoom_={}", zoom_);
    } else {
        param_ += yoffset * 0.1f;
        param_ = std::max(0.0f, std::min(1.0f, param_));
        spdlog::debug("Shader::onMouseScroll: scroll param_={}", param_);
    }
    return true;
}

const char* Shader::getVertexShader() {
    return R"(
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

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

std::string Shader::wrapFragmentShader(const std::string& userCode) {
    std::ostringstream ss;
    ss << R"(
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;

fn iTime() -> f32 { return global.iTime; }
fn iTimeRelative() -> f32 { return global.iTimeRelative; }
fn iTimeDelta() -> f32 { return global.iTimeDelta; }
fn iFrame() -> u32 { return global.iFrame; }
fn iResolution() -> vec2<f32> { return plugin.resolution; }
fn iScreenResolution() -> vec2<f32> { return global.iScreenResolution; }
fn iMouse() -> vec4<f32> { return plugin.mouse; }
fn iMouseGlobal() -> vec4<f32> { return global.iMouse; }
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

Result<void> Shader::compileShader(WebGPUContext& ctx,
                                        WGPUTextureFormat targetFormat,
                                        const std::string& fragmentCode) {
    auto* parent = getParent();
    if (!parent || !parent->getEngine()) {
        return Err<void>("Shader: no engine reference for shader compilation");
    }
    auto shaderMgr = parent->getEngine()->shaderManager();
    if (!shaderMgr) {
        return Err<void>("Shader: no shader manager for shader compilation");
    }
    WGPUBindGroupLayout globalBGL = shaderMgr->getGlobalBindGroupLayout();
    if (!globalBGL) {
        return Err<void>("Shader: no global bind group layout");
    }

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniform_buffer_ = wgpuDeviceCreateBuffer(ctx.getDevice(), &bufDesc);
    if (!uniform_buffer_) {
        return Err<void>("Failed to create uniform buffer");
    }

    std::string vertCode = getVertexShader();
    WGPUShaderSourceWGSL wgslDescVert = {};
    wgslDescVert.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDescVert.code = { .data = vertCode.c_str(), .length = vertCode.size() };

    WGPUShaderModuleDescriptor shaderDescVert = {};
    shaderDescVert.nextInChain = &wgslDescVert.chain;
    WGPUShaderModule vertModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDescVert);

    std::string fragCode = wrapFragmentShader(fragmentCode);
    spdlog::debug("Shader: compiling fragment shader");

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

    WGPUBindGroupLayoutEntry bindingEntry = {};
    bindingEntry.binding = 0;
    bindingEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bindingEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bindingEntry;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
    if (!bindGroupLayout_) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create per-plugin bind group layout");
    }

    WGPUBindGroupLayout layouts[2] = { globalBGL, bindGroupLayout_ };
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.getDevice(), &plDesc);
    if (!pipelineLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create pipeline layout");
    }

    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = uniform_buffer_;
    bgEntry.size = 48;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout_;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    bind_group_ = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    if (!bind_group_) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        wgpuPipelineLayoutRelease(pipelineLayout);
        return Err<void>("Failed to create per-plugin bind group");
    }

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

    pipeline_ = wgpuDeviceCreateRenderPipeline(ctx.getDevice(), &pipelineDesc);

    wgpuShaderModuleRelease(vertModule);
    wgpuShaderModuleRelease(fragModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) {
        return Err<void>("Failed to create render pipeline");
    }

    spdlog::debug("Shader: pipeline created with global + per-plugin uniforms");
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "shader"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::ShaderPlugin::create(std::move(engine));
    }
}
