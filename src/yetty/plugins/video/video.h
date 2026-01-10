#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <memory>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>
#include <mutex>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace yetty {

class Video;

//-----------------------------------------------------------------------------
// VideoPlugin
//-----------------------------------------------------------------------------
class VideoPlugin : public Plugin {
public:
    ~VideoPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "video"; }

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

    // Check if data looks like a video format
    static bool isVideoFormat(const std::string& data);

private:
    VideoPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// Video - displays video files using FFmpeg
//
// Two-phase construction:
//   1. Constructor (private) - stores payload
//   2. init() (private) - no args, loads video
//   3. create() (public) - factory
//-----------------------------------------------------------------------------
class Video : public Widget {
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
        auto w = std::shared_ptr<Video>(new Video(payload));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Video", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Video() override;

    Result<void> dispose() override;

    // Renderable interface - uses RenderContext from base class
    Result<void> render(WebGPUContext& ctx) override;
    void prepareFrame(WebGPUContext& ctx) override;
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Playback control
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    bool isPlaying() const { return playing_; }
    double getCurrentTime() const { return currentTime_; }
    double getDuration() const { return duration_; }

    // Input handling
    bool onMouseButton(int button, bool pressed) override;
    bool wantsMouse() const override { return true; }

private:
    explicit Video(const std::string& payload) {
        _payload = payload;
    }

    Result<void> init() override;

    Result<void> initFFmpeg(const std::string& data);
    Result<void> decodeNextFrame();
    void updateTexture(WebGPUContext& ctx);
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);

    // FFmpeg state
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVFrame* frameRgba_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    int videoStreamIdx_ = -1;

    // Video properties
    int videoWidth_ = 0;
    int videoHeight_ = 0;
    double frameRate_ = 30.0;
    double duration_ = 0.0;
    double timeBase_ = 0.0;

    // Playback state
    bool playing_ = true;
    bool loop_ = true;
    double currentTime_ = 0.0;
    double frameTime_ = 0.0;
    double accumulatedTime_ = 0.0;

    // Frame buffer (RGBA)
    std::vector<uint8_t> frameBuffer_;
    bool frameUpdated_ = false;

    // File path for video
    std::string filePath_;

    // WebGPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    bool gpuInitialized_ = false;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
