#include "emoji-atlas.h"
#include "emoji.h"
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>

// Android/Emscripten: stub implementation without FreeType/fontconfig
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
#define YETTY_EMOJI_ATLAS_STUB 1
#else
#define YETTY_EMOJI_ATLAS_STUB 0
#endif

#if !YETTY_EMOJI_ATLAS_STUB
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

// Color emoji support (FreeType 2.10+)
#ifdef FT_CONFIG_OPTION_USE_HARFBUZZ
#include FT_COLOR_H
#endif

// Include bitmap strikes info
#include FT_BITMAP_H
#include FT_SFNT_NAMES_H

#include <fontconfig/fontconfig.h>
#endif // !YETTY_EMOJI_ATLAS_STUB

#include <cstring>
#include <algorithm>

namespace yetty {

// =============================================================================
// Stub implementation for Android/Emscripten (no FreeType/fontconfig)
// =============================================================================
#if YETTY_EMOJI_ATLAS_STUB

Result<EmojiAtlas::Ptr> EmojiAtlas::create(WGPUDevice device, uint32_t glyphSize) noexcept {
    auto atlas = Ptr(new EmojiAtlas(device, glyphSize));
    if (auto res = atlas->init(); !res) {
        return Err<Ptr>("Failed to initialize EmojiAtlas", res);
    }
    return Ok(std::move(atlas));
}

EmojiAtlas::EmojiAtlas(WGPUDevice device, uint32_t glyphSize) noexcept
    : device_(device), glyphSize_(glyphSize) {
    glyphsPerRow_ = atlasSize_ / glyphSize_;
}

EmojiAtlas::~EmojiAtlas() {
    if (metadataBuffer_) wgpuBufferRelease(metadataBuffer_);
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (textureView_) wgpuTextureViewRelease(textureView_);
    if (texture_) wgpuTextureRelease(texture_);
}

Result<void> EmojiAtlas::init() noexcept {
    // Stub: no emoji font support on Android/Emscripten
    yinfo("EmojiAtlas: stub implementation (no FreeType support)");
    atlasData_.resize(atlasSize_ * atlasSize_ * 4, 0);
    return Ok();
}

Result<void> EmojiAtlas::findEmojiFont() noexcept {
    // Stub: no fontconfig
    return Err<void>("Emoji font not available on this platform");
}

Result<void> EmojiAtlas::loadCommonEmojis() noexcept {
    // Stub: no emojis
    yinfo("EmojiAtlas: no emoji support on this platform");
    return uploadToGPU();
}

Result<int> EmojiAtlas::loadEmoji(uint32_t codepoint) noexcept {
    (void)codepoint;
    // Stub: no emojis
    return Ok(-1);
}

int EmojiAtlas::getEmojiIndex(uint32_t codepoint) const noexcept {
    (void)codepoint;
    return -1;
}

bool EmojiAtlas::hasEmoji(uint32_t codepoint) const noexcept {
    (void)codepoint;
    return false;
}

Result<void> EmojiAtlas::uploadToGPU() noexcept {
    if (!gpuResourcesCreated_) {
        if (auto res = createGPUResources(); !res) {
            return res;
        }
    }
    return Ok();
}

Result<void> EmojiAtlas::createGPUResources() noexcept {
    // Create minimal texture (1x1 transparent)
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = WGPU_STR("emoji atlas stub");
    texDesc.size = {atlasSize_, atlasSize_, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    texture_ = wgpuDeviceCreateTexture(device_, &texDesc);
    if (!texture_) {
        return Err<void>("Failed to create emoji atlas texture");
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
    if (!textureView_) {
        return Err<void>("Failed to create emoji atlas texture view");
    }

    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.label = WGPU_STR("emoji sampler stub");
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.maxAnisotropy = 1;

    sampler_ = wgpuDeviceCreateSampler(device_, &samplerDesc);
    if (!sampler_) {
        return Err<void>("Failed to create emoji sampler");
    }

    uint32_t maxEmojis = glyphsPerRow_ * glyphsPerRow_;
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.label = WGPU_STR("emoji metadata stub");
    bufDesc.size = maxEmojis * sizeof(EmojiGlyphMetadata);
    bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;

    metadataBuffer_ = wgpuDeviceCreateBuffer(device_, &bufDesc);
    if (!metadataBuffer_) {
        return Err<void>("Failed to create emoji metadata buffer");
    }

    gpuResourcesCreated_ = true;
    yinfo("EmojiAtlas: stub GPU resources created");

    return Ok();
}

// =============================================================================
// Full implementation for Desktop (FreeType + fontconfig)
// =============================================================================
#else // !YETTY_EMOJI_ATLAS_STUB

Result<EmojiAtlas::Ptr> EmojiAtlas::create(WGPUDevice device, uint32_t glyphSize) noexcept {
    auto atlas = Ptr(new EmojiAtlas(device, glyphSize));
    if (auto res = atlas->init(); !res) {
        return Err<Ptr>("Failed to initialize EmojiAtlas", res);
    }
    return Ok(std::move(atlas));
}

EmojiAtlas::EmojiAtlas(WGPUDevice device, uint32_t glyphSize) noexcept
    : device_(device), glyphSize_(glyphSize) {
    glyphsPerRow_ = atlasSize_ / glyphSize_;
}

EmojiAtlas::~EmojiAtlas() {
    if (metadataBuffer_) wgpuBufferRelease(metadataBuffer_);
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (textureView_) wgpuTextureViewRelease(textureView_);
    if (texture_) wgpuTextureRelease(texture_);

    if (ftFace_) {
        FT_Done_Face(static_cast<FT_Face>(ftFace_));
    }
    if (ftLibrary_) {
        FT_Done_FreeType(static_cast<FT_Library>(ftLibrary_));
    }
}

Result<void> EmojiAtlas::init() noexcept {
    // Initialize FreeType
    FT_Library library;
    FT_Error error = FT_Init_FreeType(&library);
    if (error) {
        return Err<void>("Failed to initialize FreeType");
    }
    ftLibrary_ = library;

    // Log FreeType version
    FT_Int major, minor, patch;
    FT_Library_Version(library, &major, &minor, &patch);
    yinfo("EmojiAtlas: FreeType version {}.{}.{}", major, minor, patch);

    // Find emoji font
    if (auto res = findEmojiFont(); !res) {
        return Err<void>("Failed to find emoji font", res);
    }

    // Initialize atlas data (RGBA)
    atlasData_.resize(atlasSize_ * atlasSize_ * 4, 0);

    yinfo("EmojiAtlas initialized: {}x{} atlas, {}px glyphs",
                 atlasSize_, atlasSize_, glyphSize_);

    return Ok();
}

Result<void> EmojiAtlas::findEmojiFont() noexcept {
    // Use fontconfig to find a color emoji font
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) {
        return Err<void>("Failed to initialize fontconfig");
    }

    // Search for emoji fonts in order of preference
    const char* emojiPatterns[] = {
        "Noto Color Emoji",
        "Apple Color Emoji",
        "Segoe UI Emoji",
        "Twemoji",
        "EmojiOne",
        "emoji"  // Generic fallback
    };

    FT_Library library = static_cast<FT_Library>(ftLibrary_);
    FT_Face face = nullptr;

    for (const char* pattern : emojiPatterns) {
        FcPattern* fcPattern = FcNameParse(reinterpret_cast<const FcChar8*>(pattern));
        if (!fcPattern) continue;

        FcConfigSubstitute(config, fcPattern, FcMatchPattern);
        FcDefaultSubstitute(fcPattern);

        FcResult result;
        FcPattern* match = FcFontMatch(config, fcPattern, &result);
        FcPatternDestroy(fcPattern);

        if (match && result == FcResultMatch) {
            FcChar8* fontPath = nullptr;
            if (FcPatternGetString(match, FC_FILE, 0, &fontPath) == FcResultMatch) {
                // Try to load the font
                FT_Error error = FT_New_Face(library, reinterpret_cast<const char*>(fontPath),
                                              0, &face);
                if (error == 0) {
                    emojiFontPath_ = reinterpret_cast<const char*>(fontPath);
                    ftFace_ = face;

                    // Log font info
                    yinfo("EmojiAtlas: using font '{}'", emojiFontPath_);
                    ydebug("EmojiAtlas: font has {} glyphs, {} charmaps, {} fixed sizes",
                                  face->num_glyphs, face->num_charmaps, face->num_fixed_sizes);

                    // Log face flags
                    ydebug("EmojiAtlas: face_flags={:08X} scalable={} fixed_sizes={} color={}",
                                  face->face_flags,
                                  (face->face_flags & FT_FACE_FLAG_SCALABLE) ? 1 : 0,
                                  (face->face_flags & FT_FACE_FLAG_FIXED_SIZES) ? 1 : 0,
                                  (face->face_flags & FT_FACE_FLAG_COLOR) ? 1 : 0);

                    for (int i = 0; i < face->num_charmaps; i++) {
                        ydebug("EmojiAtlas: charmap[{}] encoding={:08X} platform={} id={}",
                                      i, static_cast<uint32_t>(face->charmaps[i]->encoding),
                                      face->charmaps[i]->platform_id, face->charmaps[i]->encoding_id);
                    }

                    // IMPORTANT: Select size BEFORE charmap for bitmap fonts!
                    // This order is required for CBDT/CBLC fonts to load properly.
                    if (face->num_fixed_sizes > 0) {
                        FT_Error sizeErr = FT_Select_Size(face, 0);
                        if (sizeErr) {
                            ywarn("EmojiAtlas: FT_Select_Size failed: {}", sizeErr);
                        } else {
                            ydebug("EmojiAtlas: FT_Select_Size(0) succeeded: {}x{}",
                                          face->size->metrics.x_ppem, face->size->metrics.y_ppem);
                        }
                    }

                    // Select Unicode charmap
                    FT_Error cmErr = FT_Select_Charmap(face, FT_ENCODING_UNICODE);
                    if (cmErr) {
                        ywarn("EmojiAtlas: failed to select Unicode charmap: {}", cmErr);
                    } else {
                        ydebug("EmojiAtlas: selected Unicode charmap");
                    }

                    // Test if we can find and load 😀 (U+1F600)
                    FT_UInt testIdx = FT_Get_Char_Index(face, 0x1F600);
                    yinfo("EmojiAtlas: test glyph U+1F600 -> index {}", testIdx);

                    // Test with FRESH FreeType instance to isolate the problem
                    yinfo("EmojiAtlas: Testing with fresh FreeType instance...");
                    {
                        FT_Library freshLib;
                        FT_Face freshFace;
                        FT_Error err;

                        err = FT_Init_FreeType(&freshLib);
                        yinfo("EmojiAtlas: Fresh FT_Init_FreeType: {}", err);

                        err = FT_New_Face(freshLib, emojiFontPath_.c_str(), 0, &freshFace);
                        yinfo("EmojiAtlas: Fresh FT_New_Face: {}", err);

                        err = FT_Select_Size(freshFace, 0);
                        yinfo("EmojiAtlas: Fresh FT_Select_Size: {}", err);

                        err = FT_Select_Charmap(freshFace, FT_ENCODING_UNICODE);
                        yinfo("EmojiAtlas: Fresh FT_Select_Charmap: {}", err);

                        FT_UInt freshIdx = FT_Get_Char_Index(freshFace, 0x1F600);
                        yinfo("EmojiAtlas: Fresh glyph index: {}", freshIdx);

                        err = FT_Load_Glyph(freshFace, freshIdx, FT_LOAD_DEFAULT | FT_LOAD_COLOR);
                        yinfo("EmojiAtlas: Fresh FT_Load_Glyph: {} (bitmap={}x{} mode={})",
                                     err, freshFace->glyph->bitmap.width, freshFace->glyph->bitmap.rows,
                                     freshFace->glyph->bitmap.pixel_mode);

                        FT_Done_Face(freshFace);
                        FT_Done_FreeType(freshLib);
                    }

                    // Test load with our face
                    yinfo("EmojiAtlas: about to call FT_Load_Glyph with our face={} idx={} flags=0x{:X}",
                                 (void*)face, testIdx, FT_LOAD_DEFAULT | FT_LOAD_COLOR);
                    yinfo("EmojiAtlas: face->size={} face->glyph={}",
                                 (void*)face->size, (void*)face->glyph);

                    FT_Error testErr = FT_Load_Glyph(face, testIdx, FT_LOAD_DEFAULT | FT_LOAD_COLOR);
                    yinfo("EmojiAtlas: Our FT_Load_Glyph(DEFAULT|COLOR) returned {}", testErr);
                    if (testErr) {
                        ywarn("EmojiAtlas: TEST FT_Load_Glyph FAILED: {}", testErr);
                    } else {
                        yinfo("EmojiAtlas: TEST FT_Load_Glyph OK! format={} bitmap={}x{} mode={}",
                                     face->glyph->format == FT_GLYPH_FORMAT_BITMAP ? "BITMAP" : "OTHER",
                                     face->glyph->bitmap.width, face->glyph->bitmap.rows,
                                     face->glyph->bitmap.pixel_mode);
                    }

                    FcPatternDestroy(match);
                    FcConfigDestroy(config);
                    return Ok();
                }
            }
            FcPatternDestroy(match);
        }
    }

    FcConfigDestroy(config);
    return Err<void>("No suitable emoji font found");
}

Result<void> EmojiAtlas::loadCommonEmojis() noexcept {
    yinfo("EmojiAtlas: loading {} common emojis", COMMON_EMOJI_COUNT);

    int loaded = 0;
    for (size_t i = 0; i < COMMON_EMOJI_COUNT; ++i) {
        if (auto res = loadEmoji(COMMON_EMOJIS[i]); res && *res >= 0) {
            loaded++;
        }
    }

    yinfo("EmojiAtlas: loaded {}/{} common emojis", loaded, COMMON_EMOJI_COUNT);

    // Upload to GPU
    return uploadToGPU();
}

Result<int> EmojiAtlas::loadEmoji(uint32_t codepoint) noexcept {
    // Check if already loaded
    auto it = codepointToIndex_.find(codepoint);
    if (it != codepointToIndex_.end()) {
        return Ok(it->second);
    }

    FT_Face face = static_cast<FT_Face>(ftFace_);
    if (!face) {
        return Err<int>("No emoji font loaded");
    }

    // Log face pointer for debugging
    static bool firstCall = true;
    if (firstCall) {
        ydebug("EmojiAtlas::loadEmoji first call: face={} num_glyphs={} charmap={} num_fixed_sizes={}",
                      (void*)face, face->num_glyphs, (void*)face->charmap, face->num_fixed_sizes);
        // Log available fixed sizes for bitmap fonts
        for (int i = 0; i < face->num_fixed_sizes; i++) {
            ydebug("EmojiAtlas: fixed size[{}]: {}x{} (ppem={})",
                          i, face->available_sizes[i].width, face->available_sizes[i].height,
                          face->available_sizes[i].y_ppem >> 6);
        }
        firstCall = false;
    }

    // Get glyph index (charmap should already be set from findEmojiFont)
    FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);

    // Log first few lookups
    static int logCount = 0;
    if (logCount < 5) {
        ydebug("EmojiAtlas: U+{:04X} -> glyph {}", codepoint, glyphIndex);
        logCount++;
    }

    if (glyphIndex == 0) {
        // Glyph not found in font
        return Ok(-1);
    }

    // Load glyph with color support
    FT_Error error = 0;
    error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT | FT_LOAD_COLOR);
    if (error) {
        ydebug("EmojiAtlas: FT_Load_Glyph (DEFAULT|COLOR) failed: {}", error);
        // Try without COLOR flag as fallback
        error = FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT);
        if (error) {
            ydebug("EmojiAtlas: FT_Load_Glyph (DEFAULT) also failed: {}", error);
            return Err<int>("Failed to load glyph");
        }
    }

    FT_GlyphSlot slot = face->glyph;

    // Log glyph info for debugging
    static int loadLogCount = 0;
    if (loadLogCount < 5) {
        const char* formatStr = "UNKNOWN";
        if (slot->format == FT_GLYPH_FORMAT_BITMAP) formatStr = "BITMAP";
        else if (slot->format == FT_GLYPH_FORMAT_OUTLINE) formatStr = "OUTLINE";
        ydebug("EmojiAtlas: glyph {} loaded: format={} bitmap={}x{} pixel_mode={}",
                      glyphIndex, formatStr, slot->bitmap.width, slot->bitmap.rows, slot->bitmap.pixel_mode);
        loadLogCount++;
    }

    // Check if we got a color bitmap (CBDT gives FT_PIXEL_MODE_BGRA)
    bool isColorBitmap = (slot->format == FT_GLYPH_FORMAT_BITMAP &&
                          slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA);

    // If not a bitmap yet, try to render it
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
        if (error) {
            ydebug("EmojiAtlas: FT_Render_Glyph failed: {}", error);
            return Err<int>("Failed to render glyph");
        }
    }

    FT_Bitmap& bitmap = slot->bitmap;

    // Log bitmap info for first few
    static int bitmapLogCount = 0;
    if (bitmapLogCount < 5) {
        ydebug("EmojiAtlas: glyph {} final bitmap: {}x{} pitch={} mode={}",
                      glyphIndex, bitmap.width, bitmap.rows, bitmap.pitch, bitmap.pixel_mode);
        bitmapLogCount++;
    }

    // Skip empty bitmaps
    if (bitmap.width == 0 || bitmap.rows == 0) {
        return Ok(-1);  // No bitmap available for this glyph
    }

    // Check if we have space in the atlas
    if (nextGlyphY_ * glyphSize_ + glyphSize_ > atlasSize_) {
        return Err<int>("Emoji atlas is full");
    }

    // Calculate position in atlas
    int atlasX = nextGlyphX_ * glyphSize_;
    int atlasY = nextGlyphY_ * glyphSize_;

    // Copy bitmap to atlas
    int srcWidth = bitmap.width;
    int srcHeight = bitmap.rows;

    // Center the glyph in its cell
    int offsetX = (glyphSize_ - srcWidth) / 2;
    int offsetY = (glyphSize_ - srcHeight) / 2;
    offsetX = std::max(0, offsetX);
    offsetY = std::max(0, offsetY);

    for (int y = 0; y < srcHeight && y + offsetY < static_cast<int>(glyphSize_); ++y) {
        for (int x = 0; x < srcWidth && x + offsetX < static_cast<int>(glyphSize_); ++x) {
            int dstX = atlasX + offsetX + x;
            int dstY = atlasY + offsetY + y;
            int dstIdx = (dstY * atlasSize_ + dstX) * 4;

            if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
                // Color bitmap (BGRA -> RGBA)
                int srcIdx = y * bitmap.pitch + x * 4;
                atlasData_[dstIdx + 0] = bitmap.buffer[srcIdx + 2]; // R
                atlasData_[dstIdx + 1] = bitmap.buffer[srcIdx + 1]; // G
                atlasData_[dstIdx + 2] = bitmap.buffer[srcIdx + 0]; // B
                atlasData_[dstIdx + 3] = bitmap.buffer[srcIdx + 3]; // A
            } else if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
                // Grayscale bitmap - render as white with alpha
                int srcIdx = y * bitmap.pitch + x;
                uint8_t alpha = bitmap.buffer[srcIdx];
                atlasData_[dstIdx + 0] = 255; // R
                atlasData_[dstIdx + 1] = 255; // G
                atlasData_[dstIdx + 2] = 255; // B
                atlasData_[dstIdx + 3] = alpha; // A
            }
        }
    }

    // Create metadata
    EmojiGlyphMetadata meta;
    meta.uvMinX = static_cast<float>(atlasX) / atlasSize_;
    meta.uvMinY = static_cast<float>(atlasY) / atlasSize_;
    meta.uvMaxX = static_cast<float>(atlasX + glyphSize_) / atlasSize_;
    meta.uvMaxY = static_cast<float>(atlasY + glyphSize_) / atlasSize_;
    meta.width = static_cast<float>(glyphSize_);
    meta.height = static_cast<float>(glyphSize_);
    meta._pad1 = 0;
    meta._pad2 = 0;

    int index = static_cast<int>(emojiMetadata_.size());
    emojiMetadata_.push_back(meta);
    codepointToIndex_[codepoint] = index;

    // Move to next position
    nextGlyphX_++;
    if (nextGlyphX_ >= static_cast<int>(glyphsPerRow_)) {
        nextGlyphX_ = 0;
        nextGlyphY_++;
    }

    needsUpload_ = true;
    return Ok(index);
}

int EmojiAtlas::getEmojiIndex(uint32_t codepoint) const noexcept {
    auto it = codepointToIndex_.find(codepoint);
    return (it != codepointToIndex_.end()) ? it->second : -1;
}

bool EmojiAtlas::hasEmoji(uint32_t codepoint) const noexcept {
    return codepointToIndex_.find(codepoint) != codepointToIndex_.end();
}

Result<void> EmojiAtlas::uploadToGPU() noexcept {
    if (!needsUpload_ && gpuResourcesCreated_) {
        return Ok();
    }

    if (!gpuResourcesCreated_) {
        if (auto res = createGPUResources(); !res) {
            return res;
        }
    }

    // Upload atlas texture data
    WGPUQueue queue = wgpuDeviceGetQueue(device_);

    WGPUTexelCopyTextureInfo destInfo = {};
    destInfo.texture = texture_;
    destInfo.mipLevel = 0;
    destInfo.origin = {0, 0, 0};
    destInfo.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout srcLayout = {};
    srcLayout.offset = 0;
    srcLayout.bytesPerRow = atlasSize_ * 4;
    srcLayout.rowsPerImage = atlasSize_;

    WGPUExtent3D extent = {atlasSize_, atlasSize_, 1};

    wgpuQueueWriteTexture(queue, &destInfo, atlasData_.data(),
                          atlasData_.size(), &srcLayout, &extent);

    // Upload metadata buffer
    if (!emojiMetadata_.empty()) {
        wgpuQueueWriteBuffer(queue, metadataBuffer_, 0,
                             emojiMetadata_.data(),
                             emojiMetadata_.size() * sizeof(EmojiGlyphMetadata));
    }

    needsUpload_ = false;
    ydebug("EmojiAtlas: uploaded {} emojis to GPU", emojiMetadata_.size());

    return Ok();
}

Result<void> EmojiAtlas::createGPUResources() noexcept {
    // Create texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = WGPU_STR("emoji atlas");
    texDesc.size = {atlasSize_, atlasSize_, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    texture_ = wgpuDeviceCreateTexture(device_, &texDesc);
    if (!texture_) {
        return Err<void>("Failed to create emoji atlas texture");
    }

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
    if (!textureView_) {
        return Err<void>("Failed to create emoji atlas texture view");
    }

    // Create sampler (linear filtering for smooth emoji scaling)
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.label = WGPU_STR("emoji sampler");
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.maxAnisotropy = 1;

    sampler_ = wgpuDeviceCreateSampler(device_, &samplerDesc);
    if (!sampler_) {
        return Err<void>("Failed to create emoji sampler");
    }

    // Create metadata buffer (pre-allocate for max emojis)
    uint32_t maxEmojis = glyphsPerRow_ * glyphsPerRow_;
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.label = WGPU_STR("emoji metadata");
    bufDesc.size = maxEmojis * sizeof(EmojiGlyphMetadata);
    bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;

    metadataBuffer_ = wgpuDeviceCreateBuffer(device_, &bufDesc);
    if (!metadataBuffer_) {
        return Err<void>("Failed to create emoji metadata buffer");
    }

    gpuResourcesCreated_ = true;
    yinfo("EmojiAtlas: created GPU resources ({}x{} texture, {} max emojis)",
                 atlasSize_, atlasSize_, maxEmojis);

    return Ok();
}

#endif // YETTY_EMOJI_ATLAS_STUB

} // namespace yetty
