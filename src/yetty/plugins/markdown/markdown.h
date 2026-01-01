#pragma once

#include <yetty/plugin.h>
#include <yetty/font.h>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>

namespace yetty {

class MarkdownLayer;

//-----------------------------------------------------------------------------
// MarkdownPlugin - renders markdown content using MSDF font atlas
//-----------------------------------------------------------------------------
class MarkdownPlugin : public Plugin {
public:
    MarkdownPlugin();
    ~MarkdownPlugin() override;

    static Result<PluginPtr> create();

    const char* pluginName() const override { return "markdown"; }

    Result<void> init(WebGPUContext* ctx) override;
    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WebGPUContext& ctx,
                           WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;
};

//-----------------------------------------------------------------------------
// TextSpan - a run of styled text
//-----------------------------------------------------------------------------
struct TextSpan {
    std::string text;
    Font::Style style = Font::Regular;
    uint8_t headerLevel = 0;  // 0=normal, 1-6=header
    bool isCode = false;
    bool isBullet = false;
};

//-----------------------------------------------------------------------------
// TextLine - a line of text with styled spans
//-----------------------------------------------------------------------------
struct TextLine {
    std::vector<TextSpan> spans;
    float indent = 0.0f;
    float scale = 1.0f;  // For headers
};

//-----------------------------------------------------------------------------
// GlyphInstance - positioned glyph for GPU rendering
//-----------------------------------------------------------------------------
struct GlyphInstance {
    float posX, posY;       // Screen position
    float uvMinX, uvMinY;   // UV coordinates
    float uvMaxX, uvMaxY;
    float sizeX, sizeY;     // Glyph size
    float colorR, colorG, colorB, colorA;
};

//-----------------------------------------------------------------------------
// MarkdownLayer - single markdown document layer
//-----------------------------------------------------------------------------
class MarkdownLayer : public PluginLayer {
public:
    MarkdownLayer();
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
    void layoutText(Font* font, float maxWidth);
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    void buildGlyphBuffer(Font* font);

    std::vector<TextLine> lines_;
    std::vector<GlyphInstance> glyphs_;
    float documentHeight_ = 0.0f;
    float scrollOffset_ = 0.0f;
    float lineHeight_ = 20.0f;

    // GPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUBuffer glyphBuffer_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    uint32_t glyphCount_ = 0;

    bool gpuInitialized_ = false;
    bool failed_ = false;
    float lastLayoutWidth_ = 0.0f;
};

using Markdown = MarkdownPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
