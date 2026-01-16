#include <yetty/rich-text.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>
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
    scale: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var fontSampler: sampler;
@group(0) @binding(2) var fontAtlas: texture_2d<f32>;
@group(0) @binding(3) var<storage, read> glyphs: array<GlyphInstance>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
    @location(1) color: vec4<f32>,
    @location(2) scale: f32,
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
    out.scale = g.scale;
    return out;
}

fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
    let msd = textureSample(fontAtlas, fontSampler, in.uv);
    let sd = median(msd.r, msd.g, msd.b);
    // Use pixelRange directly - scale affects glyph size, not SDF distance
    let screenPxRange = u.pixelRange;
    let opacity = clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);
    return vec4(in.color.rgb, in.color.a * opacity);
}
)";

//-----------------------------------------------------------------------------
// RichText Implementation
//-----------------------------------------------------------------------------

Result<RichText::Ptr> RichText::create(WebGPUContext* ctx, WGPUTextureFormat targetFormat, FontManager* fontMgr) noexcept {
    if (!ctx) {
        return Err<Ptr>("RichText::create: null context");
    }
    if (!fontMgr) {
        return Err<Ptr>("RichText::create: null FontManager");
    }
    auto rt = Ptr(new RichText(ctx, targetFormat, fontMgr));
    if (auto res = rt->init(); !res) {
        return Err<Ptr>("Failed to initialize RichText", res);
    }
    return Ok(std::move(rt));
}

RichText::RichText(WebGPUContext* ctx, WGPUTextureFormat targetFormat, FontManager* fontMgr) noexcept
    : _ctx(ctx), targetFormat_(targetFormat), fontMgr_(fontMgr) {}

RichText::~RichText() {
    dispose();
}

Result<void> RichText::init() noexcept {
    auto result = createPipeline(*_ctx, targetFormat_);
    if (!result) {
        return Err<void>("Failed to create RichText pipeline", result);
    }

    initialized_ = true;
    return Ok();
}

void RichText::dispose() noexcept {
    // Release per-font bind groups
    for (auto& [font, bindGroup] : fontBindGroups_) {
        if (bindGroup) wgpuBindGroupRelease(bindGroup);
    }
    fontBindGroups_.clear();
    fontResourceVersions_.clear();

    if (bindGroupLayout_) { wgpuBindGroupLayoutRelease(bindGroupLayout_); bindGroupLayout_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (glyphBuffer_) { wgpuBufferRelease(glyphBuffer_); glyphBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }

    fontBatches_.clear();
    chars_.clear();
    spans_.clear();
    glyphCount_ = 0;
    glyphBufferCapacity_ = 0;
    initialized_ = false;
    gpuResourcesDirty_ = true;
}

//-----------------------------------------------------------------------------
// Font Resolution
//-----------------------------------------------------------------------------

Font* RichText::resolveFont(const std::string& fontFamily, Font::Style style) {
    if (!fontMgr_) {
        yerror("RichText::resolveFont: fontMgr_ is null!");
        return nullptr;
    }

    // Use specified family or fall back to default
    std::string family = fontFamily.empty() ? defaultFontFamily_ : fontFamily;

    ydebug("RichText::resolveFont: family='{}' (input='{}', default='{}'), style={}",
                  family, fontFamily, defaultFontFamily_, static_cast<int>(style));

    // If family is specified, try to get it
    if (!family.empty()) {
        // Try to get font with specific style
        auto result = fontMgr_->getFont(family, style);
        if (result && *result) {
            ydebug("RichText::resolveFont: found font for family='{}' style={}", family, static_cast<int>(style));
            return *result;
        }

        // Fall back to regular style of same family
        result = fontMgr_->getFont(family);
        if (result && *result) {
            ydebug("RichText::resolveFont: found font for family='{}' (regular)", family);
            return *result;
        }
    }

    // Try default font from FontManager
    Font* font = fontMgr_->getDefaultFont();
    if (font) {
        ydebug("RichText::resolveFont: using FontManager default font");
        return font;
    }

    // FontManager has no default set - try loading common fallback fonts
    static const char* fallbackFonts[] = {
        "monospace",
        "DejaVu Sans Mono",
        "Liberation Mono",
        "Courier New",
        "sans-serif",
        "DejaVu Sans",
        "Liberation Sans",
        "Arial"
    };

    for (const char* fallback : fallbackFonts) {
        auto result = fontMgr_->getFont(fallback, style);
        if (result && *result) {
            yinfo("RichText::resolveFont: loaded fallback font '{}'", fallback);
            return *result;
        }
    }

    yerror("RichText::resolveFont: no font could be loaded!");
    return nullptr;
}

void RichText::clear() {
    spans_.clear();
    chars_.clear();
    fontBatches_.clear();
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
    if (!fontMgr_) {
        yerror("RichText::layoutSpans: fontMgr_ is null!");
        return;
    }

    ydebug("RichText::layoutSpans: {} spans, viewWidth={}", spans_.size(), viewWidth);

    chars_.clear();
    contentHeight_ = 0;
    contentWidth_ = 0;

    for (const auto& span : spans_) {
        if (span.text.empty()) continue;

        // Resolve font for this span
        Font* font = resolveFont(span.fontFamily, span.style);
        if (!font) {
            ywarn("RichText::layoutSpans: no font for span, skipping");
            continue;
        }

        float scale = span.size / font->getFontSize();
        float lineHeight = span.lineHeight > 0 ? span.lineHeight : font->getLineHeight() * scale;
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

            // Get glyph metrics with style
            const auto* metrics = font->getGlyph(codepoint, span.style);
            if (!metrics) continue;

            float advance = metrics->_advance * scale;

            // Word wrap check
            if (span.wrap && cursorX + advance > startX + maxWidth && cursorX > startX) {
                cursorX = startX;
                cursorY += lineHeight;
            }

            // Add character with font family for later rendering
            TextChar ch;
            ch.codepoint = codepoint;
            ch.x = cursorX;
            ch.y = cursorY;
            ch.size = span.size;
            ch.color = span.color;
            ch.style = span.style;
            ch.fontFamily = span.fontFamily;
            chars_.push_back(ch);

            cursorX += advance;
            contentWidth_ = std::max(contentWidth_, cursorX);
        }

        contentHeight_ = std::max(contentHeight_, cursorY + lineHeight);
    }

    ydebug("RichText::layoutSpans: produced {} chars, content {}x{}",
                  chars_.size(), contentWidth_, contentHeight_);
}

//-----------------------------------------------------------------------------
// Layout - Low-level chars (already positioned, just validate)
//-----------------------------------------------------------------------------

void RichText::layoutChars(float viewWidth, float viewHeight) {
    (void)viewWidth;
    (void)viewHeight;

    ydebug("RichText::layoutChars: {} chars (useCharsDirectly={})",
                  chars_.size(), useCharsDirectly_);

    contentHeight_ = 0;
    contentWidth_ = 0;

    for (const auto& ch : chars_) {
        contentWidth_ = std::max(contentWidth_, ch.x + ch.size);
        contentHeight_ = std::max(contentHeight_, ch.y + ch.size);
    }

    ydebug("RichText::layoutChars: content {}x{}", contentWidth_, contentHeight_);
}

//-----------------------------------------------------------------------------
// Build GPU glyph instances from chars, grouped by font
//-----------------------------------------------------------------------------

void RichText::buildGlyphInstances() {
    if (!fontMgr_) {
        yerror("RichText::buildGlyphInstances: fontMgr_ is null!");
        return;
    }

    ydebug("RichText::buildGlyphInstances: processing {} chars", chars_.size());

    fontBatches_.clear();
    glyphCount_ = 0;

    // Map from Font* to batch index for grouping
    std::map<Font*, size_t> fontToBatch;
    int skippedNoFont = 0;
    int skippedNoGlyph = 0;

    for (const auto& ch : chars_) {
        if (ch.codepoint == '\n' || ch.codepoint == '\r') continue;

        // Resolve font for this character
        Font* font = resolveFont(ch.fontFamily, ch.style);
        if (!font) {
            skippedNoFont++;
            continue;
        }

        const auto* metrics = font->getGlyph(ch.codepoint, ch.style);
        if (!metrics) {
            skippedNoGlyph++;
            continue;
        }

        // Scale factor from char size to atlas base
        float atlasBaseSize = font->getFontSize();
        float fontScale = ch.size / atlasBaseSize;

        float glyphW = metrics->_size.x * fontScale;
        float glyphH = metrics->_size.y * fontScale;

        // Skip empty glyphs (spaces)
        if (glyphW < 0.1f || glyphH < 0.1f) continue;

        // Position with bearing (same as terminal shader: glyphTop = baseline - bearingY)
        float glyphX = ch.x + metrics->_bearing.x * fontScale;
        float glyphY = ch.y - metrics->_bearing.y * fontScale;

        // Debug logging for descender characters
        if (ch.codepoint == 'g' || ch.codepoint == 'p' || ch.codepoint == 'q' || ch.codepoint == 'A') {
            ydebug("Glyph '{}': ch.y={:.2f}, bearing.y={:.2f}, fontScale={:.4f}, glyphY={:.2f}, size=({:.1f},{:.1f})",
                          static_cast<char>(ch.codepoint), ch.y, metrics->_bearing.y, fontScale, glyphY, glyphW, glyphH);
        }

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
        inst.scale = fontScale;  // Store scale for pixelRange adjustment in shader

        // Find or create batch for this font
        auto it = fontToBatch.find(font);
        if (it == fontToBatch.end()) {
            fontToBatch[font] = fontBatches_.size();
            FontGlyphBatch batch;
            batch.font = font;
            fontBatches_.push_back(batch);
            it = fontToBatch.find(font);
        }

        fontBatches_[it->second].glyphs.push_back(inst);
        glyphCount_++;
    }

    yinfo("RichText::buildGlyphInstances: {} glyphs in {} batches (skipped: {} no font, {} no glyph)",
                  glyphCount_, fontBatches_.size(), skippedNoFont, skippedNoGlyph);

    // Debug: print first few glyphs
    if (!fontBatches_.empty() && !fontBatches_[0].glyphs.empty()) {
        const auto& g = fontBatches_[0].glyphs[0];
        yinfo("First glyph: pos=({:.1f},{:.1f}) uv=({:.4f},{:.4f})-({:.4f},{:.4f}) size=({:.1f},{:.1f}) color=({:.1f},{:.1f},{:.1f},{:.1f})",
              g.posX, g.posY, g.uvMinX, g.uvMinY, g.uvMaxX, g.uvMaxY, g.sizeX, g.sizeY, g.colorR, g.colorG, g.colorB, g.colorA);
    }
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

    ydebug("RichText::layout: {} chars -> {} glyphs, content {}x{}",
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
// Create/Update Bind Group for a specific font
//-----------------------------------------------------------------------------

Result<void> RichText::createBindGroup(WebGPUContext& ctx, Font* font) {
    if (!font || !font->getTextureView()) {
        return Err<void>("No font texture available");
    }

    WGPUDevice device = ctx.getDevice();

    // Check if we already have a bind group for this font with current version
    auto it = fontBindGroups_.find(font);
    auto versionIt = fontResourceVersions_.find(font);
    uint32_t currentVersion = font->getResourceVersion();
    
    if (it != fontBindGroups_.end() && 
        versionIt != fontResourceVersions_.end() &&
        versionIt->second == currentVersion) {
        return Ok();  // Already cached and up to date
    }
    
    // Release old bind group if version changed
    if (it != fontBindGroups_.end() && it->second) {
        wgpuBindGroupRelease(it->second);
        fontBindGroups_.erase(it);
    }

    // Create bind group for this font
    WGPUBindGroupEntry bgEntries[4] = {};

    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer_;
    bgEntries[0].size = 48;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = sampler_;

    bgEntries[2].binding = 2;
    bgEntries[2].textureView = font->getTextureView();

    // Note: glyph buffer binding will be set per-batch during render
    // We use maximum capacity here for the layout
    bgEntries[3].binding = 3;
    bgEntries[3].buffer = glyphBuffer_;
    bgEntries[3].size = glyphBufferCapacity_ * sizeof(GlyphInstance);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout_;
    bgDesc.entryCount = 4;
    bgDesc.entries = bgEntries;

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bgDesc);
    if (!bindGroup) return Err<void>("Failed to create bind group for font");

    fontBindGroups_[font] = bindGroup;
    fontResourceVersions_[font] = currentVersion;
    return Ok();
}

Result<void> RichText::uploadGlyphBuffer(WebGPUContext& ctx) {
    (void)ctx;
    // This function is no longer used - glyph data is uploaded per-batch in render()
    return Ok();
}

//-----------------------------------------------------------------------------
// Render - renders all font batches into the provided render pass
//-----------------------------------------------------------------------------

Result<void> RichText::render(WGPURenderPassEncoder pass, WebGPUContext& ctx,
                             uint32_t screenWidth, uint32_t screenHeight,
                             float pixelX, float pixelY,
                             float pixelW, float pixelH) {
    if (!initialized_ || !pipeline_) return Ok();  // Not initialized yet

    // Re-layout if view size changed
    if (lastViewWidth_ != pixelW || lastViewHeight_ != pixelH || layoutDirty_) {
        layout(pixelW, pixelH);
    }

    if (fontBatches_.empty() || glyphCount_ == 0) {
        return Ok();  // Nothing to render
    }

    WGPUDevice device = ctx.getDevice();

    // Find max batch size for glyph buffer allocation
    size_t maxBatchSize = 0;
    for (const auto& batch : fontBatches_) {
        maxBatchSize = std::max(maxBatchSize, batch.glyphs.size());
    }

    // Ensure glyph buffer is large enough for any batch
    if (maxBatchSize > glyphBufferCapacity_) {
        if (glyphBuffer_) {
            wgpuBufferRelease(glyphBuffer_);
        }
        glyphBufferCapacity_ = std::max(static_cast<uint32_t>(maxBatchSize), glyphBufferCapacity_ * 2);
        if (glyphBufferCapacity_ < 256) glyphBufferCapacity_ = 256;

        WGPUBufferDescriptor bufDesc = {};
        bufDesc.size = glyphBufferCapacity_ * sizeof(GlyphInstance);
        bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        glyphBuffer_ = device ? wgpuDeviceCreateBuffer(device, &bufDesc) : nullptr;
        if (!glyphBuffer_) return Err<void>("RichText: failed to create glyph buffer");

        // Invalidate cached bind groups since buffer changed
        for (auto& [font, bindGroup] : fontBindGroups_) {
            if (bindGroup) wgpuBindGroupRelease(bindGroup);
        }
        fontBindGroups_.clear();
        fontResourceVersions_.clear();
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;

    struct Uniforms {
        float rect[4];
        float screenSize[2];
        float scrollOffset;
        float pixelRange;
        float _pad[4];
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = pixelW;
    uniforms.rect[3] = pixelH;
    uniforms.screenSize[0] = static_cast<float>(screenWidth);
    uniforms.screenSize[1] = static_cast<float>(screenHeight);
    uniforms.scrollOffset = scrollOffset_;
    uniforms.pixelRange = pixelRange_;

    yinfo("RichText::render uniforms: ndc=({:.3f},{:.3f}) size=({:.0f},{:.0f}) screen={}x{} scroll={:.1f} pixelRange={:.1f}",
          ndcX, ndcY, pixelW, pixelH, screenWidth, screenHeight, scrollOffset_, pixelRange_);

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    // Set scissor rect to clip content to bounds (clamped to screen)
    float sx = std::max(0.0f, pixelX);
    float sy = std::max(0.0f, pixelY);
    float sw = std::min(pixelW, screenWidth - sx);
    float sh = std::min(pixelH, screenHeight - sy);
    if (sw > 0 && sh > 0) {
        wgpuRenderPassEncoderSetScissorRect(
            pass,
            static_cast<uint32_t>(sx),
            static_cast<uint32_t>(sy),
            static_cast<uint32_t>(sw),
            static_cast<uint32_t>(sh));
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);

    yinfo("RichText::render: rendering {} batches, {} total glyphs, glyphBuffer={}, uniformBuffer={}",
          fontBatches_.size(), glyphCount_, glyphBuffer_ != nullptr, uniformBuffer_ != nullptr);

    // Render each font batch
    for (const auto& batch : fontBatches_) {
        if (batch.glyphs.empty() || !batch.font) {
            ywarn("RichText::render: skipping batch - empty={}, font={}", batch.glyphs.empty(), batch.font != nullptr);
            continue;
        }

        yinfo("RichText::render: batch has {} glyphs, font textureView={}",
              batch.glyphs.size(), batch.font->getTextureView() != nullptr);

        // Ensure bind group exists for this font
        auto result = createBindGroup(ctx, batch.font);
        if (!result) {
            yerror("RichText::render: createBindGroup failed: {}", result.error().message());
            continue;
        }

        auto it = fontBindGroups_.find(batch.font);
        if (it == fontBindGroups_.end() || !it->second) {
            yerror("RichText::render: bind group not found after createBindGroup!");
            continue;
        }

        // Upload this batch's glyphs
        wgpuQueueWriteBuffer(ctx.getQueue(), glyphBuffer_, 0,
                             batch.glyphs.data(), batch.glyphs.size() * sizeof(GlyphInstance));

        yinfo("RichText::render: drawing {} instances", batch.glyphs.size());

        // Draw with this font's bind group
        wgpuRenderPassEncoderSetBindGroup(pass, 0, it->second, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(batch.glyphs.size()), 0, 0);
    }

    gpuResourcesDirty_ = false;
    return Ok();
}

} // namespace yetty
