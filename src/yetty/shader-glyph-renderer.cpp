#include <yetty/shader-glyph-renderer.h>
#include <yetty/shader-manager.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>

namespace yetty {

// Uniform buffer layout (must match shader)
struct alignas(16) ShaderGlyphUniforms {
    float iTime;           // offset 0
    float iTimeDelta;      // offset 4
    uint32_t iFrame;       // offset 8
    uint32_t _pad0;        // offset 12
    float iResolution[2];  // offset 16
    float iMouse[2];       // offset 24
    uint32_t iParam0;      // offset 32
    uint32_t _pad1a;       // offset 36
    uint32_t _pad1b;       // offset 40
    uint32_t _pad1c;       // offset 44
    float rect[4];         // offset 48 (NDC position)
};
static_assert(sizeof(ShaderGlyphUniforms) == 64, "ShaderGlyphUniforms must be 64 bytes");

ShaderGlyphRenderer::ShaderGlyphRenderer(uint32_t id, const std::string& shaderName)
    : _id(id)
    , _shaderName(shaderName)
    , _name("ShaderGlyph:" + shaderName) {
}

ShaderGlyphRenderer::~ShaderGlyphRenderer() {
    (void)dispose();
}

Result<void> ShaderGlyphRenderer::init(WebGPUContext* ctx, ShaderManager* shaderMgr,
                                        WGPUTextureFormat targetFormat) {
    if (!ctx || !shaderMgr) {
        return Err<void>("ShaderGlyphRenderer::init: null context or shader manager");
    }

    _ctx = ctx;
    _shaderManager = shaderMgr;

    auto result = createPipeline(targetFormat);
    if (!result) {
        _failed = true;
        return Err<void>("ShaderGlyphRenderer: failed to create pipeline", result);
    }

    _initialized = true;
    yinfo("ShaderGlyphRenderer: initialized for shader {}", _shaderName);
    return Ok();
}

Result<void> ShaderGlyphRenderer::dispose() {
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

    _instances.clear();
    _initialized = false;
    _failed = false;
    return Ok();
}

Result<void> ShaderGlyphRenderer::createPipeline(WGPUTextureFormat targetFormat) {
    WGPUDevice device = _ctx->getDevice();

    // Get shared vertex shader from ShaderManager
    WGPUShaderModule vertModule = _shaderManager->getQuadVertexShader();
    if (!vertModule) {
        return Err<void>("ShaderGlyphRenderer: no quad vertex shader");
    }

    // Get fragment shader from ShaderManager
    auto fragResult = _shaderManager->getFragmentShader(_shaderName);
    if (!fragResult) {
        return Err<void>("ShaderGlyphRenderer: failed to get fragment shader", fragResult);
    }
    WGPUShaderModule fragModule = *fragResult;

    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(ShaderGlyphUniforms);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniformBuffer) {
        return Err<void>("Failed to create uniform buffer");
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.minBindingSize = sizeof(ShaderGlyphUniforms);

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
    bgEntry.size = sizeof(ShaderGlyphUniforms);

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

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = _pipelineLayout;
    pipelineDesc.vertex.module = vertModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

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
    fragState.entryPoint = WGPU_STR("fs_entry");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    if (!_pipeline) {
        return Err<void>("Failed to create render pipeline");
    }

    ydebug("ShaderGlyphRenderer: created pipeline for {}", _shaderName);
    return Ok();
}

void ShaderGlyphRenderer::setInstances(const std::vector<ShaderGlyphInstance>& instances) {
    _instances = instances;
}

void ShaderGlyphRenderer::clearInstances() {
    _instances.clear();
}

void ShaderGlyphRenderer::prepareFrame(WebGPUContext& ctx) {
    if (_failed || !_initialized || _instances.empty()) {
        return;
    }

    const auto& rc = _renderContext;
    if (!rc.targetView) {
        yerror("ShaderGlyphRenderer: no target view");
        return;
    }

    // Render each instance
    // TODO: Future optimization - batch into instanced draw
    for (const auto& inst : _instances) {
        // Convert to NDC
        float ndcX = (inst.x / rc.screenWidth) * 2.0f - 1.0f;
        float ndcY = 1.0f - (inst.y / rc.screenHeight) * 2.0f;
        float ndcW = (inst.w / rc.screenWidth) * 2.0f;
        float ndcH = (inst.h / rc.screenHeight) * 2.0f;

        // Update uniforms
        ShaderGlyphUniforms uniforms = {};
        uniforms.iTime = inst.time;
        uniforms.iTimeDelta = 0.016f;
        uniforms.iFrame = static_cast<uint32_t>(inst.time * 60);
        uniforms.iResolution[0] = inst.w;
        uniforms.iResolution[1] = inst.h;
        uniforms.iMouse[0] = -1.0f;
        uniforms.iMouse[1] = -1.0f;
        uniforms.iParam0 = 0;
        uniforms.rect[0] = ndcX;
        uniforms.rect[1] = ndcY;
        uniforms.rect[2] = ndcW;
        uniforms.rect[3] = ndcH;

        wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

        // Create command encoder
        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encDesc);
        if (!encoder) {
            yerror("ShaderGlyphRenderer: failed to create command encoder");
            return;
        }

        // Render pass
        WGPURenderPassColorAttachment colorAtt = {};
        colorAtt.view = rc.targetView;
        colorAtt.loadOp = WGPULoadOp_Load;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAtt;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        if (!pass) {
            wgpuCommandEncoderRelease(encoder);
            yerror("ShaderGlyphRenderer: failed to begin render pass");
            return;
        }

        wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, _bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        if (!cmdBuf) {
            wgpuCommandEncoderRelease(encoder);
            yerror("ShaderGlyphRenderer: failed to finish command encoder");
            return;
        }
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuf);
        wgpuCommandBufferRelease(cmdBuf);
        wgpuCommandEncoderRelease(encoder);
    }
}

Result<void> ShaderGlyphRenderer::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    (void)pass;
    (void)ctx;
    // Rendering is done in prepareFrame() with its own pass
    return Ok();
}

} // namespace yetty
