#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class MusicalScoreW;

//-----------------------------------------------------------------------------
// MusicalScorePlugin - renders a musical score sheet with WebGPU
//-----------------------------------------------------------------------------
class MusicalScorePlugin : public Plugin {
public:
    ~MusicalScorePlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "musical-score"; }

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

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    MusicalScorePlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// MusicalScoreW - a single musical score instance
//
// Payload format: "sheetWidth,numStaves"
//   e.g., "800,4" = 800px wide sheet with 4 staves
//-----------------------------------------------------------------------------
class MusicalScoreW : public Widget {
public:
    static constexpr int MAX_STAVES = 16;
    static constexpr int LINES_PER_STAFF = 5;

    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<MusicalScoreW>(new MusicalScoreW(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init MusicalScoreW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~MusicalScoreW() override;

    Result<void> dispose() override;
    Result<void> update(double deltaTime) override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    // Input handling
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    explicit MusicalScoreW(const std::string& payload) {
        _payload = payload;
    }

    Result<void> init() override;
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);

    // Configuration
    int sheetWidth_ = 800;
    int numStaves_ = 4;

    // GPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;

    bool gpuInitialized_ = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
