#include "font-manager.h"
#include "webgpu-context.h"
#include <spdlog/spdlog.h>

namespace yetty {

FontManager::FontManager() = default;

FontManager::~FontManager() {
    dispose();
}

Result<void> FontManager::init(WebGPUContext* ctx) {
    if (!ctx) {
        return Err<void>("FontManager::init: null WebGPUContext");
    }
    ctx_ = ctx;
    return Ok();
}

void FontManager::dispose() {
    unloadAll();
    ctx_ = nullptr;
}

Result<Font*> FontManager::loadFont(const std::string& name,
                                     const std::string& path,
                                     float fontSize,
                                     uint32_t atlasSize) {
    if (!ctx_) {
        return Err<Font*>("FontManager not initialized");
    }

    // Check if already loaded
    if (hasFont(name)) {
        spdlog::debug("FontManager: font '{}' already loaded", name);
        return Ok(fonts_[name].get());
    }

    auto font = std::make_unique<Font>();

#if !YETTY_USE_PREBUILT_ATLAS
    if (!font->generate(path, fontSize, atlasSize)) {
        return Err<Font*>("Failed to generate font atlas from: " + path);
    }

    if (!font->createTexture(ctx_->getDevice(), ctx_->getQueue())) {
        return Err<Font*>("Failed to create font texture");
    }

    if (!font->createGlyphMetadataBuffer(ctx_->getDevice())) {
        return Err<Font*>("Failed to create glyph metadata buffer");
    }
#else
    return Err<Font*>("Font generation not available on this platform. Use loadFontFromAtlas()");
#endif

    Font* ptr = font.get();
    fonts_[name] = std::move(font);

    // Set as default if first font
    if (defaultFontName_.empty()) {
        defaultFontName_ = name;
    }

    spdlog::info("FontManager: loaded font '{}' from {}", name, path);
    return Ok(ptr);
}

Result<Font*> FontManager::loadFont(const std::string& name,
                                     const std::string& regularPath,
                                     const std::string& boldPath,
                                     const std::string& italicPath,
                                     const std::string& boldItalicPath,
                                     float fontSize,
                                     uint32_t atlasSize) {
    if (!ctx_) {
        return Err<Font*>("FontManager not initialized");
    }

    if (hasFont(name)) {
        spdlog::debug("FontManager: font '{}' already loaded", name);
        return Ok(fonts_[name].get());
    }

    auto font = std::make_unique<Font>();

#if !YETTY_USE_PREBUILT_ATLAS
    if (!font->generate(regularPath, boldPath, italicPath, boldItalicPath, fontSize, atlasSize)) {
        return Err<Font*>("Failed to generate font atlas");
    }

    if (!font->createTexture(ctx_->getDevice(), ctx_->getQueue())) {
        return Err<Font*>("Failed to create font texture");
    }

    if (!font->createGlyphMetadataBuffer(ctx_->getDevice())) {
        return Err<Font*>("Failed to create glyph metadata buffer");
    }
#else
    return Err<Font*>("Font generation not available on this platform. Use loadFontFromAtlas()");
#endif

    Font* ptr = font.get();
    fonts_[name] = std::move(font);

    if (defaultFontName_.empty()) {
        defaultFontName_ = name;
    }

    spdlog::info("FontManager: loaded font '{}' with variants", name);
    return Ok(ptr);
}

Result<Font*> FontManager::loadFontFromAtlas(const std::string& name,
                                              const std::string& atlasPath,
                                              const std::string& metricsPath) {
    if (!ctx_) {
        return Err<Font*>("FontManager not initialized");
    }

    if (hasFont(name)) {
        spdlog::debug("FontManager: font '{}' already loaded", name);
        return Ok(fonts_[name].get());
    }

    auto font = std::make_unique<Font>();

    if (!font->loadAtlas(atlasPath, metricsPath)) {
        return Err<Font*>("Failed to load font atlas from: " + atlasPath);
    }

    if (!font->createTexture(ctx_->getDevice(), ctx_->getQueue())) {
        return Err<Font*>("Failed to create font texture");
    }

    if (!font->createGlyphMetadataBuffer(ctx_->getDevice())) {
        return Err<Font*>("Failed to create glyph metadata buffer");
    }

    Font* ptr = font.get();
    fonts_[name] = std::move(font);

    if (defaultFontName_.empty()) {
        defaultFontName_ = name;
    }

    spdlog::info("FontManager: loaded font '{}' from atlas", name);
    return Ok(ptr);
}

#if !YETTY_USE_PREBUILT_ATLAS
Result<Font*> FontManager::loadFontFromFreeType(const std::string& name,
                                                 FT_Face face,
                                                 float fontSize,
                                                 uint32_t atlasSize) {
    if (!ctx_) {
        return Err<Font*>("FontManager not initialized");
    }

    if (!face) {
        return Err<Font*>("Invalid FreeType face");
    }

    if (hasFont(name)) {
        spdlog::debug("FontManager: font '{}' already loaded", name);
        return Ok(fonts_[name].get());
    }

    // TODO: Implement font generation from FT_Face
    // This requires extending the Font class to accept FT_Face directly
    // For now, return error - will be implemented when integrating with PDF
    return Err<Font*>("loadFontFromFreeType not yet implemented");
}
#endif

Font* FontManager::getFont(const std::string& name) {
    auto it = fonts_.find(name);
    if (it != fonts_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool FontManager::hasFont(const std::string& name) const {
    return fonts_.find(name) != fonts_.end();
}

Font* FontManager::getDefaultFont() {
    if (defaultFontName_.empty()) {
        return nullptr;
    }
    return getFont(defaultFontName_);
}

void FontManager::setDefaultFont(const std::string& name) {
    if (hasFont(name)) {
        defaultFontName_ = name;
    }
}

void FontManager::unloadFont(const std::string& name) {
    fonts_.erase(name);
    if (defaultFontName_ == name) {
        defaultFontName_ = fonts_.empty() ? "" : fonts_.begin()->first;
    }
}

void FontManager::unloadAll() {
    fonts_.clear();
    defaultFontName_.clear();
}

std::vector<std::string> FontManager::getFontNames() const {
    std::vector<std::string> names;
    names.reserve(fonts_.size());
    for (const auto& [name, _] : fonts_) {
        names.push_back(name);
    }
    return names;
}

} // namespace yetty
