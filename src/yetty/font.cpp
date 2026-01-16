#include <yetty/font.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>

#if !YETTY_USE_PREBUILT_ATLAS
#include <ft2build.h>
#include FT_FREETYPE_H
#include <msdfgen.h>
#include <msdfgen-ext.h>
#include <fontconfig/fontconfig.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if !YETTY_USE_PREBUILT_ATLAS
#include <lz4.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace yetty {

Font::Font() = default;

Font::~Font() {
    // Buffer and sampler are safe to release on all platforms
    if (_glyphMetadataBuffer) wgpuBufferRelease(_glyphMetadataBuffer);
    if (_sampler) wgpuSamplerRelease(_sampler);
#if !YETTY_WEB
    // On web, texture/view releases cause Emscripten manager issues
    if (_textureView) wgpuTextureViewRelease(_textureView);
    if (_texture) wgpuTextureRelease(_texture);
#endif

#if !YETTY_USE_PREBUILT_ATLAS
    // Clean up fallback fonts
    for (auto& [path, handle] : _fallbackFonts) {
        if (handle) {
            msdfgen::destroyFont(static_cast<msdfgen::FontHandle*>(handle));
        }
    }
    _fallbackFonts.clear();

    // Clean up primary font
    if (_primaryFont) {
        msdfgen::destroyFont(static_cast<msdfgen::FontHandle*>(_primaryFont));
        _primaryFont = nullptr;
    }

    // Clean up variant fonts
    if (_boldFont) {
        msdfgen::destroyFont(static_cast<msdfgen::FontHandle*>(_boldFont));
        _boldFont = nullptr;
    }
    if (_italicFont) {
        msdfgen::destroyFont(static_cast<msdfgen::FontHandle*>(_italicFont));
        _italicFont = nullptr;
    }
    if (_boldItalicFont) {
        msdfgen::destroyFont(static_cast<msdfgen::FontHandle*>(_boldItalicFont));
        _boldItalicFont = nullptr;
    }

    // Clean up FreeType handle
    if (_freetypeHandle) {
        msdfgen::deinitializeFreetype(static_cast<msdfgen::FreetypeHandle*>(_freetypeHandle));
        _freetypeHandle = nullptr;
    }
#endif
}

#if !YETTY_USE_PREBUILT_ATLAS

// Simple rectangle packing for atlas
struct PackedGlyph {
    uint32_t codepoint;
    Font::Style style;  // Which font variant this glyph is from
    double fontScale;   // Scale factor used for this glyph's font variant
    int atlasX, atlasY;
    int atlasW, atlasH;
    double advance;
    double bearingX, bearingY;
    double sizeX, sizeY;
    double boundsL, boundsB, boundsR, boundsT;  // Raw bounds in font units
    msdfgen::Shape shape;
};

// Simple shelf-based atlas packer
class ShelfPacker {
public:
    ShelfPacker(int width, int height, int padding = 2)
        : width_(width), height_(height), padding_(padding),
          _shelfX(padding), _shelfY(padding), _shelfHeight(0) {}

    bool pack(int w, int h, int& outX, int& outY) {
        // Add padding
        int pw = w + padding_;
        int ph = h + padding_;

        // Check if fits on current shelf
        if (_shelfX + pw > width_) {
            // Move to next shelf
            _shelfX = padding_;
            _shelfY += _shelfHeight + padding_;
            _shelfHeight = 0;
        }

        // Check if fits vertically
        if (_shelfY + ph > height_) {
            return false;  // Atlas full
        }

        outX = _shelfX;
        outY = _shelfY;
        _shelfX += pw;
        _shelfHeight = std::max(_shelfHeight, ph);
        return true;
    }

private:
    int width_, height_, padding_;
    int _shelfX, _shelfY, _shelfHeight;
};

// Helper to check if a file exists
static bool fileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

// Helper to derive variant paths from base font path
// E.g., "DejaVuSansMono.ttf" -> "DejaVuSansMono-Bold.ttf", "DejaVuSansMono-Oblique.ttf", etc.
static void discoverVariantPaths(const std::string& basePath,
                                  std::string& boldPath,
                                  std::string& italicPath,
                                  std::string& boldItalicPath) {
    // Find the base name without extension
    size_t lastSlash = basePath.find_last_of("/\\");
    size_t lastDot = basePath.find_last_of('.');

    std::string dir = (lastSlash != std::string::npos) ? basePath.substr(0, lastSlash + 1) : "";
    std::string name = (lastSlash != std::string::npos) ? basePath.substr(lastSlash + 1) : basePath;
    std::string ext = (lastDot != std::string::npos && lastDot > lastSlash) ?
                       basePath.substr(lastDot) : ".ttf";

    // Remove extension from name
    if (lastDot != std::string::npos && lastDot > lastSlash) {
        name = name.substr(0, name.find_last_of('.'));
    }

    // Try common naming patterns for font variants
    // Pattern 1: Name-Bold, Name-Italic, Name-BoldItalic
    std::vector<std::string> boldPatterns = {"-Bold", "-bold", "_Bold", "_bold", "Bold"};
    std::vector<std::string> italicPatterns = {"-Oblique", "-Italic", "-oblique", "-italic",
                                                "_Oblique", "_Italic", "Oblique", "Italic"};
    std::vector<std::string> boldItalicPatterns = {"-BoldOblique", "-BoldItalic",
                                                    "-boldoblique", "-bolditalic",
                                                    "_BoldOblique", "_BoldItalic",
                                                    "BoldOblique", "BoldItalic"};

    // Try to find bold variant
    boldPath = "";
    for (const auto& pattern : boldPatterns) {
        std::string candidate = dir + name + pattern + ext;
        if (fileExists(candidate)) {
            boldPath = candidate;
            break;
        }
    }

    // Try to find italic variant
    italicPath = "";
    for (const auto& pattern : italicPatterns) {
        std::string candidate = dir + name + pattern + ext;
        if (fileExists(candidate)) {
            italicPath = candidate;
            break;
        }
    }

    // Try to find bold-italic variant
    boldItalicPath = "";
    for (const auto& pattern : boldItalicPatterns) {
        std::string candidate = dir + name + pattern + ext;
        if (fileExists(candidate)) {
            boldItalicPath = candidate;
            break;
        }
    }
}

bool Font::generate(const std::string& fontPath, float fontSize, uint32_t atlasSize) {
    // Auto-detect variant fonts
    std::string boldPath, italicPath, boldItalicPath;
    discoverVariantPaths(fontPath, boldPath, italicPath, boldItalicPath);

    yinfo("Font variants detected:");
    yinfo("  Regular: {}", fontPath);
    yinfo("  Bold: {}", boldPath.empty() ? "(not found)" : boldPath);
    yinfo("  Italic: {}", italicPath.empty() ? "(not found)" : italicPath);
    yinfo("  BoldItalic: {}", boldItalicPath.empty() ? "(not found)" : boldItalicPath);

    return generate(fontPath, boldPath, italicPath, boldItalicPath, fontSize, atlasSize);
}

bool Font::generate(const std::string& regularPath,
                    const std::string& boldPath,
                    const std::string& italicPath,
                    const std::string& boldItalicPath,
                    float fontSize, uint32_t atlasSize) {
    _fontSize = fontSize;
    _atlasWidth = atlasSize;
    _atlasHeight = atlasSize;

    // Initialize FreeType
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        std::cerr << "Failed to initialize FreeType" << std::endl;
        return false;
    }

    // Load regular font with FreeType
    msdfgen::FontHandle* font = msdfgen::loadFont(ft, regularPath.c_str());
    if (!font) {
        std::cerr << "Failed to load font: " << regularPath << std::endl;
        msdfgen::deinitializeFreetype(ft);
        return false;
    }

    // Load variant fonts (optional - if not available, variants fall back to regular)
    msdfgen::FontHandle* boldFont = nullptr;
    msdfgen::FontHandle* italicFont = nullptr;
    msdfgen::FontHandle* boldItalicFont = nullptr;

    if (!boldPath.empty()) {
        boldFont = msdfgen::loadFont(ft, boldPath.c_str());
        if (boldFont) {
            yinfo("Loaded bold font: {}", boldPath);
        }
    }
    if (!italicPath.empty()) {
        italicFont = msdfgen::loadFont(ft, italicPath.c_str());
        if (italicFont) {
            yinfo("Loaded italic font: {}", italicPath);
        }
    }
    if (!boldItalicPath.empty()) {
        boldItalicFont = msdfgen::loadFont(ft, boldItalicPath.c_str());
        if (boldItalicFont) {
            yinfo("Loaded bold-italic font: {}", boldItalicPath);
        }
    }

    // Get font metrics from regular font
    msdfgen::FontMetrics metrics;
    msdfgen::getFontMetrics(metrics, font);
    double unitsPerEm = metrics.emSize > 0 ? metrics.emSize : (metrics.ascenderY - metrics.descenderY);
    double fontScale = fontSize / unitsPerEm;
    double lineHeight = metrics.lineHeight;

    // Define character set
    std::vector<uint32_t> charset;

    // Check if this is a Nerd Font (needs icon ranges)
    bool isNerdFont = regularPath.find("NerdFont") != std::string::npos ||
                      regularPath.find("Nerd Font") != std::string::npos ||
                      regularPath.find("nerd-font") != std::string::npos;

    // ASCII printable (32-126)
    for (uint32_t c = 32; c <= 126; ++c) charset.push_back(c);

    // Latin Extended-A (0x0100-0x017F)
    for (uint32_t c = 0x0100; c <= 0x017F; ++c) charset.push_back(c);

    // Latin Extended-B subset (0x0180-0x024F)
    for (uint32_t c = 0x0180; c <= 0x024F; ++c) charset.push_back(c);

    // General Punctuation (0x2000-0x206F)
    for (uint32_t c = 0x2000; c <= 0x206F; ++c) charset.push_back(c);

    // Only include extended ranges for Nerd Fonts
    if (!isNerdFont) {
        yinfo("Using minimal charset for non-NerdFont: {}", regularPath);
        goto charset_done;
    }

    // Arrows (0x2190-0x21FF)
    for (uint32_t c = 0x2190; c <= 0x21FF; ++c) charset.push_back(c);

    // Mathematical Operators (0x2200-0x22FF)
    for (uint32_t c = 0x2200; c <= 0x22FF; ++c) charset.push_back(c);

    // Box Drawing (0x2500-0x257F)
    for (uint32_t c = 0x2500; c <= 0x257F; ++c) charset.push_back(c);

    // Block Elements (0x2580-0x259F)
    for (uint32_t c = 0x2580; c <= 0x259F; ++c) charset.push_back(c);

    // Geometric Shapes (0x25A0-0x25FF)
    for (uint32_t c = 0x25A0; c <= 0x25FF; ++c) charset.push_back(c);

    // Miscellaneous Symbols (0x2600-0x26FF)
    for (uint32_t c = 0x2600; c <= 0x26FF; ++c) charset.push_back(c);

    // Dingbats (0x2700-0x27BF)
    for (uint32_t c = 0x2700; c <= 0x27BF; ++c) charset.push_back(c);

    // Braille Patterns (0x2800-0x28FF)
    for (uint32_t c = 0x2800; c <= 0x28FF; ++c) charset.push_back(c);

    // Powerline symbols (0xE0A0-0xE0D4)
    for (uint32_t c = 0xE0A0; c <= 0xE0D4; ++c) charset.push_back(c);

    // Nerd Fonts: Seti-UI + Custom (0xE5FA-0xE6B5)
    for (uint32_t c = 0xE5FA; c <= 0xE6B5; ++c) charset.push_back(c);

    // Nerd Fonts: Devicons (0xE700-0xE7C5)
    for (uint32_t c = 0xE700; c <= 0xE7C5; ++c) charset.push_back(c);

    // Nerd Fonts: Font Awesome (0xF000-0xF2E0)
    for (uint32_t c = 0xF000; c <= 0xF2E0; ++c) charset.push_back(c);

    // Nerd Fonts: Font Awesome Extension (0xE200-0xE2A9)
    for (uint32_t c = 0xE200; c <= 0xE2A9; ++c) charset.push_back(c);

    // Nerd Fonts: Material Design Icons (0xF0001-0xF1AF0) - subset for common icons
    for (uint32_t c = 0xF0001; c <= 0xF1AF0; ++c) charset.push_back(c);

    // Nerd Fonts: Weather Icons (0xE300-0xE3E3)
    for (uint32_t c = 0xE300; c <= 0xE3E3; ++c) charset.push_back(c);

    // Nerd Fonts: Octicons (0xF400-0xF532)
    for (uint32_t c = 0xF400; c <= 0xF532; ++c) charset.push_back(c);

    // Nerd Fonts: Pomicons (0xE000-0xE00A)
    for (uint32_t c = 0xE000; c <= 0xE00A; ++c) charset.push_back(c);

    // Nerd Fonts: IEC Power Symbols (0x23FB-0x23FE, 0x2B58)
    for (uint32_t c = 0x23FB; c <= 0x23FE; ++c) charset.push_back(c);
    charset.push_back(0x2B58);

    // Nerd Fonts: Font Logos (0xF300-0xF372)
    for (uint32_t c = 0xF300; c <= 0xF372; ++c) charset.push_back(c);

    // Nerd Fonts: Codicons (0xEA60-0xEBEB)
    for (uint32_t c = 0xEA60; c <= 0xEBEB; ++c) charset.push_back(c);

charset_done:
    yinfo("Charset size: {} codepoints (isNerdFont={})", charset.size(), isNerdFont);

    // Helper lambda to load glyphs from a font variant
    auto loadGlyphsFromFont = [&](msdfgen::FontHandle* variantFont, Style style,
                                   std::vector<PackedGlyph>& glyphVec) {
        if (!variantFont) return;

        // Get font metrics for this variant (for proper scaling)
        msdfgen::FontMetrics variantMetrics;
        msdfgen::getFontMetrics(variantMetrics, variantFont);
        double variantUnitsPerEm = variantMetrics.emSize > 0 ?
            variantMetrics.emSize : (variantMetrics.ascenderY - variantMetrics.descenderY);
        double variantScale = fontSize / variantUnitsPerEm;

        for (uint32_t codepoint : charset) {
            PackedGlyph pg;
            pg.codepoint = codepoint;
            pg.style = style;
            pg.fontScale = variantScale;

            double advance;
            if (!msdfgen::loadGlyph(pg.shape, variantFont, codepoint, &advance)) continue;

            pg.advance = advance * variantScale;

            // Get shape bounds
            if (!pg.shape.contours.empty()) {
                pg.shape.normalize();
                msdfgen::Shape::Bounds bounds = pg.shape.getBounds();

                // Store raw bounds in font units (will be used for transform)
                pg.boundsL = bounds.l;
                pg.boundsB = bounds.b;
                pg.boundsR = bounds.r;
                pg.boundsT = bounds.t;

                // Scale bounds to target size
                pg.bearingX = bounds.l * variantScale;
                pg.bearingY = bounds.t * variantScale;  // Top of glyph from baseline
                pg.sizeX = (bounds.r - bounds.l) * variantScale;
                pg.sizeY = (bounds.t - bounds.b) * variantScale;

                // Calculate atlas size needed (with padding for SDF range)
                int padding = static_cast<int>(std::ceil(_pixelRange));
                pg.atlasW = static_cast<int>(std::ceil(pg.sizeX)) + padding * 2;
                pg.atlasH = static_cast<int>(std::ceil(pg.sizeY)) + padding * 2;
            } else {
                // Empty glyph (like space)
                pg.boundsL = pg.boundsB = pg.boundsR = pg.boundsT = 0;
                pg.bearingX = pg.bearingY = 0;
                pg.sizeX = pg.sizeY = 0;
                pg.atlasW = pg.atlasH = 0;
            }

            glyphVec.push_back(std::move(pg));
        }
    };

    // Load glyph shapes from all font variants
    std::vector<PackedGlyph> glyphs;

    // Load regular glyphs
    loadGlyphsFromFont(font, Regular, glyphs);
    yinfo("Loaded {} regular glyphs", glyphs.size());

    // Load variant glyphs (only if the variant font was loaded)
    size_t regularCount = glyphs.size();
    if (boldFont) {
        loadGlyphsFromFont(boldFont, Bold, glyphs);
        yinfo("Loaded {} bold glyphs", glyphs.size() - regularCount);
    }

    size_t afterBold = glyphs.size();
    if (italicFont) {
        loadGlyphsFromFont(italicFont, Italic, glyphs);
        yinfo("Loaded {} italic glyphs", glyphs.size() - afterBold);
    }

    size_t afterItalic = glyphs.size();
    if (boldItalicFont) {
        loadGlyphsFromFont(boldItalicFont, BoldItalic, glyphs);
        yinfo("Loaded {} bold-italic glyphs", glyphs.size() - afterItalic);
    }

    std::cout << "Loaded " << glyphs.size() << " total glyphs from all font variants" << std::endl;

    // Calculate required atlas height based on total glyph area
    uint64_t totalArea = 0;
    int maxGlyphHeight = 0;
    for (const auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            totalArea += static_cast<uint64_t>(glyph.atlasW) * glyph.atlasH;
            maxGlyphHeight = std::max(maxGlyphHeight, glyph.atlasH);
        }
    }
    // Add 30% padding for packing inefficiency, round up to 512
    uint32_t estimatedHeight = static_cast<uint32_t>((totalArea * 13 / 10) / _atlasWidth);
    estimatedHeight = std::max(estimatedHeight, static_cast<uint32_t>(maxGlyphHeight + 64));
    estimatedHeight = ((estimatedHeight + 511) / 512) * 512;
    _atlasHeight = std::max(_atlasHeight, estimatedHeight);
    yinfo("Dynamic atlas size: {}x{} for {} glyphs (total area: {} pixels)",
          _atlasWidth, _atlasHeight, glyphs.size(), totalArea);

    // Sort by height for better packing
    std::sort(glyphs.begin(), glyphs.end(), [](const PackedGlyph& a, const PackedGlyph& b) {
        return a.atlasH > b.atlasH;
    });

    // Pack glyphs into atlas
    ShelfPacker packer(_atlasWidth, _atlasHeight);
    for (auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            if (!packer.pack(glyph.atlasW, glyph.atlasH, glyph.atlasX, glyph.atlasY)) {
                std::cerr << "Atlas full, could not pack glyph " << glyph.codepoint << std::endl;
                continue;
            }
        } else {
            glyph.atlasX = glyph.atlasY = 0;
        }
    }

    // Create atlas bitmap (RGBA)
    _atlasData.resize(_atlasWidth * _atlasHeight * 4, 0);

    // Generate MSDF for each glyph
    int padding = static_cast<int>(std::ceil(_pixelRange));

    for (auto& glyph : glyphs) {
        if (glyph.shape.contours.empty()) continue;

        // Validate and prepare shape
        if (!glyph.shape.validate()) {
            std::cerr << "Warning: Invalid shape for codepoint " << glyph.codepoint << std::endl;
        }

        // Normalize shape and apply edge coloring
        glyph.shape.normalize();
        msdfgen::edgeColoringSimple(glyph.shape, 3.0);

        double scale = glyph.fontScale;  // Use per-glyph scale for variants

        // Create MSDF bitmap for this glyph
        msdfgen::Bitmap<float, 3> msdf(glyph.atlasW, glyph.atlasH);

        // Calculate transformation: shape coords -> bitmap coords
        // Position shape so that (boundsL, boundsB) maps to (padding, padding)
        msdfgen::Vector2 translate(
            padding - glyph.boundsL * scale,
            padding - glyph.boundsB * scale
        );

        msdfgen::generateMSDF(msdf, glyph.shape, _pixelRange, scale, translate);

        // Copy to atlas with Y-flip (TrueType Y-up -> bitmap/WebGPU Y-down)
        for (int y = 0; y < glyph.atlasH; ++y) {
            for (int x = 0; x < glyph.atlasW; ++x) {
                int atlasX = glyph.atlasX + x;
                int atlasY = glyph.atlasY + (glyph.atlasH - 1 - y);  // Flip Y

                if (atlasX >= 0 && atlasX < (int)_atlasWidth &&
                    atlasY >= 0 && atlasY < (int)_atlasHeight) {
                    size_t idx = (atlasY * _atlasWidth + atlasX) * 4;
                    _atlasData[idx + 0] = static_cast<uint8_t>(std::clamp(msdf(x, y)[0] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 1] = static_cast<uint8_t>(std::clamp(msdf(x, y)[1] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 2] = static_cast<uint8_t>(std::clamp(msdf(x, y)[2] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 3] = 255;
                }
            }
        }
    }

    // Calculate line height
    _lineHeight = static_cast<float>(lineHeight * fontScale);

    // Keep FreeType and fonts alive for fallback loading
    _freetypeHandle = ft;
    _primaryFont = font;
    _boldFont = boldFont;
    _italicFont = italicFont;
    _boldItalicFont = boldItalicFont;

    // Track shelf packer state for dynamic glyph addition
    // We need to find the final position after packing
    _shelfX = _atlasPadding;
    _shelfY = _atlasPadding;
    _shelfHeight = 0;
    for (const auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            // Track maximum Y position used
            int glyphBottom = glyph.atlasY + glyph.atlasH;
            if (glyph.atlasX + glyph.atlasW > _shelfX) {
                _shelfX = glyph.atlasX + glyph.atlasW + _atlasPadding;
            }
            if (glyphBottom > _shelfY) {
                // This glyph is on a new shelf
                _shelfY = glyph.atlasY;
                _shelfHeight = glyph.atlasH;
            }
        }
    }

    // Note: We no longer cleanup FreeType here - done in destructor
    // msdfgen::destroyFont(font);
    // msdfgen::deinitializeFreetype(ft);

    // Extract glyph metrics and store in appropriate maps based on style
    for (const auto& glyph : glyphs) {
        GlyphMetrics m;

        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            // UV coordinates
            m._uvMin = glm::vec2(
                static_cast<float>(glyph.atlasX) / _atlasWidth,
                static_cast<float>(glyph.atlasY) / _atlasHeight
            );
            m._uvMax = glm::vec2(
                static_cast<float>(glyph.atlasX + glyph.atlasW) / _atlasWidth,
                static_cast<float>(glyph.atlasY + glyph.atlasH) / _atlasHeight
            );
        } else {
            m._uvMin = m._uvMax = glm::vec2(0);
        }

        m._size = glm::vec2(static_cast<float>(glyph.atlasW), static_cast<float>(glyph.atlasH));

        // Bearing: offset from cursor position to glyph quad
        // bearingX: horizontal offset (left side of glyph minus padding)
        // bearingY: vertical offset from baseline to TOP of glyph quad
        //           In screen coords (Y-down), positive bearingY means glyph extends above baseline
        m._bearing = glm::vec2(
            static_cast<float>(glyph.bearingX - padding),                    // Left edge offset
            static_cast<float>(glyph.boundsT * glyph.fontScale + padding)    // Top of glyph from baseline
        );
        m._advance = static_cast<float>(glyph.advance);

        // Store in appropriate map based on style
        switch (glyph.style) {
            case Regular:
                _glyphs[glyph.codepoint] = m;
                break;
            case Bold:
                _boldGlyphs[glyph.codepoint] = m;
                break;
            case Italic:
                _italicGlyphs[glyph.codepoint] = m;
                break;
            case BoldItalic:
                _boldItalicGlyphs[glyph.codepoint] = m;
                break;
        }
    }

    std::cout << "Generated MSDF atlas: " << _atlasWidth << "x" << _atlasHeight
              << " with " << _glyphs.size() << " regular, "
              << _boldGlyphs.size() << " bold, "
              << _italicGlyphs.size() << " italic, "
              << _boldItalicGlyphs.size() << " bold-italic glyphs" << std::endl;

    // Build codepoint→index mapping
    buildGlyphIndexMap();

    return true;
}

bool Font::generate(FT_Face face, float fontSize, uint32_t atlasSize) {
    if (!face) {
        yerror("Font::generate(FT_Face): null FT_Face");
        return false;
    }

    // Validate the FT_Face has basic required data
    if (!face->num_glyphs || face->num_glyphs < 1) {
        yerror("Font::generate(FT_Face): FT_Face has no glyphs");
        return false;
    }

    std::string fontFamily = face->family_name ? face->family_name : "unknown";
    std::string fontStyle = face->style_name ? face->style_name : "Regular";
    yinfo("Font::generate(FT_Face): generating atlas for '{}' {} (num_glyphs={}, units_per_EM={})",
                 fontFamily, fontStyle, face->num_glyphs, face->units_per_EM);

    _fontSize = fontSize;
    _atlasWidth = atlasSize;
    _atlasHeight = atlasSize;

    // Initialize msdfgen's FreeType wrapper if not already done
    // This is needed for msdfgen's internal state, even when adopting an existing FT_Face
    static msdfgen::FreetypeHandle* s_freetypeHandle = nullptr;
    if (!s_freetypeHandle) {
        s_freetypeHandle = msdfgen::initializeFreetype();
        if (!s_freetypeHandle) {
            yerror("Failed to initialize msdfgen FreeType");
            return false;
        }
    }

    // Adopt the existing FT_Face into msdfgen
    msdfgen::FontHandle* font = msdfgen::adoptFreetypeFont(face);
    if (!font) {
        yerror("Failed to adopt FreeType face for embedded font '{}'", fontFamily);
        return false;
    }

    // Get font metrics with proper scaling (not legacy 1/64 scaling)
    msdfgen::FontMetrics metrics;
    msdfgen::getFontMetrics(metrics, font, msdfgen::FONT_SCALING_NONE);
    double unitsPerEm = metrics.emSize > 0 ? metrics.emSize : 1000.0;
    double fontScale = fontSize / unitsPerEm;
    double lineHeight = metrics.lineHeight;
    ydebug("Font::generate(FT_Face): unitsPerEm={}, fontScale={}", unitsPerEm, fontScale);

    // Define character set - we'll generate a smaller set for PDF embedded fonts
    // since they may only contain a subset of glyphs anyway
    std::vector<uint32_t> charset;

    // ASCII printable (32-126)
    for (uint32_t c = 32; c <= 126; ++c) charset.push_back(c);

    // Latin Extended-A (0x0100-0x017F)
    for (uint32_t c = 0x0100; c <= 0x017F; ++c) charset.push_back(c);

    // Common punctuation and symbols
    for (uint32_t c = 0x2000; c <= 0x206F; ++c) charset.push_back(c);

    // Load glyph shapes
    std::vector<PackedGlyph> glyphs;
    int padding = static_cast<int>(std::ceil(_pixelRange));

    for (uint32_t codepoint : charset) {
        PackedGlyph pg;
        pg.codepoint = codepoint;
        pg.style = Regular;
        pg.fontScale = fontScale;

        double advance;
        if (!msdfgen::loadGlyph(pg.shape, font, codepoint, msdfgen::FONT_SCALING_NONE, &advance)) continue;

        pg.advance = advance * fontScale;

        if (!pg.shape.contours.empty()) {
            pg.shape.normalize();
            msdfgen::Shape::Bounds bounds = pg.shape.getBounds();

            pg.boundsL = bounds.l;
            pg.boundsB = bounds.b;
            pg.boundsR = bounds.r;
            pg.boundsT = bounds.t;

            pg.bearingX = bounds.l * fontScale;
            pg.bearingY = bounds.t * fontScale;
            pg.sizeX = (bounds.r - bounds.l) * fontScale;
            pg.sizeY = (bounds.t - bounds.b) * fontScale;

            pg.atlasW = static_cast<int>(std::ceil(pg.sizeX)) + padding * 2;
            pg.atlasH = static_cast<int>(std::ceil(pg.sizeY)) + padding * 2;
        } else {
            pg.boundsL = pg.boundsB = pg.boundsR = pg.boundsT = 0;
            pg.bearingX = pg.bearingY = 0;
            pg.sizeX = pg.sizeY = 0;
            pg.atlasW = pg.atlasH = 0;
        }

        glyphs.push_back(std::move(pg));
    }

    yinfo("Font::generate(FT_Face): loaded {} glyphs from embedded font", glyphs.size());

    // Sort by height for better packing
    std::sort(glyphs.begin(), glyphs.end(), [](const PackedGlyph& a, const PackedGlyph& b) {
        return a.atlasH > b.atlasH;
    });

    // Pack glyphs into atlas
    ShelfPacker packer(_atlasWidth, _atlasHeight);
    for (auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            if (!packer.pack(glyph.atlasW, glyph.atlasH, glyph.atlasX, glyph.atlasY)) {
                ywarn("Atlas full, could not pack glyph {}", glyph.codepoint);
                continue;
            }
        } else {
            glyph.atlasX = glyph.atlasY = 0;
        }
    }

    // Create atlas bitmap (RGBA)
    _atlasData.resize(_atlasWidth * _atlasHeight * 4, 0);

    // Generate MSDF for each glyph
    for (auto& glyph : glyphs) {
        if (glyph.shape.contours.empty()) continue;

        if (!glyph.shape.validate()) {
            ywarn("Invalid shape for codepoint {} in embedded font", glyph.codepoint);
        }

        glyph.shape.normalize();
        msdfgen::edgeColoringSimple(glyph.shape, 3.0);

        double scale = glyph.fontScale;
        msdfgen::Bitmap<float, 3> msdf(glyph.atlasW, glyph.atlasH);

        msdfgen::Vector2 translate(
            padding - glyph.boundsL * scale,
            padding - glyph.boundsB * scale
        );

        msdfgen::generateMSDF(msdf, glyph.shape, _pixelRange, scale, translate);

        // Copy to atlas with Y-flip
        for (int y = 0; y < glyph.atlasH; ++y) {
            for (int x = 0; x < glyph.atlasW; ++x) {
                int atlasX = glyph.atlasX + x;
                int atlasY = glyph.atlasY + (glyph.atlasH - 1 - y);

                if (atlasX >= 0 && atlasX < (int)_atlasWidth &&
                    atlasY >= 0 && atlasY < (int)_atlasHeight) {
                    size_t idx = (atlasY * _atlasWidth + atlasX) * 4;
                    _atlasData[idx + 0] = static_cast<uint8_t>(std::clamp(msdf(x, y)[0] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 1] = static_cast<uint8_t>(std::clamp(msdf(x, y)[1] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 2] = static_cast<uint8_t>(std::clamp(msdf(x, y)[2] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 3] = 255;
                }
            }
        }
    }

    _lineHeight = static_cast<float>(lineHeight * fontScale);

    // Note: We don't take ownership of the adopted font - MuPDF owns the FT_Face
    _freetypeHandle = nullptr;
    _primaryFont = nullptr;

    // Track shelf state
    _shelfX = _atlasPadding;
    _shelfY = _atlasPadding;
    _shelfHeight = 0;
    for (const auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            int glyphBottom = glyph.atlasY + glyph.atlasH;
            if (glyph.atlasX + glyph.atlasW > _shelfX) {
                _shelfX = glyph.atlasX + glyph.atlasW + _atlasPadding;
            }
            if (glyphBottom > _shelfY) {
                _shelfY = glyph.atlasY;
                _shelfHeight = glyph.atlasH;
            }
        }
    }

    // Extract glyph metrics
    for (const auto& glyph : glyphs) {
        GlyphMetrics m;

        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            m._uvMin = glm::vec2(
                static_cast<float>(glyph.atlasX) / _atlasWidth,
                static_cast<float>(glyph.atlasY) / _atlasHeight
            );
            m._uvMax = glm::vec2(
                static_cast<float>(glyph.atlasX + glyph.atlasW) / _atlasWidth,
                static_cast<float>(glyph.atlasY + glyph.atlasH) / _atlasHeight
            );
        } else {
            m._uvMin = m._uvMax = glm::vec2(0);
        }

        m._size = glm::vec2(static_cast<float>(glyph.atlasW), static_cast<float>(glyph.atlasH));
        m._bearing = glm::vec2(
            static_cast<float>(glyph.bearingX - padding),
            static_cast<float>(glyph.boundsT * glyph.fontScale + padding)
        );
        m._advance = static_cast<float>(glyph.advance);

        _glyphs[glyph.codepoint] = m;
    }

    yinfo("Generated MSDF atlas for embedded font '{}': {}x{} with {} glyphs",
                 fontFamily, _atlasWidth, _atlasHeight, _glyphs.size());

    buildGlyphIndexMap();

    // Clean up the adopted font handle (but NOT the underlying FT_Face!)
    msdfgen::destroyFont(font);

    return true;
}

bool Font::generate(FT_Face face, const std::string& fontName, float fontSize, uint32_t atlasSize) {
    // This is just a wrapper that logs the font name properly
    // The actual implementation still uses face->family_name internally
    yinfo("Font::generate(FT_Face, name='{}'): delegating to FT_Face generate", fontName);
    return generate(face, fontSize, atlasSize);
}

bool Font::generate(const unsigned char* data, size_t dataLen,
                    const std::string& fontName, float fontSize, uint32_t atlasSize) {
    if (!data || dataLen == 0) {
        yerror("Font::generate(data): null or empty font data");
        return false;
    }

    yinfo("Font::generate(data): loading font '{}' from {} bytes", fontName, dataLen);

    _fontSize = fontSize;
    _atlasWidth = atlasSize;
    _atlasHeight = atlasSize;

    // Initialize msdfgen's FreeType
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        yerror("Failed to initialize msdfgen FreeType");
        return false;
    }

    // Load font from memory
    msdfgen::FontHandle* font = msdfgen::loadFontData(ft, data, static_cast<int>(dataLen));
    if (!font) {
        yerror("Failed to load font '{}' from data", fontName);
        msdfgen::deinitializeFreetype(ft);
        return false;
    }

    // Get font metrics
    msdfgen::FontMetrics metrics;
    msdfgen::getFontMetrics(metrics, font, msdfgen::FONT_SCALING_NONE);
    double unitsPerEm = metrics.emSize > 0 ? metrics.emSize : 1000.0;
    double fontScale = fontSize / unitsPerEm;
    double lineHeight = metrics.lineHeight;
    ydebug("Font::generate(data): unitsPerEm={}, fontScale={}", unitsPerEm, fontScale);

    // Define character set
    std::vector<uint32_t> charset;
    for (uint32_t c = 32; c <= 126; ++c) charset.push_back(c);
    for (uint32_t c = 0x0100; c <= 0x017F; ++c) charset.push_back(c);
    for (uint32_t c = 0x2000; c <= 0x206F; ++c) charset.push_back(c);
    // Nerd Font ranges
    for (uint32_t c = 0xE0A0; c <= 0xE0D4; ++c) charset.push_back(c);  // Powerline
    for (uint32_t c = 0xE5FA; c <= 0xE6B5; ++c) charset.push_back(c);  // Seti-UI
    for (uint32_t c = 0xE700; c <= 0xE7C5; ++c) charset.push_back(c);  // Devicons
    for (uint32_t c = 0xF000; c <= 0xF2E0; ++c) charset.push_back(c);  // Font Awesome
    for (uint32_t c = 0xE200; c <= 0xE2A9; ++c) charset.push_back(c);  // FA Extension
    for (uint32_t c = 0xE300; c <= 0xE3E3; ++c) charset.push_back(c);  // Weather
    for (uint32_t c = 0xF400; c <= 0xF532; ++c) charset.push_back(c);  // Octicons
    for (uint32_t c = 0xE000; c <= 0xE00A; ++c) charset.push_back(c);  // Pomicons
    for (uint32_t c = 0xF300; c <= 0xF372; ++c) charset.push_back(c);  // Font Logos
    for (uint32_t c = 0xEA60; c <= 0xEBEB; ++c) charset.push_back(c);  // Codicons

    // Load glyph shapes
    std::vector<PackedGlyph> glyphs;
    int padding = static_cast<int>(std::ceil(_pixelRange));

    for (uint32_t codepoint : charset) {
        PackedGlyph pg;
        pg.codepoint = codepoint;
        pg.style = Regular;
        pg.fontScale = fontScale;

        double advance;
        if (!msdfgen::loadGlyph(pg.shape, font, codepoint, msdfgen::FONT_SCALING_NONE, &advance)) continue;

        pg.advance = advance * fontScale;

        if (!pg.shape.contours.empty()) {
            pg.shape.normalize();
            msdfgen::Shape::Bounds bounds = pg.shape.getBounds();

            pg.boundsL = bounds.l;
            pg.boundsB = bounds.b;
            pg.boundsR = bounds.r;
            pg.boundsT = bounds.t;

            pg.bearingX = bounds.l * fontScale;
            pg.bearingY = bounds.t * fontScale;
            pg.sizeX = (bounds.r - bounds.l) * fontScale;
            pg.sizeY = (bounds.t - bounds.b) * fontScale;

            pg.atlasW = static_cast<int>(std::ceil(pg.sizeX)) + padding * 2;
            pg.atlasH = static_cast<int>(std::ceil(pg.sizeY)) + padding * 2;
        } else {
            pg.boundsL = pg.boundsB = pg.boundsR = pg.boundsT = 0;
            pg.bearingX = pg.bearingY = 0;
            pg.sizeX = pg.sizeY = 0;
            pg.atlasW = pg.atlasH = 0;
        }

        glyphs.push_back(std::move(pg));
    }

    yinfo("Font::generate(data): loaded {} glyphs from font '{}'", glyphs.size(), fontName);

    // Sort by height for better packing
    std::sort(glyphs.begin(), glyphs.end(), [](const PackedGlyph& a, const PackedGlyph& b) {
        return a.atlasH > b.atlasH;
    });

    // Pack glyphs into atlas
    ShelfPacker packer(_atlasWidth, _atlasHeight);
    for (auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            if (!packer.pack(glyph.atlasW, glyph.atlasH, glyph.atlasX, glyph.atlasY)) {
                ywarn("Atlas full, could not pack glyph {}", glyph.codepoint);
                continue;
            }
        } else {
            glyph.atlasX = glyph.atlasY = 0;
        }
    }

    // Create atlas bitmap (RGBA)
    _atlasData.resize(_atlasWidth * _atlasHeight * 4, 0);

    // Generate MSDF for each glyph
    for (auto& glyph : glyphs) {
        if (glyph.shape.contours.empty()) continue;

        if (!glyph.shape.validate()) {
            ywarn("Invalid shape for codepoint {} in font '{}'", glyph.codepoint, fontName);
        }

        glyph.shape.normalize();
        msdfgen::edgeColoringSimple(glyph.shape, 3.0);

        double scale = glyph.fontScale;
        msdfgen::Bitmap<float, 3> msdf(glyph.atlasW, glyph.atlasH);

        msdfgen::Vector2 translate(
            padding - glyph.boundsL * scale,
            padding - glyph.boundsB * scale
        );

        msdfgen::generateMSDF(msdf, glyph.shape, _pixelRange, scale, translate);

        // Copy to atlas with Y-flip
        for (int y = 0; y < glyph.atlasH; ++y) {
            for (int x = 0; x < glyph.atlasW; ++x) {
                int atlasX = glyph.atlasX + x;
                int atlasY = glyph.atlasY + (glyph.atlasH - 1 - y);

                if (atlasX >= 0 && atlasX < (int)_atlasWidth &&
                    atlasY >= 0 && atlasY < (int)_atlasHeight) {
                    size_t idx = (atlasY * _atlasWidth + atlasX) * 4;
                    _atlasData[idx + 0] = static_cast<uint8_t>(std::clamp(msdf(x, y)[0] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 1] = static_cast<uint8_t>(std::clamp(msdf(x, y)[1] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 2] = static_cast<uint8_t>(std::clamp(msdf(x, y)[2] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 3] = 255;
                }
            }
        }
    }

    _lineHeight = static_cast<float>(lineHeight * fontScale);

    // We own this FreeType instance
    _freetypeHandle = ft;
    _primaryFont = font;

    // Track shelf state
    _shelfX = _atlasPadding;
    _shelfY = _atlasPadding;
    _shelfHeight = 0;
    for (const auto& glyph : glyphs) {
        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            int glyphBottom = glyph.atlasY + glyph.atlasH;
            if (glyph.atlasX + glyph.atlasW > _shelfX) {
                _shelfX = glyph.atlasX + glyph.atlasW + _atlasPadding;
            }
            if (glyphBottom > _shelfY) {
                _shelfY = glyph.atlasY;
                _shelfHeight = glyph.atlasH;
            }
        }
    }

    // Extract glyph metrics
    for (const auto& glyph : glyphs) {
        GlyphMetrics m;

        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            m._uvMin = glm::vec2(
                static_cast<float>(glyph.atlasX) / _atlasWidth,
                static_cast<float>(glyph.atlasY) / _atlasHeight
            );
            m._uvMax = glm::vec2(
                static_cast<float>(glyph.atlasX + glyph.atlasW) / _atlasWidth,
                static_cast<float>(glyph.atlasY + glyph.atlasH) / _atlasHeight
            );
        } else {
            m._uvMin = m._uvMax = glm::vec2(0);
        }

        m._size = glm::vec2(static_cast<float>(glyph.atlasW), static_cast<float>(glyph.atlasH));
        m._bearing = glm::vec2(
            static_cast<float>(glyph.bearingX - padding),
            static_cast<float>(glyph.boundsT * glyph.fontScale + padding)
        );
        m._advance = static_cast<float>(glyph.advance);

        _glyphs[glyph.codepoint] = m;
    }

    yinfo("Generated MSDF atlas for font '{}': {}x{} with {} glyphs",
                 fontName, _atlasWidth, _atlasHeight, _glyphs.size());

    buildGlyphIndexMap();

    return true;
}

bool Font::saveAtlas(const std::string& atlasPath, const std::string& metricsPath) const {
    if (_atlasData.empty()) {
        std::cerr << "No atlas data to save" << std::endl;
        return false;
    }

    // Compress atlas data with LZ4 (fast compression/decompression)
    const int srcSize = static_cast<int>(_atlasData.size());
    const int maxDstSize = LZ4_compressBound(srcSize);
    std::vector<char> compressed(maxDstSize);

    const int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(_atlasData.data()),
        compressed.data(),
        srcSize,
        maxDstSize
    );

    if (compressedSize <= 0) {
        std::cerr << "LZ4 compression failed" << std::endl;
        return false;
    }

    // Save atlas as LZ4 compressed binary
    std::ofstream atlasFile(atlasPath, std::ios::binary);
    if (!atlasFile) {
        std::cerr << "Failed to open atlas file for writing: " << atlasPath << std::endl;
        return false;
    }

    // Write header: magic, width, height, uncompressed size, compressed size
    const uint32_t magic = 0x344A5A4C; // "LZ4\0" in little-endian
    atlasFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    atlasFile.write(reinterpret_cast<const char*>(&_atlasWidth), sizeof(_atlasWidth));
    atlasFile.write(reinterpret_cast<const char*>(&_atlasHeight), sizeof(_atlasHeight));
    const uint32_t uncompressedSize = static_cast<uint32_t>(srcSize);
    const uint32_t compressedSizeU32 = static_cast<uint32_t>(compressedSize);
    atlasFile.write(reinterpret_cast<const char*>(&uncompressedSize), sizeof(uncompressedSize));
    atlasFile.write(reinterpret_cast<const char*>(&compressedSizeU32), sizeof(compressedSizeU32));

    // Write compressed data
    atlasFile.write(compressed.data(), compressedSize);
    if (!atlasFile) {
        std::cerr << "Failed to write atlas data: " << atlasPath << std::endl;
        return false;
    }

    ydebug("Atlas compressed: {} -> {} bytes ({:.1f}%)",
                  srcSize, compressedSize, 100.0f * compressedSize / srcSize);

    // Save metrics as JSON
    std::ofstream file(metricsPath);
    if (!file) {
        std::cerr << "Failed to open metrics file: " << metricsPath << std::endl;
        return false;
    }

    file << "{\n";
    file << "  \"atlasWidth\": " << _atlasWidth << ",\n";
    file << "  \"atlasHeight\": " << _atlasHeight << ",\n";
    file << "  \"fontSize\": " << _fontSize << ",\n";
    file << "  \"lineHeight\": " << _lineHeight << ",\n";
    file << "  \"pixelRange\": " << _pixelRange << ",\n";
    file << "  \"glyphs\": {\n";

    // Helper to write a glyph map
    auto writeGlyphMap = [&file](const std::unordered_map<uint32_t, GlyphMetrics>& glyphMap, bool& first) {
        for (const auto& [codepoint, m] : glyphMap) {
            if (!first) file << ",\n";
            first = false;

            file << "    \"" << codepoint << "\": {\n";
            file << "      \"uvMin\": [" << m._uvMin.x << ", " << m._uvMin.y << "],\n";
            file << "      \"uvMax\": [" << m._uvMax.x << ", " << m._uvMax.y << "],\n";
            file << "      \"size\": [" << m._size.x << ", " << m._size.y << "],\n";
            file << "      \"bearing\": [" << m._bearing.x << ", " << m._bearing.y << "],\n";
            file << "      \"advance\": " << m._advance << "\n";
            file << "    }";
        }
    };

    bool first = true;
    writeGlyphMap(_glyphs, first);
    file << "\n  },\n";

    // Save bold glyphs
    file << "  \"boldGlyphs\": {\n";
    first = true;
    writeGlyphMap(_boldGlyphs, first);
    file << "\n  },\n";

    // Save italic glyphs
    file << "  \"italicGlyphs\": {\n";
    first = true;
    writeGlyphMap(_italicGlyphs, first);
    file << "\n  },\n";

    // Save bold-italic glyphs
    file << "  \"boldItalicGlyphs\": {\n";
    first = true;
    writeGlyphMap(_boldItalicGlyphs, first);
    file << "\n  }\n";
    file << "}\n";

    std::cout << "Saved atlas to " << atlasPath << " and metrics to " << metricsPath << std::endl;
    return true;
}

std::vector<std::string> Font::findFontsForCodepoint(uint32_t codepoint) {
    std::vector<std::string> fonts;

    // Use fontconfig to find fonts that contain this codepoint
    // We need outline fonts (not color bitmap) for MSDF rendering
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) {
        std::cerr << "Failed to initialize fontconfig" << std::endl;
        return fonts;
    }

    // Create a pattern that matches fonts containing this codepoint
    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        FcConfigDestroy(config);
        return fonts;
    }

    // Create a charset containing just the codepoint we need
    FcCharSet* charset = FcCharSetCreate();
    FcCharSetAddChar(charset, codepoint);
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);

    // Require outline fonts (not bitmap) - critical for MSDF rendering
    FcPatternAddBool(pattern, FC_OUTLINE, FcTrue);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);

    // Configure pattern for best match
    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    // Get sorted list of matching fonts
    FcResult result;
    FcFontSet* fontSet = FcFontSort(config, pattern, FcFalse, nullptr, &result);

    if (fontSet) {
        std::set<std::string> seen;  // Avoid duplicates

        for (int i = 0; i < fontSet->nfont && fonts.size() < 10; i++) {
            FcPattern* font = fontSet->fonts[i];
            FcChar8* file = nullptr;
            FcChar8* family = nullptr;

            if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch || !file) {
                continue;
            }

            std::string fontPath = reinterpret_cast<const char*>(file);

            // Skip duplicates
            if (seen.count(fontPath)) {
                continue;
            }
            seen.insert(fontPath);

            // Get font family name
            FcPatternGetString(font, FC_FAMILY, 0, &family);
            std::string familyStr = family ? reinterpret_cast<const char*>(family) : "";

            // Skip known color emoji fonts (they don't have proper outlines)
            if (familyStr.find("Color") != std::string::npos) {
                continue;
            }

            // Check if font has outline property set
            FcBool outline = FcFalse;
            if (FcPatternGetBool(font, FC_OUTLINE, 0, &outline) == FcResultMatch && !outline) {
                continue;
            }

            fonts.push_back(fontPath);
        }
        FcFontSetDestroy(fontSet);
    }

    FcCharSetDestroy(charset);
    FcPatternDestroy(pattern);
    FcConfigDestroy(config);

    return fonts;
}

bool Font::loadGlyphFromFont(const std::string& fontPath, uint32_t codepoint) {
    if (!_freetypeHandle) {
        return false;
    }

    msdfgen::FreetypeHandle* ft = static_cast<msdfgen::FreetypeHandle*>(_freetypeHandle);
    msdfgen::FontHandle* font = nullptr;

    // Check if we already loaded this font
    auto it = _fallbackFonts.find(fontPath);
    if (it != _fallbackFonts.end()) {
        font = static_cast<msdfgen::FontHandle*>(it->second);
    } else {
        // Load the font
        font = msdfgen::loadFont(ft, fontPath.c_str());
        if (!font) {
            return false;
        }
        _fallbackFonts[fontPath] = font;
    }

    // Get font metrics for scaling
    msdfgen::FontMetrics metrics;
    msdfgen::getFontMetrics(metrics, font);
    double unitsPerEm = metrics.emSize > 0 ? metrics.emSize : (metrics.ascenderY - metrics.descenderY);
    double fontScale = _fontSize / unitsPerEm;
    int padding = static_cast<int>(std::ceil(_pixelRange));

    // Load the glyph shape
    msdfgen::Shape shape;
    double advance;
    if (!msdfgen::loadGlyph(shape, font, codepoint, &advance)) {
        return false;
    }

    // Check if the glyph has actual outline data (color emoji fonts return empty shapes)
    if (shape.contours.empty()) {
        return false;
    }

    // Calculate glyph dimensions
    int atlasW = 0, atlasH = 0;
    double bearingX = 0, bearingY = 0, sizeX = 0, sizeY = 0;
    double boundsL = 0, boundsB = 0, boundsR = 0, boundsT = 0;

    if (!shape.contours.empty()) {
        shape.normalize();
        msdfgen::Shape::Bounds bounds = shape.getBounds();
        boundsL = bounds.l;
        boundsB = bounds.b;
        boundsR = bounds.r;
        boundsT = bounds.t;

        // Calculate raw scaled size
        double rawSizeY = (bounds.t - bounds.b) * fontScale;

        // Scale fallback glyphs to fit cell height
        // Use fontSize/2 as target (typical cell height for monospace fonts)
        double targetHeight = _fontSize * 0.5;
        ydebug("Fallback glyph U+{:04X}: rawSizeY={:.1f} target={:.1f} fontSize={:.1f}",
                      codepoint, rawSizeY, targetHeight, _fontSize);
        if (rawSizeY > targetHeight && targetHeight > 0) {
            double scaleDown = targetHeight / rawSizeY;
            fontScale *= scaleDown;
            ydebug("  -> Scaling down by {:.3f}", scaleDown);
        }

        bearingX = bounds.l * fontScale;
        bearingY = bounds.t * fontScale;
        sizeX = (bounds.r - bounds.l) * fontScale;
        sizeY = (bounds.t - bounds.b) * fontScale;

        atlasW = static_cast<int>(std::ceil(sizeX)) + padding * 2;
        atlasH = static_cast<int>(std::ceil(sizeY)) + padding * 2;
    }

    // Pack into atlas using shelf packer logic
    int atlasX = 0, atlasY = 0;
    if (atlasW > 0 && atlasH > 0) {
        int pw = atlasW + _atlasPadding;
        int ph = atlasH + _atlasPadding;

        // Check if fits on current shelf
        if (_shelfX + pw > static_cast<int>(_atlasWidth)) {
            // Move to next shelf
            _shelfX = _atlasPadding;
            _shelfY += _shelfHeight + _atlasPadding;
            _shelfHeight = 0;
        }

        // Check if fits vertically
        if (_shelfY + ph > static_cast<int>(_atlasHeight)) {
            std::cerr << "Atlas full, cannot add glyph U+" << std::hex << codepoint << std::dec << std::endl;
            return false;
        }

        atlasX = _shelfX;
        atlasY = _shelfY;
        _shelfX += pw;
        _shelfHeight = std::max(_shelfHeight, ph);
    }

    // Generate MSDF for the glyph
    if (!shape.contours.empty()) {
        if (!shape.validate()) {
            std::cerr << "Warning: Invalid shape for fallback glyph U+" << std::hex << codepoint << std::dec << std::endl;
        }

        shape.normalize();
        msdfgen::edgeColoringSimple(shape, 3.0);

        msdfgen::Bitmap<float, 3> msdf(atlasW, atlasH);

        msdfgen::Vector2 translate(
            padding - boundsL * fontScale,
            padding - boundsB * fontScale
        );

        msdfgen::generateMSDF(msdf, shape, _pixelRange, fontScale, translate);

        // Copy to atlas with Y-flip
        for (int y = 0; y < atlasH; ++y) {
            for (int x = 0; x < atlasW; ++x) {
                int ax = atlasX + x;
                int ay = atlasY + (atlasH - 1 - y);

                if (ax >= 0 && ax < static_cast<int>(_atlasWidth) &&
                    ay >= 0 && ay < static_cast<int>(_atlasHeight)) {
                    size_t idx = (ay * _atlasWidth + ax) * 4;
                    _atlasData[idx + 0] = static_cast<uint8_t>(std::clamp(msdf(x, y)[0] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 1] = static_cast<uint8_t>(std::clamp(msdf(x, y)[1] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 2] = static_cast<uint8_t>(std::clamp(msdf(x, y)[2] * 255.0f, 0.0f, 255.0f));
                    _atlasData[idx + 3] = 255;
                }
            }
        }
    }

    // Create glyph metrics
    GlyphMetrics m;
    if (atlasW > 0 && atlasH > 0) {
        m._uvMin = glm::vec2(
            static_cast<float>(atlasX) / _atlasWidth,
            static_cast<float>(atlasY) / _atlasHeight
        );
        m._uvMax = glm::vec2(
            static_cast<float>(atlasX + atlasW) / _atlasWidth,
            static_cast<float>(atlasY + atlasH) / _atlasHeight
        );
    } else {
        m._uvMin = m._uvMax = glm::vec2(0);
    }

    m._size = glm::vec2(static_cast<float>(atlasW), static_cast<float>(atlasH));
    m._bearing = glm::vec2(
        static_cast<float>(bearingX - padding),
        static_cast<float>(boundsT * fontScale + padding)
    );
    m._advance = static_cast<float>(advance * fontScale);

    _glyphs[codepoint] = m;

    // Add to index map and GPU metadata
    uint16_t index = static_cast<uint16_t>(_glyphMetadata.size());
    _codepointToIndex[codepoint] = index;

    GlyphMetadataGPU gpu;
    gpu._uvMinX = m._uvMin.x;
    gpu._uvMinY = m._uvMin.y;
    gpu._uvMaxX = m._uvMax.x;
    gpu._uvMaxY = m._uvMax.y;
    gpu._sizeX = m._size.x;
    gpu._sizeY = m._size.y;
    gpu._bearingX = m._bearing.x;
    gpu._bearingY = m._bearing.y;
    gpu._advance = m._advance;
    gpu._pad = 0.0f;
    _glyphMetadata.push_back(gpu);

    // Mark as pending for GPU upload
    _pendingGlyphs.insert(codepoint);

    yinfo("Loaded fallback glyph U+{:04X} at ({},{}) size {}x{}",
                 codepoint, atlasX, atlasY, atlasW, atlasH);
    ydebug("  Glyph metrics: uv({:.4f},{:.4f})->({:.4f},{:.4f}) size({:.1f},{:.1f}) bearing({:.1f},{:.1f}) adv={:.1f}",
                  m._uvMin.x, m._uvMin.y, m._uvMax.x, m._uvMax.y,
                  m._size.x, m._size.y, m._bearing.x, m._bearing.y, m._advance);

    return true;
}

bool Font::loadMissingGlyph(uint32_t codepoint) {
    ydebug("Loading fallback glyph U+{:04X}", codepoint);

    // Check if already loaded
    if (_glyphs.find(codepoint) != _glyphs.end()) {
        return true;
    }

    // Check if we already tried and failed
    if (_failedCodepoints.find(codepoint) != _failedCodepoints.end()) {
        return false;
    }

    // Check if FreeType is initialized
    if (!_freetypeHandle) {
        yerror("FreeType not initialized for fallback loading");
        _failedCodepoints.insert(codepoint);
        return false;
    }

    // Find fallback fonts using fontconfig
    std::vector<std::string> fontPaths = findFontsForCodepoint(codepoint);
    ydebug("Found {} fallback fonts for U+{:04X}", fontPaths.size(), codepoint);

    if (fontPaths.empty()) {
        _failedCodepoints.insert(codepoint);
        return false;
    }

    // Try each font until one works
    for (const auto& fontPath : fontPaths) {
        if (loadGlyphFromFont(fontPath, codepoint)) {
            ydebug("Loaded U+{:04X} from: {}", codepoint, fontPath);
            return true;
        }
    }

    // All fonts failed
    _failedCodepoints.insert(codepoint);
    return false;
}

bool Font::uploadPendingGlyphs(WGPUDevice device, WGPUQueue queue) {
    if (_pendingGlyphs.empty()) {
        return true;
    }

    // Re-upload entire atlas texture
    // (A more efficient approach would be to only upload the changed regions)
    if (_texture) {
        WGPUTexelCopyTextureInfo dest = {};
        dest.texture = _texture;
        dest.mipLevel = 0;
        dest.origin = {0, 0, 0};
        dest.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout = {};
        layout.offset = 0;
        layout.bytesPerRow = _atlasWidth * 4;
        layout.rowsPerImage = _atlasHeight;

        WGPUExtent3D extent = {_atlasWidth, _atlasHeight, 1};
        wgpuQueueWriteTexture(queue, &dest, _atlasData.data(), _atlasData.size(), &layout, &extent);
    }

    // Re-upload glyph metadata buffer
    if (_glyphMetadataBuffer) {
        // We need to recreate the buffer since it may have grown
        wgpuBufferRelease(_glyphMetadataBuffer);
        _glyphMetadataBuffer = nullptr;
    }

    if (!createGlyphMetadataBuffer(device)) {
        std::cerr << "Failed to recreate glyph metadata buffer" << std::endl;
        return false;
    }

    std::cout << "Uploaded " << _pendingGlyphs.size() << " pending glyphs to GPU" << std::endl;
    _pendingGlyphs.clear();

    return true;
}

#endif // !YETTY_USE_PREBUILT_ATLAS

// Simple JSON value parser helpers
namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

float parseFloat(const std::string& s) {
    return std::stof(trim(s));
}

uint32_t parseUint(const std::string& s) {
    return static_cast<uint32_t>(std::stoul(trim(s)));
}

glm::vec2 parseVec2(const std::string& s) {
    auto start = s.find('[');
    auto end = s.find(']');
    if (start == std::string::npos || end == std::string::npos) return glm::vec2(0);

    std::string inner = s.substr(start + 1, end - start - 1);
    auto comma = inner.find(',');
    if (comma == std::string::npos) return glm::vec2(0);

    return glm::vec2(
        parseFloat(inner.substr(0, comma)),
        parseFloat(inner.substr(comma + 1))
    );
}

} // anonymous namespace

bool Font::loadAtlas(const std::string& atlasPath, const std::string& metricsPath) {
    // Detect format based on file extension
    bool isPng = atlasPath.size() >= 4 &&
                 (atlasPath.substr(atlasPath.size() - 4) == ".png" ||
                  atlasPath.substr(atlasPath.size() - 4) == ".PNG");

    if (isPng) {
        // Load PNG atlas using stb_image
        int width, height, channels;
        unsigned char* data = stbi_load(atlasPath.c_str(), &width, &height, &channels, 4);
        if (!data) {
            std::cerr << "Failed to load PNG atlas: " << atlasPath << std::endl;
            return false;
        }

        _atlasWidth = static_cast<uint32_t>(width);
        _atlasHeight = static_cast<uint32_t>(height);
        _atlasData.resize(width * height * 4);
        std::memcpy(_atlasData.data(), data, _atlasData.size());
        stbi_image_free(data);
    } else {
#if !YETTY_USE_PREBUILT_ATLAS
        // Load LZ4 compressed atlas
        std::ifstream atlasFile(atlasPath, std::ios::binary | std::ios::ate);
        if (!atlasFile) {
            std::cerr << "Failed to open atlas file: " << atlasPath << std::endl;
            return false;
        }

        const size_t fileSize = atlasFile.tellg();
        atlasFile.seekg(0, std::ios::beg);
        (void)fileSize;  // Suppress unused warning

        // Read header: magic, width, height, uncompressed size, compressed size
        uint32_t magic, uncompressedSize, compressedSize;
        atlasFile.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        atlasFile.read(reinterpret_cast<char*>(&_atlasWidth), sizeof(_atlasWidth));
        atlasFile.read(reinterpret_cast<char*>(&_atlasHeight), sizeof(_atlasHeight));
        atlasFile.read(reinterpret_cast<char*>(&uncompressedSize), sizeof(uncompressedSize));
        atlasFile.read(reinterpret_cast<char*>(&compressedSize), sizeof(compressedSize));

        if (magic != 0x344A5A4C) { // "LZ4\0"
            std::cerr << "Invalid atlas file magic: " << atlasPath << std::endl;
            return false;
        }

        // Read compressed data
        std::vector<char> compressed(compressedSize);
        atlasFile.read(compressed.data(), compressedSize);

        // Decompress with LZ4
        _atlasData.resize(uncompressedSize);
        const int decompressedSize = LZ4_decompress_safe(
            compressed.data(),
            reinterpret_cast<char*>(_atlasData.data()),
            static_cast<int>(compressedSize),
            static_cast<int>(uncompressedSize)
        );

        if (decompressedSize < 0 || static_cast<uint32_t>(decompressedSize) != uncompressedSize) {
            std::cerr << "LZ4 decompression failed: " << atlasPath << std::endl;
            return false;
        }
#else
        std::cerr << "LZ4 atlas loading not available in prebuilt atlas mode: " << atlasPath << std::endl;
        return false;
#endif
    }

    // Load metrics JSON
    std::ifstream file(metricsPath);
    if (!file) {
        std::cerr << "Failed to open metrics file: " << metricsPath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Simple JSON parsing
    auto findValue = [&json](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";

        pos += search.length();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        size_t end = pos;
        int braceCount = 0;
        int bracketCount = 0;
        bool inString = false;

        while (end < json.size()) {
            char c = json[end];
            if (c == '"' && (end == 0 || json[end-1] != '\\')) inString = !inString;
            if (!inString) {
                if (c == '{') braceCount++;
                else if (c == '}') {
                    if (braceCount == 0) break;
                    braceCount--;
                }
                else if (c == '[') bracketCount++;
                else if (c == ']') bracketCount--;
                else if ((c == ',' || c == '\n') && braceCount == 0 && bracketCount == 0) break;
            }
            end++;
        }

        return trim(json.substr(pos, end - pos));
    };

    // Parse header fields
    std::string val;
    val = findValue("atlasWidth");
    if (!val.empty()) _atlasWidth = parseUint(val);

    val = findValue("atlasHeight");
    if (!val.empty()) _atlasHeight = parseUint(val);

    val = findValue("fontSize");
    if (!val.empty()) _fontSize = parseFloat(val);

    val = findValue("lineHeight");
    if (!val.empty()) _lineHeight = parseFloat(val);

    val = findValue("pixelRange");
    if (!val.empty()) _pixelRange = parseFloat(val);

    // Helper to parse a glyph section from JSON
    auto parseGlyphSection = [&json](const std::string& sectionName,
                                      std::unordered_map<uint32_t, GlyphMetrics>& targetMap) {
        std::string searchKey = "\"" + sectionName + "\":";
        auto glyphsPos = json.find(searchKey);
        if (glyphsPos == std::string::npos) {
            return;  // Section not found is OK (for variant sections)
        }

        auto glyphsStart = json.find('{', glyphsPos + searchKey.length());
        if (glyphsStart == std::string::npos) return;

        size_t pos = glyphsStart + 1;
        while (pos < json.size()) {
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == ',')) pos++;

            if (json[pos] == '}') break;

            if (json[pos] != '"') { pos++; continue; }

            auto keyEnd = json.find('"', pos + 1);
            if (keyEnd == std::string::npos) break;

            uint32_t codepoint = parseUint(json.substr(pos + 1, keyEnd - pos - 1));
            pos = keyEnd + 1;

            auto braceStart = json.find('{', pos);
            if (braceStart == std::string::npos) break;

            int depth = 1;
            size_t braceEnd = braceStart + 1;
            while (braceEnd < json.size() && depth > 0) {
                if (json[braceEnd] == '{') depth++;
                else if (json[braceEnd] == '}') depth--;
                braceEnd++;
            }

            std::string glyphJson = json.substr(braceStart, braceEnd - braceStart);

            GlyphMetrics m;
            auto findGlyphValue = [&glyphJson](const std::string& key) -> std::string {
                std::string search = "\"" + key + "\":";
                auto p = glyphJson.find(search);
                if (p == std::string::npos) return "";

                p += search.length();
                while (p < glyphJson.size() && (glyphJson[p] == ' ' || glyphJson[p] == '\t')) p++;

                size_t e = p;
                int bracketCount = 0;
                while (e < glyphJson.size()) {
                    char c = glyphJson[e];
                    if (c == '[') bracketCount++;
                    else if (c == ']') bracketCount--;
                    else if ((c == ',' || c == '\n' || c == '}') && bracketCount == 0) break;
                    e++;
                }
                return trim(glyphJson.substr(p, e - p));
            };

            m._uvMin = parseVec2(findGlyphValue("uvMin"));
            m._uvMax = parseVec2(findGlyphValue("uvMax"));
            m._size = parseVec2(findGlyphValue("size"));
            m._bearing = parseVec2(findGlyphValue("bearing"));

            std::string advStr = findGlyphValue("advance");
            m._advance = advStr.empty() ? 0.0f : parseFloat(advStr);

            targetMap[codepoint] = m;
            pos = braceEnd;
        }
    };

    // Parse regular glyphs (required)
    parseGlyphSection("glyphs", _glyphs);
    if (_glyphs.empty()) {
        std::cerr << "No glyphs found in metrics" << std::endl;
        return false;
    }

    // Parse variant glyphs (optional - may not exist in old cache files)
    parseGlyphSection("boldGlyphs", _boldGlyphs);
    parseGlyphSection("italicGlyphs", _italicGlyphs);
    parseGlyphSection("boldItalicGlyphs", _boldItalicGlyphs);

    std::cout << "Loaded atlas " << _atlasWidth << "x" << _atlasHeight
              << " with " << _glyphs.size() << " regular, "
              << _boldGlyphs.size() << " bold, "
              << _italicGlyphs.size() << " italic, "
              << _boldItalicGlyphs.size() << " bold-italic glyphs from " << metricsPath << std::endl;

    buildGlyphIndexMap();

#if !YETTY_USE_PREBUILT_ATLAS
    // Initialize FreeType for fallback font loading
    _freetypeHandle = msdfgen::initializeFreetype();
    if (!_freetypeHandle) {
        std::cerr << "Warning: Failed to initialize FreeType for fallback fonts" << std::endl;
    }

    // Calculate shelf packer state from loaded glyphs
    // Find the maximum Y extent and rightmost X position
    _shelfX = _atlasPadding;
    _shelfY = _atlasPadding;
    _shelfHeight = 0;
    int maxY = 0;
    for (const auto& [codepoint, m] : _glyphs) {
        if (m._size.x > 0 && m._size.y > 0) {
            // Calculate atlas position from UV coordinates
            int atlasX = static_cast<int>(m._uvMin.x * _atlasWidth);
            int atlasY = static_cast<int>(m._uvMin.y * _atlasHeight);
            int atlasW = static_cast<int>((m._uvMax.x - m._uvMin.x) * _atlasWidth);
            int atlasH = static_cast<int>((m._uvMax.y - m._uvMin.y) * _atlasHeight);

            int bottom = atlasY + atlasH;
            if (bottom > maxY) {
                maxY = bottom;
                _shelfY = atlasY;
                _shelfHeight = atlasH;
            }
            int right = atlasX + atlasW + _atlasPadding;
            if (right > _shelfX && atlasY >= _shelfY) {
                _shelfX = right;
            }
        }
    }

    std::cout << "Shelf packer state: x=" << _shelfX << " y=" << _shelfY
              << " height=" << _shelfHeight << std::endl;
#endif

    return true;
}

void Font::buildGlyphIndexMap() {
    _codepointToIndex.clear();
    _boldCodepointToIndex.clear();
    _italicCodepointToIndex.clear();
    _boldItalicCodepointToIndex.clear();
    _glyphMetadata.clear();

    // Helper lambda to add glyphs from a map to the metadata array
    auto addGlyphsFromMap = [this](const std::unordered_map<uint32_t, GlyphMetrics>& glyphMap,
                                    std::unordered_map<uint32_t, uint16_t>& indexMap) {
        std::vector<uint32_t> codepoints;
        codepoints.reserve(glyphMap.size());
        for (const auto& [cp, _] : glyphMap) {
            codepoints.push_back(cp);
        }
        std::sort(codepoints.begin(), codepoints.end());

        for (uint32_t cp : codepoints) {
            uint16_t index = static_cast<uint16_t>(_glyphMetadata.size());
            indexMap[cp] = index;

            const GlyphMetrics& m = glyphMap.at(cp);
            GlyphMetadataGPU gpu;
            gpu._uvMinX = m._uvMin.x;
            gpu._uvMinY = m._uvMin.y;
            gpu._uvMaxX = m._uvMax.x;
            gpu._uvMaxY = m._uvMax.y;
            gpu._sizeX = m._size.x;
            gpu._sizeY = m._size.y;
            gpu._bearingX = m._bearing.x;
            gpu._bearingY = m._bearing.y;
            gpu._advance = m._advance;
            gpu._pad = 0.0f;

            _glyphMetadata.push_back(gpu);
        }
    };

    // Start with empty glyph at index 0
    GlyphMetadataGPU emptyGlyph = {};
    _glyphMetadata.push_back(emptyGlyph);

    // Add regular glyphs first
    addGlyphsFromMap(_glyphs, _codepointToIndex);

    // Add variant glyphs (they share the same _glyphMetadata array)
    addGlyphsFromMap(_boldGlyphs, _boldCodepointToIndex);
    addGlyphsFromMap(_italicGlyphs, _italicCodepointToIndex);
    addGlyphsFromMap(_boldItalicGlyphs, _boldItalicCodepointToIndex);

    std::cout << "Built glyph index map with " << _glyphMetadata.size() << " entries "
              << "(regular: " << _codepointToIndex.size()
              << ", bold: " << _boldCodepointToIndex.size()
              << ", italic: " << _italicCodepointToIndex.size()
              << ", bold-italic: " << _boldItalicCodepointToIndex.size() << ")" << std::endl;

    // Log sample glyph metrics for debugging
    auto itA = _codepointToIndex.find('A');
    if (itA != _codepointToIndex.end()) {
        const auto& m = _glyphMetadata[itA->second];
        ydebug("Sample 'A' regular: uv({:.4f},{:.4f})->({:.4f},{:.4f}) size({:.1f},{:.1f}) bearing({:.1f},{:.1f})",
                      m._uvMinX, m._uvMinY, m._uvMaxX, m._uvMaxY,
                      m._sizeX, m._sizeY, m._bearingX, m._bearingY);
    }
    auto itABold = _boldCodepointToIndex.find('A');
    if (itABold != _boldCodepointToIndex.end()) {
        const auto& m = _glyphMetadata[itABold->second];
        ydebug("Sample 'A' bold: uv({:.4f},{:.4f})->({:.4f},{:.4f}) size({:.1f},{:.1f}) bearing({:.1f},{:.1f})",
                      m._uvMinX, m._uvMinY, m._uvMaxX, m._uvMaxY,
                      m._sizeX, m._sizeY, m._bearingX, m._bearingY);
    }
}

uint16_t Font::getGlyphIndex(uint32_t codepoint) {
    auto it = _codepointToIndex.find(codepoint);
    if (it != _codepointToIndex.end()) {
        return it->second;
    }

    // Log missing codepoints in emoji range
    if (codepoint > 0x1F000) {
        ydebug("Missing emoji glyph U+{:04X}", codepoint);
    }

#if !YETTY_USE_PREBUILT_ATLAS
    // Try to load the glyph from a fallback font
    if (loadMissingGlyph(codepoint)) {
        it = _codepointToIndex.find(codepoint);
        if (it != _codepointToIndex.end()) {
            ydebug("Loaded fallback glyph U+{:04X} -> index {}", codepoint, it->second);
            return it->second;
        }
    }
#endif

    // Fall back to '?' character
    it = _codepointToIndex.find('?');
    if (it != _codepointToIndex.end()) {
        return it->second;
    }
    return 0;
}

uint16_t Font::getGlyphIndex(uint32_t codepoint, Style style) {
    // Try to find in the requested style's map first
    std::unordered_map<uint32_t, uint16_t>* variantMap = nullptr;

    switch (style) {
        case Bold:
            variantMap = &_boldCodepointToIndex;
            break;
        case Italic:
            variantMap = &_italicCodepointToIndex;
            break;
        case BoldItalic:
            variantMap = &_boldItalicCodepointToIndex;
            break;
        case Regular:
        default:
            // Regular style, just use the normal lookup
            return getGlyphIndex(codepoint);
    }

    // Try to find in the variant map
    if (variantMap && !variantMap->empty()) {
        auto it = variantMap->find(codepoint);
        if (it != variantMap->end()) {
            return it->second;
        }
    }

    // Fall back to regular style if variant not found
    // This handles cases where variant font wasn't loaded or doesn't have this glyph
    return getGlyphIndex(codepoint);
}

uint16_t Font::getGlyphIndex(uint32_t codepoint, bool bold, bool italic) {
    Style style = Regular;
    if (bold && italic) {
        style = BoldItalic;
    } else if (bold) {
        style = Bold;
    } else if (italic) {
        style = Italic;
    }
    return getGlyphIndex(codepoint, style);
}

bool Font::createGlyphMetadataBuffer(WGPUDevice device) {
    if (_glyphMetadata.empty()) {
        std::cerr << "No glyph metadata to create buffer from" << std::endl;
        return false;
    }

    size_t bufferSize = _glyphMetadata.size() * sizeof(GlyphMetadataGPU);

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.label = WGPU_STR("glyph metadata");
    bufDesc.size = bufferSize;
    bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    bufDesc.mappedAtCreation = true;

    _glyphMetadataBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_glyphMetadataBuffer) {
        std::cerr << "Failed to create glyph metadata buffer" << std::endl;
        return false;
    }

    void* mapped = wgpuBufferGetMappedRange(_glyphMetadataBuffer, 0, bufferSize);
    memcpy(mapped, _glyphMetadata.data(), bufferSize);
    wgpuBufferUnmap(_glyphMetadataBuffer);
    
    // Track the glyph count this buffer was created with
    _bufferGlyphCount = static_cast<uint32_t>(_glyphMetadata.size());
    // Increment version so renderables know to recreate their bind groups
    _resourceVersion++;

    std::cout << "Created glyph metadata buffer: " << bufferSize << " bytes for "
              << _glyphMetadata.size() << " glyphs (version " << _resourceVersion << ")" << std::endl;

    return true;
}

bool Font::createTexture(WGPUDevice device, WGPUQueue queue) {
    if (_atlasData.empty()) {
        std::cerr << "No atlas data to create texture from" << std::endl;
        return false;
    }

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = WGPU_STR("font atlas");
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {_atlasWidth, _atlasHeight, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    _texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!_texture) {
        std::cerr << "Failed to create font atlas texture" << std::endl;
        return false;
    }

    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = _texture;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = _atlasWidth * 4;
    layout.rowsPerImage = _atlasHeight;

    WGPUExtent3D extent = {_atlasWidth, _atlasHeight, 1};
    wgpuQueueWriteTexture(queue, &dest, _atlasData.data(), _atlasData.size(), &layout, &extent);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    _textureView = wgpuTextureCreateView(_texture, &viewDesc);

    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
#if YETTY_WEB
    samplerDesc.mipmapFilter = WGPU_MIPMAP_FILTER_LINEAR;
#else
    samplerDesc.mipmapFilter = WGPU_MIPMAP_FILTER_LINEAR;
#endif
    samplerDesc.maxAnisotropy = 1;

    _sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    std::cout << "Created font atlas texture" << std::endl;
    return true;
}

const GlyphMetrics* Font::getGlyph(uint32_t codepoint) const {
    auto it = _glyphs.find(codepoint);
    if (it != _glyphs.end()) {
        return &it->second;
    }
    it = _glyphs.find('?');
    if (it != _glyphs.end()) {
        return &it->second;
    }
    return nullptr;
}

const GlyphMetrics* Font::getGlyph(uint32_t codepoint, Style style) const {
    // Try to find in the requested style's map first
    const std::unordered_map<uint32_t, GlyphMetrics>* variantMap = nullptr;

    switch (style) {
        case Bold:
            variantMap = &_boldGlyphs;
            break;
        case Italic:
            variantMap = &_italicGlyphs;
            break;
        case BoldItalic:
            variantMap = &_boldItalicGlyphs;
            break;
        case Regular:
        default:
            // Regular style, just use the normal lookup
            return getGlyph(codepoint);
    }

    // Try to find in the variant map
    if (variantMap && !variantMap->empty()) {
        auto it = variantMap->find(codepoint);
        if (it != variantMap->end()) {
            return &it->second;
        }
    }

    // Fall back to regular style if variant not found
    return getGlyph(codepoint);
}

} // namespace yetty
