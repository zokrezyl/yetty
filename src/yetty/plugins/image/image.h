#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class Image;

//-----------------------------------------------------------------------------
// ImagePlugin
//-----------------------------------------------------------------------------
class ImagePlugin : public Plugin {
public:
    ~ImagePlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "image"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(const std::string& payload) override;

private:
    explicit ImagePlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// Image - displays a static image
//
// Two-phase construction:
//   1. Constructor (private) - stores payload
//   2. init() (private) - no args, loads image
//   3. create() (public) - factory
//-----------------------------------------------------------------------------
class Image : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<Image>(new Image(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Image", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Image() override;

    Result<void> dispose() override;

    Result<void> render(WebGPUContext& ctx) override;
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

private:
    explicit Image(const std::string& payload) {
        payload_ = payload;
    }

    Result<void> init() override;

    Result<void> loadImage(const std::string& data);
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);

    unsigned char* imageData_ = nullptr;
    int imageWidth_ = 0;
    int imageHeight_ = 0;
    int imageChannels_ = 0;

    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    WGPUSampler sampler_ = nullptr;

    bool gpuInitialized_ = false;
    bool failed_ = false;

    float lastRect_[4] = {0, 0, 0, 0};
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
