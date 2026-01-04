#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class ImageLayer;

//-----------------------------------------------------------------------------
// ImagePlugin
//-----------------------------------------------------------------------------
class ImagePlugin : public Plugin {
public:
    ~ImagePlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "image"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

private:
    explicit ImagePlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// ImageLayer
//-----------------------------------------------------------------------------
class ImageLayer : public PluginLayer {
public:
    ImageLayer();
    ~ImageLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    // Legacy render (creates own encoder - slow)
    Result<void> render(WebGPUContext& ctx) override;

    // Batched render (draws into existing pass - fast!)
    bool renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

private:
    Result<void> loadImage(const std::string& data);
    Result<void> createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat);

    unsigned char* _image_data = nullptr;
    int _image_width = 0;
    int _image_height = 0;
    int _image_channels = 0;

    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;
    WGPUTexture _texture = nullptr;
    WGPUTextureView _texture_view = nullptr;
    WGPUSampler _sampler = nullptr;

    bool _gpu_initialized = false;
    bool _failed = false;

    // Cached rect for dirty optimization (skip uniform write if unchanged)
    float _last_rect[4] = {0, 0, 0, 0};
};

using Image = ImagePlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
