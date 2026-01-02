#include "yetty/text-renderer.h"
#include <yetty/wgpu-compat.h>
#include "yetty/terminal.h"  // For DamageRect
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>  // for getenv
#include <spdlog/spdlog.h>

#if YETTY_ANDROID
#include <android/log.h>
#define TR_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "yetty-tr", __VA_ARGS__)
#define TR_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "yetty-tr", __VA_ARGS__)
#else
#define TR_LOGI(...) do { spdlog::info(__VA_ARGS__); } while(0)
#define TR_LOGE(...) do { spdlog::error(__VA_ARGS__); } while(0)
#endif

namespace yetty {

Result<TextRenderer::Ptr> TextRenderer::create(WebGPUContext::Ptr ctx, FontManager::Ptr fontManager) noexcept {
    if (!ctx) {
        return Err<Ptr>("TextRenderer::create: null WebGPUContext");
    }
    if (!fontManager) {
        return Err<Ptr>("TextRenderer::create: null FontManager");
    }
    auto renderer = Ptr(new TextRenderer(std::move(ctx), std::move(fontManager)));
    if (auto res = renderer->init(); !res) {
        return Err<Ptr>("Failed to initialize TextRenderer", res);
    }
    return Ok(std::move(renderer));
}

TextRenderer::TextRenderer(WebGPUContext::Ptr ctx, FontManager::Ptr fontManager) noexcept
    : ctx_(std::move(ctx)), fontManager_(std::move(fontManager)) {}

TextRenderer::~TextRenderer() {
    // On web, texture and bind group releases cause Emscripten WebGPU manager issues
    // because bind groups hold references to textures
#if !YETTY_WEB
    if (cellAttrsView_) wgpuTextureViewRelease(cellAttrsView_);
    if (cellBgColorView_) wgpuTextureViewRelease(cellBgColorView_);
    if (cellFgColorView_) wgpuTextureViewRelease(cellFgColorView_);
    if (cellGlyphView_) wgpuTextureViewRelease(cellGlyphView_);
    if (cellAttrsTexture_) wgpuTextureRelease(cellAttrsTexture_);
    if (cellBgColorTexture_) wgpuTextureRelease(cellBgColorTexture_);
    if (cellFgColorTexture_) wgpuTextureRelease(cellFgColorTexture_);
    if (cellGlyphTexture_) wgpuTextureRelease(cellGlyphTexture_);
    if (bindGroup_) wgpuBindGroupRelease(bindGroup_);
#endif
    if (quadVertexBuffer_) wgpuBufferRelease(quadVertexBuffer_);
    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (bindGroupLayout_) wgpuBindGroupLayoutRelease(bindGroupLayout_);
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
    if (shaderModule_) wgpuShaderModuleRelease(shaderModule_);
}

Result<void> TextRenderer::init() noexcept {
    WGPUDevice device = ctx_->getDevice();

    // Get terminal font from FontManager
    auto fontResult = fontManager_->getFont("monospace", Font::Regular, 32.0f);
    if (!fontResult) {
        return Err<void>("Failed to get terminal font: " + fontResult.error().message());
    }
    font_ = *fontResult;

    // Create glyph metadata buffer in Font
    if (!font_->createGlyphMetadataBuffer(device)) {
        return Err<void>("Failed to create glyph metadata buffer");
    }

    // Create emoji atlas
    auto emojiResult = EmojiAtlas::create(device, 64);  // 64px emoji glyphs
    if (!emojiResult) {
        // Emoji atlas is optional - log warning but continue
        TR_LOGE("Failed to create emoji atlas: %s (emoji rendering disabled)",
                emojiResult.error().message().c_str());
    } else {
        emojiAtlas_ = *emojiResult;
        // Load common emojis
        if (auto res = emojiAtlas_->loadCommonEmojis(); !res) {
            TR_LOGE("Failed to load common emojis: %s", res.error().message().c_str());
        } else {
            TR_LOGI("Emoji atlas initialized with {} emojis", emojiAtlas_->getGlyphCount());
        }
    }

    if (auto res = createShaderModule(device); !res) {
        return Err<void>("Failed to create shader module", res);
    }
    if (auto res = createBuffers(device); !res) {
        return Err<void>("Failed to create buffers", res);
    }
    if (auto res = createBindGroupLayout(device); !res) {
        return Err<void>("Failed to create bind group layout", res);
    }
    if (auto res = createPipeline(device, ctx_->getSurfaceFormat()); !res) {
        return Err<void>("Failed to create pipeline", res);
    }

    return Ok();
}

Result<void> TextRenderer::createShaderModule(WGPUDevice device) {
#if YETTY_WEB
    const char* shaderPath = "/shaders.wgsl";
#elif YETTY_ANDROID
    // On Android, shader is extracted to app's data directory
    const char* envPath = std::getenv("YETTY_SHADER_PATH");
    const char* shaderPath = envPath ? envPath : "/data/local/tmp/shaders.wgsl";
    spdlog::info("Loading shader from: {}", shaderPath);
#else
    const char* shaderPath = CMAKE_SOURCE_DIR "/src/yetty/shaders/shaders.wgsl";
#endif

    std::ifstream file(shaderPath);
    if (!file.is_open()) {
        return Err<void>(std::string("Failed to open shader: ") + shaderPath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string shaderSource = buffer.str();

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    WGPU_SHADER_CODE(wgslDesc, shaderSource);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    moduleDesc.label = WGPU_STR("text shader");

    shaderModule_ = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    if (!shaderModule_) {
        return Err<void>("Failed to create shader module");
    }

    return Ok();
}

Result<void> TextRenderer::createBuffers(WGPUDevice device) {
    // Uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.label = WGPU_STR("uniforms");
    uniformDesc.size = sizeof(Uniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &uniformDesc);
    if (!uniformBuffer_) {
        return Err<void>("Failed to create uniform buffer");
    }

    // Fullscreen quad vertices (2 triangles, 6 vertices)
    float quadVertices[] = {
        -1.0f, -1.0f,  // bottom-left
         1.0f, -1.0f,  // bottom-right
        -1.0f,  1.0f,  // top-left
        -1.0f,  1.0f,  // top-left
         1.0f, -1.0f,  // bottom-right
         1.0f,  1.0f,  // top-right
    };

    WGPUBufferDescriptor quadDesc = {};
    quadDesc.label = WGPU_STR("quad vertices");
    quadDesc.size = sizeof(quadVertices);
    quadDesc.usage = WGPUBufferUsage_Vertex;
    quadDesc.mappedAtCreation = true;
    quadVertexBuffer_ = wgpuDeviceCreateBuffer(device, &quadDesc);
    if (!quadVertexBuffer_) {
        return Err<void>("Failed to create quad vertex buffer");
    }
    void* mapped = wgpuBufferGetMappedRange(quadVertexBuffer_, 0, sizeof(quadVertices));
    memcpy(mapped, quadVertices, sizeof(quadVertices));
    wgpuBufferUnmap(quadVertexBuffer_);

    return Ok();
}

Result<void> TextRenderer::createCellTextures(WGPUDevice device, uint32_t cols, uint32_t rows) {
#if YETTY_WEB
    // On web, only create textures once to avoid Emscripten WebGPU manager issues
    if (cellGlyphTexture_) {
        // Already created, just update dimensions for shader
        textureCols_ = cols;
        textureRows_ = rows;
        return Ok();
    }
    // First creation - use large fixed size to avoid needing recreation
    cols = 200;
    rows = 100;
#else
    // Release old textures if they exist (views first)
    if (cellGlyphView_) { wgpuTextureViewRelease(cellGlyphView_); cellGlyphView_ = nullptr; }
    if (cellFgColorView_) { wgpuTextureViewRelease(cellFgColorView_); cellFgColorView_ = nullptr; }
    if (cellBgColorView_) { wgpuTextureViewRelease(cellBgColorView_); cellBgColorView_ = nullptr; }
    if (cellAttrsView_) { wgpuTextureViewRelease(cellAttrsView_); cellAttrsView_ = nullptr; }
    if (cellGlyphTexture_) { wgpuTextureRelease(cellGlyphTexture_); cellGlyphTexture_ = nullptr; }
    if (cellFgColorTexture_) { wgpuTextureRelease(cellFgColorTexture_); cellFgColorTexture_ = nullptr; }
    if (cellBgColorTexture_) { wgpuTextureRelease(cellBgColorTexture_); cellBgColorTexture_ = nullptr; }
    if (cellAttrsTexture_) { wgpuTextureRelease(cellAttrsTexture_); cellAttrsTexture_ = nullptr; }
#endif

    textureCols_ = cols;
    textureRows_ = rows;

    // Glyph texture: R16Uint (16-bit unsigned int per cell)
    WGPUTextureDescriptor glyphTexDesc = {};
    glyphTexDesc.label = WGPU_STR("cell glyphs");
    glyphTexDesc.size = {cols, rows, 1};
    glyphTexDesc.mipLevelCount = 1;
    glyphTexDesc.sampleCount = 1;
    glyphTexDesc.dimension = WGPUTextureDimension_2D;
    glyphTexDesc.format = WGPUTextureFormat_R16Uint;
    glyphTexDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    cellGlyphTexture_ = wgpuDeviceCreateTexture(device, &glyphTexDesc);
    if (!cellGlyphTexture_) {
        return Err<void>("Failed to create glyph texture");
    }

    WGPUTextureViewDescriptor glyphViewDesc = {};
    glyphViewDesc.format = WGPUTextureFormat_R16Uint;
    glyphViewDesc.dimension = WGPUTextureViewDimension_2D;
    glyphViewDesc.mipLevelCount = 1;
    glyphViewDesc.arrayLayerCount = 1;
    cellGlyphView_ = wgpuTextureCreateView(cellGlyphTexture_, &glyphViewDesc);
    if (!cellGlyphView_) {
        return Err<void>("Failed to create glyph texture view");
    }

    // FG color texture: RGBA8Unorm
    WGPUTextureDescriptor fgTexDesc = {};
    fgTexDesc.label = WGPU_STR("cell fg colors");
    fgTexDesc.size = {cols, rows, 1};
    fgTexDesc.mipLevelCount = 1;
    fgTexDesc.sampleCount = 1;
    fgTexDesc.dimension = WGPUTextureDimension_2D;
    fgTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    fgTexDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    cellFgColorTexture_ = wgpuDeviceCreateTexture(device, &fgTexDesc);
    if (!cellFgColorTexture_) {
        return Err<void>("Failed to create foreground color texture");
    }

    WGPUTextureViewDescriptor fgViewDesc = {};
    fgViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    fgViewDesc.dimension = WGPUTextureViewDimension_2D;
    fgViewDesc.mipLevelCount = 1;
    fgViewDesc.arrayLayerCount = 1;
    cellFgColorView_ = wgpuTextureCreateView(cellFgColorTexture_, &fgViewDesc);
    if (!cellFgColorView_) {
        return Err<void>("Failed to create foreground color texture view");
    }

    // BG color texture: RGBA8Unorm
    WGPUTextureDescriptor bgTexDesc = {};
    bgTexDesc.label = WGPU_STR("cell bg colors");
    bgTexDesc.size = {cols, rows, 1};
    bgTexDesc.mipLevelCount = 1;
    bgTexDesc.sampleCount = 1;
    bgTexDesc.dimension = WGPUTextureDimension_2D;
    bgTexDesc.format = WGPUTextureFormat_RGBA8Unorm;
    bgTexDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    cellBgColorTexture_ = wgpuDeviceCreateTexture(device, &bgTexDesc);
    if (!cellBgColorTexture_) {
        return Err<void>("Failed to create background color texture");
    }

    WGPUTextureViewDescriptor bgViewDesc = {};
    bgViewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    bgViewDesc.dimension = WGPUTextureViewDimension_2D;
    bgViewDesc.mipLevelCount = 1;
    bgViewDesc.arrayLayerCount = 1;
    cellBgColorView_ = wgpuTextureCreateView(cellBgColorTexture_, &bgViewDesc);
    if (!cellBgColorView_) {
        return Err<void>("Failed to create background color texture view");
    }

    // Attrs texture: R8Uint (8-bit packed attributes per cell)
    WGPUTextureDescriptor attrsTexDesc = {};
    attrsTexDesc.label = WGPU_STR("cell attrs");
    attrsTexDesc.size = {cols, rows, 1};
    attrsTexDesc.mipLevelCount = 1;
    attrsTexDesc.sampleCount = 1;
    attrsTexDesc.dimension = WGPUTextureDimension_2D;
    attrsTexDesc.format = WGPUTextureFormat_R8Uint;
    attrsTexDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    cellAttrsTexture_ = wgpuDeviceCreateTexture(device, &attrsTexDesc);
    if (!cellAttrsTexture_) {
        return Err<void>("Failed to create attrs texture");
    }

    WGPUTextureViewDescriptor attrsViewDesc = {};
    attrsViewDesc.format = WGPUTextureFormat_R8Uint;
    attrsViewDesc.dimension = WGPUTextureViewDimension_2D;
    attrsViewDesc.mipLevelCount = 1;
    attrsViewDesc.arrayLayerCount = 1;
    cellAttrsView_ = wgpuTextureCreateView(cellAttrsTexture_, &attrsViewDesc);
    if (!cellAttrsView_) {
        return Err<void>("Failed to create attrs texture view");
    }

    return Ok();
}

Result<void> TextRenderer::createBindGroupLayout(WGPUDevice device) {
    // Bind group layout: 11 bindings (8 base + 3 emoji)
    WGPUBindGroupLayoutEntry entries[11] = {};

    // 0: Uniforms
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(Uniforms);

    // 1: Font atlas texture
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // 2: Font sampler
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    // 3: Glyph metadata SSBO (still a buffer - one per font, not per cell)
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[3].buffer.minBindingSize = sizeof(GlyphMetadataGPU);

    // 4: Cell glyph indices texture (R16Uint)
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Fragment;
    entries[4].texture.sampleType = WGPUTextureSampleType_Uint;
    entries[4].texture.viewDimension = WGPUTextureViewDimension_2D;

    // 5: Cell FG colors texture (RGBA8Unorm)
    entries[5].binding = 5;
    entries[5].visibility = WGPUShaderStage_Fragment;
    entries[5].texture.sampleType = WGPUTextureSampleType_Float;
    entries[5].texture.viewDimension = WGPUTextureViewDimension_2D;

    // 6: Cell BG colors texture (RGBA8Unorm)
    entries[6].binding = 6;
    entries[6].visibility = WGPUShaderStage_Fragment;
    entries[6].texture.sampleType = WGPUTextureSampleType_Float;
    entries[6].texture.viewDimension = WGPUTextureViewDimension_2D;

    // 7: Cell attributes texture (R8Uint - packed bold/italic/underline/strike/emoji)
    entries[7].binding = 7;
    entries[7].visibility = WGPUShaderStage_Fragment;
    entries[7].texture.sampleType = WGPUTextureSampleType_Uint;
    entries[7].texture.viewDimension = WGPUTextureViewDimension_2D;

    // 8: Emoji atlas texture (RGBA8Unorm - color emoji bitmap)
    entries[8].binding = 8;
    entries[8].visibility = WGPUShaderStage_Fragment;
    entries[8].texture.sampleType = WGPUTextureSampleType_Float;
    entries[8].texture.viewDimension = WGPUTextureViewDimension_2D;

    // 9: Emoji sampler
    entries[9].binding = 9;
    entries[9].visibility = WGPUShaderStage_Fragment;
    entries[9].sampler.type = WGPUSamplerBindingType_Filtering;

    // 10: Emoji metadata SSBO
    entries[10].binding = 10;
    entries[10].visibility = WGPUShaderStage_Fragment;
    entries[10].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    entries[10].buffer.minBindingSize = sizeof(EmojiGlyphMetadata);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 11;
    layoutDesc.entries = entries;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    if (!bindGroupLayout_) {
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout_;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);
    if (!pipelineLayout_) {
        return Err<void>("Failed to create pipeline layout");
    }

    return Ok();
}

Result<void> TextRenderer::createBindGroup(WGPUDevice device, Font& font) {
#if YETTY_WEB
    // On web, only create bind group once to avoid Emscripten WebGPU manager issues
    if (bindGroup_) {
        return Ok();
    }
#else
    // Release old bind group if it exists (for recreation when textures change)
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
#endif

    // Validate required resources exist
    if (!bindGroupLayout_) return Err<void>("bindGroupLayout_ is null");
    if (!uniformBuffer_) return Err<void>("uniformBuffer_ is null");
    if (!font.getTextureView()) return Err<void>("font texture view is null");
    if (!font.getSampler()) return Err<void>("font sampler is null");
    if (!font.getGlyphMetadataBuffer()) return Err<void>("glyph metadata buffer is null");
    if (!cellGlyphView_) return Err<void>("cellGlyphView_ is null - call render() first");
    if (!cellFgColorView_) return Err<void>("cellFgColorView_ is null");
    if (!cellBgColorView_) return Err<void>("cellBgColorView_ is null");
    if (!cellAttrsView_) return Err<void>("cellAttrsView_ is null");

    // Check emoji atlas resources (required for bind group)
    if (!emojiAtlas_ || !emojiAtlas_->getTextureView() || !emojiAtlas_->getSampler() ||
        !emojiAtlas_->getMetadataBuffer()) {
        return Err<void>("emoji atlas resources not ready");
    }

    // Bind group entries - uses current texture views (11 entries)
    WGPUBindGroupEntry bgEntries[11] = {};

    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer_;
    bgEntries[0].size = sizeof(Uniforms);

    bgEntries[1].binding = 1;
    bgEntries[1].textureView = font.getTextureView();

    bgEntries[2].binding = 2;
    bgEntries[2].sampler = font.getSampler();

    bgEntries[3].binding = 3;
    bgEntries[3].buffer = font.getGlyphMetadataBuffer();
    bgEntries[3].size = font.getGlyphCount() * sizeof(GlyphMetadataGPU);

    bgEntries[4].binding = 4;
    bgEntries[4].textureView = cellGlyphView_;

    bgEntries[5].binding = 5;
    bgEntries[5].textureView = cellFgColorView_;

    bgEntries[6].binding = 6;
    bgEntries[6].textureView = cellBgColorView_;

    bgEntries[7].binding = 7;
    bgEntries[7].textureView = cellAttrsView_;

    // Emoji atlas resources
    bgEntries[8].binding = 8;
    bgEntries[8].textureView = emojiAtlas_->getTextureView();

    bgEntries[9].binding = 9;
    bgEntries[9].sampler = emojiAtlas_->getSampler();

    bgEntries[10].binding = 10;
    bgEntries[10].buffer = emojiAtlas_->getMetadataBuffer();
    bgEntries[10].size = std::max(1u, emojiAtlas_->getGlyphCount()) * sizeof(EmojiGlyphMetadata);

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = bindGroupLayout_;
    bindGroupDesc.entryCount = 11;
    bindGroupDesc.entries = bgEntries;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);
    if (!bindGroup_) {
        return Err<void>("Failed to create bind group");
    }

    return Ok();
}

Result<void> TextRenderer::createPipeline(WGPUDevice device, WGPUTextureFormat format) {
    // Vertex attributes: just position (vec2)
    WGPUVertexAttribute posAttr = {};
    posAttr.format = WGPUVertexFormat_Float32x2;
    posAttr.offset = 0;
    posAttr.shaderLocation = 0;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = 2 * sizeof(float);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &posAttr;

    // Pipeline descriptor
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = WGPU_STR("text pipeline");
    pipelineDesc.layout = pipelineLayout_;

    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;

    // Fragment state with blending (for MSDF alpha)
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

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule_;
    fragState.entryPoint = WGPU_STR("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    if (!pipeline_) {
        return Err<void>("Failed to create render pipeline");
    }

    return Ok();
}

void TextRenderer::resize(uint32_t width, uint32_t height) noexcept {
    screenWidth_ = width;
    screenHeight_ = height;
}

void TextRenderer::setCellSize(float width, float height) noexcept {
    cellSize_ = {width, height};
}

void TextRenderer::updateFontBindings(Font& font) noexcept {
    if (!ctx_) return;

    font_ = &font;

    // Only recreate bind group if cell textures already exist
    // Otherwise, render() will create both textures and bind group
    if (!cellGlyphView_ || !cellFgColorView_ || !cellBgColorView_ || !cellAttrsView_) {
        // Mark that bind group needs recreation on next render
        needsBindGroupRecreation_ = true;
        return;
    }

    // Recreate bind group with updated font resources
    auto result = createBindGroup(ctx_->getDevice(), font);
    if (!result) {
        std::cerr << "Failed to update font bindings: " << error_msg(result) << std::endl;
    } else {
        needsBindGroupRecreation_ = false;
    }
}

void TextRenderer::updateUniformBuffer(WGPUQueue queue, const Grid& grid,
                                       int cursorCol, int cursorRow, bool cursorVisible) {
    uniforms_.projection = glm::ortho(
        0.0f, static_cast<float>(screenWidth_),
        static_cast<float>(screenHeight_), 0.0f,
        -1.0f, 1.0f
    );
    uniforms_.screenSize = {static_cast<float>(screenWidth_), static_cast<float>(screenHeight_)};
    uniforms_.cellSize = cellSize_;
    uniforms_.gridSize = {static_cast<float>(grid.getCols()), static_cast<float>(grid.getRows())};
    uniforms_.pixelRange = font_ ? font_->getPixelRange() : 2.0f;
    uniforms_.scale = scale_;
    uniforms_.cursorPos = {static_cast<float>(cursorCol), static_cast<float>(cursorRow)};
    uniforms_.cursorVisible = cursorVisible ? 1.0f : 0.0f;
    uniforms_._pad = 0.0f;

    wgpuQueueWriteBuffer(queue, uniformBuffer_, 0, &uniforms_, sizeof(Uniforms));
}

void TextRenderer::updateCellTextures(WGPUQueue queue, const Grid& grid) {
    const uint32_t cols = grid.getCols();
    const uint32_t rows = grid.getRows();

    // Guard against zero-sized grids (would cause GPU texture errors)
    if (cols == 0 || rows == 0) {
        return;
    }

    gridCols_ = cols;
    gridRows_ = rows;

    // Write glyph indices directly (uint16 -> R16Uint texture)
    WGPUTexelCopyTextureInfo glyphDest = {};
    glyphDest.texture = cellGlyphTexture_;
    glyphDest.mipLevel = 0;
    glyphDest.origin = {0, 0, 0};
    glyphDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout glyphLayout = {};
    glyphLayout.offset = 0;
    glyphLayout.bytesPerRow = cols * sizeof(uint16_t);
    glyphLayout.rowsPerImage = rows;

    WGPUExtent3D glyphSize = {cols, rows, 1};
    wgpuQueueWriteTexture(queue, &glyphDest, grid.getGlyphData(),
                          cols * rows * sizeof(uint16_t), &glyphLayout, &glyphSize);

    // Write FG colors directly (RGBA8 -> RGBA8Unorm texture)
    WGPUTexelCopyTextureInfo fgDest = {};
    fgDest.texture = cellFgColorTexture_;
    fgDest.mipLevel = 0;
    fgDest.origin = {0, 0, 0};
    fgDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout fgLayout = {};
    fgLayout.offset = 0;
    fgLayout.bytesPerRow = cols * 4;  // RGBA8 = 4 bytes per pixel
    fgLayout.rowsPerImage = rows;

    WGPUExtent3D fgSize = {cols, rows, 1};
    wgpuQueueWriteTexture(queue, &fgDest, grid.getFgColorData(),
                          cols * rows * 4, &fgLayout, &fgSize);

    // Write BG colors directly (RGBA8 -> RGBA8Unorm texture)
    WGPUTexelCopyTextureInfo bgDest = {};
    bgDest.texture = cellBgColorTexture_;
    bgDest.mipLevel = 0;
    bgDest.origin = {0, 0, 0};
    bgDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout bgLayout = {};
    bgLayout.offset = 0;
    bgLayout.bytesPerRow = cols * 4;
    bgLayout.rowsPerImage = rows;

    WGPUExtent3D bgSize = {cols, rows, 1};
    wgpuQueueWriteTexture(queue, &bgDest, grid.getBgColorData(),
                          cols * rows * 4, &bgLayout, &bgSize);

    // Write attributes directly (uint8 -> R8Uint texture)
    WGPUTexelCopyTextureInfo attrsDest = {};
    attrsDest.texture = cellAttrsTexture_;
    attrsDest.mipLevel = 0;
    attrsDest.origin = {0, 0, 0};
    attrsDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout attrsLayout = {};
    attrsLayout.offset = 0;
    attrsLayout.bytesPerRow = cols;  // 1 byte per cell
    attrsLayout.rowsPerImage = rows;

    WGPUExtent3D attrsSize = {cols, rows, 1};
    wgpuQueueWriteTexture(queue, &attrsDest, grid.getAttrsData(),
                          cols * rows, &attrsLayout, &attrsSize);
}

void TextRenderer::updateCellTextureRegion(WGPUQueue queue, const Grid& grid, const DamageRect& rect) {
    const uint32_t cols = grid.getCols();
    const uint32_t regionWidth = rect._endCol - rect._startCol;
    const uint32_t regionHeight = rect._endRow - rect._startRow;

    if (regionWidth == 0 || regionHeight == 0) return;

    // For partial updates, we need to extract the subregion from the grid's linear data
    // and write it to the correct location in the texture

    // Glyph texture update
    std::vector<uint16_t> glyphRegion(regionWidth * regionHeight);
    const uint16_t* srcGlyphs = grid.getGlyphData();
    for (uint32_t row = 0; row < regionHeight; row++) {
        for (uint32_t col = 0; col < regionWidth; col++) {
            uint32_t srcIdx = (rect._startRow + row) * cols + (rect._startCol + col);
            glyphRegion[row * regionWidth + col] = srcGlyphs[srcIdx];
        }
    }

    WGPUTexelCopyTextureInfo glyphDest = {};
    glyphDest.texture = cellGlyphTexture_;
    glyphDest.mipLevel = 0;
    glyphDest.origin = {rect._startCol, rect._startRow, 0};
    glyphDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout glyphLayout = {};
    glyphLayout.offset = 0;
    glyphLayout.bytesPerRow = regionWidth * sizeof(uint16_t);
    glyphLayout.rowsPerImage = regionHeight;

    WGPUExtent3D glyphSize = {regionWidth, regionHeight, 1};
    wgpuQueueWriteTexture(queue, &glyphDest, glyphRegion.data(),
                          regionWidth * regionHeight * sizeof(uint16_t), &glyphLayout, &glyphSize);

    // FG color texture update
    std::vector<uint8_t> fgRegion(regionWidth * regionHeight * 4);
    const uint8_t* srcFg = grid.getFgColorData();
    for (uint32_t row = 0; row < regionHeight; row++) {
        for (uint32_t col = 0; col < regionWidth; col++) {
            uint32_t srcIdx = ((rect._startRow + row) * cols + (rect._startCol + col)) * 4;
            uint32_t dstIdx = (row * regionWidth + col) * 4;
            fgRegion[dstIdx + 0] = srcFg[srcIdx + 0];
            fgRegion[dstIdx + 1] = srcFg[srcIdx + 1];
            fgRegion[dstIdx + 2] = srcFg[srcIdx + 2];
            fgRegion[dstIdx + 3] = srcFg[srcIdx + 3];
        }
    }

    WGPUTexelCopyTextureInfo fgDest = {};
    fgDest.texture = cellFgColorTexture_;
    fgDest.mipLevel = 0;
    fgDest.origin = {rect._startCol, rect._startRow, 0};
    fgDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout fgLayout = {};
    fgLayout.offset = 0;
    fgLayout.bytesPerRow = regionWidth * 4;
    fgLayout.rowsPerImage = regionHeight;

    WGPUExtent3D fgSize = {regionWidth, regionHeight, 1};
    wgpuQueueWriteTexture(queue, &fgDest, fgRegion.data(),
                          regionWidth * regionHeight * 4, &fgLayout, &fgSize);

    // BG color texture update
    std::vector<uint8_t> bgRegion(regionWidth * regionHeight * 4);
    const uint8_t* srcBg = grid.getBgColorData();
    for (uint32_t row = 0; row < regionHeight; row++) {
        for (uint32_t col = 0; col < regionWidth; col++) {
            uint32_t srcIdx = ((rect._startRow + row) * cols + (rect._startCol + col)) * 4;
            uint32_t dstIdx = (row * regionWidth + col) * 4;
            bgRegion[dstIdx + 0] = srcBg[srcIdx + 0];
            bgRegion[dstIdx + 1] = srcBg[srcIdx + 1];
            bgRegion[dstIdx + 2] = srcBg[srcIdx + 2];
            bgRegion[dstIdx + 3] = srcBg[srcIdx + 3];
        }
    }

    WGPUTexelCopyTextureInfo bgDest = {};
    bgDest.texture = cellBgColorTexture_;
    bgDest.mipLevel = 0;
    bgDest.origin = {rect._startCol, rect._startRow, 0};
    bgDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout bgLayout = {};
    bgLayout.offset = 0;
    bgLayout.bytesPerRow = regionWidth * 4;
    bgLayout.rowsPerImage = regionHeight;

    WGPUExtent3D bgSize = {regionWidth, regionHeight, 1};
    wgpuQueueWriteTexture(queue, &bgDest, bgRegion.data(),
                          regionWidth * regionHeight * 4, &bgLayout, &bgSize);

    // Attrs texture update
    std::vector<uint8_t> attrsRegion(regionWidth * regionHeight);
    const uint8_t* srcAttrs = grid.getAttrsData();
    for (uint32_t row = 0; row < regionHeight; row++) {
        for (uint32_t col = 0; col < regionWidth; col++) {
            uint32_t srcIdx = (rect._startRow + row) * cols + (rect._startCol + col);
            attrsRegion[row * regionWidth + col] = srcAttrs[srcIdx];
        }
    }

    WGPUTexelCopyTextureInfo attrsDest = {};
    attrsDest.texture = cellAttrsTexture_;
    attrsDest.mipLevel = 0;
    attrsDest.origin = {rect._startCol, rect._startRow, 0};
    attrsDest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout attrsLayout = {};
    attrsLayout.offset = 0;
    attrsLayout.bytesPerRow = regionWidth;
    attrsLayout.rowsPerImage = regionHeight;

    WGPUExtent3D attrsSize = {regionWidth, regionHeight, 1};
    wgpuQueueWriteTexture(queue, &attrsDest, attrsRegion.data(),
                          regionWidth * regionHeight, &attrsLayout, &attrsSize);
}

void TextRenderer::render(const Grid& grid,
                          int cursorCol, int cursorRow, bool cursorVisible) noexcept {
    static int frameCount = 0;
    frameCount++;

    WGPUDevice device = ctx_->getDevice();
    WGPUQueue queue = ctx_->getQueue();
    const uint32_t cols = grid.getCols();
    const uint32_t rows = grid.getRows();

#if YETTY_ANDROID
    if (frameCount <= 3) {
        TR_LOGI("render() frame %d: grid %ux%u, cursor (%d,%d)", frameCount, cols, rows, cursorCol, cursorRow);
    }
#endif

    // Recreate textures and bind group if grid size changed
    if (cols != textureCols_ || rows != textureRows_) {
        TR_LOGI("Creating cell textures: %ux%u", cols, rows);
        if (auto res = createCellTextures(device, cols, rows); !res) {
            TR_LOGE("TextRenderer: %s", error_msg(res).c_str());
            return;
        }
        if (auto res = createBindGroup(device, *font_); !res) {
            TR_LOGE("TextRenderer: %s", error_msg(res).c_str());
            return;
        }
        needsBindGroupRecreation_ = false;
        TR_LOGI("Cell textures and bind group created successfully");
    } else if (needsBindGroupRecreation_) {
        // Deferred bind group recreation (e.g., after glyph metadata buffer update)
        if (auto res = createBindGroup(device, *font_); !res) {
            TR_LOGE("TextRenderer: %s", error_msg(res).c_str());
            return;
        }
        needsBindGroupRecreation_ = false;
    }

    updateUniformBuffer(queue, grid, cursorCol, cursorRow, cursorVisible);
    updateCellTextures(queue, grid);

    auto targetViewResult = ctx_->getCurrentTextureView();
    if (!targetViewResult) {
        TR_LOGE("TextRenderer: getCurrentTextureView failed: %s", error_msg(targetViewResult).c_str());
        return;
    }
    WGPUTextureView targetView = *targetViewResult;

#if YETTY_ANDROID
    if (frameCount <= 3) {
        TR_LOGI("Frame %d: targetView=%p, pipeline=%p, bindGroup=%p",
                frameCount, (void*)targetView, (void*)pipeline_, (void*)bindGroup_);
    }
#else
    if (frameCount <= 3) {
        spdlog::info("Frame {}: targetView={}, pipeline={}, bindGroup={}",
                     frameCount, (void*)targetView, (void*)pipeline_, (void*)bindGroup_);
    }
#endif

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
#if YETTY_WEB
    WGPU_COLOR_ATTACHMENT_CLEAR(colorAttachment, 0.1, 0.1, 0.1, 1.0);
#else
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // v27: required for 2D textures
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quadVertexBuffer_, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

#if YETTY_ANDROID
    if (frameCount <= 3) {
        TR_LOGI("Frame %d: render pass completed, command buffer submitted", frameCount);
    }
#endif

    // Note: targetView is cached by WebGPUContext, don't release here
    // Note: present() should be called by main loop after all rendering
}

void TextRenderer::render(const Grid& grid,
                          const std::vector<DamageRect>& damageRects, bool fullDamage,
                          int cursorCol, int cursorRow, bool cursorVisible) noexcept {
    WGPUDevice device = ctx_->getDevice();
    WGPUQueue queue = ctx_->getQueue();
    const uint32_t cols = grid.getCols();
    const uint32_t rows = grid.getRows();

    // Recreate textures and bind group if grid size changed
    if (cols != textureCols_ || rows != textureRows_) {
        if (auto res = createCellTextures(device, cols, rows); !res) {
            std::cerr << "TextRenderer: " << error_msg(res) << std::endl;
            return;
        }
        if (auto res = createBindGroup(device, *font_); !res) {
            std::cerr << "TextRenderer: " << error_msg(res) << std::endl;
            return;
        }
        needsBindGroupRecreation_ = false;
        // Force full update when textures are recreated
        updateCellTextures(queue, grid);
    } else if (needsBindGroupRecreation_) {
        // Deferred bind group recreation (e.g., after glyph metadata buffer update)
        if (auto res = createBindGroup(device, *font_); !res) {
            std::cerr << "TextRenderer: " << error_msg(res) << std::endl;
            return;
        }
        needsBindGroupRecreation_ = false;
        // Also need full update for new glyphs
        updateCellTextures(queue, grid);
    } else if (fullDamage) {
        // Full damage - update entire texture
        updateCellTextures(queue, grid);
    } else if (!damageRects.empty()) {
        // Partial damage - only update changed regions
        for (const auto& rect : damageRects) {
            updateCellTextureRegion(queue, grid, rect);
        }

        if (config_ && config_->debugDamageRects()) {
            uint32_t totalCells = 0;
            for (const auto& rect : damageRects) {
                totalCells += (rect._endCol - rect._startCol) * (rect._endRow - rect._startRow);
            }
            std::cout << "Updated " << totalCells << " cells in " << damageRects.size()
                      << " rects (vs " << (cols * rows) << " total)" << std::endl;
        }
    }
    // else: no damage, skip texture update entirely

    updateUniformBuffer(queue, grid, cursorCol, cursorRow, cursorVisible);

    auto targetViewResult = ctx_->getCurrentTextureView();
    if (!targetViewResult) {
        std::cerr << "TextRenderer: " << error_msg(targetViewResult) << std::endl;
        return;
    }
    WGPUTextureView targetView = *targetViewResult;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
#if YETTY_WEB
    WGPU_COLOR_ATTACHMENT_CLEAR(colorAttachment, 0.1, 0.1, 0.1, 1.0);
#else
    colorAttachment.clearValue = {0.1, 0.1, 0.1, 1.0};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;  // v27: required for 2D textures
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, quadVertexBuffer_, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    // Note: targetView is cached by WebGPUContext, don't release here
    // Note: present() should be called by main loop after all rendering
}

} // namespace yetty
