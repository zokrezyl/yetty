#pragma once

#include <yetty/plugin.h>
#include <yetty/font.h>
#include <yetty/rich-text.h>
#include <string>
#include <vector>

namespace yetty {

class MarkdownPlugin;
class MarkdownW;

//-----------------------------------------------------------------------------
// MarkdownPlugin - renders markdown content using RichText
//-----------------------------------------------------------------------------
class MarkdownPlugin : public Plugin {
public:
    ~MarkdownPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "markdown"; }

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

    // Access to font manager (from engine)
    FontManager* getFontManager();

private:
    MarkdownPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;

    FontManager* _fontManager = nullptr;
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
// MarkdownW - single markdown document widget (uses RichText for rendering)
//-----------------------------------------------------------------------------
class MarkdownW : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload, MarkdownPlugin* plugin) {
        auto w = std::shared_ptr<MarkdownW>(new MarkdownW(payload, plugin));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init MarkdownW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~MarkdownW() override;

    Result<void> dispose() override;

    // Renderable interface
    Result<void> render(WebGPUContext& ctx) override;
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit MarkdownW(const std::string& payload, MarkdownPlugin* plugin)
        : plugin_(plugin) {
        _payload = payload;
    }

    Result<void> init() override;

    void parseMarkdown(const std::string& content);
    void buildRichTextSpans(float fontSize, float maxWidth);

    MarkdownPlugin* plugin_ = nullptr;
    std::vector<ParsedLine> parsedLines_;
    RichText::Ptr richText_;

    float baseSize_ = 16.0f;
    float lastLayoutWidth_ = 0.0f;
    bool _initialized = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
