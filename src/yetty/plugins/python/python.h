#pragma once

#include <yetty/plugin.h>
#include <webgpu/webgpu.h>
#include <memory>
#include <string>

// Forward declare Python types to avoid including Python.h in header
struct _object;
typedef _object PyObject;
struct _ts;
typedef _ts PyThreadState;

namespace yetty {

class PythonW;

//-----------------------------------------------------------------------------
// PythonPlugin - Embeds Python interpreter
//-----------------------------------------------------------------------------
class PythonPlugin : public Plugin {
public:
    ~PythonPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "python"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(const std::string& payload) override;

    // Execute Python code and return result as string
    Result<std::string> execute(const std::string& code);

    // Run a Python file
    Result<void> runFile(const std::string& path);

    // Check if Python is initialized
    bool isInitialized() const { return pyInitialized_; }

    // Callback functions from init.py
    PyObject* getInitWidgetFunc() const { return initWidgetFunc_; }
    PyObject* getDisposeWidgetFunc() const { return disposeWidgetFunc_; }

private:
    explicit PythonPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> pluginInit() noexcept;
    Result<void> initPython();
    Result<void> loadInitCallbacks();

    bool pyInitialized_ = false;
    PyObject* mainModule_ = nullptr;
    PyObject* mainDict_ = nullptr;
    PyThreadState* mainThreadState_ = nullptr;  // For GIL management

    // Callback functions from init.py
    PyObject* initModule_ = nullptr;
    PyObject* initPluginFunc_ = nullptr;
    PyObject* initWidgetFunc_ = nullptr;
    PyObject* disposeWidgetFunc_ = nullptr;
    PyObject* disposePluginFunc_ = nullptr;
};

//-----------------------------------------------------------------------------
// PythonW - Displays Python output or runs Python scripts
//-----------------------------------------------------------------------------
class PythonW : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload, PythonPlugin* plugin) {
        auto w = std::shared_ptr<PythonW>(new PythonW(payload, plugin));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init PythonW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~PythonW() override;

    Result<void> dispose() override;

    // Renderable interface
    const std::string& name() const override { return name_; }
    void start() override { running_ = true; }
    void stop() override { running_ = false; }
    bool isRunning() const override { return running_; }
    Result<void> render(WebGPUContext& ctx) override;

    // Pre-render phase - render pygfx to texture BEFORE shared pass
    void prepareFrame(WebGPUContext& ctx) override;

    // Batched render - only blits pre-rendered texture
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Input handling for REPL-like interaction
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsKeyboard() const override { return true; }

    // Mouse input handling for pygfx interaction
    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool wantsMouse() const override { return true; }

    // pygfx integration
    bool initPygfx(WebGPUContext& ctx, uint32_t width, uint32_t height);
    bool renderPygfx();
    bool blitRenderTexture(WebGPUContext& ctx);
    bool blitToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx);
    bool isPygfxInitialized() const { return pygfxInitialized_; }

    // Callback management
    bool callInitWidget(WebGPUContext& ctx, uint32_t width, uint32_t height);
    bool callRender(WebGPUContext& ctx, uint32_t frameNum, uint32_t width, uint32_t height);
    bool callDisposeWidget();

private:
    explicit PythonW(const std::string& payload, PythonPlugin* plugin)
        : plugin_(plugin)
    {
        payload_ = payload;
    }

    Result<void> init() override;
    bool createBlitPipeline(WebGPUContext& ctx);

    PythonPlugin* plugin_ = nullptr;
    std::string scriptPath_;
    std::string output_;
    std::string inputBuffer_;
    bool failed_ = false;

    // For rendering output
    float scrollOffset_ = 0.0f;

    // pygfx integration state
    bool pygfxInitialized_ = false;
    bool wgpuHandlesSet_ = false;
    PyObject* pygfxModule_ = nullptr;
    PyObject* renderFrameFunc_ = nullptr;
    uint32_t textureWidth_ = 0;
    uint32_t textureHeight_ = 0;
    uint32_t frameCount_ = 0;

    // User render callback
    PyObject* userRenderFunc_ = nullptr;

    // Mouse state for pygfx interaction
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    bool mouseDown_ = false;
    int mouseButton_ = 0;

    // Blit pipeline resources
    WGPURenderPipeline blitPipeline_ = nullptr;
    WGPUBindGroup blitBindGroup_ = nullptr;
    WGPUSampler blitSampler_ = nullptr;
    bool blitInitialized_ = false;
};

using Python = PythonPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
