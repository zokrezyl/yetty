#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <vector>

namespace yetty {

class PlotLayer;

//-----------------------------------------------------------------------------
// PlotPlugin - manages all plot layers
// Each layer renders N plots from an NxM data texture
//-----------------------------------------------------------------------------
class PlotPlugin : public Plugin {
public:
    ~PlotPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "plot"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    explicit PlotPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// PlotLayer - a single plot widget showing N line plots
//
// Data format: NxM matrix where:
//   - N = number of plots (rows)
//   - M = number of data points per plot (columns)
//   - Each value is a float Y coordinate
//
// The X axis is implicit: x[i] = i / (M-1) normalized to [0,1]
//-----------------------------------------------------------------------------
class PlotLayer : public PluginLayer {
public:
    static constexpr uint32_t MAX_PLOTS = 16;  // Maximum number of plots per layer

    PlotLayer();
    ~PlotLayer() override;

    Result<void> init(const std::string& payload) override;
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
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> updateDataTexture(WebGPUContext& ctx);

    // Plot data (CPU side, for updates)
    std::vector<float> _data;
    uint32_t _num_plots = 0;
    uint32_t _num_points = 0;
    bool _data_dirty = false;

    // Viewport
    float _x_min = 0.0f;
    float _x_max = 1.0f;
    float _y_min = 0.0f;
    float _y_max = 1.0f;

    // Visual settings
    float _line_width = 2.0f;
    bool _grid_enabled = true;
    float _colors[MAX_PLOTS * 4];  // RGBA for each plot

    // Pan/zoom state
    float _mouse_x = 0.0f;
    float _mouse_y = 0.0f;
    bool _panning = false;
    float _pan_start_x = 0.0f;
    float _pan_start_y = 0.0f;
    float _viewport_start_x_min = 0.0f;
    float _viewport_start_x_max = 1.0f;
    float _viewport_start_y_min = 0.0f;
    float _viewport_start_y_max = 1.0f;

    // WebGPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;
    WGPUTexture _data_texture = nullptr;
    WGPUTextureView _data_texture_view = nullptr;
    WGPUSampler _sampler = nullptr;

    bool _gpu_initialized = false;
    bool _failed = false;
};

using Plot = PlotPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
