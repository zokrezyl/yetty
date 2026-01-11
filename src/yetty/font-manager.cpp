#include <yetty/font-manager.h>
#include <yetty/webgpu-context.h>
#include <ytrace/ytrace.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdlib>

#if !YETTY_USE_PREBUILT_ATLAS
#include <ft2build.h>
#include FT_FREETYPE_H
#ifdef _WIN32
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#pragma comment(lib, "dwrite.lib")
using Microsoft::WRL::ComPtr;
#else
#include <fontconfig/fontconfig.h>
#endif
#endif

namespace fs = std::filesystem;

namespace yetty {

Result<FontManager::Ptr> FontManager::create(WebGPUContext::Ptr ctx) noexcept {
    if (!ctx) {
        return Err<Ptr>("FontManager::create: null WebGPUContext");
    }
    auto mgr = Ptr(new FontManager(std::move(ctx)));
    if (auto res = mgr->init(); !res) {
        return Err<Ptr>("Failed to initialize FontManager", res);
    }
    return Ok(std::move(mgr));
}

FontManager::FontManager(WebGPUContext::Ptr ctx) noexcept
    : _ctx(std::move(ctx)) {}

FontManager::~FontManager() {
    unloadAll();
}

Result<void> FontManager::init() noexcept {
    // Nothing to initialize currently
    return Ok();
}

FontCacheKey FontManager::createCacheKey(FT_Face face) noexcept {
    FontCacheKey key;
#if !YETTY_USE_PREBUILT_ATLAS
    key.familyName = face->family_name ? face->family_name : "";
    key.styleName = face->style_name ? face->style_name : "";
    // Use actual font properties to distinguish embedded fonts with empty/same names
    key.numGlyphs = face->num_glyphs;
    key.unitsPerEM = face->units_per_EM;
#endif
    return key;
}

FontCacheKey FontManager::createCacheKey(const std::string& family, Font::Style style) noexcept {
    FontCacheKey key;
    key.familyName = family;

    switch (style) {
        case Font::Bold:
            key.styleName = "Bold";
            break;
        case Font::Italic:
            key.styleName = "Italic";
            break;
        case Font::BoldItalic:
            key.styleName = "Bold Italic";
            break;
        case Font::Regular:
        default:
            key.styleName = "Regular";
            break;
    }

    key.numGlyphs = 0;
    key.unitsPerEM = 0;

    return key;
}

std::string FontManager::findFontPath(const std::string& family, Font::Style style) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS

    // If family looks like a file path, return it directly (possibly with style suffix)
    bool isPath = (family.find('/') != std::string::npos || 
                   family.find(".ttf") != std::string::npos ||
                   family.find(".otf") != std::string::npos);
    if (isPath) {
        // For paths, try to find style variants using the same pattern as Font::discoverVariantPaths
        if (style == Font::Regular) {
            return family;
        }
        // Extract base name and extension
        std::string basePath = family;
        std::string ext;
        auto dotPos = family.rfind('.');
        if (dotPos != std::string::npos) {
            basePath = family.substr(0, dotPos);
            ext = family.substr(dotPos);
        }
        // Remove -Regular suffix if present
        if (basePath.size() > 8 && basePath.substr(basePath.size() - 8) == "-Regular") {
            basePath = basePath.substr(0, basePath.size() - 8);
        }
        // Add style suffix
        std::string styleSuffix;
        switch (style) {
            case Font::Bold: styleSuffix = "-Bold"; break;
            case Font::Italic: styleSuffix = "-Oblique"; break;
            case Font::BoldItalic: styleSuffix = "-BoldOblique"; break;
            default: styleSuffix = "-Regular"; break;
        }
        return basePath + styleSuffix + ext;
    }

#ifdef _WIN32
    // Windows DirectWrite implementation
    ComPtr<IDWriteFactory> factory;
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory.GetAddressOf())
    );
    if (FAILED(hr)) {
        yerror("FontManager: Failed to create DirectWrite factory");
        return "";
    }

    ComPtr<IDWriteFontCollection> fontCollection;
    hr = factory->GetSystemFontCollection(&fontCollection);
    if (FAILED(hr)) {
        yerror("FontManager: Failed to get system font collection");
        return "";
    }

    // Convert family name to wide string
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, family.c_str(), -1, nullptr, 0);
    std::wstring wideFamily(wideLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, family.c_str(), -1, wideFamily.data(), wideLen);

    // Find the font family
    UINT32 familyIndex = 0;
    BOOL exists = FALSE;
    hr = fontCollection->FindFamilyName(wideFamily.c_str(), &familyIndex, &exists);
    if (FAILED(hr) || !exists) {
        ywarn("FontManager: Could not find font family '{}'", family);
        return "";
    }

    ComPtr<IDWriteFontFamily> fontFamily;
    hr = fontCollection->GetFontFamily(familyIndex, &fontFamily);
    if (FAILED(hr)) {
        return "";
    }

    // Determine weight and style
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR;
    DWRITE_FONT_STYLE fontStyle = DWRITE_FONT_STYLE_NORMAL;

    switch (style) {
        case Font::Bold:
            weight = DWRITE_FONT_WEIGHT_BOLD;
            fontStyle = DWRITE_FONT_STYLE_NORMAL;
            break;
        case Font::Italic:
            weight = DWRITE_FONT_WEIGHT_REGULAR;
            fontStyle = DWRITE_FONT_STYLE_ITALIC;
            break;
        case Font::BoldItalic:
            weight = DWRITE_FONT_WEIGHT_BOLD;
            fontStyle = DWRITE_FONT_STYLE_ITALIC;
            break;
        case Font::Regular:
        default:
            weight = DWRITE_FONT_WEIGHT_REGULAR;
            fontStyle = DWRITE_FONT_STYLE_NORMAL;
            break;
    }

    ComPtr<IDWriteFont> font;
    hr = fontFamily->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, fontStyle, &font);
    if (FAILED(hr)) {
        ywarn("FontManager: Could not find matching font for '{}' with style {}", family, static_cast<int>(style));
        return "";
    }

    ComPtr<IDWriteFontFace> fontFace;
    hr = font->CreateFontFace(&fontFace);
    if (FAILED(hr)) {
        return "";
    }

    UINT32 fileCount = 0;
    hr = fontFace->GetFiles(&fileCount, nullptr);
    if (FAILED(hr) || fileCount == 0) {
        return "";
    }

    ComPtr<IDWriteFontFile> fontFile;
    hr = fontFace->GetFiles(&fileCount, &fontFile);
    if (FAILED(hr)) {
        return "";
    }

    const void* fontFileReferenceKey = nullptr;
    UINT32 fontFileReferenceKeySize = 0;
    hr = fontFile->GetReferenceKey(&fontFileReferenceKey, &fontFileReferenceKeySize);
    if (FAILED(hr)) {
        return "";
    }

    ComPtr<IDWriteFontFileLoader> loader;
    hr = fontFile->GetLoader(&loader);
    if (FAILED(hr)) {
        return "";
    }

    ComPtr<IDWriteLocalFontFileLoader> localLoader;
    hr = loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), reinterpret_cast<void**>(localLoader.GetAddressOf()));
    if (FAILED(hr)) {
        ywarn("FontManager: Font is not a local file");
        return "";
    }

    UINT32 pathLen = 0;
    hr = localLoader->GetFilePathLengthFromKey(fontFileReferenceKey, fontFileReferenceKeySize, &pathLen);
    if (FAILED(hr)) {
        return "";
    }

    std::wstring widePath(pathLen + 1, L'\0');
    hr = localLoader->GetFilePathFromKey(fontFileReferenceKey, fontFileReferenceKeySize, widePath.data(), pathLen + 1);
    if (FAILED(hr)) {
        return "";
    }

    // Convert wide path to UTF-8
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string fontPath(utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, widePath.c_str(), -1, fontPath.data(), utf8Len, nullptr, nullptr);

    // Remove null terminator from string
    if (!fontPath.empty() && fontPath.back() == '\0') {
        fontPath.pop_back();
    }

    ydebug("FontManager: Found font '{}' {} at: {}",
                  family,
                  style == Font::Bold ? "Bold" :
                  style == Font::Italic ? "Italic" :
                  style == Font::BoldItalic ? "BoldItalic" : "Regular",
                  fontPath);

    return fontPath;

#else
    // Unix fontconfig implementation
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config) {
        yerror("FontManager: Failed to initialize fontconfig");
        return "";
    }

    FcPattern* pattern = FcPatternCreate();
    if (!pattern) {
        FcConfigDestroy(config);
        return "";
    }

    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));

    switch (style) {
        case Font::Bold:
            FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
            FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
            break;
        case Font::Italic:
            FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
            FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
            break;
        case Font::BoldItalic:
            FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
            FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ITALIC);
            break;
        case Font::Regular:
        default:
            FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR);
            FcPatternAddInteger(pattern, FC_SLANT, FC_SLANT_ROMAN);
            break;
    }

    FcPatternAddBool(pattern, FC_OUTLINE, FcTrue);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);

    FcConfigSubstitute(config, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);

    FcResult result;
    FcPattern* match = FcFontMatch(config, pattern, &result);

    std::string fontPath;
    if (match) {
        FcChar8* file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            fontPath = reinterpret_cast<const char*>(file);
        }
        FcPatternDestroy(match);
    }

    FcPatternDestroy(pattern);
    FcConfigDestroy(config);

    if (!fontPath.empty()) {
        ydebug("FontManager: Found font '{}' {} at: {}",
                      family,
                      style == Font::Bold ? "Bold" :
                      style == Font::Italic ? "Italic" :
                      style == Font::BoldItalic ? "BoldItalic" : "Regular",
                      fontPath);
    } else {
        ywarn("FontManager: Could not find font '{}' with style {}", family, static_cast<int>(style));
    }

    return fontPath;
#endif

#else
    ywarn("FontManager: findFontPath not available on this platform");
    return "";
#endif
}

Result<std::unique_ptr<Font>> FontManager::generateFont(const std::string& path, float fontSize) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    auto font = std::make_unique<Font>();

    if (!font->generate(path, fontSize)) {
        return Err<std::unique_ptr<Font>>("Failed to generate font atlas from: " + path);
    }

    if (!font->createTexture(_ctx->getDevice(), _ctx->getQueue())) {
        return Err<std::unique_ptr<Font>>("Failed to create font texture");
    }

    if (!font->createGlyphMetadataBuffer(_ctx->getDevice())) {
        return Err<std::unique_ptr<Font>>("Failed to create glyph metadata buffer");
    }

    return Ok(std::move(font));
#else
    return Err<std::unique_ptr<Font>>("Font generation not available on this platform");
#endif
}

Result<std::unique_ptr<Font>> FontManager::generateFont(FT_Face face, const std::string& fontName, float fontSize) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    auto font = std::make_unique<Font>();

    if (!font->generate(face, fontName, fontSize)) {
        return Err<std::unique_ptr<Font>>("Failed to generate font atlas from FT_Face");
    }

    if (!font->createTexture(_ctx->getDevice(), _ctx->getQueue())) {
        return Err<std::unique_ptr<Font>>("Failed to create font texture for embedded font");
    }

    if (!font->createGlyphMetadataBuffer(_ctx->getDevice())) {
        return Err<std::unique_ptr<Font>>("Failed to create glyph metadata buffer for embedded font");
    }

    return Ok(std::move(font));
#else
    return Err<std::unique_ptr<Font>>("Font generation not available on this platform");
#endif
}

Result<Font*> FontManager::getFont(const std::string& family, Font::Style style, float fontSize) noexcept {
    FontCacheKey key = createCacheKey(family, style);

    // Check memory cache first
    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        ydebug("FontManager: memory cache hit for '{}' {}", family, static_cast<int>(style));
        return Ok(it->second.get());
    }

    // Also check if font was loaded from embedded data (uses empty styleName)
    FontCacheKey embeddedKey;
    embeddedKey.familyName = family;
    embeddedKey.styleName = "";
    embeddedKey.numGlyphs = 0;
    embeddedKey.unitsPerEM = 0;
    it = fontCache_.find(embeddedKey);
    if (it != fontCache_.end()) {
        ydebug("FontManager: found embedded font '{}' in cache", family);
        return Ok(it->second.get());
    }

    std::string fontPath = findFontPath(family, style);
    if (fontPath.empty()) {
#if YETTY_USE_PREBUILT_ATLAS
        // On web/Android, fall back to default prebuilt font
        if (hasDefaultFont_) {
            auto defaultIt = fontCache_.find(defaultFontKey_);
            if (defaultIt != fontCache_.end()) {
                yinfo("FontManager: using default prebuilt font for '{}'", family);
                return Ok(defaultIt->second.get());
            }
        }
#endif
        return Err<Font*>("Could not find font: " + family);
    }

    // Read font file to get data for disk cache key
    std::ifstream fontFile(fontPath, std::ios::binary | std::ios::ate);
    if (!fontFile) {
        return Err<Font*>("Could not read font file: " + fontPath);
    }
    size_t fileSize = fontFile.tellg();
    fontFile.seekg(0, std::ios::beg);
    std::vector<unsigned char> fontData(fileSize);
    if (!fontFile.read(reinterpret_cast<char*>(fontData.data()), fileSize)) {
        return Err<Font*>("Failed to read font file: " + fontPath);
    }
    fontFile.close();

    // Create cache path using font file data hash
    std::string styleName = style == Font::Bold ? "Bold" :
                            style == Font::Italic ? "Italic" :
                            style == Font::BoldItalic ? "BoldItalic" : "Regular";
    std::string cacheName = family + "-" + styleName;
    std::string cachePath = getCachePath(cacheName, fontSize, fontData.data(), fontData.size());

    // Check disk cache
    yinfo("FontManager: looking for disk cache: {}.lz4", cachePath);
    auto diskResult = loadFromDiskCache(cachePath);
    if (diskResult) {
        yinfo("FontManager: DISK CACHE HIT for font '{}' {}", family, styleName);
        Font* ptr = diskResult.value().get();
        fontCache_[key] = std::move(diskResult.value());

        if (!hasDefaultFont_) {
            defaultFontKey_ = key;
            hasDefaultFont_ = true;
        }

        return Ok(ptr);
    }

    // Generate font from file path
    yinfo("FontManager: DISK CACHE MISS for font '{}' {}, generating atlas...", family, styleName);
    auto result = generateFont(fontPath, fontSize);
    if (!result) {
        return Err<Font*>(result.error().message());
    }

    Font* ptr = result.value().get();

    // Save to disk cache
    saveToDiskCache(ptr, cachePath);

    fontCache_[key] = std::move(result.value());

    if (!hasDefaultFont_) {
        defaultFontKey_ = key;
        hasDefaultFont_ = true;
    }

    yinfo("FontManager: loaded font '{}' {} from {}", family, static_cast<int>(style), fontPath);
    return Ok(ptr);
}

Result<Font*> FontManager::getFont(FT_Face face, const std::string& fontName, float fontSize) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    if (!face) {
        return Err<Font*>("Invalid FreeType face");
    }

    // Use the provided fontName for caching (MuPDF font names are unique)
    FontCacheKey key;
    key.familyName = fontName;
    key.styleName = "";
    key.numGlyphs = 0;
    key.unitsPerEM = 0;

    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        ydebug("FontManager: cache hit for FT_Face '{}'", fontName);
        return Ok(it->second.get());
    }

    yinfo("FontManager: cache miss for FT_Face '{}', generating font", fontName);

    auto result = generateFont(face, fontName, fontSize);
    if (!result) {
        return Err<Font*>(result.error().message());
    }

    Font* ptr = result.value().get();
    fontCache_[key] = std::move(result.value());

    if (!hasDefaultFont_) {
        defaultFontKey_ = key;
        hasDefaultFont_ = true;
    }

    return Ok(ptr);
#else
    return Err<Font*>("FreeType font loading not available on this platform");
#endif
}

Result<std::unique_ptr<Font>> FontManager::generateFont(const unsigned char* data, size_t dataLen,
                                                         const std::string& fontName, float fontSize) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    auto font = std::make_unique<Font>();

    if (!font->generate(data, dataLen, fontName, fontSize)) {
        return Err<std::unique_ptr<Font>>("Failed to generate font atlas from font data");
    }

    if (!font->createTexture(_ctx->getDevice(), _ctx->getQueue())) {
        return Err<std::unique_ptr<Font>>("Failed to create font texture for embedded font");
    }

    if (!font->createGlyphMetadataBuffer(_ctx->getDevice())) {
        return Err<std::unique_ptr<Font>>("Failed to create glyph metadata buffer for embedded font");
    }

    return Ok(std::move(font));
#else
    return Err<std::unique_ptr<Font>>("Font generation not available on this platform");
#endif
}

Result<Font*> FontManager::getFont(const unsigned char* data, size_t dataLen,
                                    const std::string& fontName, float fontSize) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    if (!data || dataLen == 0) {
        return Err<Font*>("Invalid font data");
    }

    // Use the provided fontName for memory caching
    FontCacheKey key;
    key.familyName = fontName;
    key.styleName = "";
    key.numGlyphs = 0;
    key.unitsPerEM = 0;

    // Check memory cache first
    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        ydebug("FontManager: memory cache hit for font data '{}'", fontName);
        return Ok(it->second.get());
    }

    // Check disk cache
    std::string cachePath = getCachePath(fontName, fontSize, data, dataLen);
    yinfo("FontManager: looking for disk cache: {}.lz4", cachePath);
    auto diskResult = loadFromDiskCache(cachePath);
    if (diskResult) {
        yinfo("FontManager: DISK CACHE HIT for font '{}'", fontName);
        Font* ptr = diskResult.value().get();
        fontCache_[key] = std::move(diskResult.value());

        if (!hasDefaultFont_) {
            defaultFontKey_ = key;
            hasDefaultFont_ = true;
        }

        return Ok(ptr);
    }

    // Generate font
    yinfo("FontManager: DISK CACHE MISS for font '{}', generating atlas...", fontName);
    auto result = generateFont(data, dataLen, fontName, fontSize);
    if (!result) {
        return Err<Font*>(result.error().message());
    }

    Font* ptr = result.value().get();

    // Save to disk cache
    saveToDiskCache(ptr, cachePath);

    fontCache_[key] = std::move(result.value());

    if (!hasDefaultFont_) {
        defaultFontKey_ = key;
        hasDefaultFont_ = true;
    }

    return Ok(ptr);
#else
    return Err<Font*>("Font loading from data not available on this platform");
#endif
}

Result<Font*> FontManager::loadFromAtlas(const std::string& atlasPath,
                                          const std::string& metricsPath,
                                          const std::string& fontName) noexcept {
    // Check cache first
    FontCacheKey key;
    key.familyName = fontName;
    key.styleName = "prebuilt";
    key.numGlyphs = 0;
    key.unitsPerEM = 0;

    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        ydebug("FontManager: cache hit for prebuilt atlas '{}'", fontName);
        return Ok(it->second.get());
    }

    // Load font from prebuilt atlas
    auto font = std::make_unique<Font>();

    yinfo("FontManager: loading prebuilt atlas from: {}", atlasPath);
    if (!font->loadAtlas(atlasPath, metricsPath)) {
        return Err<Font*>("Failed to load font atlas from: " + atlasPath);
    }

    if (!font->createTexture(_ctx->getDevice(), _ctx->getQueue())) {
        return Err<Font*>("Failed to create font texture from prebuilt atlas");
    }

    Font* ptr = font.get();
    fontCache_[key] = std::move(font);

    if (!hasDefaultFont_) {
        defaultFontKey_ = key;
        hasDefaultFont_ = true;
    }

    yinfo("FontManager: loaded prebuilt atlas '{}' successfully", fontName);
    return Ok(ptr);
}

Font* FontManager::getDefaultFont() noexcept {
    if (!hasDefaultFont_) {
        return nullptr;
    }

    auto it = fontCache_.find(defaultFontKey_);
    if (it != fontCache_.end()) {
        return it->second.get();
    }

    return nullptr;
}

void FontManager::setDefaultFont(const std::string& family, Font::Style style) noexcept {
    FontCacheKey key = createCacheKey(family, style);

    if (fontCache_.find(key) != fontCache_.end()) {
        defaultFontKey_ = key;
        hasDefaultFont_ = true;
    }
}

bool FontManager::hasFont(const std::string& family, Font::Style style) const noexcept {
    FontCacheKey key = createCacheKey(family, style);
    return fontCache_.find(key) != fontCache_.end();
}


void FontManager::unloadFont(const std::string& family, Font::Style style) noexcept {
    FontCacheKey key = createCacheKey(family, style);

    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        if (hasDefaultFont_ && key == defaultFontKey_) {
            hasDefaultFont_ = false;
            for (const auto& [k, v] : fontCache_) {
                if (!(k == key)) {
                    defaultFontKey_ = k;
                    hasDefaultFont_ = true;
                    break;
                }
            }
        }

        fontCache_.erase(it);
        yinfo("FontManager: unloaded font '{}' {}", family, static_cast<int>(style));
    }
}

void FontManager::unloadAll() noexcept {
    fontCache_.clear();
    hasDefaultFont_ = false;
    ydebug("FontManager: unloaded all fonts");
}

//-----------------------------------------------------------------------------
// Disk Cache Support
//-----------------------------------------------------------------------------

std::string FontManager::getCacheDir() const noexcept {
    if (!cacheDir_.empty()) {
        return cacheDir_;
    }

    std::string cacheHome;
#ifdef _WIN32
    // Windows: use %LOCALAPPDATA%
    if (const char* localAppData = std::getenv("LOCALAPPDATA")) {
        cacheHome = localAppData;
    } else if (const char* appData = std::getenv("APPDATA")) {
        cacheHome = appData;
    } else if (const char* temp = std::getenv("TEMP")) {
        cacheHome = temp;
    } else {
        cacheHome = "C:\\Temp";
    }
    return cacheHome + "\\yetty\\msdf-font-cache";
#else
    // Unix: Follow XDG Base Directory Specification
    if (const char* xdgCache = std::getenv("XDG_CACHE_HOME")) {
        cacheHome = xdgCache;
    } else if (const char* home = std::getenv("HOME")) {
        cacheHome = std::string(home) + "/.cache";
    } else {
        cacheHome = "/tmp";
    }
    return cacheHome + "/yetty/msdf-font-cache";
#endif
}

std::string FontManager::computeHash(const unsigned char* data, size_t dataLen) noexcept {
    // Simple FNV-1a hash for speed
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < dataLen; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

std::string FontManager::getCachePath(const std::string& fontName, float fontSize,
                                       const unsigned char* data, size_t dataLen) const noexcept {
    std::string hash = computeHash(data, dataLen);

    // Sanitize font name for filesystem
    std::string safeName;
    for (char c : fontName) {
        if (std::isalnum(c) || c == '-' || c == '_') {
            safeName += c;
        } else if (c == '+' || c == ' ') {
            safeName += '_';
        }
    }

    std::ostringstream oss;
    oss << getCacheDir() << "/" << safeName << "-" << static_cast<int>(fontSize) << "-" << hash;
    return oss.str();
}

Result<std::unique_ptr<Font>> FontManager::loadFromDiskCache(const std::string& cachePath) noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    std::string atlasPath = cachePath + ".lz4";
    std::string metricsPath = cachePath + ".json";

    if (!fs::exists(atlasPath) || !fs::exists(metricsPath)) {
        return Err<std::unique_ptr<Font>>("Cache miss");
    }

    yinfo("FontManager: loading from disk cache: {}", cachePath);

    auto font = std::make_unique<Font>();
    if (!font->loadAtlas(atlasPath, metricsPath)) {
        return Err<std::unique_ptr<Font>>("Failed to load cached atlas");
    }

    if (!font->createTexture(_ctx->getDevice(), _ctx->getQueue())) {
        return Err<std::unique_ptr<Font>>("Failed to create texture from cached atlas");
    }

    if (!font->createGlyphMetadataBuffer(_ctx->getDevice())) {
        return Err<std::unique_ptr<Font>>("Failed to create glyph buffer from cached atlas");
    }

    yinfo("FontManager: loaded {} glyphs from disk cache", font->getGlyphCount());
    return Ok(std::move(font));
#else
    return Err<std::unique_ptr<Font>>("Disk cache not available on this platform");
#endif
}

bool FontManager::saveToDiskCache(const Font* font, const std::string& cachePath) const noexcept {
#if !YETTY_USE_PREBUILT_ATLAS
    if (!font) return false;

    std::string cacheDir = getCacheDir();

    // Create cache directory if it doesn't exist
    std::error_code ec;
    if (!fs::exists(cacheDir)) {
        if (!fs::create_directories(cacheDir, ec)) {
            ywarn("FontManager: failed to create cache directory '{}': {}",
                         cacheDir, ec.message());
            return false;
        }
        yinfo("FontManager: created cache directory: {}", cacheDir);
    }

    std::string atlasPath = cachePath + ".lz4";
    std::string metricsPath = cachePath + ".json";

    if (!font->saveAtlas(atlasPath, metricsPath)) {
        ywarn("FontManager: failed to save font to disk cache: {}", cachePath);
        return false;
    }

    yinfo("FontManager: saved font to disk cache: {}", cachePath);
    return true;
#else
    return false;
#endif
}

} // namespace yetty
