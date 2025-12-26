#pragma once

#include "../Plugin.h"
#include <webgpu/webgpu.h>

namespace yetty {

class ShaderToyLayer;

//-----------------------------------------------------------------------------
// ShaderToyPlugin - manages all shader layers
// Each layer has its own compiled pipeline and state
//-----------------------------------------------------------------------------
class ShaderToyPlugin : public Plugin {
public:
    ShaderToyPlugin();
    ~ShaderToyPlugin() override;

    // Factory method for plugin creation
    static Result<PluginPtr> create();

    // Plugin interface
    const char* pluginName() const override { return "shader"; }

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
// ShaderToyLayer - a single shader instance at a position
//-----------------------------------------------------------------------------
class ShaderToyLayer : public PluginLayer {
public:
    ShaderToyLayer();
    ~ShaderToyLayer() override;

    Result<void> init(const std::string& payload) override;
    void dispose() override;
    void update(double deltaTime) override;

    // Called by ShaderToyPlugin::renderAll
    void render(WebGPUContext& ctx,
                WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                uint32_t screenWidth, uint32_t screenHeight,
                float pixelX, float pixelY, float pixelW, float pixelH);

    // Input handling
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    Result<void> compileShader(WebGPUContext& ctx,
                               WGPUTextureFormat targetFormat,
                               const std::string& fragmentCode);

    // Vertex shader that positions the quad
    static const char* getVertexShader();
    // Wrapper for user fragment shader
    static std::string wrapFragmentShader(const std::string& userCode);

    // WebGPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bindGroup_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;

    float time_ = 0.0f;
    bool compiled_ = false;
    bool failed_ = false;

    // Mouse state (in local coordinates, normalized 0-1)
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool mouseDown_ = false;
    bool mouseGrabbed_ = false;

    // Scroll-controlled parameters
    float param_ = 0.5f;
    float zoom_ = 1.0f;
};

// Backward compatibility alias
using ShaderToy = ShaderToyPlugin;

} // namespace yetty

// C exports for dynamic loading
extern "C" {
    const char* shader_plugin_name();
    yetty::Result<yetty::PluginPtr> shader_plugin_create();
}
