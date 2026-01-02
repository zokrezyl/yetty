#pragma once

#include <yetty/result.hpp>
#include <yetty/webgpu-context.h>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

namespace yetty {

// Emoji glyph metadata for GPU (matches shader struct)
struct EmojiGlyphMetadata {
    float uvMinX, uvMinY;  // UV coordinates in atlas
    float uvMaxX, uvMaxY;
    float width, height;   // Glyph size in pixels (at atlas resolution)
    float _pad1, _pad2;    // Padding for alignment
};

/**
 * EmojiAtlas manages a color (RGBA) texture atlas for emoji rendering.
 * Unlike MSDF fonts, emojis are stored as pre-rendered bitmaps.
 */
class EmojiAtlas {
public:
    using Ptr = std::shared_ptr<EmojiAtlas>;

    static Result<Ptr> create(WGPUDevice device, uint32_t glyphSize = 64) noexcept;

    ~EmojiAtlas();

    // Non-copyable
    EmojiAtlas(const EmojiAtlas&) = delete;
    EmojiAtlas& operator=(const EmojiAtlas&) = delete;

    // Load common emojis from system emoji font
    Result<void> loadCommonEmojis() noexcept;

    // Load a specific emoji (for lazy loading)
    Result<int> loadEmoji(uint32_t codepoint) noexcept;

    // Get emoji index (returns -1 if not loaded)
    int getEmojiIndex(uint32_t codepoint) const noexcept;

    // Check if an emoji is loaded
    bool hasEmoji(uint32_t codepoint) const noexcept;

    // GPU resources
    WGPUTextureView getTextureView() const noexcept { return textureView_; }
    WGPUSampler getSampler() const noexcept { return sampler_; }
    WGPUBuffer getMetadataBuffer() const noexcept { return metadataBuffer_; }

    // Atlas info
    uint32_t getAtlasSize() const noexcept { return atlasSize_; }
    uint32_t getGlyphSize() const noexcept { return glyphSize_; }
    uint32_t getGlyphCount() const noexcept { return static_cast<uint32_t>(emojiMetadata_.size()); }

    // Create/update GPU resources after loading
    Result<void> uploadToGPU() noexcept;

private:
    EmojiAtlas(WGPUDevice device, uint32_t glyphSize) noexcept;

    Result<void> init() noexcept;
    Result<void> findEmojiFont() noexcept;
    Result<void> createGPUResources() noexcept;

    // Render a single emoji glyph to the atlas
    Result<void> renderEmoji(uint32_t codepoint, int atlasX, int atlasY) noexcept;

    WGPUDevice device_ = nullptr;

    // FreeType handles
    void* ftLibrary_ = nullptr;
    void* ftFace_ = nullptr;
    std::string emojiFontPath_;

    // Atlas data
    uint32_t glyphSize_ = 64;      // Size of each emoji glyph
    uint32_t atlasSize_ = 1024;    // Atlas texture size (1024x1024 = 16x16 grid of 64px emojis)
    uint32_t glyphsPerRow_ = 16;   // atlasSize / glyphSize
    std::vector<uint8_t> atlasData_; // RGBA pixel data

    // Metadata
    std::vector<EmojiGlyphMetadata> emojiMetadata_;
    std::unordered_map<uint32_t, int> codepointToIndex_; // codepoint -> metadata index

    // Current position in atlas for next glyph
    int nextGlyphX_ = 0;
    int nextGlyphY_ = 0;

    // GPU resources
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBuffer metadataBuffer_ = nullptr;
    bool gpuResourcesCreated_ = false;
    bool needsUpload_ = false;
};

} // namespace yetty
