#pragma once

#include <yetty/font.h>
#include <yetty/result.hpp>
#include <yetty/webgpu-context.h>
#include <string>
#include <unordered_map>
#include <memory>

// Forward declarations for FreeType
typedef struct FT_FaceRec_* FT_Face;

namespace yetty {

/**
 * @brief Cache key for font lookup based on font properties.
 */
struct FontCacheKey {
    std::string familyName;
    std::string styleName;
    long numGlyphs;
    int unitsPerEM;

    bool operator==(const FontCacheKey& other) const {
        return familyName == other.familyName &&
               styleName == other.styleName &&
               numGlyphs == other.numGlyphs &&
               unitsPerEM == other.unitsPerEM;
    }
};

/**
 * @brief Hash function for FontCacheKey.
 */
struct FontCacheKeyHash {
    size_t operator()(const FontCacheKey& key) const {
        size_t h = 0;
        h ^= std::hash<std::string>{}(key.familyName);
        h ^= std::hash<std::string>{}(key.styleName) << 1;
        h ^= std::hash<long>{}(key.numGlyphs) << 2;
        h ^= std::hash<int>{}(key.unitsPerEM) << 3;
        return h;
    }
};

/**
 * @brief Centralized font management with automatic caching.
 */
class FontManager {
public:
    using Ptr = std::shared_ptr<FontManager>;

    static Result<Ptr> create(WebGPUContext::Ptr ctx) noexcept;

    ~FontManager();

    // Non-copyable
    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    /**
     * @brief Get a font by family name and style.
     */
    Result<Font*> getFont(const std::string& family,
                          Font::Style style = Font::Regular,
                          float fontSize = 32.0f) noexcept;

    /**
     * @brief Get a font from a FreeType face.
     * @param fontName Unique name for caching (e.g., MuPDF font name like 'WZZSSZ+CenturySchoolbook')
     * @deprecated Use getFont(data, dataLen, fontName) instead to avoid FreeType lock issues
     */
    Result<Font*> getFont(FT_Face face, const std::string& fontName, float fontSize = 32.0f) noexcept;

    /**
     * @brief Get a font from raw font data (TTF/OTF bytes).
     * @param data Pointer to font file data
     * @param dataLen Length of font data in bytes
     * @param fontName Unique name for caching
     * @param fontSize Target font size for atlas generation
     */
    Result<Font*> getFont(const unsigned char* data, size_t dataLen,
                          const std::string& fontName, float fontSize = 32.0f) noexcept;

    /**
     * @brief Get the default font.
     */
    Font* getDefaultFont() noexcept;

    /**
     * @brief Set which font should be returned by getDefaultFont().
     */
    void setDefaultFont(const std::string& family, Font::Style style = Font::Regular) noexcept;

    /**
     * @brief Check if a font is already cached.
     */
    bool hasFont(const std::string& family, Font::Style style = Font::Regular) const noexcept;


    /**
     * @brief Remove a specific font from the cache.
     */
    void unloadFont(const std::string& family, Font::Style style = Font::Regular) noexcept;

    /**
     * @brief Remove all fonts from the cache.
     */
    void unloadAll() noexcept;

    /**
     * @brief Get the number of fonts currently cached.
     */
    size_t getCacheSize() const noexcept { return fontCache_.size(); }

    /**
     * @brief Create a cache key from an FT_Face.
     */
    static FontCacheKey createCacheKey(FT_Face face) noexcept;

    /**
     * @brief Create a cache key from family and style.
     */
    static FontCacheKey createCacheKey(const std::string& family, Font::Style style) noexcept;

private:
    explicit FontManager(WebGPUContext::Ptr ctx) noexcept;
    Result<void> init() noexcept;

    std::string findFontPath(const std::string& family, Font::Style style) noexcept;
    Result<std::unique_ptr<Font>> generateFont(const std::string& path, float fontSize) noexcept;
    Result<std::unique_ptr<Font>> generateFont(FT_Face face, const std::string& fontName, float fontSize) noexcept;
    Result<std::unique_ptr<Font>> generateFont(const unsigned char* data, size_t dataLen,
                                                const std::string& fontName, float fontSize) noexcept;

    // Disk cache support
    std::string getCacheDir() const noexcept;
    std::string getCachePath(const std::string& fontName, float fontSize,
                              const unsigned char* data, size_t dataLen) const noexcept;
    static std::string computeHash(const unsigned char* data, size_t dataLen) noexcept;
    Result<std::unique_ptr<Font>> loadFromDiskCache(const std::string& cachePath) noexcept;
    bool saveToDiskCache(const Font* font, const std::string& cachePath) const noexcept;

    WebGPUContext::Ptr ctx_;
    std::unordered_map<FontCacheKey, std::unique_ptr<Font>, FontCacheKeyHash> fontCache_;
    FontCacheKey defaultFontKey_;
    bool hasDefaultFont_ = false;
    std::string cacheDir_;
};

} // namespace yetty
