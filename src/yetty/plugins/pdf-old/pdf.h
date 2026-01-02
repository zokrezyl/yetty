#pragma once

#include <yetty/plugin.h>
#include <yetty/font.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

// MuPDF uses opaque pointer types, just use void* in header
// The actual types are used in the cpp file

namespace yetty {

class PDFLayer;

//-----------------------------------------------------------------------------
// PDFPlugin - renders PDF documents using MSDF text rendering
//-----------------------------------------------------------------------------
class PDFPlugin : public Plugin {
public:
    ~PDFPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "pdf"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    explicit PDFPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;

    void* fzCtx_ = nullptr;  // fz_context*
};

//-----------------------------------------------------------------------------
// GlyphInstance - positioned glyph for GPU rendering (forward declaration)
//-----------------------------------------------------------------------------
struct PDFGlyphInstance {
    float posX, posY;       // Screen position
    float uvMinX, uvMinY;   // UV coordinates
    float uvMaxX, uvMaxY;
    float sizeX, sizeY;     // Glyph size
    float colorR, colorG, colorB, colorA;
};

//-----------------------------------------------------------------------------
// PDFFont - font extracted from PDF for MSDF rendering
//-----------------------------------------------------------------------------
struct PDFFont {
    void* fzFont = nullptr;      // fz_font* from MuPDF
    void* ftFace = nullptr;      // FT_Face from fz_font_ft_face()
    std::string name;
    bool bold = false;
    bool italic = false;

    // MSDF atlas for this font
    std::vector<uint8_t> atlasData;
    uint32_t atlasWidth = 0;
    uint32_t atlasHeight = 0;
    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;

    // Glyph metrics for characters in this font
    struct GlyphInfo {
        float uvMinX, uvMinY, uvMaxX, uvMaxY;
        float sizeX, sizeY;
        float bearingX, bearingY;
        float advance;
    };
    std::unordered_map<uint32_t, GlyphInfo> glyphs;

    // Per-font GPU resources for multi-font rendering
    WGPUBindGroup bindGroup = nullptr;
    WGPUBuffer glyphBuffer = nullptr;
    std::vector<PDFGlyphInstance> glyphInstances;
    uint32_t glyphCount = 0;
};

//-----------------------------------------------------------------------------
// PDFTextChar - a character with position and style from PDF
//-----------------------------------------------------------------------------
struct PDFTextChar {
    uint32_t codepoint;
    float x, y;          // Position in PDF coordinates
    float size;          // Font size in PDF points
    uint32_t color;      // ARGB color
    bool bold = false;
    bool italic = false;
    int fontIndex = -1;  // Index into PDFLayer::fonts_ vector
};

//-----------------------------------------------------------------------------
// PDFImage - an image extracted from PDF
//-----------------------------------------------------------------------------
struct PDFImage {
    float x, y, width, height;  // Position in PDF coordinates
    std::vector<uint8_t> pixels; // RGBA pixels
    uint32_t pixelWidth, pixelHeight;
    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;
};

//-----------------------------------------------------------------------------
// PDFPage - parsed page content
//-----------------------------------------------------------------------------
struct PDFPage {
    float width, height;  // Page dimensions in PDF points
    std::vector<PDFTextChar> chars;
    std::vector<PDFImage> images;
};

//-----------------------------------------------------------------------------
// PDFLayer - single PDF document layer
//-----------------------------------------------------------------------------
class PDFLayer : public PluginLayer {
public:
    PDFLayer(void* ctx);  // fz_context*
    ~PDFLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

    // Keyboard for page navigation
    bool onKey(int key, int scancode, int action, int mods) override;
    bool wantsKeyboard() const override { return true; }

private:
    Result<void> loadPDF(const std::string& path);
    Result<void> extractPageContent(int pageNum);
    void layoutPage(float viewWidth, float viewHeight);
    Result<void> createPipelines(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    void buildGlyphBuffer(float viewWidth, float viewHeight);
    Result<void> createImageTextures(WebGPUContext& ctx);

    // PDF font extraction and MSDF generation
    int getOrCreateFont(void* fzFont);  // Returns font index, creates if needed
    Result<void> generateFontAtlas(PDFFont& font, const std::vector<uint32_t>& codepoints);
    Result<void> uploadFontTexture(WebGPUContext& ctx, PDFFont& font);

    void* mupdfCtx_ = nullptr;  // fz_context*
    void* doc_ = nullptr;       // fz_document*
    int pageCount_ = 0;
    int currentPage_ = 0;
    float zoom_ = 1.0f;

    std::vector<PDFPage> pages_;
    std::vector<PDFFont> fonts_;  // Fonts extracted from PDF
    std::vector<PDFGlyphInstance> glyphs_;
    float documentHeight_ = 0.0f;
    float scrollOffset_ = 0.0f;

    // GPU resources
    WGPURenderPipeline textPipeline_ = nullptr;
    WGPURenderPipeline imagePipeline_ = nullptr;
    WGPUBindGroup textBindGroup_ = nullptr;
    WGPUBindGroupLayout textBindGroupLayout_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer glyphBuffer_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    uint32_t glyphCount_ = 0;

    // For MSDF generation
    void* freetypeHandle_ = nullptr;  // msdfgen::FreetypeHandle*
    static constexpr float pixelRange_ = 4.0f;

    bool gpuInitialized_ = false;
    bool failed_ = false;
    float lastViewWidth_ = 0.0f;
    float lastViewHeight_ = 0.0f;
    int activeFontIndex_ = -1;  // Font whose atlas we're using for rendering
};

using PDF = PDFPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
