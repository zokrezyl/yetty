#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class ShaderToyLayer;

//-----------------------------------------------------------------------------
// ShaderToyPlugin - manages all shader layers
// Each layer has its own compiled pipeline and state
//-----------------------------------------------------------------------------
class ShaderToyPlugin : public Plugin {
public:
    ~ShaderToyPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "shader"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

private:
    explicit ShaderToyPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// ShaderToyLayer - a single shader instance at a position
//-----------------------------------------------------------------------------
class ShaderToyLayer : public PluginLayer {
public:
    ShaderToyLayer();
    ~ShaderToyLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;
    Result<void> update(double deltaTime) override;

    // Called by ShaderToyPlugin::renderAll
    Result<void> render(WebGPUContext& ctx,
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
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bind_group = nullptr;
    WGPUBuffer _uniform_buffer = nullptr;

    float _time = 0.0f;
    bool _compiled = false;
    bool _failed = false;

    // Mouse state (in local coordinates, normalized 0-1)
    float _mouse_x = 0.0f;
    float _mouse_y = 0.0f;
    bool _mouse_down = false;
    bool _mouse_grabbed = false;

    // Scroll-controlled parameters
    float _param = 0.5f;
    float _zoom = 1.0f;
};

// Backward compatibility alias
using ShaderToy = ShaderToyPlugin;

} // namespace yetty

// C exports for dynamic loading
extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
