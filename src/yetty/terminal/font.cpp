#include "font.h"

#if !YETTY_USE_PREBUILT_ATLAS
#include <msdfgen.h>
#include <msdfgen-ext.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace yetty {

Font::Font() = default;

Font::~Font() {
    if (glyphMetadataBuffer_) wgpuBufferRelease(glyphMetadataBuffer_);
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (textureView_) wgpuTextureViewRelease(textureView_);
    if (texture_) wgpuTextureRelease(texture_);
}

#if !YETTY_USE_PREBUILT_ATLAS

// Simple rectangle packing for atlas
struct PackedGlyph {
    uint32_t codepoint;
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
          shelfX_(padding), shelfY_(padding), shelfHeight_(0) {}

    bool pack(int w, int h, int& outX, int& outY) {
        // Add padding
        int pw = w + padding_;
        int ph = h + padding_;

        // Check if fits on current shelf
        if (shelfX_ + pw > width_) {
            // Move to next shelf
            shelfX_ = padding_;
            shelfY_ += shelfHeight_ + padding_;
            shelfHeight_ = 0;
        }

        // Check if fits vertically
        if (shelfY_ + ph > height_) {
            return false;  // Atlas full
        }

        outX = shelfX_;
        outY = shelfY_;
        shelfX_ += pw;
        shelfHeight_ = std::max(shelfHeight_, ph);
        return true;
    }

private:
    int width_, height_, padding_;
    int shelfX_, shelfY_, shelfHeight_;
};

bool Font::generate(const std::string& fontPath, float fontSize, uint32_t atlasSize) {
    fontSize_ = fontSize;
    atlasWidth_ = atlasSize;
    atlasHeight_ = atlasSize;

    // Initialize FreeType
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        std::cerr << "Failed to initialize FreeType" << std::endl;
        return false;
    }

    // Load font with FreeType
    msdfgen::FontHandle* font = msdfgen::loadFont(ft, fontPath.c_str());
    if (!font) {
        std::cerr << "Failed to load font: " << fontPath << std::endl;
        msdfgen::deinitializeFreetype(ft);
        return false;
    }

    // Get font metrics
    msdfgen::FontMetrics metrics;
    msdfgen::getFontMetrics(metrics, font);
    double unitsPerEm = metrics.emSize > 0 ? metrics.emSize : (metrics.ascenderY - metrics.descenderY);
    double fontScale = fontSize / unitsPerEm;
    double lineHeight = metrics.lineHeight;

    // Define character set
    std::vector<uint32_t> charset;

    // ASCII printable (32-126)
    for (uint32_t c = 32; c <= 126; ++c) charset.push_back(c);

    // Latin Extended-A (0x0100-0x017F)
    for (uint32_t c = 0x0100; c <= 0x017F; ++c) charset.push_back(c);

    // Latin Extended-B subset (0x0180-0x024F)
    for (uint32_t c = 0x0180; c <= 0x024F; ++c) charset.push_back(c);

    // General Punctuation (0x2000-0x206F)
    for (uint32_t c = 0x2000; c <= 0x206F; ++c) charset.push_back(c);

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

    // Load glyph shapes
    std::vector<PackedGlyph> glyphs;

    for (uint32_t codepoint : charset) {
        PackedGlyph pg;
        pg.codepoint = codepoint;

        double advance;
        if (!msdfgen::loadGlyph(pg.shape, font, codepoint, &advance)) continue;

        pg.advance = advance * fontScale;

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
            pg.bearingX = bounds.l * fontScale;
            pg.bearingY = bounds.t * fontScale;  // Top of glyph from baseline
            pg.sizeX = (bounds.r - bounds.l) * fontScale;
            pg.sizeY = (bounds.t - bounds.b) * fontScale;

            // Calculate atlas size needed (with padding for SDF range)
            int padding = static_cast<int>(std::ceil(pixelRange_));
            pg.atlasW = static_cast<int>(std::ceil(pg.sizeX)) + padding * 2;
            pg.atlasH = static_cast<int>(std::ceil(pg.sizeY)) + padding * 2;
        } else {
            // Empty glyph (like space)
            pg.boundsL = pg.boundsB = pg.boundsR = pg.boundsT = 0;
            pg.bearingX = pg.bearingY = 0;
            pg.sizeX = pg.sizeY = 0;
            pg.atlasW = pg.atlasH = 0;
        }

        glyphs.push_back(std::move(pg));
    }

    std::cout << "Loaded " << glyphs.size() << " glyphs from font" << std::endl;

    // Sort by height for better packing
    std::sort(glyphs.begin(), glyphs.end(), [](const PackedGlyph& a, const PackedGlyph& b) {
        return a.atlasH > b.atlasH;
    });

    // Pack glyphs into atlas
    ShelfPacker packer(atlasWidth_, atlasHeight_);
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
    atlasData_.resize(atlasWidth_ * atlasHeight_ * 4, 0);

    // Generate MSDF for each glyph
    int padding = static_cast<int>(std::ceil(pixelRange_));

    for (auto& glyph : glyphs) {
        if (glyph.shape.contours.empty()) continue;

        // Validate and prepare shape
        if (!glyph.shape.validate()) {
            std::cerr << "Warning: Invalid shape for codepoint " << glyph.codepoint << std::endl;
        }

        // Normalize shape and apply edge coloring
        glyph.shape.normalize();
        msdfgen::edgeColoringSimple(glyph.shape, 3.0);

        double scale = fontScale;

        // Create MSDF bitmap for this glyph
        msdfgen::Bitmap<float, 3> msdf(glyph.atlasW, glyph.atlasH);

        // Calculate transformation: shape coords -> bitmap coords
        // Position shape so that (boundsL, boundsB) maps to (padding, padding)
        msdfgen::Vector2 translate(
            padding - glyph.boundsL * scale,
            padding - glyph.boundsB * scale
        );

        msdfgen::generateMSDF(msdf, glyph.shape, pixelRange_, scale, translate);

        // Copy to atlas with Y-flip (TrueType Y-up -> bitmap/WebGPU Y-down)
        for (int y = 0; y < glyph.atlasH; ++y) {
            for (int x = 0; x < glyph.atlasW; ++x) {
                int atlasX = glyph.atlasX + x;
                int atlasY = glyph.atlasY + (glyph.atlasH - 1 - y);  // Flip Y

                if (atlasX >= 0 && atlasX < (int)atlasWidth_ &&
                    atlasY >= 0 && atlasY < (int)atlasHeight_) {
                    size_t idx = (atlasY * atlasWidth_ + atlasX) * 4;
                    atlasData_[idx + 0] = static_cast<uint8_t>(std::clamp(msdf(x, y)[0] * 255.0f, 0.0f, 255.0f));
                    atlasData_[idx + 1] = static_cast<uint8_t>(std::clamp(msdf(x, y)[1] * 255.0f, 0.0f, 255.0f));
                    atlasData_[idx + 2] = static_cast<uint8_t>(std::clamp(msdf(x, y)[2] * 255.0f, 0.0f, 255.0f));
                    atlasData_[idx + 3] = 255;
                }
            }
        }
    }

    // Calculate line height
    lineHeight_ = static_cast<float>(lineHeight * fontScale);

    // Cleanup FreeType
    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(ft);

    // Extract glyph metrics
    for (const auto& glyph : glyphs) {
        GlyphMetrics m;

        if (glyph.atlasW > 0 && glyph.atlasH > 0) {
            // UV coordinates
            m.uvMin = glm::vec2(
                static_cast<float>(glyph.atlasX) / atlasWidth_,
                static_cast<float>(glyph.atlasY) / atlasHeight_
            );
            m.uvMax = glm::vec2(
                static_cast<float>(glyph.atlasX + glyph.atlasW) / atlasWidth_,
                static_cast<float>(glyph.atlasY + glyph.atlasH) / atlasHeight_
            );
        } else {
            m.uvMin = m.uvMax = glm::vec2(0);
        }

        m.size = glm::vec2(static_cast<float>(glyph.atlasW), static_cast<float>(glyph.atlasH));

        // Bearing: offset from cursor position to glyph quad
        // bearingX: horizontal offset (left side of glyph minus padding)
        // bearingY: vertical offset from baseline to TOP of glyph quad
        //           In screen coords (Y-down), positive bearingY means glyph extends above baseline
        m.bearing = glm::vec2(
            static_cast<float>(glyph.bearingX - padding),           // Left edge offset
            static_cast<float>(glyph.boundsT * fontScale + padding) // Top of glyph from baseline
        );
        m.advance = static_cast<float>(glyph.advance);

        glyphs_[glyph.codepoint] = m;
    }

    std::cout << "Generated MSDF atlas: " << atlasWidth_ << "x" << atlasHeight_
              << " with " << glyphs_.size() << " glyphs" << std::endl;

    // Build codepoint→index mapping
    buildGlyphIndexMap();

    return true;
}

bool Font::saveAtlas(const std::string& atlasPath, const std::string& metricsPath) const {
    if (atlasData_.empty()) {
        std::cerr << "No atlas data to save" << std::endl;
        return false;
    }

    // Save atlas as PNG
    if (!stbi_write_png(atlasPath.c_str(), atlasWidth_, atlasHeight_, 4, atlasData_.data(), atlasWidth_ * 4)) {
        std::cerr << "Failed to write atlas PNG: " << atlasPath << std::endl;
        return false;
    }

    // Save metrics as JSON
    std::ofstream file(metricsPath);
    if (!file) {
        std::cerr << "Failed to open metrics file: " << metricsPath << std::endl;
        return false;
    }

    file << "{\n";
    file << "  \"atlasWidth\": " << atlasWidth_ << ",\n";
    file << "  \"atlasHeight\": " << atlasHeight_ << ",\n";
    file << "  \"fontSize\": " << fontSize_ << ",\n";
    file << "  \"lineHeight\": " << lineHeight_ << ",\n";
    file << "  \"pixelRange\": " << pixelRange_ << ",\n";
    file << "  \"glyphs\": {\n";

    bool first = true;
    for (const auto& [codepoint, m] : glyphs_) {
        if (!first) file << ",\n";
        first = false;

        file << "    \"" << codepoint << "\": {\n";
        file << "      \"uvMin\": [" << m.uvMin.x << ", " << m.uvMin.y << "],\n";
        file << "      \"uvMax\": [" << m.uvMax.x << ", " << m.uvMax.y << "],\n";
        file << "      \"size\": [" << m.size.x << ", " << m.size.y << "],\n";
        file << "      \"bearing\": [" << m.bearing.x << ", " << m.bearing.y << "],\n";
        file << "      \"advance\": " << m.advance << "\n";
        file << "    }";
    }

    file << "\n  }\n";
    file << "}\n";

    std::cout << "Saved atlas to " << atlasPath << " and metrics to " << metricsPath << std::endl;
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
    // Load atlas image
    int width, height, channels;
    unsigned char* data = stbi_load(atlasPath.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load atlas: " << atlasPath << std::endl;
        return false;
    }

    atlasWidth_ = static_cast<uint32_t>(width);
    atlasHeight_ = static_cast<uint32_t>(height);
    atlasData_.assign(data, data + (width * height * 4));
    stbi_image_free(data);

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
    if (!val.empty()) atlasWidth_ = parseUint(val);

    val = findValue("atlasHeight");
    if (!val.empty()) atlasHeight_ = parseUint(val);

    val = findValue("fontSize");
    if (!val.empty()) fontSize_ = parseFloat(val);

    val = findValue("lineHeight");
    if (!val.empty()) lineHeight_ = parseFloat(val);

    val = findValue("pixelRange");
    if (!val.empty()) pixelRange_ = parseFloat(val);

    // Parse glyphs
    auto glyphsPos = json.find("\"glyphs\":");
    if (glyphsPos == std::string::npos) {
        std::cerr << "No glyphs found in metrics" << std::endl;
        return false;
    }

    auto glyphsStart = json.find('{', glyphsPos + 9);
    if (glyphsStart == std::string::npos) return false;

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

        m.uvMin = parseVec2(findGlyphValue("uvMin"));
        m.uvMax = parseVec2(findGlyphValue("uvMax"));
        m.size = parseVec2(findGlyphValue("size"));
        m.bearing = parseVec2(findGlyphValue("bearing"));

        val = findGlyphValue("advance");
        m.advance = val.empty() ? 0.0f : parseFloat(val);

        glyphs_[codepoint] = m;
        pos = braceEnd;
    }

    std::cout << "Loaded atlas " << atlasWidth_ << "x" << atlasHeight_
              << " with " << glyphs_.size() << " glyphs from " << metricsPath << std::endl;

    buildGlyphIndexMap();

    return true;
}

void Font::buildGlyphIndexMap() {
    codepointToIndex_.clear();
    glyphMetadata_.clear();

    GlyphMetadataGPU emptyGlyph = {};
    glyphMetadata_.push_back(emptyGlyph);

    std::vector<uint32_t> codepoints;
    codepoints.reserve(glyphs_.size());
    for (const auto& [cp, _] : glyphs_) {
        codepoints.push_back(cp);
    }
    std::sort(codepoints.begin(), codepoints.end());

    for (uint32_t cp : codepoints) {
        uint16_t index = static_cast<uint16_t>(glyphMetadata_.size());
        codepointToIndex_[cp] = index;

        const GlyphMetrics& m = glyphs_[cp];
        GlyphMetadataGPU gpu;
        gpu.uvMinX = m.uvMin.x;
        gpu.uvMinY = m.uvMin.y;
        gpu.uvMaxX = m.uvMax.x;
        gpu.uvMaxY = m.uvMax.y;
        gpu.sizeX = m.size.x;
        gpu.sizeY = m.size.y;
        gpu.bearingX = m.bearing.x;
        gpu.bearingY = m.bearing.y;
        gpu.advance = m.advance;
        gpu._pad = 0.0f;

        glyphMetadata_.push_back(gpu);
    }

    std::cout << "Built glyph index map with " << glyphMetadata_.size() << " entries" << std::endl;
}

uint16_t Font::getGlyphIndex(uint32_t codepoint) const {
    auto it = codepointToIndex_.find(codepoint);
    if (it != codepointToIndex_.end()) {
        return it->second;
    }
    it = codepointToIndex_.find('?');
    if (it != codepointToIndex_.end()) {
        return it->second;
    }
    return 0;
}

bool Font::createGlyphMetadataBuffer(WGPUDevice device) {
    if (glyphMetadata_.empty()) {
        std::cerr << "No glyph metadata to create buffer from" << std::endl;
        return false;
    }

    size_t bufferSize = glyphMetadata_.size() * sizeof(GlyphMetadataGPU);

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.label = "glyph metadata";
    bufDesc.size = bufferSize;
    bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    bufDesc.mappedAtCreation = true;

    glyphMetadataBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!glyphMetadataBuffer_) {
        std::cerr << "Failed to create glyph metadata buffer" << std::endl;
        return false;
    }

    void* mapped = wgpuBufferGetMappedRange(glyphMetadataBuffer_, 0, bufferSize);
    memcpy(mapped, glyphMetadata_.data(), bufferSize);
    wgpuBufferUnmap(glyphMetadataBuffer_);

    std::cout << "Created glyph metadata buffer: " << bufferSize << " bytes for "
              << glyphMetadata_.size() << " glyphs" << std::endl;

    return true;
}

bool Font::createTexture(WGPUDevice device, WGPUQueue queue) {
    if (atlasData_.empty()) {
        std::cerr << "No atlas data to create texture from" << std::endl;
        return false;
    }

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = "font atlas";
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {atlasWidth_, atlasHeight_, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    texture_ = wgpuDeviceCreateTexture(device, &texDesc);
    if (!texture_) {
        std::cerr << "Failed to create font atlas texture" << std::endl;
        return false;
    }

    WGPUImageCopyTexture dest = {};
    dest.texture = texture_;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTextureDataLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = atlasWidth_ * 4;
    layout.rowsPerImage = atlasHeight_;

    WGPUExtent3D extent = {atlasWidth_, atlasHeight_, 1};
    wgpuQueueWriteTexture(queue, &dest, atlasData_.data(), atlasData_.size(), &layout, &extent);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);

    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
#if YETTY_WEB
    samplerDesc.mipmapFilter = WGPUFilterMode_Linear;
#else
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
#endif
    samplerDesc.maxAnisotropy = 1;

    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);

    std::cout << "Created font atlas texture" << std::endl;
    return true;
}

const GlyphMetrics* Font::getGlyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    it = glyphs_.find('?');
    if (it != glyphs_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace yetty
