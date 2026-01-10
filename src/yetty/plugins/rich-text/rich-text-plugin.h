#pragma once

#include <yetty/plugin.h>
#include <yetty/rich-text.h>
#include <yetty/font-manager.h>
#include <webgpu/webgpu.h>
#include <string>
#include <memory>

namespace yetty {

class RichTextW;

//-----------------------------------------------------------------------------
// RichTextPlugin - renders styled text from YAML input
//
// YAML format:
//   font: "default"          # optional, font name from FontManager
//   spans:
//     - text: "Hello "
//       x: 10                 # optional, default 0
//       y: 20                 # optional, default continues from previous
//       size: 24              # optional, default 16
//       style: bold           # optional: regular, bold, italic, bolditalic
//       color: [1, 0.5, 0, 1] # optional, RGBA, default white
//       wrap: true            # optional, default false
//       maxWidth: 400         # optional, for wrap
//       lineHeight: 30        # optional, 0 = auto
//     - text: "World"
//       size: 16
//       color: [0, 1, 0.5, 1]
//-----------------------------------------------------------------------------
class RichTextPlugin : public Plugin {
public:
    ~RichTextPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "rich-text"; }

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

    // Access to font manager for layers (from engine)
    FontManager* getFontManager();

private:
    RichTextPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;

    FontManager* _fontManager = nullptr;
};

//-----------------------------------------------------------------------------
// RichTextW - single rich text document widget
//-----------------------------------------------------------------------------
class RichTextW : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload, RichTextPlugin* plugin) {
        auto w = std::shared_ptr<RichTextW>(new RichTextW(payload, plugin));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init RichTextW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~RichTextW() override;

    Result<void> dispose() override;

    // Renderable interface - uses RenderContext from base class
    Result<void> render(WebGPUContext& ctx) override;
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit RichTextW(const std::string& payload, RichTextPlugin* plugin)
        : plugin_(plugin) {
        _payload = payload;
    }

    Result<void> init() override;
    Result<void> parseYAML(const std::string& yaml);

    RichTextPlugin* plugin_ = nullptr;
    RichText::Ptr richText_;
    std::string fontName_;
    std::vector<TextSpan> pendingSpans_;  // Stored until RichText is created

    bool _initialized = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
