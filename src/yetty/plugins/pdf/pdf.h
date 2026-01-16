#pragma once

#include <yetty/plugin.h>
#include <yetty/rich-text.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace yetty {

class Pdf;
class FontManager;

//-----------------------------------------------------------------------------
// PDFPlugin - renders PDF documents using RichText
//-----------------------------------------------------------------------------
class PDFPlugin : public Plugin {
public:
    ~PDFPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "pdf"; }

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

    FontManager* getFontManager();

    void* getMupdfContext() const noexcept { return _fzCtx; }

private:
    PDFPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;

    void* _fzCtx = nullptr;  // fz_context*
    FontManager* _fontManager = nullptr;
};

//-----------------------------------------------------------------------------
// Pdf - single PDF document widget using RichText for rendering
//-----------------------------------------------------------------------------
class Pdf : public Widget {
public:
    ~Pdf() override;

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
        PDFPlugin* plugin,
        void* ctx
    ) noexcept;

    Result<void> dispose() override;

    void prepareFrame(WebGPUContext& ctx) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

    // Keyboard for page navigation
    bool onKey(int key, int scancode, int action, int mods) override;
    bool wantsKeyboard() const override { return true; }

private:
    explicit Pdf(const std::string& payload, PDFPlugin* plugin, void* ctx) noexcept;
    Result<void> init() override;

    Result<void> loadPDF(const std::string& path);
    Result<void> extractPageContent(int pageNum);
    void buildRichTextContent(float viewWidth);

    // Font registration with FontManager
    std::string registerFont(void* fzFont);
    Result<void> generateFontAtlases();

    PDFPlugin* _plugin = nullptr;
    void* _mupdfCtx = nullptr;  // fz_context*
    void* _doc = nullptr;       // fz_document*
    int _pageCount = 0;
    int _currentPage = 0;
    float _zoom = 1.0f;

    // Extracted page data
    struct ExtractedChar {
        uint32_t codepoint;
        float x, y;              // PDF coordinates
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
    std::unordered_map<void*, std::string> _fontNameMap;  // fz_font* -> family name

    // Store font data instead of FT_Face to avoid MuPDF lock callback issues
    struct PendingFont {
        std::vector<unsigned char> data;
        std::string name;
    };
    std::unordered_map<void*, PendingFont> _pendingFonts;  // fz_font* -> font data for deferred atlas generation

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
