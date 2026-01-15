#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class MusicalScore;

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

private:
    MusicalScorePlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// MusicalScore - a single musical score instance
//
// Payload format: "sheetWidth,numStaves"
//   e.g., "800,4" = 800px wide sheet with 4 staves
//-----------------------------------------------------------------------------
class MusicalScore : public Widget {
public:
    static constexpr int MAX_STAVES = 16;
    static constexpr int LINES_PER_STAFF = 5;

    static Result<WidgetPtr> create(
        WidgetFactory* factory,
        FontManager* fontManager,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload
    ) {
        (void)factory;
        (void)fontManager;
        (void)loop;
        (void)pluginArgs;
        auto w = std::shared_ptr<MusicalScore>(new MusicalScore(payload));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init MusicalScore", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~MusicalScore() override;

    Result<void> dispose() override;
    void update(double deltaTime) override;

    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Input handling
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    explicit MusicalScore(const std::string& payload) {
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
