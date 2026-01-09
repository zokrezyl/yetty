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

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "pdf"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(const std::string& payload) override;

    FontManager* getFontManager();

    void* getMupdfContext() const noexcept { return fzCtx_; }

private:
    explicit PDFPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> pluginInit() noexcept;

    void* fzCtx_ = nullptr;  // fz_context*
};

//-----------------------------------------------------------------------------
// Pdf - single PDF document widget using RichText for rendering
//-----------------------------------------------------------------------------
class Pdf : public Widget {
public:
    ~Pdf() override;

    static Result<WidgetPtr> create(const std::string& payload, PDFPlugin* plugin, void* ctx) noexcept;

    Result<void> dispose() override;

    // Renderable interface - uses RenderContext from base class
    Result<void> render(WebGPUContext& ctx) override;
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

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

    PDFPlugin* plugin_ = nullptr;
    void* mupdfCtx_ = nullptr;  // fz_context*
    void* doc_ = nullptr;       // fz_document*
    int pageCount_ = 0;
    int currentPage_ = 0;
    float zoom_ = 1.0f;

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

    std::vector<ExtractedPage> pages_;
    std::unordered_map<void*, std::string> fontNameMap_;  // fz_font* -> family name

    // Store font data instead of FT_Face to avoid MuPDF lock callback issues
    struct PendingFont {
        std::vector<unsigned char> data;
        std::string name;
    };
    std::unordered_map<void*, PendingFont> pendingFonts_;  // fz_font* -> font data for deferred atlas generation

    // RichText for rendering
    RichText::Ptr richText_;
    float documentHeight_ = 0.0f;
    float scrollOffset_ = 0.0f;

    bool initialized_ = false;
    bool failed_ = false;
    float lastViewWidth_ = 0.0f;
    float lastViewHeight_ = 0.0f;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
