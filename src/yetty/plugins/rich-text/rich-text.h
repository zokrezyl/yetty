#pragma once

#include <yetty/plugin.h>
#include <yetty/rich-text.h>
#include <yetty/font-manager.h>
#include <webgpu/webgpu.h>
#include <string>
#include <memory>

namespace yetty::plugins {

class RichText;

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
class RichTextPlugin : public yetty::Plugin {
public:
    ~RichTextPlugin() override;

    static yetty::Result<yetty::PluginPtr> create() noexcept;

    const char* pluginName() const override { return "rich-text"; }

    yetty::Result<void> dispose() override;

    yetty::Result<yetty::WidgetPtr> createWidget(
        const std::string& widgetName,
        yetty::WidgetFactory* factory,
        yetty::FontManager* fontManager,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload
    ) override;

    // Access to font manager for layers (from engine)
    yetty::FontManager* getFontManager();

private:
    RichTextPlugin() noexcept = default;
    yetty::Result<void> pluginInit() noexcept;

    yetty::FontManager* _fontManager = nullptr;
};

//-----------------------------------------------------------------------------
// RichText - single rich text document widget
//-----------------------------------------------------------------------------
class RichText : public yetty::Widget {
public:
    static yetty::Result<yetty::WidgetPtr> create(
        yetty::WidgetFactory* factory,
        yetty::FontManager* fontManager,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload,
        RichTextPlugin* plugin
    ) {
        (void)factory;
        (void)fontManager;
        (void)loop;
        (void)pluginArgs;
        auto w = std::shared_ptr<RichText>(new RichText(payload, plugin));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return yetty::Err<yetty::WidgetPtr>("Failed to init RichText", res);
        }
        return yetty::Ok(std::static_pointer_cast<yetty::Widget>(w));
    }

    ~RichText() override;

    yetty::Result<void> dispose() override;

    void prepareFrame(yetty::WebGPUContext& ctx, bool on) override;
    yetty::Result<void> render(WGPURenderPassEncoder pass, yetty::WebGPUContext& ctx, bool on) override;

    // Mouse scrolling
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit RichText(const std::string& payload, RichTextPlugin* plugin)
        : _plugin(plugin) {
        _payload = payload;
    }

    yetty::Result<void> init() override;
    yetty::Result<void> parseYAML(const std::string& yaml);

    RichTextPlugin* _plugin = nullptr;
    yetty::RichText::Ptr _richText;  // The utility class
    std::string _fontName;
    std::vector<yetty::TextSpan> _pendingSpans;  // Stored until RichText is created

    bool _initialized = false;
    bool _failed = false;
};

} // namespace yetty::plugins

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
