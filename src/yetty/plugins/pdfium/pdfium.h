#pragma once

#include <yetty/plugin.h>
#include <yetty/rich-text.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>

// Include PDFium headers for proper type declarations
#include <fpdfview.h>

namespace yetty {

class PdfiumWidget;
class FontManager;

//-----------------------------------------------------------------------------
// PdfiumPlugin - renders PDF documents using PDFium + RichText
// Uses BSD-3-Clause licensed PDFium (MIT compatible)
//-----------------------------------------------------------------------------
class PdfiumPlugin : public Plugin {
public:
    ~PdfiumPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "pdfium"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(
        const std::string& widgetName,
        WidgetFactory* factory,
        FontManager* fontManager,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload
    ) override;

    FontManager* getFontManager() const { return _fontManager; }

private:
    PdfiumPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;

    FontManager* _fontManager = nullptr;
    bool _libraryInitialized = false;
};

//-----------------------------------------------------------------------------
// PdfiumWidget - single PDF document widget using RichText for rendering
//-----------------------------------------------------------------------------
class PdfiumWidget : public Widget {
public:
    ~PdfiumWidget() override;

    static Result<WidgetPtr> create(
        WidgetFactory* factory,
        FontManager* fontManager,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload,
        PdfiumPlugin* plugin
    ) noexcept;

    Result<void> dispose() override;

    void prepareFrame(WebGPUContext& ctx, bool on) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) override;

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

    // Keyboard for page navigation
    bool onKey(int key, int scancode, int action, int mods) override;
    bool wantsKeyboard() const override { return true; }

private:
    explicit PdfiumWidget(const std::string& payload, PdfiumPlugin* plugin) noexcept;
    Result<void> init() override;

    Result<void> loadPDF(const std::string& path);
    Result<void> extractPageContent(int pageNum);
    void extractFontsFromPage(FPDF_PAGE page);
    void buildRichTextContent(float viewWidth);

    // Font registration with FontManager
    std::string registerFont(FPDF_FONT font);
    Result<void> generateFontAtlases();

    PdfiumPlugin* _plugin = nullptr;
    FPDF_DOCUMENT _doc = nullptr;
    int _pageCount = 0;
    int _currentPage = 0;
    float _zoom = 1.0f;

    // Extracted page data
    struct ExtractedChar {
        uint32_t codepoint;
        float x, y;              // PDF coordinates - origin (baseline) position
        float width, height;     // Bounding box dimensions
        float ascent;            // Distance from baseline to charbox TOP (positive)
        float size;              // Font size in PDF points
        uint32_t color;          // ARGB color
        std::string fontFamily;  // Registered font family name
        bool bold = false;
        bool italic = false;
    };

    struct ExtractedPage {
        float width, height;
        std::vector<ExtractedChar> chars;
    };

    std::vector<ExtractedPage> _pages;

    // Font tracking - map font pointer to registered family name
    std::unordered_map<FPDF_FONT, std::string> _fontNameMap;
    std::set<FPDF_FONT> _processedFonts;

    // Map from font info name (FPDFText_GetFontInfo) to registered font family name
    std::unordered_map<std::string, std::string> _fontInfoToFamily;

    // Store font data for deferred atlas generation
    struct PendingFont {
        std::vector<unsigned char> data;
        std::string name;
    };
    std::unordered_map<FPDF_FONT, PendingFont> _pendingFonts;

    // RichText for rendering
    RichText::Ptr _richText;
    float _documentHeight = 0.0f;
    float _scrollOffset = 0.0f;

    bool _initialized = false;
    bool _failed = false;
    float _lastViewWidth = 0.0f;
    float _lastViewHeight = 0.0f;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
