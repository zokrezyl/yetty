#pragma once

#include <yetty/plugin.h>
#include <yetty/font.h>
#include <yetty/rich-text.h>
#include <string>
#include <vector>

namespace yetty {

class MarkdownPlugin;
class MarkdownLayer;

//-----------------------------------------------------------------------------
// MarkdownPlugin - renders markdown content using RichText
//-----------------------------------------------------------------------------
class MarkdownPlugin : public Plugin {
public:
    ~MarkdownPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "markdown"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

    // Access to font manager (from engine)
    FontManager* getFontManager();

private:
    explicit MarkdownPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// ParsedSpan - a run of styled text from markdown parsing
//-----------------------------------------------------------------------------
struct ParsedSpan {
    std::string text;
    Font::Style style = Font::Regular;
    uint8_t headerLevel = 0;  // 0=normal, 1-6=header
    bool isCode = false;
    bool isBullet = false;
};

//-----------------------------------------------------------------------------
// ParsedLine - a line of text with styled spans from markdown parsing
//-----------------------------------------------------------------------------
struct ParsedLine {
    std::vector<ParsedSpan> spans;
    float indent = 0.0f;
    float scale = 1.0f;  // For headers
};

//-----------------------------------------------------------------------------
// MarkdownLayer - single markdown document layer (uses RichText for rendering)
//-----------------------------------------------------------------------------
class MarkdownLayer : public PluginLayer {
public:
    explicit MarkdownLayer(MarkdownPlugin* plugin);
    ~MarkdownLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    void parseMarkdown(const std::string& content);
    void buildRichTextSpans(float fontSize, float maxWidth);

    MarkdownPlugin* plugin_ = nullptr;
    std::vector<ParsedLine> parsedLines_;
    RichText::Ptr richText_;

    float baseSize_ = 16.0f;
    float lastLayoutWidth_ = 0.0f;
    bool initialized_ = false;
    bool failed_ = false;
};

using Markdown = MarkdownPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
