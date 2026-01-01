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

class VideoLayer;

//-----------------------------------------------------------------------------
// VideoPlugin
//-----------------------------------------------------------------------------
class VideoPlugin : public Plugin {
public:
    VideoPlugin();
    ~VideoPlugin() override;

    static Result<PluginPtr> create();

    const char* pluginName() const override { return "video"; }

    Result<void> init(WebGPUContext* ctx) override;
    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WebGPUContext& ctx,
                           WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

    // Check if data looks like a video format
    static bool isVideoFormat(const std::string& data);
};

//-----------------------------------------------------------------------------
// VideoLayer
//-----------------------------------------------------------------------------
class VideoLayer : public PluginLayer {
public:
    VideoLayer();
    ~VideoLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;
    Result<void> update(double deltaTime) override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

    // Playback control
    void play();
    void pause();
    void stop();
    void seek(double seconds);
    bool isPlaying() const { return _playing; }
    double getCurrentTime() const { return _current_time; }
    double getDuration() const { return _duration; }

    // Input handling
    bool onMouseButton(int button, bool pressed) override;
    bool wantsMouse() const override { return true; }

private:
    Result<void> initFFmpeg(const std::string& data);
    Result<void> decodeNextFrame();
    void updateTexture(WebGPUContext& ctx);
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);

    // FFmpeg state
    AVFormatContext* _format_ctx = nullptr;
    AVCodecContext* _codec_ctx = nullptr;
    AVFrame* _frame = nullptr;
    AVFrame* _frame_rgba = nullptr;
    AVPacket* _packet = nullptr;
    SwsContext* _sws_ctx = nullptr;
    int _video_stream_idx = -1;

    // Video properties
    int _video_width = 0;
    int _video_height = 0;
    double _frame_rate = 30.0;
    double _duration = 0.0;
    double _time_base = 0.0;

    // Playback state
    bool _playing = true;
    bool _loop = true;
    double _current_time = 0.0;
    double _frame_time = 0.0;
    double _accumulated_time = 0.0;

    // Frame buffer (RGBA)
    std::vector<uint8_t> _frame_buffer;
    bool _frame_updated = false;

    // Custom I/O buffer for in-memory data
    std::vector<uint8_t> _input_data;
    size_t _input_pos = 0;

    // WebGPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;
    WGPUTexture _texture = nullptr;
    WGPUTextureView _texture_view = nullptr;
    WGPUSampler _sampler = nullptr;

    bool _gpu_initialized = false;
    bool _failed = false;
};

using Video = VideoPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
