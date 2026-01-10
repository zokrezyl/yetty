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

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "shader"; }

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

private:
    ShaderPlugin() noexcept = default;
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
class WidgetFactory;

class Shader : public Widget {
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
        (void)fontManager;
        (void)loop;
        (void)pluginArgs;
        auto w = std::shared_ptr<Shader>(new Shader(payload, factory));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
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
    explicit Shader(const std::string& payload, WidgetFactory* factory)
        : _factory(factory) {
        _payload = payload;
    }

    Result<void> init() override;

    WidgetFactory* _factory = nullptr;

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
    yetty::Result<yetty::PluginPtr> create();
}
