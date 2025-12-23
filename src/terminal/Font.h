#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

namespace yetty {

// Glyph metrics for CPU-side lookups
struct GlyphMetrics {
    // UV coordinates in atlas (normalized 0-1)
    glm::vec2 uvMin;
    glm::vec2 uvMax;

    // Glyph metrics in pixels
    glm::vec2 size;           // Width and height of glyph
    glm::vec2 bearing;        // Offset from baseline to left/top of glyph
    float advance;            // Horizontal advance to next character
};

// GPU-friendly glyph metadata for SSBO (40 bytes, 8-byte aligned)
struct GlyphMetadataGPU {
    float uvMinX, uvMinY;     // 8 bytes
    float uvMaxX, uvMaxY;     // 8 bytes
    float sizeX, sizeY;       // 8 bytes
    float bearingX, bearingY; // 8 bytes
    float advance;            // 4 bytes
    float _pad;               // 4 bytes (alignment)
};

class Font {
public:
    Font();
    ~Font();

#if !YETTY_USE_PREBUILT_ATLAS
    // Generate MSDF atlas from a TTF font file (native only)
    bool generate(const std::string& fontPath, float fontSize, uint32_t atlasSize = 512);

    // Save atlas to PNG and metrics to JSON
    bool saveAtlas(const std::string& atlasPath, const std::string& metricsPath) const;
#endif

    // Load pre-generated atlas and metrics (works on all platforms)
    bool loadAtlas(const std::string& atlasPath, const std::string& metricsPath);

    // Create WebGPU texture from atlas
    bool createTexture(WGPUDevice device, WGPUQueue queue);

    // Create glyph metadata SSBO buffer
    bool createGlyphMetadataBuffer(WGPUDevice device);

    // Get glyph index for a codepoint (for Grid)
    uint16_t getGlyphIndex(uint32_t codepoint) const;

    // Get glyph metrics for a codepoint (for CPU-side calculations)
    const GlyphMetrics* getGlyph(uint32_t codepoint) const;

    // Atlas properties
    uint32_t getAtlasWidth() const { return atlasWidth_; }
    uint32_t getAtlasHeight() const { return atlasHeight_; }
    float getFontSize() const { return fontSize_; }
    float getLineHeight() const { return lineHeight_; }

    WGPUTexture getTexture() const { return texture_; }
    WGPUTextureView getTextureView() const { return textureView_; }
    WGPUSampler getSampler() const { return sampler_; }

    // Glyph metadata buffer for shader access
    WGPUBuffer getGlyphMetadataBuffer() const { return glyphMetadataBuffer_; }
    uint32_t getGlyphCount() const { return static_cast<uint32_t>(glyphMetadata_.size()); }

    // Get pixel range for MSDF shader
    float getPixelRange() const { return pixelRange_; }

private:
    // Build the codepoint→index mapping and GPU metadata array
    void buildGlyphIndexMap();

    std::unordered_map<uint32_t, GlyphMetrics> glyphs_;        // codepoint → metrics
    std::unordered_map<uint32_t, uint16_t> codepointToIndex_;  // codepoint → glyph index
    std::vector<GlyphMetadataGPU> glyphMetadata_;              // GPU metadata array
    std::vector<uint8_t> atlasData_;

    uint32_t atlasWidth_ = 0;
    uint32_t atlasHeight_ = 0;
    float fontSize_ = 0.0f;
    float lineHeight_ = 0.0f;
    float pixelRange_ = 2.0f;  // MSDF pixel range

    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUBuffer glyphMetadataBuffer_ = nullptr;
};

} // namespace yetty
