#include "python.h"
#include "yetty_wgpu.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <spdlog/spdlog.h>

// Python must be included after other headers to avoid conflicts
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// Forward declaration of yetty_wgpu module init
extern "C" PyObject* PyInit_yetty_wgpu(void);

namespace yetty {

//-----------------------------------------------------------------------------
// Venv setup helper
//-----------------------------------------------------------------------------

static std::string getPythonPackagesPath() {
    // Use XDG_CACHE_HOME/yetty/python-packages (defaults to ~/.cache/yetty/python-packages)
    // Packages are cache-like since they can be regenerated via pip install
    const char* cacheHome = std::getenv("XDG_CACHE_HOME");
    if (cacheHome && cacheHome[0] != '\0') {
        return std::string(cacheHome) + "/yetty/python-packages";
    }
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.cache/yetty/python-packages";
}

static bool setupPythonPackages() {
    std::string pkgPath = getPythonPackagesPath();

    // Check if packages already exist (check for pygfx directory)
    if (fs::exists(pkgPath + "/pygfx")) {
        spdlog::info("Python packages ready at {}", pkgPath);
        return true;
    }

    // Create the directory
    spdlog::info("Installing pygfx and fastplotlib to {}...", pkgPath);
    fs::create_directories(pkgPath);

    // Use the embedded Python's pip (CMAKE_BINARY_DIR/python/install/bin/python3)
    // Need to set LD_LIBRARY_PATH for libpython3.13.so
    std::string pythonDir = std::string(CMAKE_BINARY_DIR) + "/python/install";
    std::string ldPath = "LD_LIBRARY_PATH=" + pythonDir + "/lib:$LD_LIBRARY_PATH ";
    std::string embeddedPip = pythonDir + "/bin/python3 -m pip";
    std::string installCmd = ldPath + embeddedPip + " install --target=" + pkgPath + " --quiet pygfx fastplotlib wgpu glfw pillow imageio 2>&1";

    spdlog::info("Running: {}", installCmd);
    if (std::system(installCmd.c_str()) != 0) {
        spdlog::error("Failed to install Python packages");
        return false;
    }

    spdlog::info("Python packages installed successfully");
    return true;
}

//-----------------------------------------------------------------------------
// PythonPlugin
//-----------------------------------------------------------------------------

PythonPlugin::~PythonPlugin() {
    (void)dispose();
}

Result<PluginPtr> PythonPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new PythonPlugin(std::move(engine)));
    if (auto res = static_cast<PythonPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init PythonPlugin", res);
    }
    return Ok(p);
}

Result<void> PythonPlugin::init() noexcept {
    // Setup packages with pygfx/fastplotlib
    if (!setupPythonPackages()) {
        spdlog::warn("Failed to setup Python packages - pygfx features may not work");
    }

    auto result = initPython();
    if (!result) {
        return result;
    }
    
    // Load init.py callbacks
    auto init_result = loadInitCallbacks();
    if (!init_result) {
        return init_result;
    }

    _initialized = true;
    spdlog::info("PythonPlugin initialized");
    
    // Release GIL so render thread can acquire it later
    _main_thread_state = PyEval_SaveThread();
    spdlog::debug("GIL released after init");
    
    return Ok();
}

Result<void> PythonPlugin::initPython() {
    if (_py_initialized) {
        spdlog::debug("Python already initialized, skipping");
        return Ok();
    }

    spdlog::info("=== Initializing Python interpreter ===");
    spdlog::info("CMAKE_BINARY_DIR: {}", CMAKE_BINARY_DIR);
    
    // Set YETTY_WGPU_LIB_PATH so wgpu-py uses the same wgpu-native as yetty
    // This MUST be done before any Python/wgpu imports
    std::string wgpuLibPath = std::string(CMAKE_BINARY_DIR) + "/_deps/wgpu-native/lib/libwgpu_native.so";
    setenv("YETTY_WGPU_LIB_PATH", wgpuLibPath.c_str(), 1);
    spdlog::info("Set YETTY_WGPU_LIB_PATH={}", wgpuLibPath);

    // Register yetty_wgpu as a built-in module BEFORE Py_Initialize
    spdlog::debug("Registering yetty_wgpu built-in module");
    if (PyImport_AppendInittab("yetty_wgpu", PyInit_yetty_wgpu) == -1) {
        return Err<void>("Failed to register yetty_wgpu module");
    }

    // Configure Python for embedding
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);

    // Set program name
    PyStatus status = PyConfig_SetString(&config, &config.program_name, L"yetty-python");
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&config);
        return Err<void>("Failed to set Python program name");
    }

    // Import site module for full stdlib support
    config.site_import = 1;

    // Initialize Python
    spdlog::debug("Calling Py_InitializeFromConfig");
    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);

    if (PyStatus_Exception(status)) {
        return Err<void>("Failed to initialize Python interpreter");
    }

    // Get main module and its dictionary
    _main_module = PyImport_AddModule("__main__");
    if (!_main_module) {
        Py_Finalize();
        return Err<void>("Failed to get Python __main__ module");
    }

    _main_dict = PyModule_GetDict(_main_module);
    if (!_main_dict) {
        Py_Finalize();
        return Err<void>("Failed to get Python __main__ dict");
    }

    _py_initialized = true;
    spdlog::info("Python {} interpreter initialized", Py_GetVersion());
    
    // Log Python executable path
    PyRun_SimpleString("import sys; print('[Python] executable:', sys.executable)");
    PyRun_SimpleString("import sys; print('[Python] prefix:', sys.prefix)");
    PyRun_SimpleString("import sys; print('[Python] version:', sys.version)");

    // Add packages directory to sys.path
    std::string pkgPath = getPythonPackagesPath();
    spdlog::info("Python packages path: {}", pkgPath);
    if (fs::exists(pkgPath)) {
        std::string code = "import sys; sys.path.insert(0, '" + pkgPath + "')";
        PyRun_SimpleString(code.c_str());
        spdlog::info("Added Python packages to path: {}", pkgPath);
    } else {
        spdlog::warn("Python packages path does not exist: {}", pkgPath);
    }

    // Also add the yetty_pygfx module path
    std::string pygfxPath = std::string(CMAKE_BINARY_DIR) + "/python";
    spdlog::info("yetty_pygfx module path: {}", pygfxPath);
    if (fs::exists(pygfxPath)) {
        std::string code = "import sys; sys.path.insert(0, '" + pygfxPath + "')";
        PyRun_SimpleString(code.c_str());
        spdlog::info("Added yetty_pygfx module to path: {}", pygfxPath);
    } else {
        spdlog::warn("yetty_pygfx module path does not exist: {}", pygfxPath);
    }
    
    // Log sys.path
    PyRun_SimpleString("import sys; print('[Python] sys.path (first 5):', sys.path[:5])");

    return Ok();
}

Result<void> PythonPlugin::loadInitCallbacks() {
    spdlog::info("Loading init.py callbacks...");
    
    // Load init module from src/yetty/plugins/python/init.py
    std::string initPath = std::string(__FILE__);
    initPath = initPath.substr(0, initPath.find_last_of("/\\"));
    initPath += "/init.py";
    
    spdlog::debug("Loading init.py from: {}", initPath);
    
    // Add the directory to sys.path
    std::string addPath = "import sys; sys.path.insert(0, '" + 
        initPath.substr(0, initPath.find_last_of("/\\")) + "')";
    PyRun_SimpleString(addPath.c_str());
    
    // Import init module
    _init_module = PyImport_ImportModule("init");
    if (!_init_module) {
        PyErr_Print();
        return Err<void>("Failed to import init module");
    }
    
    // Get callback functions
    _init_plugin_func = PyObject_GetAttrString(_init_module, "init_plugin");
    _init_layer_func = PyObject_GetAttrString(_init_module, "init_layer");
    _dispose_layer_func = PyObject_GetAttrString(_init_module, "dispose_layer");
    _dispose_plugin_func = PyObject_GetAttrString(_init_module, "dispose_plugin");
    
    if (!_init_plugin_func || !_init_layer_func || !_dispose_layer_func) {
        PyErr_Print();
        return Err<void>("Failed to get callback functions from init module");
    }
    
    // Call init_plugin()
    spdlog::info("Calling init_plugin()...");
    PyObject* result = PyObject_CallObject(_init_plugin_func, nullptr);
    if (!result) {
        PyErr_Print();
        return Err<void>("init_plugin() failed");
    }
    Py_DECREF(result);
    
    spdlog::info("init.py callbacks loaded successfully");
    return Ok();
}

Result<void> PythonPlugin::dispose() {
    // Dispose layers first
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PythonPlugin base", res);
    }

    // Cleanup yetty_wgpu resources
    yetty_wgpu_cleanup();

    // Note: We intentionally don't call Py_Finalize() here because it causes
    // segfaults when wgpu-py's resources are still being cleaned up.
    // The OS will clean up when the process exits.
    if (_py_initialized) {
        _main_module = nullptr;
        _main_dict = nullptr;
        // Py_Finalize();  // Causes segfault - skip for now
        _py_initialized = false;
        spdlog::info("Python interpreter cleanup complete");
    }

    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> PythonPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<PythonLayer>(this);
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init PythonLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<std::string> PythonPlugin::execute(const std::string& code) {
    if (!_py_initialized) {
        return Err<std::string>("Python not initialized");
    }

    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    // Redirect stdout/stderr to capture output
    PyObject* sys = PyImport_ImportModule("sys");
    if (!sys) {
        PyGILState_Release(gstate);
        return Err<std::string>("Failed to import sys module");
    }

    PyObject* io = PyImport_ImportModule("io");
    if (!io) {
        Py_DECREF(sys);
        PyGILState_Release(gstate);
        return Err<std::string>("Failed to import io module");
    }

    // Create StringIO for capturing output
    PyObject* string_io_class = PyObject_GetAttrString(io, "StringIO");
    PyObject* string_io = PyObject_CallObject(string_io_class, nullptr);
    Py_DECREF(string_io_class);

    // Save original stdout/stderr
    PyObject* old_stdout = PyObject_GetAttrString(sys, "stdout");
    PyObject* old_stderr = PyObject_GetAttrString(sys, "stderr");

    // Redirect stdout/stderr to our StringIO
    PyObject_SetAttrString(sys, "stdout", string_io);
    PyObject_SetAttrString(sys, "stderr", string_io);

    // Execute the code
    PyObject* result = PyRun_String(code.c_str(), Py_file_input, _main_dict, _main_dict);

    // Get captured output
    PyObject* getvalue = PyObject_GetAttrString(string_io, "getvalue");
    PyObject* output_obj = PyObject_CallObject(getvalue, nullptr);
    Py_DECREF(getvalue);

    std::string output;
    if (output_obj && PyUnicode_Check(output_obj)) {
        output = PyUnicode_AsUTF8(output_obj);
    }
    Py_XDECREF(output_obj);

    // Restore stdout/stderr
    PyObject_SetAttrString(sys, "stdout", old_stdout);
    PyObject_SetAttrString(sys, "stderr", old_stderr);
    Py_DECREF(old_stdout);
    Py_DECREF(old_stderr);
    Py_DECREF(string_io);
    Py_DECREF(io);
    Py_DECREF(sys);

    if (!result) {
        // Get error info
        PyErr_Print();
        PyErr_Clear();
        PyGILState_Release(gstate);
        return Err<std::string>("Python execution error: " + output);
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);
    return Ok(output);
}

Result<void> PythonPlugin::runFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Err<void>("Failed to open Python file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto result = execute(buffer.str());
    if (!result) {
        return Err<void>("Failed to execute Python file", result);
    }

    spdlog::info("Python file executed: {}", path);
    return Ok();
}

//-----------------------------------------------------------------------------
// PythonLayer
//-----------------------------------------------------------------------------

PythonLayer::PythonLayer(PythonPlugin* plugin)
    : _plugin(plugin) {}

PythonLayer::~PythonLayer() {
    (void)dispose();
}

Result<void> PythonLayer::init(const std::string& payload) {
    // Store the payload but DON'T execute yet
    // We need to wait for render() to be called so we have WebGPU context
    // to call init_layer() first
    
    if (payload.empty()) {
        return Err("Empty payload");
    }

    // Check if it's inline content (prefixed with "inline:")
    if (payload.compare(0, 7, "inline:") == 0) {
        // Inline code - extract content after "inline:" prefix
        _payload = payload.substr(7);
        spdlog::info("PythonLayer: inline code provided ({} bytes)", _payload.size());
    } else {
        // File path - verify it exists and load it
        std::ifstream file(payload);
        if (!file.good()) {
            return Err("Failed to open Python script file: " + payload);
        }
        
        // Read file content
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();
        
        if (content.empty()) {
            return Err("Empty Python script file: " + payload);
        }
        
        _payload = content;
        _script_path = payload;
        spdlog::info("PythonLayer: loaded script from file: {} ({} bytes)", 
                    payload, _payload.size());
    }

    _initialized = true;
    return Ok();
}

Result<void> PythonLayer::dispose() {
    // Call dispose_layer callback
    callDisposeLayer();
    
    // Cleanup blit resources first (before Python cleanup)
    if (_blit_bind_group) {
        wgpuBindGroupRelease(_blit_bind_group);
        _blit_bind_group = nullptr;
    }
    if (_blit_pipeline) {
        wgpuRenderPipelineRelease(_blit_pipeline);
        _blit_pipeline = nullptr;
    }
    if (_blit_sampler) {
        wgpuSamplerRelease(_blit_sampler);
        _blit_sampler = nullptr;
    }
    _blit_initialized = false;

    // Cleanup pygfx resources (only if Python is still initialized)
    if (_plugin && _plugin->isInitialized()) {
        if (_user_render_func) {
            Py_DECREF(_user_render_func);
            _user_render_func = nullptr;
        }
        if (_render_frame_func) {
            Py_DECREF(_render_frame_func);
            _render_frame_func = nullptr;
        }
        if (_pygfx_module) {
            // Call cleanup
            PyObject* cleanup_func = PyObject_GetAttrString(_pygfx_module, "cleanup");
            if (cleanup_func) {
                PyObject* result = PyObject_CallObject(cleanup_func, nullptr);
                Py_XDECREF(result);
                Py_DECREF(cleanup_func);
            }
            Py_DECREF(_pygfx_module);
            _pygfx_module = nullptr;
        }
    } else {
        // Python already finalized, just null out pointers
        _user_render_func = nullptr;
        _render_frame_func = nullptr;
        _pygfx_module = nullptr;
    }
    _pygfx_initialized = false;
    _wgpu_handles_set = false;

    _initialized = false;
    return Ok();
}

Result<void> PythonLayer::render(WebGPUContext& ctx) {
    (void)ctx;
    // Legacy render - not used. Use renderToPass() instead.
    return Ok();
}

void PythonLayer::prepareFrame(WebGPUContext& ctx) {
    // This is called BEFORE the shared render pass begins
    // Here we initialize and render pygfx content to our texture
    
    if (_failed || !_visible) {
        return;
    }

    // First time: call init_layer, then execute user script
    if (!_wgpu_handles_set) {
        uint32_t width = getPixelWidth();
        uint32_t height = getPixelHeight();
        
        spdlog::info("PythonLayer: First prepareFrame - layer dimensions: {}x{}", width, height);
        
        // Use defaults if not set
        if (width == 0) width = 1024;
        if (height == 0) height = 768;
        
        spdlog::info("PythonLayer: Initializing layer with dimensions: {}x{}", width, height);
        
        // Call init_layer() callback with WebGPU context
        if (!callInitLayer(ctx, width, height)) {
            _failed = true;
            return;
        }
        
        // Now execute the user script (init.py has already been called)
        if (!_script_path.empty()) {
            spdlog::info("PythonLayer: Executing user script: {}", _script_path);
            auto result = _plugin->runFile(_script_path);
            if (!result) {
                _output = "Error: " + result.error().message();
                spdlog::error("PythonLayer: failed to run script: {}", _script_path);
                _failed = true;
                return;
            }
            _output = "Script executed: " + _script_path;
            spdlog::info("PythonLayer: User script executed successfully");
        } else if (!_payload.empty()) {
            // Inline code
            spdlog::info("PythonLayer: Executing inline code");
            auto result = _plugin->execute(_payload);
            if (!result) {
                _output = "Error: " + result.error().message();
                _failed = true;
                return;
            }
            _output = *result;
        }
    }

    // Call user's render() callback every frame to render to texture
    uint32_t width = getPixelWidth();
    uint32_t height = getPixelHeight();
    if (width == 0) width = _texture_width;
    if (height == 0) height = _texture_height;
    
    if (!callRender(ctx, _frame_count, width, height)) {
        // Render callback failed, but don't mark as failed permanently
        // The user script might recover
    }
    
    _frame_count++;
}

bool PythonLayer::renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    // This is called INSIDE the shared render pass
    // We only blit our pre-rendered texture here - NO Python rendering!
    
    if (_failed) return false;
    if (!_visible) return false;
    if (!_wgpu_handles_set) return false;  // prepareFrame() hasn't run yet

    // Blit the rendered texture to the layer rectangle in the pass
    if (!blitToPass(pass, ctx)) {
        spdlog::error("PythonLayer: Failed to blit render texture");
        return false;
    }
    
    return true;
}

bool PythonLayer::callInitLayer(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    spdlog::info("PythonLayer: calling init_layer({}, {})", width, height);
    
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    auto init_func = _plugin->getInitLayerFunc();
    if (!init_func) {
        spdlog::error("PythonLayer: init_layer function not available");
        PyGILState_Release(gstate);
        return false;
    }
    
    // Create context dict with WebGPU handles
    PyObject* ctx_dict = PyDict_New();
    if (!ctx_dict) {
        spdlog::error("PythonLayer: Failed to create ctx dict");
        PyGILState_Release(gstate);
        return false;
    }
    
    void* device = (void*)ctx.getDevice();
    void* queue = (void*)ctx.getQueue();
    
    PyObject* device_obj = PyLong_FromVoidPtr(device);
    PyObject* queue_obj = PyLong_FromVoidPtr(queue);
    PyObject* width_obj = PyLong_FromUnsignedLong(width);
    PyObject* height_obj = PyLong_FromUnsignedLong(height);
    
    PyDict_SetItemString(ctx_dict, "device", device_obj);
    PyDict_SetItemString(ctx_dict, "queue", queue_obj);
    PyDict_SetItemString(ctx_dict, "width", width_obj);
    PyDict_SetItemString(ctx_dict, "height", height_obj);
    
    Py_DECREF(device_obj);
    Py_DECREF(queue_obj);
    Py_DECREF(width_obj);
    Py_DECREF(height_obj);
    
    // Call init_layer(ctx, width, height)
    PyObject* args = Py_BuildValue("(OII)", ctx_dict, width, height);
    if (!args) {
        Py_DECREF(ctx_dict);
        spdlog::error("PythonLayer: Failed to build args");
        PyErr_Print();
        PyGILState_Release(gstate);
        return false;
    }
    
    spdlog::info("PythonLayer: Calling Python init_layer...");
    PyObject* result = PyObject_CallObject(init_func, args);
    
    Py_DECREF(ctx_dict);
    Py_DECREF(args);
    
    if (!result) {
        spdlog::error("PythonLayer: init_layer() raised exception");
        PyErr_Print();
        PyGILState_Release(gstate);
        return false;
    }
    
    Py_DECREF(result);
    PyGILState_Release(gstate);
    
    _wgpu_handles_set = true;
    _texture_width = width;
    _texture_height = height;
    
    spdlog::info("PythonLayer: init_layer() completed successfully");
    return true;
}

bool PythonLayer::callRender(WebGPUContext& ctx, uint32_t frame_num, uint32_t width, uint32_t height) {
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    if (!_user_render_func) {
        // Try to get render function from main dict
        PyObject* main_module = PyImport_AddModule("__main__");
        PyObject* main_dict = PyModule_GetDict(main_module);
        _user_render_func = PyDict_GetItemString(main_dict, "render");
        
        if (!_user_render_func) {
            spdlog::warn("PythonLayer: No render() function found in user script");
            PyGILState_Release(gstate);
            return false;
        }
        
        Py_INCREF(_user_render_func);  // Keep reference
        spdlog::info("PythonLayer: Found user render() function");
    }
    
    // Create context dict
    PyObject* ctx_dict = PyDict_New();
    PyDict_SetItemString(ctx_dict, "device", PyLong_FromVoidPtr((void*)ctx.getDevice()));
    PyDict_SetItemString(ctx_dict, "queue", PyLong_FromVoidPtr((void*)ctx.getQueue()));
    
    // Call render(ctx, frame_num, width, height)
    PyObject* args = Py_BuildValue("(Oiii)", ctx_dict, frame_num, width, height);
    PyObject* result = PyObject_CallObject(_user_render_func, args);
    
    Py_DECREF(ctx_dict);
    Py_DECREF(args);
    
    if (!result) {
        PyErr_Print();
        spdlog::error("PythonLayer: render() failed");
        PyGILState_Release(gstate);
        return false;
    }
    
    Py_DECREF(result);
    PyGILState_Release(gstate);
    return true;
}

bool PythonLayer::callDisposeLayer() {
    spdlog::info("PythonLayer: calling dispose_layer()");
    
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    auto dispose_func = _plugin->getDisposeLayerFunc();
    if (!dispose_func) {
        PyGILState_Release(gstate);
        return true;  // Not an error if not available
    }
    
    PyObject* result = PyObject_CallObject(dispose_func, nullptr);
    if (!result) {
        PyErr_Print();
        spdlog::warn("PythonLayer: dispose_layer() failed");
        PyGILState_Release(gstate);
        return false;
    }
    
    Py_DECREF(result);
    PyGILState_Release(gstate);
    return true;
}

bool PythonLayer::initPygfx(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    if (_pygfx_initialized) {
        return true;
    }

    // Ensure WebGPU handles are set
    if (!_wgpu_handles_set) {
        yetty_wgpu_set_handles(
            nullptr,
            nullptr,
            ctx.getDevice(),
            ctx.getQueue()
        );
        _wgpu_handles_set = true;
    }

    // Create render texture
    if (!yetty_wgpu_create_render_texture(width, height)) {
        spdlog::error("PythonLayer: Failed to create render texture");
        return false;
    }
    _texture_width = width;
    _texture_height = height;

    // Import and initialize yetty_pygfx module
    // First, add the build/python directory to sys.path
    auto result = _plugin->execute(
        "import sys\n"
        "sys.path.insert(0, '" + std::string(CMAKE_BINARY_DIR) + "/python')\n"
    );
    if (!result) {
        spdlog::error("PythonLayer: Failed to set Python path");
        return false;
    }

    // Import yetty_pygfx
    result = _plugin->execute(
        "import yetty_pygfx\n"
        "yetty_pygfx.init_pygfx()\n"
    );
    if (!result) {
        spdlog::error("PythonLayer: Failed to import yetty_pygfx: {}", result.error().message());
        return false;
    }

    // Create the figure
    std::string create_fig_code =
        "fig = yetty_pygfx.create_figure(" + std::to_string(width) + ", " + std::to_string(height) + ")\n";
    result = _plugin->execute(create_fig_code);
    if (!result) {
        spdlog::error("PythonLayer: Failed to create figure: {}", result.error().message());
        return false;
    }

    // Get the render_frame function for later use
    _pygfx_module = PyImport_ImportModule("yetty_pygfx");
    if (_pygfx_module) {
        _render_frame_func = PyObject_GetAttrString(_pygfx_module, "render_frame");
    }

    _pygfx_initialized = true;
    spdlog::info("PythonLayer: pygfx initialized with {}x{} render target", width, height);

    return true;
}

bool PythonLayer::renderPygfx() {
    if (!_pygfx_initialized || !_render_frame_func) {
        return false;
    }

    // Call render_frame()
    PyObject* result = PyObject_CallObject(_render_frame_func, nullptr);
    if (!result) {
        PyErr_Print();
        PyErr_Clear();
        return false;
    }

    bool success = PyObject_IsTrue(result);
    Py_DECREF(result);

    return success;
}

bool PythonLayer::createBlitPipeline(WebGPUContext& ctx) {
    if (_blit_initialized) return true;

    WGPUDevice device = ctx.getDevice();

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.maxAnisotropy = 1;

    _blit_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!_blit_sampler) {
        spdlog::error("PythonLayer: Failed to create blit sampler");
        return false;
    }

    // Simple fullscreen blit shader
    const char* shaderCode = R"(
        @group(0) @binding(0) var tex: texture_2d<f32>;
        @group(0) @binding(1) var samp: sampler;

        struct VertexOutput {
            @builtin(position) position: vec4f,
            @location(0) uv: vec2f,
        };

        @vertex
        fn vs_main(@builtin(vertex_index) idx: u32) -> VertexOutput {
            var positions = array<vec2f, 6>(
                vec2f(-1.0, -1.0),
                vec2f( 1.0, -1.0),
                vec2f(-1.0,  1.0),
                vec2f(-1.0,  1.0),
                vec2f( 1.0, -1.0),
                vec2f( 1.0,  1.0)
            );
            var uvs = array<vec2f, 6>(
                vec2f(0.0, 1.0),
                vec2f(1.0, 1.0),
                vec2f(0.0, 0.0),
                vec2f(0.0, 0.0),
                vec2f(1.0, 1.0),
                vec2f(1.0, 0.0)
            );
            var out: VertexOutput;
            out.position = vec4f(positions[idx], 0.0, 1.0);
            out.uv = uvs[idx];
            return out;
        }

        @fragment
        fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
            return textureSample(tex, samp, uv);
        }
    )";

    WGPUShaderSourceWGSL wgslSource = {};
    wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslSource.code = {.data = shaderCode, .length = strlen(shaderCode)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslSource.chain;

    WGPUShaderModule shader = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shader) {
        spdlog::error("PythonLayer: Failed to create blit shader");
        return false;
    }

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;

    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = layout;

    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = {.data = "vs_main", .length = WGPU_STRLEN};

    WGPUFragmentState fragState = {};
    fragState.module = shader;
    fragState.entryPoint = {.data = "fs_main", .length = WGPU_STRLEN};

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = ctx.getSurfaceFormat();
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _blit_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(bgl);

    if (!_blit_pipeline) {
        spdlog::error("PythonLayer: Failed to create blit pipeline");
        return false;
    }

    _blit_initialized = true;
    spdlog::info("PythonLayer: Blit pipeline created");
    return true;
}

bool PythonLayer::blitToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    // Get the render texture view
    WGPUTextureView texView = yetty_wgpu_get_render_texture_view();
    if (!texView) {
        return false;
    }

    // Create blit pipeline if needed
    if (!_blit_initialized) {
        if (!createBlitPipeline(ctx)) {
            return false;
        }
    }

    // Create bind group (recreate each frame in case texture changed)
    if (_blit_bind_group) {
        wgpuBindGroupRelease(_blit_bind_group);
        _blit_bind_group = nullptr;
    }

    // Get bind group layout from pipeline
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(_blit_pipeline, 0);

    WGPUBindGroupEntry bgEntries[2] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = texView;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = _blit_sampler;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;

    _blit_bind_group = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    if (!_blit_bind_group) {
        spdlog::error("PythonLayer: Failed to create blit bind group");
        return false;
    }

    // Calculate pixel position from render context
    const auto& rc = _render_context;
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    // Adjust for scroll offset if relative positioning
    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Check if layer is off-screen (scrolled out of view)
    if (pixelY + pixelH < 0 || pixelY >= (float)rc.screenHeight ||
        pixelX + pixelW < 0 || pixelX >= (float)rc.screenWidth) {
        // Layer is completely off-screen, skip rendering
        return true;
    }

    // Clamp scissor rect to screen bounds to avoid negative/huge values
    uint32_t scissorX = pixelX < 0 ? 0 : (uint32_t)pixelX;
    uint32_t scissorY = pixelY < 0 ? 0 : (uint32_t)pixelY;
    uint32_t scissorW = (uint32_t)pixelW;
    uint32_t scissorH = (uint32_t)pixelH;
    
    // Clamp width/height to screen bounds
    if (scissorX + scissorW > rc.screenWidth) {
        scissorW = rc.screenWidth - scissorX;
    }
    if (scissorY + scissorH > rc.screenHeight) {
        scissorH = rc.screenHeight - scissorY;
    }

    // Set viewport to layer rectangle
    wgpuRenderPassEncoderSetViewport(pass, pixelX, pixelY, pixelW, pixelH, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, scissorX, scissorY, scissorW, scissorH);

    wgpuRenderPassEncoderSetPipeline(pass, _blit_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _blit_bind_group, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    // Reset viewport to full screen for next layer
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, (float)rc.screenWidth, (float)rc.screenHeight, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, rc.screenWidth, rc.screenHeight);

    return true;
}

bool PythonLayer::blitRenderTexture(WebGPUContext& ctx) {
    // Legacy method - creates own render pass (slow, don't use)
    // Use blitToPass() instead
    (void)ctx;
    return false;
}

bool PythonLayer::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    if (action != 1) return false; // GLFW_PRESS only

    // Enter key - execute input buffer
    if (key == 257) { // GLFW_KEY_ENTER
        if (!_input_buffer.empty()) {
            auto result = _plugin->execute(_input_buffer);
            if (result) {
                _output += ">>> " + _input_buffer + "\n" + *result;
            } else {
                _output += ">>> " + _input_buffer + "\nError: " + result.error().message() + "\n";
            }
            _input_buffer.clear();
            return true;
        }
    }

    // Backspace - remove last character
    if (key == 259) { // GLFW_KEY_BACKSPACE
        if (!_input_buffer.empty()) {
            _input_buffer.pop_back();
            return true;
        }
    }

    return false;
}

bool PythonLayer::onChar(unsigned int codepoint) {
    if (codepoint < 128) {
        _input_buffer += static_cast<char>(codepoint);
        return true;
    }
    return false;
}

bool PythonLayer::onMouseMove(float localX, float localY) {
    _mouse_x = localX;
    _mouse_y = localY;
    
    if (!_pygfx_initialized) return false;
    
    // Forward to pygfx via Python callback
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    PyObject* pygfx = PyImport_ImportModule("yetty_pygfx");
    if (pygfx) {
        PyObject* func = PyObject_GetAttrString(pygfx, "on_mouse_move");
        if (func && PyCallable_Check(func)) {
            PyObject* args = Py_BuildValue("(ffii)", localX, localY, 
                                           _mouse_down ? _mouse_button : -1, 0);
            PyObject* result = PyObject_CallObject(func, args);
            Py_XDECREF(result);
            Py_DECREF(args);
        }
        Py_XDECREF(func);
        Py_DECREF(pygfx);
    }
    PyErr_Clear();
    
    PyGILState_Release(gstate);
    return true;
}

bool PythonLayer::onMouseButton(int button, bool pressed) {
    _mouse_down = pressed;
    _mouse_button = button;
    
    if (!_pygfx_initialized) return false;
    
    // Forward to pygfx via Python callback
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    PyObject* pygfx = PyImport_ImportModule("yetty_pygfx");
    if (pygfx) {
        PyObject* func = PyObject_GetAttrString(pygfx, "on_mouse_button");
        if (func && PyCallable_Check(func)) {
            PyObject* args = Py_BuildValue("(ffii)", _mouse_x, _mouse_y, button, pressed ? 1 : 0);
            PyObject* result = PyObject_CallObject(func, args);
            Py_XDECREF(result);
            Py_DECREF(args);
        }
        Py_XDECREF(func);
        Py_DECREF(pygfx);
    }
    PyErr_Clear();
    
    PyGILState_Release(gstate);
    return true;
}

bool PythonLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    if (!_pygfx_initialized) return false;
    
    // Forward to pygfx via Python callback
    PyGILState_STATE gstate = PyGILState_Ensure();
    
    PyObject* pygfx = PyImport_ImportModule("yetty_pygfx");
    if (pygfx) {
        PyObject* func = PyObject_GetAttrString(pygfx, "on_mouse_scroll");
        if (func && PyCallable_Check(func)) {
            PyObject* args = Py_BuildValue("(ffffi)", _mouse_x, _mouse_y, xoffset, yoffset, mods);
            PyObject* result = PyObject_CallObject(func, args);
            Py_XDECREF(result);
            Py_DECREF(args);
        }
        Py_XDECREF(func);
        Py_DECREF(pygfx);
    }
    PyErr_Clear();
    
    PyGILState_Release(gstate);
    return true;
}

} // namespace yetty

extern "C" {
    const char* name() { return "python"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::PythonPlugin::create(std::move(engine));
    }
}
