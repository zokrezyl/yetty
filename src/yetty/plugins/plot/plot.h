#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <vector>

namespace yetty {

class PlotW;

//-----------------------------------------------------------------------------
// PlotPlugin - manages all plot layers
// Each layer renders N plots from an NxM data texture
//-----------------------------------------------------------------------------
class PlotPlugin : public Plugin {
public:
    ~PlotPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "plot"; }

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
    PlotPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// PlotW - a single plot widget showing N line plots
//
// Data format: NxM matrix where:
//   - N = number of plots (rows)
//   - M = number of data points per plot (columns)
//   - Each value is a float Y coordinate
//
// The X axis is implicit: x[i] = i / (M-1) normalized to [0,1]
//
// Two-phase construction:
//   1. Constructor (private) - stores payload
//   2. init() (private) - no args, parses payload
//   3. create() (public) - factory
//-----------------------------------------------------------------------------
class PlotW : public Widget {
public:
    static constexpr uint32_t MAX_PLOTS = 16;  // Maximum number of plots per layer

    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<PlotW>(new PlotW(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init PlotW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~PlotW() override;

    Result<void> dispose() override;
    Result<void> update(double deltaTime) override;

    // Called by PlotPlugin::renderAll
    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    // Update plot data - NxM matrix of Y values
    // Data layout: row-major, data[row * M + col] = Y value for plot 'row' at point 'col'
    Result<void> setData(const float* data, uint32_t numPlots, uint32_t numPoints);

    // Set viewport range
    void setViewport(float xMin, float xMax, float yMin, float yMax);

    // Set color for a specific plot (RGBA)
    void setPlotColor(uint32_t plotIndex, float r, float g, float b, float a = 1.0f);

    // Set line width in pixels
    void setLineWidth(float width);

    // Enable/disable grid
    void setGridEnabled(bool enabled);

    // Input handling for pan/zoom
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit PlotW(const std::string& payload) {
        _payload = payload;
    }

    Result<void> init() override;
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> updateDataTexture(WebGPUContext& ctx);

    // Plot data (CPU side, for updates)
    std::vector<float> data_;
    uint32_t numPlots_ = 0;
    uint32_t numPoints_ = 0;
    bool dataDirty_ = false;

    // Viewport
    float xMin_ = 0.0f;
    float xMax_ = 1.0f;
    float yMin_ = 0.0f;
    float yMax_ = 1.0f;

    // Visual settings
    float lineWidth_ = 2.0f;
    bool gridEnabled_ = true;
    float colors_[MAX_PLOTS * 4];  // RGBA for each plot

    // Pan/zoom state
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool panning_ = false;
    float panStartX_ = 0.0f;
    float panStartY_ = 0.0f;
    float viewportStartXMin_ = 0.0f;
    float viewportStartXMax_ = 1.0f;
    float viewportStartYMin_ = 0.0f;
    float viewportStartYMax_ = 1.0f;

    // WebGPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUTexture dataTexture_ = nullptr;
    WGPUTextureView dataTextureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    bool gpuInitialized_ = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
