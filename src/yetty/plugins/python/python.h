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

class PythonLayer;

//-----------------------------------------------------------------------------
// PythonPlugin - Embeds Python interpreter
//-----------------------------------------------------------------------------
class PythonPlugin : public Plugin {
public:
    ~PythonPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "python"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    // Execute Python code and return result as string
    Result<std::string> execute(const std::string& code);

    // Run a Python file
    Result<void> runFile(const std::string& path);

    // Check if Python is initialized
    bool isInitialized() const { return _py_initialized; }
    
    // Callback functions from init.py
    PyObject* getInitLayerFunc() const { return _init_layer_func; }
    PyObject* getDisposeLayerFunc() const { return _dispose_layer_func; }

private:
    explicit PythonPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
    Result<void> initPython();
    Result<void> loadInitCallbacks();

    bool _py_initialized = false;
    PyObject* _main_module = nullptr;
    PyObject* _main_dict = nullptr;
    PyThreadState* _main_thread_state = nullptr;  // For GIL management
    
    // Callback functions from init.py
    PyObject* _init_module = nullptr;
    PyObject* _init_plugin_func = nullptr;
    PyObject* _init_layer_func = nullptr;
    PyObject* _dispose_layer_func = nullptr;
    PyObject* _dispose_plugin_func = nullptr;
};

//-----------------------------------------------------------------------------
// PythonLayer - Displays Python output or runs Python scripts
//-----------------------------------------------------------------------------
class PythonLayer : public PluginLayer {
public:
    PythonLayer(PythonPlugin* plugin);
    ~PythonLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    // Renderable interface
    const std::string& name() const override { return _name; }
    void start() override { _running = true; }
    void stop() override { _running = false; }
    bool isRunning() const override { return _running; }
    Result<void> render(WebGPUContext& ctx) override;
    bool renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    // Input handling for REPL-like interaction
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsKeyboard() const override { return true; }

    // pygfx integration
    bool initPygfx(WebGPUContext& ctx, uint32_t width, uint32_t height);
    bool renderPygfx();
    bool blitRenderTexture(WebGPUContext& ctx);
    bool blitToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx);
    bool isPygfxInitialized() const { return _pygfx_initialized; }
    
    // Callback management
    bool callInitLayer(WebGPUContext& ctx, uint32_t width, uint32_t height);
    bool callRender(WebGPUContext& ctx, uint32_t frame_num, uint32_t width, uint32_t height);
    bool callDisposeLayer();

private:
    PythonPlugin* _plugin = nullptr;
    std::string _name = "python";
    std::string _script_path;
    std::string _output;
    std::string _input_buffer;
    bool _initialized = false;
    bool _failed = false;
    bool _running = false;

    // For rendering output
    float _scroll_offset = 0.0f;

    // pygfx integration state
    bool _pygfx_initialized = false;
    bool _wgpu_handles_set = false;
    PyObject* _pygfx_module = nullptr;
    PyObject* _render_frame_func = nullptr;
    uint32_t _texture_width = 0;
    uint32_t _texture_height = 0;
    uint32_t _frame_count = 0;
    
    // User render callback
    PyObject* _user_render_func = nullptr;

    // Blit pipeline resources
    WGPURenderPipeline _blit_pipeline = nullptr;
    WGPUBindGroup _blit_bind_group = nullptr;
    WGPUSampler _blit_sampler = nullptr;
    bool _blit_initialized = false;
    bool createBlitPipeline(WebGPUContext& ctx);
};

using Python = PythonPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
