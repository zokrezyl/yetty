#pragma once

#include <yetty/plugin.h>
#include <yetty/rich-text.h>
#include <yetty/font-manager.h>
#include <webgpu/webgpu.h>
#include <string>
#include <memory>

namespace yetty {

class RichTextLayer;

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
    RichTextPlugin();
    ~RichTextPlugin() override;

    static Result<PluginPtr> create();

    const char* pluginName() const override { return "rich-text"; }

    Result<void> init(WebGPUContext* ctx) override;
    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WebGPUContext& ctx,
                           WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

    // Access to font manager for layers
    FontManager& getFontManager() { return fontManager_; }

private:
    FontManager fontManager_;
};

//-----------------------------------------------------------------------------
// RichTextLayer - single rich text document layer
//-----------------------------------------------------------------------------
class RichTextLayer : public PluginLayer {
public:
    RichTextLayer(RichTextPlugin* plugin);
    ~RichTextLayer() override;

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
    Result<void> parseYAML(const std::string& yaml);

    RichTextPlugin* plugin_ = nullptr;
    std::unique_ptr<RichText> richText_;
    std::string fontName_;

    bool initialized_ = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
