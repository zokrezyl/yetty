#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <thorvg.h>
#include <memory>
#include <vector>

namespace yetty {

class Lottie;

//-----------------------------------------------------------------------------
// ThorvgPlugin - Vector graphics plugin using ThorVG library
// Supports SVG, Lottie animations, and other vector formats
// Uses ThorVG's WebGPU renderer for native GPU rendering integration
//-----------------------------------------------------------------------------
class ThorvgPlugin : public Plugin {
public:
    ~ThorvgPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "thorvg"; }

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
    ThorvgPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// Lottie - A single vector graphics widget instance
// Can display static SVG or animated Lottie content
// Renders directly using WebGPU via ThorVG's WgCanvas
//-----------------------------------------------------------------------------
class Lottie : public Widget {
public:
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
        auto w = std::shared_ptr<Lottie>(new Lottie(payload));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Lottie", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Lottie() override;

    Result<void> dispose() override;

    void prepareFrame(WebGPUContext& ctx, bool on) override;

    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) override;

    // Animation control
    bool isAnimated() const { return isAnimated_; }
    float getTotalFrames() const { return totalFrames_; }
    float getCurrentFrame() const { return currentFrame_; }
    float getDuration() const { return duration_; }
    void setFrame(float frame);
    void setPlaying(bool playing) { playing_ = playing; }
    bool isPlaying() const { return playing_; }
    void setLoop(bool loop) { loop_ = loop; }
    bool isLooping() const { return loop_; }

private:
    explicit Lottie(const std::string& payload) {
        _payload = payload;
    }

    Result<void> init() override;
    Result<void> loadContent(const std::string& data, const std::string& mimeType = "");
    Result<void> initWebGPU(WebGPUContext& ctx);
    Result<void> createCompositePipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> renderThorvgFrame(WGPUDevice device);
    Result<std::string> yamlToSvg(const std::string& yamlContent);

    // ThorVG objects - using WebGPU canvas
    std::unique_ptr<tvg::WgCanvas> canvas_;
    tvg::Picture* picture_ = nullptr;  // Owned by canvas or animation
    std::unique_ptr<tvg::Animation> animation_;  // For both Lottie and static content

    // Content dimensions
    uint32_t contentWidth_ = 0;
    uint32_t contentHeight_ = 0;

    // Animation state
    bool isAnimated_ = false;
    float totalFrames_ = 0.0f;
    float currentFrame_ = 0.0f;
    float duration_ = 0.0f;
    bool playing_ = true;
    bool loop_ = true;
    double accumulatedTime_ = 0.0;

    // WebGPU resources for compositing ThorVG output to screen
    WGPURenderPipeline compositePipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUTexture renderTexture_ = nullptr;  // ThorVG renders to this
    WGPUTextureView renderTextureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    bool gpuInitialized_ = false;
    bool contentDirty_ = true;
    bool failed_ = false;

    // Cached rect for dirty optimization
    float lastRect_[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // On/off state tracking for GPU resource management
    bool wasOn_ = true;
    void releaseGPUResources();
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
