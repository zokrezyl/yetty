#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <glm/glm.hpp>

// Forward declaration for FreeType
typedef struct FT_FaceRec_* FT_Face;

namespace yetty {

// Glyph metrics for CPU-side lookups
struct GlyphMetrics {
    // UV coordinates in atlas (normalized 0-1)
    glm::vec2 _uvMin;
    glm::vec2 _uvMax;

    // Glyph metrics in pixels
    glm::vec2 _size;           // Width and height of glyph
    glm::vec2 _bearing;        // Offset from baseline to left/top of glyph
    float _advance;            // Horizontal advance to next character
};

// GPU-friendly glyph metadata for SSBO (40 bytes, 8-byte aligned)
struct GlyphMetadataGPU {
    float _uvMinX, _uvMinY;     // 8 bytes
    float _uvMaxX, _uvMaxY;     // 8 bytes
    float _sizeX, _sizeY;       // 8 bytes
    float _bearingX, _bearingY; // 8 bytes
    float _advance;             // 4 bytes
    float _pad;                 // 4 bytes (alignment)
};

class Font {
public:
    Font();
    ~Font();

#if !YETTY_USE_PREBUILT_ATLAS
    // Generate MSDF atlas from a TTF font file (native only)
    // Automatically discovers Bold, Italic, BoldItalic variants if available
    bool generate(const std::string& fontPath, float fontSize, uint32_t atlasSize = 4096);

    // Generate MSDF atlas with explicit variant paths
    bool generate(const std::string& regularPath,
                  const std::string& boldPath,
                  const std::string& italicPath,
                  const std::string& boldItalicPath,
                  float fontSize, uint32_t atlasSize = 4096);

    // Generate MSDF atlas from an existing FreeType face (for embedded fonts)
    // WARNING: This may cause issues with MuPDF-owned FT_Face due to lock callbacks
    // Prefer using generate(data, dataLen, fontName, ...) instead
    bool generate(FT_Face face, float fontSize, uint32_t atlasSize = 4096);

    // Generate MSDF atlas from an existing FreeType face with explicit font name
    bool generate(FT_Face face, const std::string& fontName, float fontSize, uint32_t atlasSize = 4096);

    // Generate MSDF atlas from raw font data (TTF/OTF bytes)
    // This is the safest way to load embedded fonts (avoids FreeType lock issues)
    bool generate(const unsigned char* data, size_t dataLen,
                  const std::string& fontName, float fontSize, uint32_t atlasSize = 4096);

    // Save atlas to PNG and metrics to JSON
    bool saveAtlas(const std::string& atlasPath, const std::string& metricsPath) const;
#endif

    // Load pre-generated atlas and metrics (works on all platforms)
    bool loadAtlas(const std::string& atlasPath, const std::string& metricsPath);

    // Create WebGPU texture from atlas
    bool createTexture(WGPUDevice device, WGPUQueue queue);

    // Create glyph metadata SSBO buffer
    bool createGlyphMetadataBuffer(WGPUDevice device);

    // Font style flags
    enum Style : uint8_t {
        Regular = 0,
        Bold = 1,
        Italic = 2,
        BoldItalic = 3  // Bold | Italic
    };

    // Get glyph index for a codepoint (for Grid)
    // On native builds, tries to load missing glyphs from fallback fonts
    uint16_t getGlyphIndex(uint32_t codepoint);

    // Get glyph index with style (bold/italic)
    // Falls back to regular style if variant not available
    uint16_t getGlyphIndex(uint32_t codepoint, Style style);
    uint16_t getGlyphIndex(uint32_t codepoint, bool bold, bool italic);

    // Get glyph metrics for a codepoint (for CPU-side calculations)
    const GlyphMetrics* getGlyph(uint32_t codepoint) const;

    // Atlas properties
    uint32_t getAtlasWidth() const { return _atlasWidth; }
    uint32_t getAtlasHeight() const { return _atlasHeight; }
    float getFontSize() const { return _fontSize; }
    float getLineHeight() const { return _lineHeight; }

    WGPUTexture getTexture() const { return _texture; }
    WGPUTextureView getTextureView() const { return _textureView; }
    WGPUSampler getSampler() const { return _sampler; }

    // Glyph metadata buffer for shader access
    WGPUBuffer getGlyphMetadataBuffer() const { return _glyphMetadataBuffer; }
    uint32_t getGlyphCount() const { return static_cast<uint32_t>(_glyphMetadata.size()); }

    // Get pixel range for MSDF shader
    float getPixelRange() const { return _pixelRange; }

#if !YETTY_USE_PREBUILT_ATLAS
    // Request loading of a missing glyph (will use fontconfig for fallback)
    // Returns true if glyph was loaded, false if not available
    bool loadMissingGlyph(uint32_t codepoint);

    // Check if there are pending glyphs that need GPU upload
    bool hasPendingGlyphs() const { return !_pendingGlyphs.empty(); }

    // Upload pending glyphs to GPU (call after loadMissingGlyph)
    bool uploadPendingGlyphs(WGPUDevice device, WGPUQueue queue);
#endif

private:
    // Build the codepoint→index mapping and GPU metadata array
    void buildGlyphIndexMap();

#if !YETTY_USE_PREBUILT_ATLAS
    // Find font files that contain the given codepoint using fontconfig
    // Returns multiple candidates in priority order
    std::vector<std::string> findFontsForCodepoint(uint32_t codepoint);

    // Load a single glyph from a font file into the atlas
    bool loadGlyphFromFont(const std::string& fontPath, uint32_t codepoint);

    // Track which fallback fonts are already loaded
    std::unordered_map<std::string, void*> _fallbackFonts;  // path → FontHandle*

    // Pending glyphs that need to be uploaded to GPU
    std::set<uint32_t> _pendingGlyphs;

    // Track failed codepoints to avoid repeated lookups
    std::set<uint32_t> _failedCodepoints;

    // Shelf packer state for dynamic glyph addition
    int _shelfX = 0;
    int _shelfY = 0;
    int _shelfHeight = 0;
    int _atlasPadding = 2;

    // FreeType handle (kept alive for fallback loading)
    void* _freetypeHandle = nullptr;
    void* _primaryFont = nullptr;

    // Font variant handles
    void* _boldFont = nullptr;
    void* _italicFont = nullptr;
    void* _boldItalicFont = nullptr;

    // Helper to generate glyphs from a font into a specific glyph map
    bool generateGlyphsFromFont(void* fontHandle, Style style,
                                const std::vector<uint32_t>& charset,
                                double fontScale, int& shelfX, int& shelfY, int& shelfHeight);
#endif

    // Regular font glyphs and index map
    std::unordered_map<uint32_t, GlyphMetrics> _glyphs;        // codepoint → metrics
    std::unordered_map<uint32_t, uint16_t> _codepointToIndex;  // codepoint → glyph index

    // Font variant glyphs and index maps (Bold, Italic, BoldItalic)
    std::unordered_map<uint32_t, GlyphMetrics> _boldGlyphs;
    std::unordered_map<uint32_t, GlyphMetrics> _italicGlyphs;
    std::unordered_map<uint32_t, GlyphMetrics> _boldItalicGlyphs;
    std::unordered_map<uint32_t, uint16_t> _boldCodepointToIndex;
    std::unordered_map<uint32_t, uint16_t> _italicCodepointToIndex;
    std::unordered_map<uint32_t, uint16_t> _boldItalicCodepointToIndex;

    std::vector<GlyphMetadataGPU> _glyphMetadata;              // GPU metadata array
    std::vector<uint8_t> _atlasData;

    uint32_t _atlasWidth = 0;
    uint32_t _atlasHeight = 0;
    float _fontSize = 0.0f;
    float _lineHeight = 0.0f;
    float _pixelRange = 4.0f;  // MSDF pixel range (higher = better AA quality)

    WGPUTexture _texture = nullptr;
    WGPUTextureView _textureView = nullptr;
    WGPUSampler _sampler = nullptr;
    WGPUBuffer _glyphMetadataBuffer = nullptr;
};

} // namespace yetty
