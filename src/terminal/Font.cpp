#include "Font.h"

#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdf-atlas-gen/GlyphGeometry.h>
#include <msdf-atlas-gen/FontGeometry.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <iostream>
#include <fstream>

namespace yetty {

Font::Font() = default;

Font::~Font() {
    if (sampler_) wgpuSamplerRelease(sampler_);
    if (textureView_) wgpuTextureViewRelease(textureView_);
    if (texture_) wgpuTextureRelease(texture_);
}

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

        // Size should match atlas bounds (includes SDF padding) for correct UV mapping
        // Scale from atlas pixels to target fontSize
        double atlasScale = fontSize / fontGeometry.getMetrics().emSize;
        m.size = glm::vec2(
            static_cast<float>((ar - al) * atlasScale),
            static_cast<float>((at - ab) * atlasScale)
        );
        // Bearing uses plane bounds for correct positioning
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

    // Load metrics from JSON (simplified - you'd want a proper JSON parser)
    // For now, regenerate is the preferred method
    std::cerr << "loadAtlas: metrics loading not implemented, use generate() instead" << std::endl;
    return false;
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
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
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
