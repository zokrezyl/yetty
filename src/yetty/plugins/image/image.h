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
    ImagePlugin();
    ~ImagePlugin() override;

    static Result<PluginPtr> create();

    const char* pluginName() const override { return "image"; }

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
// ImageLayer
//-----------------------------------------------------------------------------
class ImageLayer : public PluginLayer {
public:
    ImageLayer();
    ~ImageLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY, float pixelW, float pixelH);

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
};

using Image = ImagePlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
