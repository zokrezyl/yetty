#include "rich-text.h"
#include "webgpu-context.h"
#include "wgpu-compat.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace yetty {

//-----------------------------------------------------------------------------
// WGSL Shader for MSDF text rendering (same as PDF/Markdown)
//-----------------------------------------------------------------------------
static const char* RICH_TEXT_SHADER = R"(
struct Uniforms {
    rect: vec4<f32>,       // x, y, w, h in NDC
    screenSize: vec2<f32>,
    scrollOffset: f32,
    pixelRange: f32,
}

struct GlyphInstance {
    posX: f32, posY: f32,
    uvMinX: f32, uvMinY: f32,
    uvMaxX: f32, uvMaxY: f32,
    sizeX: f32, sizeY: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var fontSampler: sampler;
@group(0) @binding(2) var fontAtlas: texture_2d<f32>;
@group(0) @binding(3) var<storage, read> glyphs: array<GlyphInstance>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32, @builtin(instance_index) ii: u32) -> VertexOutput {
    let corners = array<vec2<f32>, 6>(
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    let corner = corners[vi];
    let g = glyphs[ii];

    let layerX = g.posX + corner.x * g.sizeX;
    let layerY = g.posY + corner.y * g.sizeY - u.scrollOffset;

    let ndcX = u.rect.x + (layerX / u.screenSize.x) * 2.0;
    let ndcY = u.rect.y - (layerY / u.screenSize.y) * 2.0;

    var out: VertexOutput;
    out.position = vec4(ndcX, ndcY, 0.0, 1.0);
    out.uv = vec2(
        g.uvMinX + corner.x * (g.uvMaxX - g.uvMinX),
        g.uvMinY + corner.y * (g.uvMaxY - g.uvMinY)
    );
    out.color = vec4(g.colorR, g.colorG, g.colorB, g.colorA);
    return out;
}

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let msd = textureSample(fontAtlas, fontSampler, in.uv);
    let sd = median(msd.r, msd.g, msd.b);
    let screenPxDistance = u.pixelRange * (sd - 0.5);
    let opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);
    return vec4(in.color.rgb, in.color.a * opacity);
}
)";

//-----------------------------------------------------------------------------
// RichText Implementation
//-----------------------------------------------------------------------------

RichText::RichText() = default;

RichText::~RichText() {
    dispose();
}

Result<void> RichText::init(WebGPUContext* ctx, WGPUTextureFormat targetFormat) {
    if (!ctx) {
        return Err<void>("RichText::init: null context");
    }
    ctx_ = ctx;
    targetFormat_ = targetFormat;

    auto result = createPipeline(*ctx, targetFormat);
    if (!result) {
        return Err<void>("Failed to create RichText pipeline", result);
    }

    initialized_ = true;
    return Ok();
}

void RichText::dispose() {
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (bindGroupLayout_) { wgpuBindGroupLayoutRelease(bindGroupLayout_); bindGroupLayout_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (glyphBuffer_) { wgpuBufferRelease(glyphBuffer_); glyphBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }

    glyphs_.clear();
    chars_.clear();
    spans_.clear();
    glyphCount_ = 0;
    glyphBufferCapacity_ = 0;
    initialized_ = false;
    gpuResourcesDirty_ = true;
}

void RichText::setFont(Font* font) {
    if (font_ != font) {
        font_ = font;
        // Bind group needs to be recreated with new font texture
        gpuResourcesDirty_ = true;
        layoutDirty_ = true;
    }
}

void RichText::clear() {
    spans_.clear();
    chars_.clear();
    glyphs_.clear();
    contentHeight_ = 0;
    contentWidth_ = 0;
    glyphCount_ = 0;
    layoutDirty_ = true;
    gpuResourcesDirty_ = true;
    useCharsDirectly_ = false;
}

void RichText::addSpan(const TextSpan& span) {
    spans_.push_back(span);
    layoutDirty_ = true;
    gpuResourcesDirty_ = true;
}

void RichText::addSpans(const std::vector<TextSpan>& spans) {
    spans_.insert(spans_.end(), spans.begin(), spans.end());
    layoutDirty_ = true;
    gpuResourcesDirty_ = true;
}

void RichText::addChar(const TextChar& ch) {
    chars_.push_back(ch);
    useCharsDirectly_ = true;
    layoutDirty_ = true;
    gpuResourcesDirty_ = true;
}

void RichText::addChars(const std::vector<TextChar>& chars) {
    chars_.insert(chars_.end(), chars.begin(), chars.end());
    useCharsDirectly_ = true;
    layoutDirty_ = true;
    gpuResourcesDirty_ = true;
}

void RichText::setScrollOffset(float offset) {
    scrollOffset_ = std::max(0.0f, offset);
}

void RichText::scroll(float delta) {
    scrollOffset_ = std::max(0.0f, scrollOffset_ + delta);
}

//-----------------------------------------------------------------------------
// UTF-8 Decoding
//-----------------------------------------------------------------------------

uint32_t RichText::decodeUTF8(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr >= end) return 0;

    uint32_t codepoint = 0;
    if ((*ptr & 0x80) == 0) {
        codepoint = *ptr++;
    } else if ((*ptr & 0xE0) == 0xC0) {
        codepoint = (*ptr++ & 0x1F) << 6;
        if (ptr < end) codepoint |= (*ptr++ & 0x3F);
    } else if ((*ptr & 0xF0) == 0xE0) {
        codepoint = (*ptr++ & 0x0F) << 12;
        if (ptr < end) codepoint |= (*ptr++ & 0x3F) << 6;
        if (ptr < end) codepoint |= (*ptr++ & 0x3F);
    } else if ((*ptr & 0xF8) == 0xF0) {
        codepoint = (*ptr++ & 0x07) << 18;
        if (ptr < end) codepoint |= (*ptr++ & 0x3F) << 12;
        if (ptr < end) codepoint |= (*ptr++ & 0x3F) << 6;
        if (ptr < end) codepoint |= (*ptr++ & 0x3F);
    } else {
        ptr++;  // Invalid, skip
        return 0xFFFD;  // Replacement character
    }
    return codepoint;
}

//-----------------------------------------------------------------------------
// Layout - High-level spans
//-----------------------------------------------------------------------------

void RichText::layoutSpans(float viewWidth, float viewHeight) {
    (void)viewHeight;
    if (!font_) return;

    chars_.clear();
    contentHeight_ = 0;
    contentWidth_ = 0;

    for (const auto& span : spans_) {
        if (span.text.empty()) continue;

        float scale = span.size / font_->getFontSize();
        float lineHeight = span.lineHeight > 0 ? span.lineHeight : font_->getLineHeight() * scale;
        float maxWidth = span.wrap ? span.maxWidth : viewWidth;
        float startX = span.x;

        float cursorX = span.x;
        float cursorY = span.y;

        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(span.text.data());
        const uint8_t* end = ptr + span.text.size();

        while (ptr < end) {
            uint32_t codepoint = decodeUTF8(ptr, end);
            if (codepoint == 0) continue;

            // Handle newlines
            if (codepoint == '\n') {
                cursorX = startX;
                cursorY += lineHeight;
                continue;
            }
            if (codepoint == '\r') continue;

            // Get glyph metrics
            const auto* metrics = font_->getGlyph(codepoint);
            if (!metrics) continue;

            float advance = metrics->_advance * scale;

            // Word wrap check
            if (span.wrap && cursorX + advance > startX + maxWidth && cursorX > startX) {
                cursorX = startX;
                cursorY += lineHeight;
            }

            // Add character
            TextChar ch;
            ch.codepoint = codepoint;
            ch.x = cursorX;
            ch.y = cursorY;
            ch.size = span.size;
            ch.color = span.color;
            ch.style = span.style;
            chars_.push_back(ch);

            cursorX += advance;
            contentWidth_ = std::max(contentWidth_, cursorX);
        }

        contentHeight_ = std::max(contentHeight_, cursorY + lineHeight);
    }
}

//-----------------------------------------------------------------------------
// Layout - Low-level chars (already positioned, just validate)
//-----------------------------------------------------------------------------

void RichText::layoutChars(float viewWidth, float viewHeight) {
    (void)viewWidth;
    (void)viewHeight;

    contentHeight_ = 0;
    contentWidth_ = 0;

    for (const auto& ch : chars_) {
        contentWidth_ = std::max(contentWidth_, ch.x + ch.size);
        contentHeight_ = std::max(contentHeight_, ch.y + ch.size);
    }
}

//-----------------------------------------------------------------------------
// Build GPU glyph instances from chars
//-----------------------------------------------------------------------------

void RichText::buildGlyphInstances() {
    if (!font_) return;

    glyphs_.clear();

    // Base font size used for MSDF atlas
    float atlasBaseSize = font_->getFontSize();

    for (const auto& ch : chars_) {
        if (ch.codepoint == '\n' || ch.codepoint == '\r') continue;

        const auto* metrics = font_->getGlyph(ch.codepoint);
        if (!metrics) continue;

        // Scale factor from char size to atlas base
        float fontScale = ch.size / atlasBaseSize;

        float glyphW = metrics->_size.x * fontScale;
        float glyphH = metrics->_size.y * fontScale;

        // Skip empty glyphs (spaces)
        if (glyphW < 0.1f || glyphH < 0.1f) continue;

        // Position with bearing
        float glyphX = ch.x + metrics->_bearing.x * fontScale;
        float glyphY = ch.y + (ch.size - metrics->_bearing.y * fontScale);

        GlyphInstance inst;
        inst.posX = glyphX;
        inst.posY = glyphY;
        inst.uvMinX = metrics->_uvMin.x;
        inst.uvMinY = metrics->_uvMin.y;
        inst.uvMaxX = metrics->_uvMax.x;
        inst.uvMaxY = metrics->_uvMax.y;
        inst.sizeX = glyphW;
        inst.sizeY = glyphH;
        inst.colorR = ch.color.r;
        inst.colorG = ch.color.g;
        inst.colorB = ch.color.b;
        inst.colorA = ch.color.a;

        glyphs_.push_back(inst);
    }

    glyphCount_ = static_cast<uint32_t>(glyphs_.size());
}

//-----------------------------------------------------------------------------
// Layout entry point
//-----------------------------------------------------------------------------

void RichText::layout(float viewWidth, float viewHeight) {
    if (!layoutDirty_ && viewWidth == lastViewWidth_ && viewHeight == lastViewHeight_) {
        return;
    }

    if (useCharsDirectly_) {
        layoutChars(viewWidth, viewHeight);
    } else {
        layoutSpans(viewWidth, viewHeight);
    }

    buildGlyphInstances();

    lastViewWidth_ = viewWidth;
    lastViewHeight_ = viewHeight;
    layoutDirty_ = false;
    gpuResourcesDirty_ = true;

    spdlog::debug("RichText::layout: {} chars -> {} glyphs, content {}x{}",
                  chars_.size(), glyphCount_, contentWidth_, contentHeight_);
}

//-----------------------------------------------------------------------------
// GPU Pipeline Creation (mirrors PDFLayer::createPipelines)
//-----------------------------------------------------------------------------

Result<void> RichText::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler_) return Err<void>("Failed to create sampler");

    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;  // vec4 rect + vec2 screenSize + f32 scroll + f32 pixelRange + padding
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(RICH_TEXT_SHADER);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    // Create bind group layout
    WGPUBindGroupLayoutEntry entries[4] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Vertex;
    entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
    layoutDesc.entries = entries;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    if (!bindGroupLayout_) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout_;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Create render pipeline
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

    WGPUFragmentState fragment = {};
    fragment.module = shaderModule;
    fragment.entryPoint = WGPU_STR("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.fragment = &fragment;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    if (!pipeline_) return Err<void>("Failed to create render pipeline");

    return Ok();
}

//-----------------------------------------------------------------------------
// Create/Update Bind Group
//-----------------------------------------------------------------------------

Result<void> RichText::createBindGroup(WebGPUContext& ctx) {
    if (!font_ || !font_->getTextureView()) {
        return Err<void>("No font texture available");
    }

    if (glyphs_.empty()) {
        return Ok();  // Nothing to render
    }

    WGPUDevice device = ctx.getDevice();

    // Create or resize glyph buffer
    uint32_t requiredCapacity = static_cast<uint32_t>(glyphs_.size());
    if (requiredCapacity > glyphBufferCapacity_) {
        if (glyphBuffer_) {
            wgpuBufferRelease(glyphBuffer_);
        }
        // Allocate with some headroom
        glyphBufferCapacity_ = std::max(requiredCapacity, glyphBufferCapacity_ * 2);
        if (glyphBufferCapacity_ < 256) glyphBufferCapacity_ = 256;

        WGPUBufferDescriptor bufDesc = {};
        bufDesc.size = glyphBufferCapacity_ * sizeof(GlyphInstance);
        bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        glyphBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
        if (!glyphBuffer_) return Err<void>("Failed to create glyph buffer");
    }

    // Upload glyph data
    wgpuQueueWriteBuffer(ctx.getQueue(), glyphBuffer_, 0,
                         glyphs_.data(), glyphs_.size() * sizeof(GlyphInstance));

    // Release old bind group
    if (bindGroup_) {
        wgpuBindGroupRelease(bindGroup_);
        bindGroup_ = nullptr;
    }

    // Create new bind group
    WGPUBindGroupEntry bgEntries[4] = {};

    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer_;
    bgEntries[0].size = 48;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = sampler_;

    bgEntries[2].binding = 2;
    bgEntries[2].textureView = font_->getTextureView();

    bgEntries[3].binding = 3;
    bgEntries[3].buffer = glyphBuffer_;
    bgEntries[3].size = glyphs_.size() * sizeof(GlyphInstance);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout_;
    bgDesc.entryCount = 4;
    bgDesc.entries = bgEntries;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

    if (!bindGroup_) return Err<void>("Failed to create bind group");

    gpuResourcesDirty_ = false;
    return Ok();
}

Result<void> RichText::uploadGlyphBuffer(WebGPUContext& ctx) {
    if (glyphs_.empty() || !glyphBuffer_) return Ok();

    wgpuQueueWriteBuffer(ctx.getQueue(), glyphBuffer_, 0,
                         glyphs_.data(), glyphs_.size() * sizeof(GlyphInstance));
    return Ok();
}

//-----------------------------------------------------------------------------
// Render (mirrors PDFLayer::render)
//-----------------------------------------------------------------------------

Result<void> RichText::render(WebGPUContext& ctx,
                               WGPUTextureView targetView,
                               uint32_t screenWidth, uint32_t screenHeight,
                               float pixelX, float pixelY,
                               float pixelW, float pixelH) {
    if (!initialized_) {
        return Err<void>("RichText not initialized");
    }

    // Re-layout if view size changed
    if (lastViewWidth_ != pixelW || lastViewHeight_ != pixelH || layoutDirty_) {
        layout(pixelW, pixelH);
    }

    // Update GPU resources if needed
    if (gpuResourcesDirty_) {
        auto result = createBindGroup(ctx);
        if (!result) {
            return Err<void>("Failed to create bind group", result);
        }
    }

    if (!pipeline_ || !bindGroup_ || glyphCount_ == 0) {
        return Ok();  // Nothing to render
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;

    struct Uniforms {
        float rect[4];      // NDC viewport
        float screenSize[2];
        float scrollOffset;
        float pixelRange;
        float _pad[4];      // Padding to 48 bytes
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = pixelW;
    uniforms.rect[3] = pixelH;
    uniforms.screenSize[0] = static_cast<float>(screenWidth);
    uniforms.screenSize[1] = static_cast<float>(screenHeight);
    uniforms.scrollOffset = scrollOffset_;
    uniforms.pixelRange = pixelRange_;

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    // Create render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), nullptr);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Set scissor rect to clip content to bounds
    wgpuRenderPassEncoderSetScissorRect(
        pass,
        static_cast<uint32_t>(pixelX),
        static_cast<uint32_t>(pixelY),
        static_cast<uint32_t>(pixelW),
        static_cast<uint32_t>(pixelH));

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, glyphCount_, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    return Ok();
}

} // namespace yetty
