#include "markdown.h"
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace yetty {

//-----------------------------------------------------------------------------
// MarkdownPlugin
//-----------------------------------------------------------------------------

MarkdownPlugin::MarkdownPlugin() = default;
MarkdownPlugin::~MarkdownPlugin() { (void)dispose(); }

Result<PluginPtr> MarkdownPlugin::create() {
    return Ok<PluginPtr>(std::make_shared<MarkdownPlugin>());
}

Result<void> MarkdownPlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    _initialized = true;
    return Ok();
}

Result<void> MarkdownPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose MarkdownPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> MarkdownPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<MarkdownLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init MarkdownLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> MarkdownPlugin::renderAll(WebGPUContext& ctx,
                                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                        uint32_t screenWidth, uint32_t screenHeight,
                                        float cellWidth, float cellHeight,
                                        int scrollOffset, uint32_t termRows,
                                        bool isAltScreen) {
    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<MarkdownLayer>(layerBase);

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
            return Err<void>("Failed to render MarkdownLayer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// MarkdownLayer
//-----------------------------------------------------------------------------

MarkdownLayer::MarkdownLayer() = default;

MarkdownLayer::~MarkdownLayer() { (void)dispose(); }

Result<void> MarkdownLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("MarkdownLayer: empty payload");
    }

    _payload = payload;
    (void)dispose();

    std::string content;

    // Check if payload is inline content or file path
    if (payload.substr(0, 7) == "inline:") {
        content = payload.substr(7);
    } else {
        // Try to load from file
        std::ifstream file(payload);
        if (!file) {
            return Err<void>("Failed to open markdown file: " + payload);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        content = buffer.str();
    }

    parseMarkdown(content);
    std::cout << "MarkdownLayer: parsed " << lines_.size() << " lines" << std::endl;
    return Ok();
}

Result<void> MarkdownLayer::dispose() {
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (bindGroupLayout_) { wgpuBindGroupLayoutRelease(bindGroupLayout_); bindGroupLayout_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (glyphBuffer_) { wgpuBufferRelease(glyphBuffer_); glyphBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }
    gpuInitialized_ = false;
    return Ok();
}

//-----------------------------------------------------------------------------
// Markdown Parser
//-----------------------------------------------------------------------------

void MarkdownLayer::parseMarkdown(const std::string& content) {
    lines_.clear();

    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        TextLine textLine;

        // Check for headers
        int headerLevel = 0;
        size_t i = 0;
        while (i < line.size() && line[i] == '#') {
            headerLevel++;
            i++;
        }
        if (headerLevel > 0 && i < line.size() && line[i] == ' ') {
            line = line.substr(i + 1);
            textLine.scale = 1.0f + (0.15f * (7 - std::min(headerLevel, 6)));
        }

        // Check for bullet list
        bool isBullet = false;
        if (line.size() >= 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
            isBullet = true;
            textLine.indent = 20.0f;
            line = line.substr(2);
        }

        // Parse inline styles
        size_t pos = 0;
        while (pos < line.size()) {
            TextSpan span;

            // Check for code
            if (line[pos] == '`') {
                size_t end = line.find('`', pos + 1);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 1, end - pos - 1);
                    span.isCode = true;
                    span.style = Font::Regular;
                    textLine.spans.push_back(span);
                    pos = end + 1;
                    continue;
                }
            }

            // Check for bold+italic (***text***)
            if (pos + 2 < line.size() && line.substr(pos, 3) == "***") {
                size_t end = line.find("***", pos + 3);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 3, end - pos - 3);
                    span.style = Font::BoldItalic;
                    textLine.spans.push_back(span);
                    pos = end + 3;
                    continue;
                }
            }

            // Check for bold (**text**)
            if (pos + 1 < line.size() && line.substr(pos, 2) == "**") {
                size_t end = line.find("**", pos + 2);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 2, end - pos - 2);
                    span.style = Font::Bold;
                    textLine.spans.push_back(span);
                    pos = end + 2;
                    continue;
                }
            }

            // Check for italic (*text*)
            if (line[pos] == '*') {
                size_t end = line.find('*', pos + 1);
                if (end != std::string::npos) {
                    span.text = line.substr(pos + 1, end - pos - 1);
                    span.style = Font::Italic;
                    textLine.spans.push_back(span);
                    pos = end + 1;
                    continue;
                }
            }

            // Regular text - find next special character
            size_t next = line.find_first_of("*`", pos);
            if (next == std::string::npos) next = line.size();
            if (next > pos) {
                span.text = line.substr(pos, next - pos);
                span.style = (headerLevel > 0) ? Font::Bold : Font::Regular;
                textLine.spans.push_back(span);
            }
            pos = next;
        }

        // Add bullet if needed
        if (isBullet && !textLine.spans.empty()) {
            TextSpan bullet;
            bullet.text = "\xE2\x80\xA2 ";  // Unicode bullet
            bullet.style = Font::Regular;
            bullet.isBullet = true;
            textLine.spans.insert(textLine.spans.begin(), bullet);
        }

        // Add empty span for empty lines (paragraph break)
        if (textLine.spans.empty()) {
            TextSpan empty;
            empty.text = "";
            empty.style = Font::Regular;
            textLine.spans.push_back(empty);
        }

        lines_.push_back(textLine);
    }
}

//-----------------------------------------------------------------------------
// Text Layout
//-----------------------------------------------------------------------------

void MarkdownLayer::layoutText(Font* font, float maxWidth) {
    if (!font) return;

    glyphs_.clear();
    float cursorY = 0.0f;
    lineHeight_ = font->getLineHeight();

    for (const auto& line : lines_) {
        float cursorX = line.indent;
        float scale = line.scale;
        float scaledLineHeight = lineHeight_ * scale;

        for (const auto& span : line.spans) {
            // Determine colors
            float r = 0.9f, g = 0.9f, b = 0.9f, a = 1.0f;
            if (span.isCode) {
                r = 0.6f; g = 0.8f; b = 0.6f;  // Green for code
            } else if (span.style == Font::Bold || span.style == Font::BoldItalic) {
                r = 1.0f; g = 1.0f; b = 1.0f;  // Brighter for bold
            }

            // Process each codepoint
            const char* ptr = span.text.c_str();
            const char* end = ptr + span.text.size();

            while (ptr < end) {
                // Decode UTF-8
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
                    continue;
                }

                if (codepoint == '\n' || codepoint == '\r') continue;

                // Get glyph metrics
                const auto* metrics = font->getGlyph(codepoint);
                if (!metrics) continue;

                float glyphW = metrics->_size.x * scale;
                float glyphH = metrics->_size.y * scale;
                float advance = metrics->_advance * scale;

                // Word wrap check
                if (cursorX + glyphW > maxWidth && cursorX > line.indent) {
                    cursorX = line.indent;
                    cursorY += scaledLineHeight;
                }

                // Position glyph
                float x = cursorX + metrics->_bearing.x * scale;
                float y = cursorY + (scaledLineHeight - metrics->_bearing.y * scale);

                GlyphInstance inst;
                inst.posX = x;
                inst.posY = y;
                inst.uvMinX = metrics->_uvMin.x;
                inst.uvMinY = metrics->_uvMin.y;
                inst.uvMaxX = metrics->_uvMax.x;
                inst.uvMaxY = metrics->_uvMax.y;
                inst.sizeX = glyphW;
                inst.sizeY = glyphH;
                inst.colorR = r;
                inst.colorG = g;
                inst.colorB = b;
                inst.colorA = a;

                glyphs_.push_back(inst);
                cursorX += advance;
            }
        }

        cursorY += lineHeight_ * line.scale;
    }

    documentHeight_ = cursorY;
    glyphCount_ = static_cast<uint32_t>(glyphs_.size());
}

void MarkdownLayer::buildGlyphBuffer(Font* font) {
    layoutText(font, static_cast<float>(_pixel_width));
}

//-----------------------------------------------------------------------------
// GPU Pipeline
//-----------------------------------------------------------------------------

Result<void> MarkdownLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();
    Plugin* parent = getParent();
    Font* font = parent ? parent->getFont() : nullptr;
    if (!font) {
        return Err<void>("MarkdownLayer: no font available");
    }

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

    // Create uniform buffer (rect + screen size + scroll + pixel range)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;  // 4 floats rect + 2 floats screen + 1 float scroll + 1 float pixelRange + padding
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

    // Create glyph buffer
    if (!glyphs_.empty()) {
        WGPUBufferDescriptor glyphBufDesc = {};
        glyphBufDesc.size = glyphs_.size() * sizeof(GlyphInstance);
        glyphBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        glyphBuffer_ = wgpuDeviceCreateBuffer(device, &glyphBufDesc);
        if (!glyphBuffer_) return Err<void>("Failed to create glyph buffer");

        wgpuQueueWriteBuffer(ctx.getQueue(), glyphBuffer_, 0,
                             glyphs_.data(), glyphs_.size() * sizeof(GlyphInstance));
    }

    // Shader
    const char* shaderCode = R"(
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

    // Position in layer pixels
    let layerX = g.posX + corner.x * g.sizeX;
    let layerY = g.posY + corner.y * g.sizeY - u.scrollOffset;

    // Convert to NDC
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

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    // Bind group layout
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

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 4;
    bglDesc.entries = entries;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bindGroupLayout_) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bindGroupLayout_;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Bind group
    WGPUTextureView atlasView = font->getTextureView();
    if (!atlasView) {
        wgpuShaderModuleRelease(shaderModule);
        wgpuPipelineLayoutRelease(pipelineLayout);
        return Err<void>("Font atlas not available");
    }

    WGPUBindGroupEntry bgEntries[4] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer_;
    bgEntries[0].size = 48;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = sampler_;

    bgEntries[2].binding = 2;
    bgEntries[2].textureView = atlasView;

    bgEntries[3].binding = 3;
    bgEntries[3].buffer = glyphBuffer_ ? glyphBuffer_ : uniformBuffer_;  // Fallback if no glyphs
    bgEntries[3].size = glyphBuffer_ ? (glyphs_.size() * sizeof(GlyphInstance)) : 48;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout_;
    bgDesc.entryCount = 4;
    bgDesc.entries = bgEntries;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

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
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) return Err<void>("Failed to create render pipeline");

    std::cout << "MarkdownLayer: pipeline created with " << glyphCount_ << " glyphs" << std::endl;
    return Ok();
}

//-----------------------------------------------------------------------------
// Render
//-----------------------------------------------------------------------------

Result<void> MarkdownLayer::render(WebGPUContext& ctx,
                                    WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                    uint32_t screenWidth, uint32_t screenHeight,
                                    float pixelX, float pixelY, float pixelW, float pixelH) {
    if (failed_) return Err<void>("MarkdownLayer already failed");

    Plugin* parent = getParent();
    Font* font = parent ? parent->getFont() : nullptr;
    if (!font) return Err<void>("No font available");

    // Re-layout if width changed
    if (lastLayoutWidth_ != pixelW) {
        layoutText(font, pixelW);
        lastLayoutWidth_ = pixelW;
        gpuInitialized_ = false;  // Need to recreate glyph buffer
    }

    if (!gpuInitialized_) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to create pipeline", result);
        }
        gpuInitialized_ = true;
    }

    if (!pipeline_ || !uniformBuffer_ || !bindGroup_ || glyphCount_ == 0) {
        return Ok();  // Nothing to render
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;

    struct Uniforms {
        float rect[4];
        float screenSize[2];
        float scrollOffset;
        float pixelRange;
        float padding[4];
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = pixelW;
    uniforms.rect[3] = pixelH;
    uniforms.screenSize[0] = static_cast<float>(screenWidth);
    uniforms.screenSize[1] = static_cast<float>(screenHeight);
    uniforms.scrollOffset = scrollOffset_;
    uniforms.pixelRange = font->getPixelRange();

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

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

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, glyphCount_, 0, 0);
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

//-----------------------------------------------------------------------------
// Mouse Scroll
//-----------------------------------------------------------------------------

bool MarkdownLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    (void)mods;

    float scrollAmount = yoffset * lineHeight_ * 3.0f;
    scrollOffset_ -= scrollAmount;

    // Clamp scroll
    float maxScroll = std::max(0.0f, documentHeight_ - static_cast<float>(_pixel_height));
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);

    return true;
}

} // namespace yetty

extern "C" {
    const char* name() { return "markdown"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::MarkdownPlugin::create(); }
}
