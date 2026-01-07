#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <thorvg.h>
#include <memory>
#include <vector>

namespace yetty {

class ThorvgLayer;

//-----------------------------------------------------------------------------
// ThorvgPlugin - Vector graphics plugin using ThorVG library
// Supports SVG, Lottie animations, and other vector formats
// Uses ThorVG's WebGPU renderer for native GPU rendering integration
//-----------------------------------------------------------------------------
class ThorvgPlugin : public Plugin {
public:
    ~ThorvgPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "thorvg"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

private:
    explicit ThorvgPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// ThorvgLayer - A single vector graphics layer instance
// Can display static SVG or animated Lottie content
// Renders directly using WebGPU via ThorVG's WgCanvas
//-----------------------------------------------------------------------------
class ThorvgLayer : public PluginLayer {
public:
    ThorvgLayer();
    ~ThorvgLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    // Legacy render (creates own encoder - slow)
    Result<void> render(WebGPUContext& ctx) override;

    // Pre-render phase - render ThorVG content to texture BEFORE shared pass
    void prepareFrame(WebGPUContext& ctx) override;

    // Batched render (draws into existing pass - fast!)
    // Only blits pre-rendered texture, no ThorVG rendering here
    bool renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Animation control
    bool isAnimated() const { return _is_animated; }
    float getTotalFrames() const { return _total_frames; }
    float getCurrentFrame() const { return _current_frame; }
    float getDuration() const { return _duration; }
    void setFrame(float frame);
    void setPlaying(bool playing) { _playing = playing; }
    bool isPlaying() const { return _playing; }
    void setLoop(bool loop) { _loop = loop; }
    bool isLooping() const { return _loop; }

private:
    Result<void> loadContent(const std::string& data, const std::string& mimeType = "");
    Result<void> initWebGPU(WebGPUContext& ctx);
    Result<void> createCompositePipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);
    Result<void> renderThorvgFrame(WGPUDevice device);
    Result<std::string> yamlToSvg(const std::string& yamlContent);

    // ThorVG objects - using WebGPU canvas
    std::unique_ptr<tvg::WgCanvas> _canvas;
    tvg::Picture* _picture = nullptr;  // Owned by canvas or animation
    std::unique_ptr<tvg::Animation> _animation;  // For both Lottie and static content

    // Content dimensions
    uint32_t _content_width = 0;
    uint32_t _content_height = 0;

    // Animation state
    bool _is_animated = false;
    float _total_frames = 0.0f;
    float _current_frame = 0.0f;
    float _duration = 0.0f;
    bool _playing = true;
    bool _loop = true;
    double _accumulated_time = 0.0;

    // WebGPU resources for compositing ThorVG output to screen
    WGPURenderPipeline _composite_pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;
    WGPUTexture _render_texture = nullptr;  // ThorVG renders to this
    WGPUTextureView _render_texture_view = nullptr;
    WGPUSampler _sampler = nullptr;

    bool _gpu_initialized = false;
    bool _content_dirty = true;
    bool _failed = false;

    // Cached rect for dirty optimization
    float _last_rect[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Cached payload for re-init
    std::string _payload;
};

using Thorvg = ThorvgPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
