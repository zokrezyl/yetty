#pragma once

#include <yetty/plugin.h>
#include <yetty/font.h>
#include <yetty/rich-text.h>
#include <string>
#include <vector>

namespace yetty::plugins {

class MarkdownPlugin;
class Markdown;

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
// Markdown - single markdown document widget (uses RichText for rendering)
//-----------------------------------------------------------------------------
class Markdown : public Widget {
public:
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
        MarkdownPlugin* plugin
    ) {
        (void)factory;
        (void)fontManager;
        (void)loop;
        (void)pluginArgs;
        auto w = std::shared_ptr<Markdown>(new Markdown(payload, plugin));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Markdown", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Markdown() override;

    Result<void> dispose() override;

    void prepareFrame(WebGPUContext& ctx) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit Markdown(const std::string& payload, MarkdownPlugin* plugin)
        : _plugin(plugin) {
        _payload = payload;
    }

    Result<void> init() override;

    void parseMarkdown(const std::string& content);
    void buildRichTextSpans(float fontSize, float maxWidth);

    MarkdownPlugin* _plugin = nullptr;
    std::vector<ParsedLine> _parsedLines;
    RichText::Ptr _richText;

    float _baseSize = 16.0f;
    float _lastLayoutWidth = 0.0f;
    bool _initialized = false;
    bool _failed = false;
};

} // namespace yetty::plugins

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
