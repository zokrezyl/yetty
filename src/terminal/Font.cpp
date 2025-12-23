#include "Font.h"

#if !YETTY_USE_PREBUILT_ATLAS
#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdf-atlas-gen/GlyphGeometry.h>
#include <msdf-atlas-gen/FontGeometry.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

namespace yetty {

Font::Font() = default;

Font::~Font() {
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (textureView_) wgpuTextureViewRelease(textureView_);
    if (texture_) wgpuTextureRelease(texture_);
}

#if !YETTY_USE_PREBUILT_ATLAS

bool Font::generate(const std::string& fontPath, float fontSize, uint32_t atlasSize) {
    using namespace msdf_atlas;

    fontSize_ = fontSize;
    atlasWidth_ = atlasSize;
    atlasHeight_ = atlasSize;

    // Load font
    msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
    if (!ft) {
        std::cerr << "Failed to initialize FreeType" << std::endl;
        return false;
    }

    msdfgen::FontHandle* font = msdfgen::loadFont(ft, fontPath.c_str());
    if (!font) {
        std::cerr << "Failed to load font: " << fontPath << std::endl;
        msdfgen::deinitializeFreetype(ft);
        return false;
    }

    // Define character set (ASCII printable range)
    std::vector<msdf_atlas::GlyphGeometry> glyphGeometries;
    msdf_atlas::FontGeometry fontGeometry(&glyphGeometries);

    // Load ASCII glyphs (32-126)
    msdf_atlas::Charset charset;
    for (uint32_t c = 32; c <= 126; ++c) {
        charset.add(c);
    }

    fontGeometry.loadCharset(font, 1.0, charset);

    // Apply MSDF edge coloring
    const double maxCornerAngle = 3.0;
    for (auto& glyph : glyphGeometries) {
        glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, maxCornerAngle, 0);
    }

    // Pack atlas
    TightAtlasPacker packer;
    packer.setDimensions(atlasSize, atlasSize);
    packer.setMinimumScale(fontSize);
    packer.setPixelRange(pixelRange_);
    packer.setMiterLimit(1.0);
    packer.pack(glyphGeometries.data(), static_cast<int>(glyphGeometries.size()));

    int width = 0, height = 0;
    packer.getDimensions(width, height);
    atlasWidth_ = static_cast<uint32_t>(width);
    atlasHeight_ = static_cast<uint32_t>(height);

    // Generate MSDF atlas
    ImmediateAtlasGenerator<float, 3, msdfGenerator, BitmapAtlasStorage<msdfgen::byte, 3>> generator(width, height);
    GeneratorAttributes attributes;
    generator.setAttributes(attributes);
    generator.setThreadCount(4);
    generator.generate(glyphGeometries.data(), static_cast<int>(glyphGeometries.size()));

    // Get atlas bitmap
    msdfgen::BitmapConstRef<msdfgen::byte, 3> bitmap = generator.atlasStorage();

    // Convert to RGBA format for WebGPU
    // Flip Y: msdf-atlas uses bottom-left origin, WebGPU uses top-left
    atlasData_.resize(atlasWidth_ * atlasHeight_ * 4);
    for (uint32_t y = 0; y < atlasHeight_; ++y) {
        for (uint32_t x = 0; x < atlasWidth_; ++x) {
            uint32_t dstY = atlasHeight_ - 1 - y;  // Flip destination
            uint32_t dstIdx = (dstY * atlasWidth_ + x) * 4;
            const msdfgen::byte* pixel = bitmap(x, y);
            atlasData_[dstIdx + 0] = pixel[0];  // R
            atlasData_[dstIdx + 1] = pixel[1];  // G
            atlasData_[dstIdx + 2] = pixel[2];  // B
            atlasData_[dstIdx + 3] = 255;       // A
        }
    }

    // Extract glyph metrics
    const auto& metrics = fontGeometry.getMetrics();
    lineHeight_ = static_cast<float>(metrics.lineHeight * fontSize);

    for (const auto& glyph : glyphGeometries) {
        if (glyph.getCodepoint() == 0) continue;

        double al, ab, ar, at;  // Atlas bounds
        glyph.getQuadAtlasBounds(al, ab, ar, at);

        double pl, pb, pr, pt;  // Plane bounds
        glyph.getQuadPlaneBounds(pl, pb, pr, pt);

        GlyphMetrics m;
        // Flip UV Y coordinates since we flipped the texture
        float uvBottom = 1.0f - static_cast<float>(ab) / atlasHeight_;
        float uvTop = 1.0f - static_cast<float>(at) / atlasHeight_;
        m.uvMin = glm::vec2(static_cast<float>(al) / atlasWidth_, uvTop);    // top-left
        m.uvMax = glm::vec2(static_cast<float>(ar) / atlasWidth_, uvBottom); // bottom-right

        // Atlas bounds are in atlas pixels. The packer scaled glyphs by fontSize.
        // So atlas pixel size / fontSize gives em units, then * fontSize gives screen pixels.
        // But actually the atlas is at its own scale. We use plane bounds for logical size.
        m.size = glm::vec2(static_cast<float>((pr - pl) * fontSize), static_cast<float>((pt - pb) * fontSize));
        m.bearing = glm::vec2(static_cast<float>(pl * fontSize), static_cast<float>(pt * fontSize));
        m.advance = static_cast<float>(glyph.getAdvance() * fontSize);

        glyphs_[glyph.getCodepoint()] = m;

        // Debug output for 'A'
        if (glyph.getCodepoint() == 'A') {
            std::cout << "Glyph 'A' metrics:" << std::endl;
            std::cout << "  Atlas bounds: " << al << ", " << ab << " -> " << ar << ", " << at << std::endl;
            std::cout << "  Plane bounds: " << pl << ", " << pb << " -> " << pr << ", " << pt << std::endl;
            std::cout << "  UV: " << m.uvMin.x << ", " << m.uvMin.y << " -> " << m.uvMax.x << ", " << m.uvMax.y << std::endl;
            std::cout << "  Size: " << m.size.x << " x " << m.size.y << std::endl;
            std::cout << "  Bearing: " << m.bearing.x << ", " << m.bearing.y << std::endl;
        }
    }

    // Cleanup
    msdfgen::destroyFont(font);
    msdfgen::deinitializeFreetype(ft);

    std::cout << "Generated MSDF atlas: " << atlasWidth_ << "x" << atlasHeight_
              << " with " << glyphs_.size() << " glyphs" << std::endl;

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

// Parse a simple array like [1.0, 2.0]
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

    // Simple JSON parsing (not a full parser, just for our specific format)
    // Parse top-level fields
    auto findValue = [&json](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";

        pos += search.length();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;

        // Find end of value (comma, newline, or closing brace)
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

    // Find the glyphs object
    auto glyphsStart = json.find('{', glyphsPos + 9);
    if (glyphsStart == std::string::npos) return false;

    // Parse each glyph entry: "codepoint": { ... }
    size_t pos = glyphsStart + 1;
    while (pos < json.size()) {
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == ',')) pos++;

        if (json[pos] == '}') break;  // End of glyphs

        // Find codepoint key
        if (json[pos] != '"') { pos++; continue; }

        auto keyEnd = json.find('"', pos + 1);
        if (keyEnd == std::string::npos) break;

        uint32_t codepoint = parseUint(json.substr(pos + 1, keyEnd - pos - 1));
        pos = keyEnd + 1;

        // Find colon and opening brace
        auto braceStart = json.find('{', pos);
        if (braceStart == std::string::npos) break;

        // Find matching closing brace
        int depth = 1;
        size_t braceEnd = braceStart + 1;
        while (braceEnd < json.size() && depth > 0) {
            if (json[braceEnd] == '{') depth++;
            else if (json[braceEnd] == '}') depth--;
            braceEnd++;
        }

        std::string glyphJson = json.substr(braceStart, braceEnd - braceStart);

        // Parse glyph fields
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

    return true;
}

bool Font::createTexture(WGPUDevice device, WGPUQueue queue) {
    if (atlasData_.empty()) {
        std::cerr << "No atlas data to create texture from" << std::endl;
        return false;
    }

    // Create texture
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

    // Upload texture data
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

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
#if YETTY_WEB
    samplerDesc.mipmapFilter = WGPUFilterMode_Linear;  // Emscripten uses WGPUFilterMode
#else
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;  // wgpu-native
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
    // Fallback to space or question mark
    it = glyphs_.find('?');
    if (it != glyphs_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace yetty
