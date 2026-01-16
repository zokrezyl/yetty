#pragma once

#include <yetty/plugin.h>
#include <yetty/font.h>
#include <webgpu/webgpu.h>
#include <vector>
#include <array>

namespace yetty {

class Plot;

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

private:
    PlotPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// Plot - a single plot widget showing N line plots
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
class Plot : public Widget {
public:
    static constexpr uint32_t MAX_PLOTS = 16;  // Maximum number of plots per layer
    static constexpr uint32_t MAX_TICKS = 10;  // Maximum ticks per axis
    static constexpr uint32_t MAX_LABEL_CHARS = 12;  // Max chars per label

    static Result<WidgetPtr> create(const std::string& payload, Font* font = nullptr) {
        auto w = std::shared_ptr<Plot>(new Plot(payload, font));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Plot", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Plot() override;

    Result<void> dispose() override;
    void update(double deltaTime) override;

    // Widget render interface (batched rendering)
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

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
    explicit Plot(const std::string& payload, Font* font = nullptr)
        : _font(font) {
        _payload = payload;
    }

    Result<void> init() override;
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> updateDataTexture(WebGPUContext& ctx);
    void calculateAxisTicks();
    std::string formatTickValue(float value) const;

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

    // Font resources (from FontManager)
    Font* _font = nullptr;

    // Axis tick data (computed on viewport change)
    struct AxisTickData {
        float positions[MAX_TICKS];              // Screen positions (pixels)
        uint16_t glyphIndices[MAX_TICKS][MAX_LABEL_CHARS];  // Glyph indices per label
        uint32_t charCounts[MAX_TICKS];          // Chars per label
        uint32_t count = 0;                      // Number of ticks
    };
    AxisTickData _xTicks;
    AxisTickData _yTicks;
    float _marginLeft = 60.0f;   // Pixels for Y-axis labels
    float _marginBottom = 30.0f; // Pixels for X-axis labels
    bool _ticksDirty = true;

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
