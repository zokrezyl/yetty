#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <array>
#include <string>

namespace yetty {

// Maximum number of buffer passes (BufferA, BufferB, BufferC, BufferD like Shadertoy)
constexpr int kMaxBufferPasses = 4;
// Maximum number of input channels (iChannel0-3 like Shadertoy)
constexpr int kMaxChannels = 4;

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
        auto w = std::shared_ptr<Shader>(new Shader(payload, pluginArgs, factory));
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

    void prepareFrame(WebGPUContext& ctx) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Input handling
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

private:
    explicit Shader(const std::string& payload, const std::string& pluginArgs, WidgetFactory* factory)
        : _factory(factory), _pluginArgs(pluginArgs) {
        _payload = payload;
    }

    Result<void> init() override;

    WidgetFactory* _factory = nullptr;
    std::string _pluginArgs;

    // Parse plugin args for channel textures (--channel0=path, --channel1=path, etc.)
    void parsePluginArgs();
    
    // Load texture from file for iChannel
    Result<void> loadChannelTexture(WebGPUContext& ctx, int channel, const std::string& path);
    
    // Create sampler for channel textures
    Result<void> createChannelSampler(WebGPUContext& ctx);

    Result<void> compileShader(WebGPUContext& ctx,
                               WGPUTextureFormat targetFormat,
                               const std::string& fragmentCode);

    // Vertex shader that positions the quad
    static const char* getVertexShader();
    // Wrapper for user fragment shader (main image pass, no textures)
    static std::string wrapFragmentShader(const std::string& userCode);
    // Wrapper for buffer pass fragment shader (with iChannel support)
    static std::string wrapBufferPassShader(const std::string& userCode, int passIndex);
    // Wrapper for Image pass in multipass mode (with iChannel support)
    std::string wrapImagePassShader(const std::string& userCode);

    // WebGPU resources for main pass
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroup _bindGroup = nullptr;
    WGPUBuffer _uniformBuffer = nullptr;
    WGPUBindGroupLayout _bindGroupLayout = nullptr;
    bool _compiled = false;
    bool _failed = false;
    bool _isMultipass = false;

    // Multipass support - buffer passes
    struct BufferPass {
        std::string shaderCode;
        WGPURenderPipeline pipeline = nullptr;
        WGPUBindGroup bindGroup = nullptr;
        WGPUTexture texture = nullptr;
        WGPUTexture texturePrev = nullptr;  // Previous frame for feedback
        WGPUTextureView textureView = nullptr;
        WGPUTextureView texturePrevView = nullptr;
        bool enabled = false;
    };
    std::array<BufferPass, kMaxBufferPasses> _bufferPasses;
    WGPUSampler _bufferSampler = nullptr;
    WGPUBindGroupLayout _bufferBindGroupLayout = nullptr;
    uint32_t _bufferWidth = 0;
    uint32_t _bufferHeight = 0;

    // Parse multipass shader (sections separated by //--- BufferA, //--- Image, etc.)
    bool parseMultipassShader(const std::string& code);
    Result<void> compileBufferPass(WebGPUContext& ctx, int passIndex);
    Result<void> createBufferTextures(WebGPUContext& ctx, uint32_t width, uint32_t height);
    void renderBufferPasses(WebGPUContext& ctx);
    void swapBufferTextures();

    // Main image shader code (after parsing)
    std::string _mainImageCode;

    // Input channel textures (iChannel0-3 like Shadertoy)
    struct ChannelTexture {
        std::string path;
        WGPUTexture texture = nullptr;
        WGPUTextureView textureView = nullptr;
        uint8_t* imageData = nullptr;
        int width = 0;
        int height = 0;
        bool loaded = false;
    };
    std::array<ChannelTexture, kMaxChannels> _channels;
    WGPUSampler _channelSampler = nullptr;
    bool _hasChannelTextures = false;
    
    // Placeholder texture for unloaded channels
    WGPUTexture _placeholderTexture = nullptr;
    WGPUTextureView _placeholderTextureView = nullptr;
    Result<void> createPlaceholderTexture(WebGPUContext& ctx);

    // Standalone mode (when factory is null) - own global bind group
    WGPUBuffer _globalUniformBuffer = nullptr;
    WGPUBindGroup _globalBindGroup = nullptr;
    WGPUBindGroupLayout _globalBindGroupLayout = nullptr;
    Result<void> createStandaloneGlobalBindGroup(WebGPUContext& ctx);
    
    // Helper to get global bind group (from factory or standalone)
    WGPUBindGroup getGlobalBindGroup();
    WGPUBindGroupLayout getGlobalBindGroupLayout();

    // Mouse state (in local coordinates, normalized 0-1)
    float _mouseX = 0.0f;
    float _mouseY = 0.0f;
    bool _mouseDown = false;
    bool _mouseGrabbed = false;

    // Scroll-controlled parameters
    float _param = 0.5f;
    float _zoom = 1.0f;
};

// Backward compatibility alias
using ShaderToy = ShaderPlugin;

} // namespace yetty

// C exports for dynamic loading
extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
