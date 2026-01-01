#pragma once

#include "../terminal/font.h"
#include "../result.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <webgpu/webgpu.h>

// Forward declarations for FreeType
typedef struct FT_FaceRec_* FT_Face;

namespace yetty {

class WebGPUContext;

//-----------------------------------------------------------------------------
// FontManager - manages font loading, caching, and MSDF atlas generation
//-----------------------------------------------------------------------------
class FontManager {
public:
    FontManager();
    ~FontManager();

    // Initialize with WebGPU context (needed for texture creation)
    Result<void> init(WebGPUContext* ctx);
    void dispose();

    // Load font from TTF file
    // name: identifier for this font (e.g., "default", "monospace")
    // path: path to TTF file (will auto-discover Bold/Italic variants)
    // fontSize: base font size for MSDF generation
    Result<Font*> loadFont(const std::string& name,
                           const std::string& path,
                           float fontSize = 32.0f,
                           uint32_t atlasSize = 2048);

    // Load font with explicit variant paths
    Result<Font*> loadFont(const std::string& name,
                           const std::string& regularPath,
                           const std::string& boldPath,
                           const std::string& italicPath,
                           const std::string& boldItalicPath,
                           float fontSize = 32.0f,
                           uint32_t atlasSize = 2048);

    // Load font from pre-built atlas (for platforms without MSDF generation)
    Result<Font*> loadFontFromAtlas(const std::string& name,
                                     const std::string& atlasPath,
                                     const std::string& metricsPath);

#if !YETTY_USE_PREBUILT_ATLAS
    // Load font from FreeType face (for PDF embedded fonts)
    // The FT_Face must remain valid for the lifetime of the font
    Result<Font*> loadFontFromFreeType(const std::string& name,
                                        FT_Face face,
                                        float fontSize = 32.0f,
                                        uint32_t atlasSize = 1024);
#endif

    // Get a loaded font by name
    Font* getFont(const std::string& name);

    // Check if font exists
    bool hasFont(const std::string& name) const;

    // Get default font (first loaded, or nullptr)
    Font* getDefaultFont();

    // Set default font by name
    void setDefaultFont(const std::string& name);

    // Unload a specific font
    void unloadFont(const std::string& name);

    // Unload all fonts
    void unloadAll();

    // Get list of loaded font names
    std::vector<std::string> getFontNames() const;

private:
    WebGPUContext* ctx_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Font>> fonts_;
    std::string defaultFontName_;
};

} // namespace yetty
