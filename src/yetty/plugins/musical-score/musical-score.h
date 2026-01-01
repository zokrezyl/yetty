#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class MusicalScoreLayer;

//-----------------------------------------------------------------------------
// MusicalScorePlugin - renders a musical score sheet with WebGPU
//-----------------------------------------------------------------------------
class MusicalScorePlugin : public Plugin {
public:
    MusicalScorePlugin();
    ~MusicalScorePlugin() override;

    static Result<PluginPtr> create();

    const char* pluginName() const override { return "musical-score"; }

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
// MusicalScoreLayer - a single musical score instance
//
// Payload format: "sheetWidth,numStaves"
//   e.g., "800,4" = 800px wide sheet with 4 staves
//-----------------------------------------------------------------------------
class MusicalScoreLayer : public PluginLayer {
public:
    static constexpr int MAX_STAVES = 16;
    static constexpr int LINES_PER_STAFF = 5;

    MusicalScoreLayer();
    ~MusicalScoreLayer() override;

    Result<void> init(const std::string& payload) override;
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
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);

    // Configuration
    int _sheet_width = 800;
    int _num_staves = 4;

    // GPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;

    bool _gpu_initialized = false;
    bool _failed = false;
};

using MusicalScore = MusicalScorePlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
