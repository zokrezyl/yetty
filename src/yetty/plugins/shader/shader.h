#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>

namespace yetty {

class Shader;

//-----------------------------------------------------------------------------
// ShaderPlugin - manages all shader layers
// Each layer has its own compiled pipeline and state
//-----------------------------------------------------------------------------
class ShaderPlugin : public Plugin {
public:
    ~ShaderPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "shader"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(const std::string& payload) override;

private:
    explicit ShaderPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// Shader - a single shader instance at a position
//
// Two-phase construction:
//   1. Constructor (private) - stores payload
//   2. init() (private) - no args, compiles shader
//   3. create() (public) - factory
//-----------------------------------------------------------------------------
class Shader : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<Shader>(new Shader(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Shader", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Shader() override;

    Result<void> dispose() override;

    // Legacy render (creates own encoder - slow)
    Result<void> render(WebGPUContext& ctx) override;

    // Batched render (draws into existing pass - fast!)
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Input handling
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit Shader(const std::string& payload) {
        payload_ = payload;
    }

    Result<void> init() override;

    Result<void> compileShader(WebGPUContext& ctx,
                               WGPUTextureFormat targetFormat,
                               const std::string& fragmentCode);

    // Vertex shader that positions the quad
    static const char* getVertexShader();
    // Wrapper for user fragment shader
    static std::string wrapFragmentShader(const std::string& userCode);

    // WebGPU resources
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bind_group_ = nullptr;
    WGPUBuffer uniform_buffer_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    bool compiled_ = false;
    bool failed_ = false;

    // Mouse state (in local coordinates, normalized 0-1)
    float mouse_x_ = 0.0f;
    float mouse_y_ = 0.0f;
    bool mouse_down_ = false;
    bool mouse_grabbed_ = false;

    // Scroll-controlled parameters
    float param_ = 0.5f;
    float zoom_ = 1.0f;
};

// Backward compatibility alias
using ShaderToy = ShaderPlugin;

} // namespace yetty

// C exports for dynamic loading
extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
