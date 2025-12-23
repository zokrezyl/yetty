#include "TextRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace yetty {

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer() {
    if (bgIndexBuffer_) wgpuBufferRelease(bgIndexBuffer_);
    if (bgVertexBuffer_) wgpuBufferRelease(bgVertexBuffer_);
    if (glyphIndexBuffer_) wgpuBufferRelease(glyphIndexBuffer_);
    if (glyphVertexBuffer_) wgpuBufferRelease(glyphVertexBuffer_);
    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (bindGroupLayout_) wgpuBindGroupLayoutRelease(bindGroupLayout_);
    if (bgPipeline_) wgpuRenderPipelineRelease(bgPipeline_);
    if (glyphPipeline_) wgpuRenderPipelineRelease(glyphPipeline_);
    if (shaderModule_) wgpuShaderModuleRelease(shaderModule_);
}

bool TextRenderer::init(WebGPUContext& ctx, Font& font) {
    font_ = &font;

    if (!createShaderModule(ctx.getDevice())) return false;
    if (!createBuffers(ctx.getDevice())) return false;
    if (!createBindGroup(ctx.getDevice(), font)) return false;
    if (!createPipelines(ctx.getDevice(), ctx.getSurfaceFormat())) return false;

    return true;
}

bool TextRenderer::createShaderModule(WGPUDevice device) {
    // Load shader source - different paths for native vs web
#if YETTY_WEB
    const char* shaderPath = "/shaders.wgsl";  // Emscripten virtual filesystem
#else
    const char* shaderPath = CMAKE_SOURCE_DIR "/src/renderer/shaders.wgsl";
#endif

    std::ifstream file(shaderPath);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << shaderPath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string shaderSource = buffer.str();

    WGPUShaderModuleWGSLDescriptor wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderModuleWGSLDescriptor;
#if YETTY_WEB
    wgslDesc.source = shaderSource.c_str();  // Emscripten uses 'source'
#else
    wgslDesc.code = shaderSource.c_str();    // wgpu-native uses 'code'
#endif

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    moduleDesc.label = "text shader";

    shaderModule_ = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    if (!shaderModule_) {
        std::cerr << "Failed to create shader module" << std::endl;
        return false;
    }

    return true;
}

bool TextRenderer::createBuffers(WGPUDevice device) {
    maxCells_ = 256 * 64;  // Support up to 256x64 grid

    // Uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.label = "uniforms";
    uniformDesc.size = sizeof(Uniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &uniformDesc);

    // Glyph vertex buffer (4 vertices per glyph)
    WGPUBufferDescriptor glyphVertexDesc = {};
    glyphVertexDesc.label = "glyph vertices";
    glyphVertexDesc.size = maxCells_ * 4 * sizeof(GlyphVertex);
    glyphVertexDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    glyphVertexBuffer_ = wgpuDeviceCreateBuffer(device, &glyphVertexDesc);

    // Glyph index buffer (6 indices per glyph - 2 triangles)
    WGPUBufferDescriptor glyphIndexDesc = {};
    glyphIndexDesc.label = "glyph indices";
    glyphIndexDesc.size = maxCells_ * 6 * sizeof(uint32_t);
    glyphIndexDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    glyphIndexBuffer_ = wgpuDeviceCreateBuffer(device, &glyphIndexDesc);

    // Background vertex buffer
    WGPUBufferDescriptor bgVertexDesc = {};
    bgVertexDesc.label = "bg vertices";
    bgVertexDesc.size = maxCells_ * 4 * sizeof(BgVertex);
    bgVertexDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    bgVertexBuffer_ = wgpuDeviceCreateBuffer(device, &bgVertexDesc);

    // Background index buffer
    WGPUBufferDescriptor bgIndexDesc = {};
    bgIndexDesc.label = "bg indices";
    bgIndexDesc.size = maxCells_ * 6 * sizeof(uint32_t);
    bgIndexDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    bgIndexBuffer_ = wgpuDeviceCreateBuffer(device, &bgIndexDesc);

    return true;
}

bool TextRenderer::createBindGroup(WGPUDevice device, Font& font) {
    // Bind group layout
    WGPUBindGroupLayoutEntry entries[3] = {};

    // Uniform buffer
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(Uniforms);

    // Texture
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    // Sampler
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout_;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Bind group
    WGPUBindGroupEntry bgEntries[3] = {};

    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer_;
    bgEntries[0].offset = 0;
    bgEntries[0].size = sizeof(Uniforms);

    bgEntries[1].binding = 1;
    bgEntries[1].textureView = font.getTextureView();

    bgEntries[2].binding = 2;
    bgEntries[2].sampler = font.getSampler();

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = bindGroupLayout_;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = bgEntries;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    return true;
}

bool TextRenderer::createPipelines(WGPUDevice device, WGPUTextureFormat format) {
    // Glyph pipeline vertex attributes
    WGPUVertexAttribute glyphAttrs[8] = {};
    glyphAttrs[0] = {WGPUVertexFormat_Float32x2, offsetof(GlyphVertex, position), 0};
    glyphAttrs[1] = {WGPUVertexFormat_Float32x2, offsetof(GlyphVertex, cellPos), 1};
    glyphAttrs[2] = {WGPUVertexFormat_Float32x2, offsetof(GlyphVertex, uvMin), 2};
    glyphAttrs[3] = {WGPUVertexFormat_Float32x2, offsetof(GlyphVertex, uvMax), 3};
    glyphAttrs[4] = {WGPUVertexFormat_Float32x2, offsetof(GlyphVertex, glyphSize), 4};
    glyphAttrs[5] = {WGPUVertexFormat_Float32x2, offsetof(GlyphVertex, glyphBearing), 5};
    glyphAttrs[6] = {WGPUVertexFormat_Float32x4, offsetof(GlyphVertex, fgColor), 6};
    glyphAttrs[7] = {WGPUVertexFormat_Float32x4, offsetof(GlyphVertex, bgColor), 7};

    WGPUVertexBufferLayout glyphBufferLayout = {};
    glyphBufferLayout.arrayStride = sizeof(GlyphVertex);
    glyphBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    glyphBufferLayout.attributeCount = 8;
    glyphBufferLayout.attributes = glyphAttrs;

    // Glyph pipeline
    WGPURenderPipelineDescriptor glyphPipelineDesc = {};
    glyphPipelineDesc.label = "glyph pipeline";
    glyphPipelineDesc.layout = pipelineLayout_;

    glyphPipelineDesc.vertex.module = shaderModule_;
    glyphPipelineDesc.vertex.entryPoint = "vs_main";
    glyphPipelineDesc.vertex.bufferCount = 1;
    glyphPipelineDesc.vertex.buffers = &glyphBufferLayout;

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule_;
    fragState.entryPoint = "fs_main";

    // Alpha blending for glyphs over background
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = format;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    glyphPipelineDesc.fragment = &fragState;

    glyphPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    glyphPipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    glyphPipelineDesc.primitive.cullMode = WGPUCullMode_None;

    glyphPipelineDesc.multisample.count = 1;
    glyphPipelineDesc.multisample.mask = 0xFFFFFFFF;

    glyphPipeline_ = wgpuDeviceCreateRenderPipeline(device, &glyphPipelineDesc);

    // Background pipeline vertex attributes
    WGPUVertexAttribute bgAttrs[3] = {};
    bgAttrs[0] = {WGPUVertexFormat_Float32x2, offsetof(BgVertex, position), 0};
    bgAttrs[1] = {WGPUVertexFormat_Float32x2, offsetof(BgVertex, cellPos), 1};
    bgAttrs[2] = {WGPUVertexFormat_Float32x4, offsetof(BgVertex, bgColor), 2};

    WGPUVertexBufferLayout bgBufferLayout = {};
    bgBufferLayout.arrayStride = sizeof(BgVertex);
    bgBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    bgBufferLayout.attributeCount = 3;
    bgBufferLayout.attributes = bgAttrs;

    WGPURenderPipelineDescriptor bgPipelineDesc = {};
    bgPipelineDesc.label = "background pipeline";
    bgPipelineDesc.layout = pipelineLayout_;

    bgPipelineDesc.vertex.module = shaderModule_;
    bgPipelineDesc.vertex.entryPoint = "vs_background";
    bgPipelineDesc.vertex.bufferCount = 1;
    bgPipelineDesc.vertex.buffers = &bgBufferLayout;

    // Background doesn't need blending - just overwrite
    WGPUColorTargetState bgColorTarget = {};
    bgColorTarget.format = format;
    bgColorTarget.blend = nullptr;  // No blending, just overwrite
    bgColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState bgFragState = {};
    bgFragState.module = shaderModule_;
    bgFragState.entryPoint = "fs_background";
    bgFragState.targetCount = 1;
    bgFragState.targets = &bgColorTarget;
    bgPipelineDesc.fragment = &bgFragState;

    bgPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    bgPipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    bgPipelineDesc.primitive.cullMode = WGPUCullMode_None;

    bgPipelineDesc.multisample.count = 1;
    bgPipelineDesc.multisample.mask = 0xFFFFFFFF;

    bgPipeline_ = wgpuDeviceCreateRenderPipeline(device, &bgPipelineDesc);

    return glyphPipeline_ && bgPipeline_;
}

void TextRenderer::resize(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
}

void TextRenderer::setCellSize(float width, float height) {
    cellSize_ = {width, height};
}

void TextRenderer::updateUniformBuffer(WGPUQueue queue) {
    // Orthographic projection (top-left origin)
    uniforms_.projection = glm::ortho(
        0.0f, static_cast<float>(screenWidth_),
        static_cast<float>(screenHeight_), 0.0f,
        -1.0f, 1.0f
    );
    uniforms_.screenSize = {static_cast<float>(screenWidth_), static_cast<float>(screenHeight_)};
    uniforms_.cellSize = cellSize_;
    uniforms_.pixelRange = font_ ? font_->getPixelRange() : 2.0f;
    uniforms_.scale = scale_;

    wgpuQueueWriteBuffer(queue, uniformBuffer_, 0, &uniforms_, sizeof(Uniforms));
}

void TextRenderer::updateVertexBuffers(WGPUQueue queue, const Grid& grid, Font& font) {
    const uint32_t cols = grid.getCols();
    const uint32_t rows = grid.getRows();
    const uint32_t cellCount = cols * rows;

    std::vector<GlyphVertex> glyphVertices;
    std::vector<uint32_t> glyphIndices;
    std::vector<BgVertex> bgVertices;
    std::vector<uint32_t> bgIndices;

    glyphVertices.reserve(cellCount * 4);
    glyphIndices.reserve(cellCount * 6);
    bgVertices.reserve(cellCount * 4);
    bgIndices.reserve(cellCount * 6);

    // Quad corners
    const glm::vec2 corners[4] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}
    };

    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t col = 0; col < cols; ++col) {
            const Cell& cell = grid.getCell(col, row);
            glm::vec2 cellPos = {static_cast<float>(col), static_cast<float>(row)};

            // BACKGROUND: Add quad for EVERY cell
            uint32_t bgBase = static_cast<uint32_t>(bgVertices.size());
            for (int i = 0; i < 4; ++i) {
                BgVertex v;
                v.position = corners[i];
                v.cellPos = cellPos;
                v.bgColor = cell.bgColor;
                bgVertices.push_back(v);
            }
            bgIndices.push_back(bgBase + 0);
            bgIndices.push_back(bgBase + 1);
            bgIndices.push_back(bgBase + 2);
            bgIndices.push_back(bgBase + 2);
            bgIndices.push_back(bgBase + 1);
            bgIndices.push_back(bgBase + 3);

            // GLYPH: Only for non-space characters
            if (cell.codepoint != ' ' && cell.codepoint != 0) {
                const GlyphMetrics* glyph = font.getGlyph(cell.codepoint);
                if (glyph) {
                    uint32_t glyphBase = static_cast<uint32_t>(glyphVertices.size());
                    for (int i = 0; i < 4; ++i) {
                        GlyphVertex v;
                        v.position = corners[i];
                        v.cellPos = cellPos;
                        v.uvMin = glyph->uvMin;
                        v.uvMax = glyph->uvMax;
                        v.glyphSize = glyph->size;
                        v.glyphBearing = glyph->bearing;
                        v.fgColor = cell.fgColor;
                        v.bgColor = cell.bgColor;
                        glyphVertices.push_back(v);
                    }
                    glyphIndices.push_back(glyphBase + 0);
                    glyphIndices.push_back(glyphBase + 1);
                    glyphIndices.push_back(glyphBase + 2);
                    glyphIndices.push_back(glyphBase + 2);
                    glyphIndices.push_back(glyphBase + 1);
                    glyphIndices.push_back(glyphBase + 3);
                }
            }
        }
    }

    glyphCount_ = static_cast<uint32_t>(glyphIndices.size());
    bgCount_ = static_cast<uint32_t>(bgIndices.size());

    if (!glyphVertices.empty()) {
        wgpuQueueWriteBuffer(queue, glyphVertexBuffer_, 0,
                             glyphVertices.data(), glyphVertices.size() * sizeof(GlyphVertex));
        wgpuQueueWriteBuffer(queue, glyphIndexBuffer_, 0,
                             glyphIndices.data(), glyphIndices.size() * sizeof(uint32_t));
    }

    if (!bgVertices.empty()) {
        wgpuQueueWriteBuffer(queue, bgVertexBuffer_, 0,
                             bgVertices.data(), bgVertices.size() * sizeof(BgVertex));
        wgpuQueueWriteBuffer(queue, bgIndexBuffer_, 0,
                             bgIndices.data(), bgIndices.size() * sizeof(uint32_t));
    }
}

void TextRenderer::render(WebGPUContext& ctx, const Grid& grid) {
    WGPUQueue queue = ctx.getQueue();

    updateUniformBuffer(queue);
    updateVertexBuffers(queue, grid, *font_);

    WGPUTextureView targetView = ctx.getCurrentTextureView();
    if (!targetView) return;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
#if YETTY_WEB
    colorAttachment.clearColor = {0.1, 0.1, 0.1, 1.0};  // Emscripten
#else
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};  // wgpu-native
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // PASS 1: Render backgrounds (solid color, no blending)
    if (bgCount_ > 0 && bgPipeline_) {
        wgpuRenderPassEncoderSetPipeline(pass, bgPipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, bgVertexBuffer_, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, bgIndexBuffer_, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, bgCount_, 1, 0, 0, 0);
    }

    // PASS 2: Render glyphs on top (alpha blended)
    if (glyphCount_ > 0 && glyphPipeline_) {
        wgpuRenderPassEncoderSetPipeline(pass, glyphPipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, glyphVertexBuffer_, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, glyphIndexBuffer_, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, glyphCount_, 1, 0, 0, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    wgpuTextureViewRelease(targetView);

    ctx.present();
}

} // namespace yetty
