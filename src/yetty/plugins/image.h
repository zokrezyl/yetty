#pragma once

#include "../plugin.h"
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
    void dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    void renderAll(WebGPUContext& ctx,
                   WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                   uint32_t screenWidth, uint32_t screenHeight,
                   float cellWidth, float cellHeight,
                   int scrollOffset, uint32_t termRows) override;
};

//-----------------------------------------------------------------------------
// ImageLayer
//-----------------------------------------------------------------------------
class ImageLayer : public PluginLayer {
public:
    ImageLayer();
    ~ImageLayer() override;

    Result<void> init(const std::string& payload) override;
    void dispose() override;

    void render(WebGPUContext& ctx,
                WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                uint32_t screenWidth, uint32_t screenHeight,
                float pixelX, float pixelY, float pixelW, float pixelH);

private:
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
};

using Image = ImagePlugin;

} // namespace yetty

extern "C" {
    const char* image_plugin_name();
    yetty::Result<yetty::PluginPtr> image_plugin_create();
}
