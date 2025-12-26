#include "shader-toy.h"
#include "../renderer/webgpu-context.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <sstream>

namespace yetty {

//-----------------------------------------------------------------------------
// ShaderToyPlugin
//-----------------------------------------------------------------------------

ShaderToyPlugin::ShaderToyPlugin() = default;

ShaderToyPlugin::~ShaderToyPlugin() {
    dispose();
}

Result<PluginPtr> ShaderToyPlugin::create() {
    return Ok<PluginPtr>(std::make_shared<ShaderToyPlugin>());
}

Result<void> ShaderToyPlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    // No shared resources for ShaderToy - each layer compiles its own shader
    initialized_ = true;
    return Ok();
}

void ShaderToyPlugin::dispose() {
    Plugin::dispose();
    initialized_ = false;
}

Result<PluginLayerPtr> ShaderToyPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<ShaderToyLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize ShaderToy layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

void ShaderToyPlugin::renderAll(WebGPUContext& ctx,
                                 WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float cellWidth, float cellHeight,
                                 int scrollOffset, uint32_t termRows) {
    for (auto& layerBase : layers_) {
        if (!layerBase->isVisible()) continue;

        auto layer = std::static_pointer_cast<ShaderToyLayer>(layerBase);

        // Calculate pixel position
        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        // For Relative layers, adjust position when viewing scrollback
        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        // Skip if off-screen
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
// ShaderToyLayer
//-----------------------------------------------------------------------------

ShaderToyLayer::ShaderToyLayer() = default;

ShaderToyLayer::~ShaderToyLayer() {
    dispose();
}

Result<void> ShaderToyLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("ShaderToyLayer: empty payload");
    }

    payload_ = payload;
    compiled_ = false;
    failed_ = false;
    time_ = 0.0f;

    std::cout << "ShaderToyLayer: initialized with " << payload.size() << " bytes of shader code" << std::endl;
    return Ok();
}

void ShaderToyLayer::dispose() {
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (uniformBuffer_) {
        wgpuBufferRelease(uniformBuffer_);
        uniformBuffer_ = nullptr;
    }
    compiled_ = false;
}

void ShaderToyLayer::update(double deltaTime) {
    time_ += static_cast<float>(deltaTime);
}

void ShaderToyLayer::render(WebGPUContext& ctx,
                             WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                             uint32_t screenWidth, uint32_t screenHeight,
                             float pixelX, float pixelY, float pixelW, float pixelH) {
    if (failed_) return;

    // First time: compile shader
    if (!compiled_) {
        auto result = compileShader(ctx, targetFormat, payload_);
        if (!result) {
            std::cerr << "ShaderToyLayer: " << error_msg(result) << std::endl;
            failed_ = true;
            return;
        }
        compiled_ = true;
    }

    if (!pipeline_ || !uniformBuffer_ || !bindGroup_) {
        failed_ = true;
        return;
    }

    // Update uniform buffer
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

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

    uniforms.time = time_;
    uniforms.param = param_;
    uniforms.zoom = zoom_;
    uniforms._pad1 = 0.0f;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms._pad2[0] = 0.0f;
    uniforms._pad2[1] = 0.0f;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = mouseX_;
    uniforms.mouse[1] = mouseY_;
    uniforms.mouse[2] = mouseGrabbed_ ? 1.0f : 0.0f;
    uniforms.mouse[3] = mouseDown_ ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        std::cerr << "ShaderToyLayer: failed to create command encoder" << std::endl;
        return;
    }

    // Render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        std::cerr << "ShaderToyLayer: failed to begin render pass" << std::endl;
        wgpuCommandEncoderRelease(encoder);
        return;
    }

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

bool ShaderToyLayer::onMouseMove(float localX, float localY) {
    mouseX_ = localX / static_cast<float>(pixelWidth_);
    mouseY_ = localY / static_cast<float>(pixelHeight_);
    spdlog::debug("ShaderToyLayer::onMouseMove: local=({},{}) normalized=({},{})",
                  localX, localY, mouseX_, mouseY_);
    return true;
}

bool ShaderToyLayer::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        mouseDown_ = pressed;
        mouseGrabbed_ = pressed;
        spdlog::debug("ShaderToyLayer::onMouseButton: button={} pressed={} grabbed={}",
                      button, pressed, mouseGrabbed_);
        return true;
    }
    if (button == -1) {
        mouseGrabbed_ = false;
        spdlog::debug("ShaderToyLayer::onMouseButton: focus lost");
        return false;
    }
    return false;
}

bool ShaderToyLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    bool ctrlPressed = (mods & 0x0002) != 0;

    if (ctrlPressed) {
        zoom_ += yoffset * 0.1f;
        zoom_ = std::max(0.1f, std::min(5.0f, zoom_));
        spdlog::debug("ShaderToyLayer::onMouseScroll: CTRL+scroll zoom_={}", zoom_);
    } else {
        param_ += yoffset * 0.1f;
        param_ = std::max(0.0f, std::min(1.0f, param_));
        spdlog::debug("ShaderToyLayer::onMouseScroll: scroll param_={}", param_);
    }
    return true;
}

const char* ShaderToyLayer::getVertexShader() {
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

std::string ShaderToyLayer::wrapFragmentShader(const std::string& userCode) {
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

Result<void> ShaderToyLayer::compileShader(WebGPUContext& ctx,
                                            WGPUTextureFormat targetFormat,
                                            const std::string& fragmentCode) {
    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 64;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(ctx.getDevice(), &bufDesc);
    if (!uniformBuffer_) {
        return Err<void>("Failed to create uniform buffer");
    }

    // Compile vertex shader
    std::string vertCode = getVertexShader();
    WGPUShaderModuleWGSLDescriptor wgslDescVert = {};
    wgslDescVert.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDescVert.code = vertCode.c_str();

    WGPUShaderModuleDescriptor shaderDescVert = {};
    shaderDescVert.nextInChain = &wgslDescVert.chain;
    WGPUShaderModule vertModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDescVert);

    // Compile fragment shader
    std::string fragCode = wrapFragmentShader(fragmentCode);
    std::cout << "ShaderToyLayer: compiling fragment shader" << std::endl;

    WGPUShaderModuleWGSLDescriptor wgslDescFrag = {};
    wgslDescFrag.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
    wgslDescFrag.code = fragCode.c_str();

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
    bgEntry.buffer = uniformBuffer_;
    bgEntry.size = 64;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    bindGroup_ = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    if (!bindGroup_) {
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
    pipelineDesc.vertex.entryPoint = "vs_main";

    WGPUFragmentState fragState = {};
    fragState.module = fragModule;
    fragState.entryPoint = "fs_main";

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
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) {
        return Err<void>("Failed to create render pipeline");
    }

    std::cout << "ShaderToyLayer: pipeline created successfully" << std::endl;
    return Ok();
}

} // namespace yetty

// C exports
extern "C" {
    const char* shader_plugin_name() { return "shader"; }
    yetty::Result<yetty::PluginPtr> shader_plugin_create() { return yetty::ShaderToyPlugin::create(); }
}
