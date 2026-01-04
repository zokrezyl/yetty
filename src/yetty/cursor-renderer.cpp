#include <yetty/cursor-renderer.h>
#include <yetty/shader-manager.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>

namespace yetty {

CursorRenderer::CursorRenderer(uint32_t id)
    : _id(id)
    , _name("cursor-" + std::to_string(id))
{
}

CursorRenderer::~CursorRenderer() {
    dispose();
}

Result<void> CursorRenderer::init(WebGPUContext* ctx, ShaderManager* shaderMgr,
                                   WGPUTextureFormat targetFormat) {
    if (!ctx || !shaderMgr) {
        return Err<void>("CursorRenderer::init: null context or shader manager");
    }

    _ctx = ctx;
    _shaderManager = shaderMgr;

    if (auto res = createPipeline(targetFormat); !res) {
        _failed = true;
        return Err<void>("CursorRenderer: failed to create pipeline", res);
    }

    _initialized = true;
    spdlog::info("CursorRenderer: initialized");
    return Ok();
}

Result<void> CursorRenderer::dispose() {
    if (_bindGroup) {
        wgpuBindGroupRelease(_bindGroup);
        _bindGroup = nullptr;
    }
    if (_uniformBuffer) {
        wgpuBufferRelease(_uniformBuffer);
        _uniformBuffer = nullptr;
    }
    if (_pipeline) {
        wgpuRenderPipelineRelease(_pipeline);
        _pipeline = nullptr;
    }
    if (_pipelineLayout) {
        wgpuPipelineLayoutRelease(_pipelineLayout);
        _pipelineLayout = nullptr;
    }
    if (_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(_bindGroupLayout);
        _bindGroupLayout = nullptr;
    }

    _initialized = false;
    return Ok();
}

void CursorRenderer::setCursorPos(int col, int row) {
    _cursorCol = col;
    _cursorRow = row;
}

void CursorRenderer::setCellSize(float width, float height) {
    _cellWidth = width;
    _cellHeight = height;
}

void CursorRenderer::setScreenSize(uint32_t width, uint32_t height) {
    _screenWidth = width;
    _screenHeight = height;
}

Result<void> CursorRenderer::createPipeline(WGPUTextureFormat targetFormat) {
    WGPUDevice device = _ctx->getDevice();

    // Get shaders from ShaderManager
    WGPUShaderModule vertModule = _shaderManager->getQuadVertexShader();
    if (!vertModule) {
        return Err<void>("CursorRenderer: no quad vertex shader");
    }

    auto fragResult = _shaderManager->getFragmentShader("cursor.wgsl");
    if (!fragResult) {
        return Err<void>("CursorRenderer: failed to load cursor.wgsl", fragResult);
    }
    WGPUShaderModule fragModule = *fragResult;

    // Create uniform buffer (matches QuadUniforms in shader-manager.cpp)
    struct QuadUniforms {
        float iTime;
        float iTimeDelta;
        uint32_t iFrame;
        uint32_t _pad0;
        float iResolution[2];
        float iMouse[2];
        uint32_t iParam0;
        uint32_t _pad1a;
        uint32_t _pad1b;
        uint32_t _pad1c;
        float rect[4];
    };

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(QuadUniforms);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniformBuffer) {
        return Err<void>("CursorRenderer: failed to create uniform buffer");
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry bindingEntry = {};
    bindingEntry.binding = 0;
    bindingEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    bindingEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &bindingEntry;
    _bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!_bindGroupLayout) {
        return Err<void>("CursorRenderer: failed to create bind group layout");
    }

    // Bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = _uniformBuffer;
    bgEntry.offset = 0;
    bgEntry.size = sizeof(QuadUniforms);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _bindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    _bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (!_bindGroup) {
        return Err<void>("CursorRenderer: failed to create bind group");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &_bindGroupLayout;
    _pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);
    if (!_pipelineLayout) {
        return Err<void>("CursorRenderer: failed to create pipeline layout");
    }

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = _pipelineLayout;

    // Vertex state
    pipelineDesc.vertex.module = vertModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    // Fragment state with alpha blending
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
    fragState.entryPoint = WGPU_STR("fs_entry");  // Uses wrapper from ShaderManager
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    if (!_pipeline) {
        return Err<void>("CursorRenderer: failed to create render pipeline");
    }

    spdlog::debug("CursorRenderer: pipeline created successfully");
    return Ok();
}

Result<void> CursorRenderer::render(WebGPUContext& ctx) {
    if (_failed || !_initialized) {
        return Ok();  // Silently skip if failed or not initialized
    }

    if (!_visible) {
        return Ok();  // Cursor hidden
    }

    // Get current texture view from context (same view Terminal rendered to)
    auto viewResult = ctx.getCurrentTextureView();
    if (!viewResult) {
        return Ok();  // No view available yet
    }
    _targetView = *viewResult;

    // Calculate cursor position in pixels
    float pixelX = _cursorCol * _cellWidth;
    float pixelY = _cursorRow * _cellHeight;
    float pixelW = _cellWidth;
    float pixelH = _cellHeight;

    // Convert to NDC
    float ndcX = (pixelX / static_cast<float>(_screenWidth)) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / static_cast<float>(_screenHeight)) * 2.0f;
    float ndcW = (pixelW / static_cast<float>(_screenWidth)) * 2.0f;
    float ndcH = (pixelH / static_cast<float>(_screenHeight)) * 2.0f;

    // Update uniform buffer
    struct QuadUniforms {
        float iTime;
        float iTimeDelta;
        uint32_t iFrame;
        uint32_t _pad0;
        float iResolution[2];
        float iMouse[2];
        uint32_t iParam0;
        uint32_t _pad1a;
        uint32_t _pad1b;
        uint32_t _pad1c;
        float rect[4];
    } uniforms;

    uniforms.iTime = _time;
    uniforms.iTimeDelta = 0.016f;  // ~60fps
    uniforms.iFrame = 0;
    uniforms._pad0 = 0;
    uniforms.iResolution[0] = pixelW;
    uniforms.iResolution[1] = pixelH;
    uniforms.iMouse[0] = 0.0f;
    uniforms.iMouse[1] = 0.0f;
    uniforms.iParam0 = static_cast<uint32_t>(_cursorStyle);
    uniforms._pad1a = 0;
    uniforms._pad1b = 0;
    uniforms._pad1c = 0;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        return Err<void>("CursorRenderer: failed to create command encoder");
    }

    // Render pass - Load existing content
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = _targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("CursorRenderer: failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);  // 6 vertices for quad
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("CursorRenderer: failed to finish command encoder");
    }

    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    return Ok();
}

} // namespace yetty
