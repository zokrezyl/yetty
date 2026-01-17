#include "python.h"
#include "yetty_wgpu.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <ytrace/ytrace.hpp>
#include <ytrace/ytrace.hpp>

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
        yinfo("Python packages ready at {}", pkgPath);
        return true;
    }

    // Create the directory
    yinfo("Installing pygfx and fastplotlib to {}...", pkgPath);
    fs::create_directories(pkgPath);

    // Use the embedded Python's pip (CMAKE_BINARY_DIR/python/install/bin/python3)
    std::string pythonDir = std::string(CMAKE_BINARY_DIR) + "/python/install";
    std::string embeddedPip = pythonDir + "/bin/python3 -m pip";
    std::string installCmd = embeddedPip + " install --target=" + pkgPath + " pygfx fastplotlib wgpu glfw pillow imageio 2>&1";

    yinfo("Running: {}", installCmd);
    if (std::system(installCmd.c_str()) != 0) {
        yerror("Failed to install Python packages");
        return false;
    }

    yinfo("Python packages installed successfully");
    return true;
}

//-----------------------------------------------------------------------------
// PythonPlugin
//-----------------------------------------------------------------------------

PythonPlugin::~PythonPlugin() {
    (void)dispose();
}

Result<PluginPtr> PythonPlugin::create() noexcept {
    auto p = PluginPtr(new PythonPlugin());
    if (auto res = static_cast<PythonPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init PythonPlugin", res);
    }
    return Ok(p);
}

Result<void> PythonPlugin::pluginInit() noexcept {
    // Setup packages with pygfx/fastplotlib
    if (!setupPythonPackages()) {
        ywarn("Failed to setup Python packages - pygfx features may not work");
    }

    auto result = initPython();
    if (!result) {
        return result;
    }

    // Load init.py callbacks
    auto initResult = loadInitCallbacks();
    if (!initResult) {
        return initResult;
    }

    _initialized = true;
    yinfo("PythonPlugin initialized");

    // Release GIL so render thread can acquire it later
    mainThreadState_ = PyEval_SaveThread();
    ydebug("GIL released after init");

    return Ok();
}

Result<void> PythonPlugin::initPython() {
    if (pyInitialized_) {
        ydebug("Python already initialized, skipping");
        return Ok();
    }

    yinfo("=== Initializing Python interpreter ===");
    yinfo("CMAKE_BINARY_DIR: {}", CMAKE_BINARY_DIR);

    // Set YETTY_WGPU_LIB_PATH so wgpu-py uses the same wgpu-native as yetty
    // This MUST be done before any Python/wgpu imports
#ifdef __APPLE__
    std::string wgpuLibPath = std::string(CMAKE_BINARY_DIR) + "/_deps/wgpu-native/lib/libwgpu_native.dylib";
#else
    std::string wgpuLibPath = std::string(CMAKE_BINARY_DIR) + "/_deps/wgpu-native/lib/libwgpu_native.so";
#endif
    setenv("YETTY_WGPU_LIB_PATH", wgpuLibPath.c_str(), 1);
    yinfo("Set YETTY_WGPU_LIB_PATH={}", wgpuLibPath);

    // Register yetty_wgpu as a built-in module BEFORE Py_Initialize
    ydebug("Registering yetty_wgpu built-in module");
    if (PyImport_AppendInittab("yetty_wgpu", PyInit_yetty_wgpu) == -1) {
        return Err<void>("Failed to register yetty_wgpu module");
    }

    // Configure Python for embedding
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);

    // Set Python home to the embedded Python installation
    // This is where the stdlib (lib/python3.13/) is located
    std::string pythonHome = std::string(CMAKE_BINARY_DIR) + "/python/install";
    std::wstring pythonHomeW(pythonHome.begin(), pythonHome.end());
    yinfo("Setting Python home to: {}", pythonHome);

    PyStatus status = PyConfig_SetString(&config, &config.home, pythonHomeW.c_str());
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&config);
        return Err<void>("Failed to set Python home");
    }

    // Set program name
    status = PyConfig_SetString(&config, &config.program_name, L"yetty-python");
    if (PyStatus_Exception(status)) {
        PyConfig_Clear(&config);
        return Err<void>("Failed to set Python program name");
    }

    // Import site module for full stdlib support
    config.site_import = 1;

    // Initialize Python
    ydebug("Calling Py_InitializeFromConfig");
    status = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);

    if (PyStatus_Exception(status)) {
        return Err<void>("Failed to initialize Python interpreter");
    }

    // Get main module and its dictionary
    mainModule_ = PyImport_AddModule("__main__");
    if (!mainModule_) {
        Py_Finalize();
        return Err<void>("Failed to get Python __main__ module");
    }

    mainDict_ = PyModule_GetDict(mainModule_);
    if (!mainDict_) {
        Py_Finalize();
        return Err<void>("Failed to get Python __main__ dict");
    }

    pyInitialized_ = true;
    yinfo("Python {} interpreter initialized", Py_GetVersion());

    // Log Python executable path
    PyRun_SimpleString("import sys; print('[Python] executable:', sys.executable)");
    PyRun_SimpleString("import sys; print('[Python] prefix:', sys.prefix)");
    PyRun_SimpleString("import sys; print('[Python] version:', sys.version)");

    // Add packages directory to sys.path
    std::string pkgPath = getPythonPackagesPath();
    yinfo("Python packages path: {}", pkgPath);
    if (fs::exists(pkgPath)) {
        std::string code = "import sys; sys.path.insert(0, '" + pkgPath + "')";
        PyRun_SimpleString(code.c_str());
        yinfo("Added Python packages to path: {}", pkgPath);
    } else {
        ywarn("Python packages path does not exist: {}", pkgPath);
    }

    // Also add the yetty_pygfx module path
    std::string pygfxPath = std::string(CMAKE_BINARY_DIR) + "/python";
    yinfo("yetty_pygfx module path: {}", pygfxPath);
    if (fs::exists(pygfxPath)) {
        std::string code = "import sys; sys.path.insert(0, '" + pygfxPath + "')";
        PyRun_SimpleString(code.c_str());
        yinfo("Added yetty_pygfx module to path: {}", pygfxPath);
    } else {
        ywarn("yetty_pygfx module path does not exist: {}", pygfxPath);
    }

    // Log sys.path
    PyRun_SimpleString("import sys; print('[Python] sys.path (first 5):', sys.path[:5])");

    return Ok();
}

Result<void> PythonPlugin::loadInitCallbacks() {
    yinfo("Loading init.py callbacks...");

    // Load init module from src/yetty/plugins/python/init.py
    std::string initPath = std::string(__FILE__);
    initPath = initPath.substr(0, initPath.find_last_of("/\\"));
    initPath += "/init.py";

    ydebug("Loading init.py from: {}", initPath);

    // Add the directory to sys.path
    std::string addPath = "import sys; sys.path.insert(0, '" +
        initPath.substr(0, initPath.find_last_of("/\\")) + "')";
    PyRun_SimpleString(addPath.c_str());

    // Import init module
    initModule_ = PyImport_ImportModule("init");
    if (!initModule_) {
        PyErr_Print();
        return Err<void>("Failed to import init module");
    }

    // Get callback functions
    initPluginFunc_ = PyObject_GetAttrString(initModule_, "init_plugin");
    initWidgetFunc_ = PyObject_GetAttrString(initModule_, "init_widget");
    disposeWidgetFunc_ = PyObject_GetAttrString(initModule_, "dispose_widget");
    disposePluginFunc_ = PyObject_GetAttrString(initModule_, "dispose_plugin");

    if (!initPluginFunc_ || !initWidgetFunc_ || !disposeWidgetFunc_) {
        PyErr_Print();
        return Err<void>("Failed to get callback functions from init module");
    }

    // Call init_plugin()
    yinfo("Calling init_plugin()...");
    PyObject* result = PyObject_CallObject(initPluginFunc_, nullptr);
    if (!result) {
        PyErr_Print();
        return Err<void>("init_plugin() failed");
    }
    Py_DECREF(result);

    yinfo("init.py callbacks loaded successfully");
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
    if (pyInitialized_) {
        mainModule_ = nullptr;
        mainDict_ = nullptr;
        // Py_Finalize();  // Causes segfault - skip for now
        pyInitialized_ = false;
        yinfo("Python interpreter cleanup complete");
    }

    _initialized = false;
    return Ok();
}

Result<WidgetPtr> PythonPlugin::createWidget(
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
) {
    (void)widgetName;
    yfunc();
    yinfo("payload size={} x={} y={} w={} h={}", payload.size(), x, y, widthCells, heightCells);
    return Python::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload, this);
}

Result<std::string> PythonPlugin::execute(const std::string& code) {
    if (!pyInitialized_) {
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
    PyObject* stringIoClass = PyObject_GetAttrString(io, "StringIO");
    PyObject* stringIo = PyObject_CallObject(stringIoClass, nullptr);
    Py_DECREF(stringIoClass);

    // Save original stdout/stderr
    PyObject* oldStdout = PyObject_GetAttrString(sys, "stdout");
    PyObject* oldStderr = PyObject_GetAttrString(sys, "stderr");

    // Redirect stdout/stderr to our StringIO
    PyObject_SetAttrString(sys, "stdout", stringIo);
    PyObject_SetAttrString(sys, "stderr", stringIo);

    // Execute the code
    PyObject* result = PyRun_String(code.c_str(), Py_file_input, mainDict_, mainDict_);

    // Get captured output
    PyObject* getvalue = PyObject_GetAttrString(stringIo, "getvalue");
    PyObject* outputObj = PyObject_CallObject(getvalue, nullptr);
    Py_DECREF(getvalue);

    std::string output;
    if (outputObj && PyUnicode_Check(outputObj)) {
        output = PyUnicode_AsUTF8(outputObj);
    }
    Py_XDECREF(outputObj);

    // Restore stdout/stderr
    PyObject_SetAttrString(sys, "stdout", oldStdout);
    PyObject_SetAttrString(sys, "stderr", oldStderr);
    Py_DECREF(oldStdout);
    Py_DECREF(oldStderr);
    Py_DECREF(stringIo);
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

    yinfo("Python file executed: {}", path);
    return Ok();
}

Result<std::string> PythonPlugin::executeInNamespace(const std::string& code, PyObject* namespaceDict) {
    if (!pyInitialized_) {
        return Err<std::string>("Python not initialized");
    }
    if (!namespaceDict) {
        return execute(code);  // Fall back to main dict
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
    PyObject* stringIoClass = PyObject_GetAttrString(io, "StringIO");
    PyObject* stringIo = PyObject_CallObject(stringIoClass, nullptr);
    Py_DECREF(stringIoClass);

    // Save original stdout/stderr
    PyObject* oldStdout = PyObject_GetAttrString(sys, "stdout");
    PyObject* oldStderr = PyObject_GetAttrString(sys, "stderr");

    // Redirect stdout/stderr to our StringIO
    PyObject_SetAttrString(sys, "stdout", stringIo);
    PyObject_SetAttrString(sys, "stderr", stringIo);

    // Execute the code in the provided namespace
    PyObject* result = PyRun_String(code.c_str(), Py_file_input, namespaceDict, namespaceDict);

    // Get captured output
    PyObject* getvalue = PyObject_GetAttrString(stringIo, "getvalue");
    PyObject* outputObj = PyObject_CallObject(getvalue, nullptr);
    Py_DECREF(getvalue);

    std::string output;
    if (outputObj && PyUnicode_Check(outputObj)) {
        output = PyUnicode_AsUTF8(outputObj);
    }
    Py_XDECREF(outputObj);

    // Restore stdout/stderr
    PyObject_SetAttrString(sys, "stdout", oldStdout);
    PyObject_SetAttrString(sys, "stderr", oldStderr);
    Py_DECREF(oldStdout);
    Py_DECREF(oldStderr);
    Py_DECREF(stringIo);
    Py_DECREF(io);
    Py_DECREF(sys);

    if (!result) {
        PyErr_Print();
        PyErr_Clear();
        PyGILState_Release(gstate);
        return Err<std::string>("Python execution error: " + output);
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);
    return Ok(output);
}

Result<void> PythonPlugin::runFileInNamespace(const std::string& path, PyObject* namespaceDict) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return Err<void>("Failed to open Python file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    auto result = executeInNamespace(buffer.str(), namespaceDict);
    if (!result) {
        return Err<void>("Failed to execute Python file", result);
    }

    yinfo("Python file executed in namespace: {}", path);
    return Ok();
}

//-----------------------------------------------------------------------------
// Python
//-----------------------------------------------------------------------------

Python::~Python() {
    (void)dispose();
}

Result<void> Python::init() {
    // Store the payload but DON'T execute yet
    // We need to wait for render() to be called so we have WebGPU context
    // to call init_widget() first

    if (_payload.empty()) {
        return Err("Empty payload");
    }

    // Check if it's inline content (prefixed with "inline:")
    if (_payload.compare(0, 7, "inline:") == 0) {
        // Inline code - extract content after "inline:" prefix
        _payload = _payload.substr(7);
        yinfo("Python: inline code provided ({} bytes)", _payload.size());
    } else {
        // File path - verify it exists and load it
        std::ifstream file(_payload);
        if (!file.good()) {
            return Err("Failed to open Python script file: " + _payload);
        }

        // Read file content
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();

        if (content.empty()) {
            return Err("Empty Python script file: " + _payload);
        }

        scriptPath_ = _payload;
        _payload = content;
        yinfo("Python: loaded script from file: {} ({} bytes)",
                    scriptPath_, _payload.size());
    }

    return Ok();
}

void Python::releaseGPUResources() {
    // Release blit pipeline resources when widget goes off-screen
    // This saves GPU memory for widgets that are scrolled out of view
    if (blitBindGroup_) {
        wgpuBindGroupRelease(blitBindGroup_);
        blitBindGroup_ = nullptr;
    }
    if (blitPipeline_) {
        wgpuRenderPipelineRelease(blitPipeline_);
        blitPipeline_ = nullptr;
    }
    if (blitSampler_) {
        wgpuSamplerRelease(blitSampler_);
        blitSampler_ = nullptr;
    }
    blitInitialized_ = false;
    yinfo("Python: GPU resources released");
}

Result<void> Python::dispose() {
    // Call dispose_layer callback
    callDisposeWidget();

    // Cleanup blit resources first (before Python cleanup)
    if (blitBindGroup_) {
        wgpuBindGroupRelease(blitBindGroup_);
        blitBindGroup_ = nullptr;
    }
    if (blitPipeline_) {
        wgpuRenderPipelineRelease(blitPipeline_);
        blitPipeline_ = nullptr;
    }
    if (blitSampler_) {
        wgpuSamplerRelease(blitSampler_);
        blitSampler_ = nullptr;
    }
    blitInitialized_ = false;

    // Cleanup Python references (only if Python is still initialized)
    // Note: yetty_pygfx.cleanup() is already called by dispose_widget() above
    if (plugin_ && plugin_->isInitialized()) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        if (userRenderFunc_) {
            Py_DECREF(userRenderFunc_);
            userRenderFunc_ = nullptr;
        }
        if (renderFrameFunc_) {
            Py_DECREF(renderFrameFunc_);
            renderFrameFunc_ = nullptr;
        }
        if (pygfxModule_) {
            Py_DECREF(pygfxModule_);
            pygfxModule_ = nullptr;
        }
        if (widgetDict_) {
            Py_DECREF(widgetDict_);
            widgetDict_ = nullptr;
        }
        PyGILState_Release(gstate);
    } else {
        // Python already finalized, just null out pointers
        userRenderFunc_ = nullptr;
        renderFrameFunc_ = nullptr;
        pygfxModule_ = nullptr;
        widgetDict_ = nullptr;
    }
    pygfxInitialized_ = false;
    wgpuHandlesSet_ = false;

    return Ok();
}

void Python::prepareFrame(WebGPUContext& ctx, bool on) {
    // This is called BEFORE the shared render pass begins
    // Here we initialize and render pygfx content to our texture

    // Handle on/off transitions for GPU resource management
    if (!on && wasOn_) {
        // Transitioning to off - release GPU resources
        yinfo("Python: Transitioning to off - releasing GPU resources");
        releaseGPUResources();
        wasOn_ = false;
        return;
    }

    if (on && !wasOn_) {
        // Transitioning to on - will reinitialize on next frame
        yinfo("Python: Transitioning to on - will reinitialize");
        wasOn_ = true;
        // Reset initialization flags so we recreate resources
        blitInitialized_ = false;
        // Note: wgpuHandlesSet_ stays true, we just need to recreate blit pipeline
    }

    if (!on || failed_ || !_visible) {
        return;
    }

    // First time: call init_layer, then execute user script
    if (!wgpuHandlesSet_) {
        uint32_t width = getPixelWidth();
        uint32_t height = getPixelHeight();

        yinfo("Python: First prepareFrame - layer dimensions: {}x{}", width, height);

        // Use defaults if not set
        if (width == 0) width = 1024;
        if (height == 0) height = 768;

        yinfo("Python: Initializing layer with dimensions: {}x{}", width, height);

        // Call init_widget() callback with WebGPU context
        if (!callInitWidget(ctx, width, height)) {
            failed_ = true;
            return;
        }

        // Create per-widget namespace for isolated execution
        if (!widgetDict_) {
            PyGILState_STATE gstate = PyGILState_Ensure();

            // Create a new dict that inherits from __main__ but is isolated
            widgetDict_ = PyDict_New();

            // Copy builtins so import etc. works
            PyObject* mainModule = PyImport_AddModule("__main__");
            PyObject* mainDict = PyModule_GetDict(mainModule);
            PyObject* builtins = PyDict_GetItemString(mainDict, "__builtins__");
            if (builtins) {
                PyDict_SetItemString(widgetDict_, "__builtins__", builtins);
            }

            // Set widget name
            PyDict_SetItemString(widgetDict_, "__name__", PyUnicode_FromString("__widget__"));
            PyDict_SetItemString(widgetDict_, "__widget_id__", PyLong_FromLong(widgetId_));

            PyGILState_Release(gstate);
            yinfo("Python: Created per-widget namespace for widget {}", widgetId_);
        }

        // Now execute the user script in the widget's namespace
        if (!scriptPath_.empty()) {
            yinfo("Python: Executing user script in widget namespace: {}", scriptPath_);
            auto result = plugin_->runFileInNamespace(scriptPath_, widgetDict_);
            if (!result) {
                output_ = "Error: " + result.error().message();
                yerror("Python: failed to run script: {}", scriptPath_);
                failed_ = true;
                return;
            }
            output_ = "Script executed: " + scriptPath_;
            yinfo("Python: User script executed successfully in widget namespace");
        } else if (!_payload.empty()) {
            // Inline code
            yinfo("Python: Executing inline code in widget namespace");
            auto result = plugin_->executeInNamespace(_payload, widgetDict_);
            if (!result) {
                output_ = "Error: " + result.error().message();
                failed_ = true;
                return;
            }
            output_ = *result;
        }
    }

    // Call user's render() callback every frame to render to texture
    uint32_t width = getPixelWidth();
    uint32_t height = getPixelHeight();
    if (width == 0) width = textureWidth_;
    if (height == 0) height = textureHeight_;

    if (!callRender(ctx, frameCount_, width, height)) {
        // Render callback failed, but don't mark as failed permanently
        // The user script might recover
    }

    frameCount_++;
}

Result<void> Python::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    // This is called INSIDE the shared render pass
    // We only blit our pre-rendered texture here - NO Python rendering!

    if (!on) return Ok();  // Skip rendering when off
    if (failed_) return Err<void>("Python: failed flag is set");
    if (!_visible) return Ok();
    if (!wgpuHandlesSet_) return Ok();  // prepareFrame() hasn't run yet

    // Blit the rendered texture to the layer rectangle in the pass
    if (!blitToPass(pass, ctx)) {
        return Err<void>("Python: Failed to blit render texture");
    }

    return Ok();
}

bool Python::callInitWidget(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    yinfo("Python: calling init_widget({}, {})", width, height);

    // Allocate a unique widget ID for this widget's texture
    widgetId_ = yetty_wgpu_allocate_widget_id();
    yinfo("Python: allocated widget_id={}", widgetId_);

    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    auto initFunc = plugin_->getInitWidgetFunc();
    if (!initFunc) {
        yerror("Python: init_widget function not available");
        PyGILState_Release(gstate);
        return false;
    }

    // Create context dict with WebGPU handles and widget_id
    PyObject* ctxDict = PyDict_New();
    if (!ctxDict) {
        yerror("Python: Failed to create ctx dict");
        PyGILState_Release(gstate);
        return false;
    }

    void* device = (void*)ctx.getDevice();
    void* queue = (void*)ctx.getQueue();

    PyObject* deviceObj = PyLong_FromVoidPtr(device);
    PyObject* queueObj = PyLong_FromVoidPtr(queue);
    PyObject* widgetIdObj = PyLong_FromLong(widgetId_);

    PyDict_SetItemString(ctxDict, "device", deviceObj);
    PyDict_SetItemString(ctxDict, "queue", queueObj);
    PyDict_SetItemString(ctxDict, "widget_id", widgetIdObj);

    Py_DECREF(deviceObj);
    Py_DECREF(queueObj);
    Py_DECREF(widgetIdObj);

    // Call init_widget(ctx, width, height)
    PyObject* args = Py_BuildValue("(OII)", ctxDict, width, height);
    if (!args) {
        Py_DECREF(ctxDict);
        yerror("Python: Failed to build args");
        PyErr_Print();
        PyGILState_Release(gstate);
        return false;
    }

    yinfo("Python: Calling Python init_widget...");
    PyObject* result = PyObject_CallObject(initFunc, args);

    Py_DECREF(ctxDict);
    Py_DECREF(args);

    if (!result) {
        yerror("Python: init_widget() raised exception");
        PyErr_Print();
        PyGILState_Release(gstate);
        return false;
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);

    wgpuHandlesSet_ = true;
    textureWidth_ = width;
    textureHeight_ = height;

    yinfo("Python: init_widget() completed successfully");
    return true;
}

bool Python::callRender(WebGPUContext& ctx, uint32_t frameNum, uint32_t width, uint32_t height) {
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    if (!userRenderFunc_) {
        // Try to get render function from widget's namespace (per-widget isolation)
        if (widgetDict_) {
            userRenderFunc_ = PyDict_GetItemString(widgetDict_, "render");
        }

        // Fall back to main dict if not found in widget namespace
        if (!userRenderFunc_) {
            PyObject* mainModule = PyImport_AddModule("__main__");
            PyObject* mainDict = PyModule_GetDict(mainModule);
            userRenderFunc_ = PyDict_GetItemString(mainDict, "render");
        }

        if (!userRenderFunc_) {
            ywarn("Python: No render() function found in user script for widget {}", widgetId_);
            PyGILState_Release(gstate);
            return false;
        }

        Py_INCREF(userRenderFunc_);  // Keep reference
        yinfo("Python: Found user render() function for widget {}", widgetId_);
    }

    // Create context dict
    PyObject* ctxDict = PyDict_New();
    PyDict_SetItemString(ctxDict, "device", PyLong_FromVoidPtr((void*)ctx.getDevice()));
    PyDict_SetItemString(ctxDict, "queue", PyLong_FromVoidPtr((void*)ctx.getQueue()));
    PyDict_SetItemString(ctxDict, "widget_id", PyLong_FromLong(widgetId_));

    // Set current widget in yetty_pygfx before calling user's render function
    // This ensures render_frame() uses the correct widget even if called without args
    if (pygfxModule_) {
        PyObject* setCurrentFunc = PyObject_GetAttrString(pygfxModule_, "set_current_widget");
        if (setCurrentFunc) {
            PyObject* widgetIdArg = PyLong_FromLong(widgetId_);
            PyObject* setResult = PyObject_CallFunctionObjArgs(setCurrentFunc, widgetIdArg, NULL);
            Py_XDECREF(setResult);
            Py_DECREF(widgetIdArg);
            Py_DECREF(setCurrentFunc);
        }
    }

    // Call render(ctx, frame_num, width, height)
    PyObject* args = Py_BuildValue("(Oiii)", ctxDict, frameNum, width, height);
    PyObject* result = PyObject_CallObject(userRenderFunc_, args);

    Py_DECREF(ctxDict);
    Py_DECREF(args);

    if (!result) {
        PyErr_Print();
        yerror("Python: render() failed");
        PyGILState_Release(gstate);
        return false;
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);
    return true;
}

bool Python::callDisposeWidget() {
    if (widgetId_ < 0) {
        return true;  // Widget was never initialized
    }

    yinfo("Python: calling dispose_widget({})", widgetId_);

    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    auto disposeFunc = plugin_->getDisposeWidgetFunc();
    if (!disposeFunc) {
        PyGILState_Release(gstate);
        // Still cleanup C++ side
        yetty_wgpu_cleanup_widget(widgetId_);
        widgetId_ = -1;
        return true;
    }

    // Call dispose_widget(widget_id)
    PyObject* args = Py_BuildValue("(i)", widgetId_);
    PyObject* result = PyObject_CallObject(disposeFunc, args);
    Py_DECREF(args);

    if (!result) {
        PyErr_Print();
        ywarn("Python: dispose_widget() failed");
        PyGILState_Release(gstate);
        // Still cleanup C++ side
        yetty_wgpu_cleanup_widget(widgetId_);
        widgetId_ = -1;
        return false;
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);

    widgetId_ = -1;
    return true;
}

bool Python::initPygfx(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    if (pygfxInitialized_) {
        return true;
    }

    // Allocate widget ID if not already done
    if (widgetId_ < 0) {
        widgetId_ = yetty_wgpu_allocate_widget_id();
        yinfo("Python: allocated widget_id={} for pygfx", widgetId_);
    }

    // Ensure WebGPU handles are set
    if (!wgpuHandlesSet_) {
        yetty_wgpu_set_handles(
            nullptr,
            nullptr,
            ctx.getDevice(),
            ctx.getQueue()
        );
        wgpuHandlesSet_ = true;
    }

    // Create render texture for this widget
    if (!yetty_wgpu_create_render_texture(widgetId_, width, height)) {
        yerror("Python: Failed to create render texture");
        return false;
    }
    textureWidth_ = width;
    textureHeight_ = height;

    // Import and initialize yetty_pygfx module
    auto result = plugin_->execute(
        "import sys\n"
        "sys.path.insert(0, '" + std::string(CMAKE_BINARY_DIR) + "/python')\n"
    );
    if (!result) {
        yerror("Python: Failed to set Python path");
        return false;
    }

    // Import yetty_pygfx and init for this widget
    result = plugin_->execute(
        "import yetty_pygfx\n"
        "yetty_pygfx.init(" + std::to_string(widgetId_) + ")\n"
    );
    if (!result) {
        yerror("Python: Failed to import yetty_pygfx: {}", result.error().message());
        return false;
    }

    // Create the figure for this widget
    std::string createFigCode =
        "fig = yetty_pygfx.create_figure(" + std::to_string(widgetId_) + ")\n";
    result = plugin_->execute(createFigCode);
    if (!result) {
        yerror("Python: Failed to create figure: {}", result.error().message());
        return false;
    }

    // Get the render_frame function for later use
    pygfxModule_ = PyImport_ImportModule("yetty_pygfx");
    if (pygfxModule_) {
        renderFrameFunc_ = PyObject_GetAttrString(pygfxModule_, "render_frame");
    }

    pygfxInitialized_ = true;
    yinfo("Python: pygfx initialized with {}x{} render target (widget_id={})", width, height, widgetId_);

    return true;
}

bool Python::renderPygfx() {
    if (!pygfxInitialized_ || !renderFrameFunc_) {
        return false;
    }

    // Call render_frame(widget_id)
    PyObject* args = Py_BuildValue("(i)", widgetId_);
    PyObject* result = PyObject_CallObject(renderFrameFunc_, args);
    Py_DECREF(args);

    if (!result) {
        PyErr_Print();
        PyErr_Clear();
        return false;
    }

    bool success = PyObject_IsTrue(result);
    Py_DECREF(result);

    return success;
}

bool Python::createBlitPipeline(WebGPUContext& ctx) {
    if (blitInitialized_) return true;

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

    blitSampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!blitSampler_) {
        yerror("Python: Failed to create blit sampler");
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
        yerror("Python: Failed to create blit shader");
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

    blitPipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shader);
    wgpuPipelineLayoutRelease(layout);
    wgpuBindGroupLayoutRelease(bgl);

    if (!blitPipeline_) {
        yerror("Python: Failed to create blit pipeline");
        return false;
    }

    blitInitialized_ = true;
    yinfo("Python: Blit pipeline created");
    return true;
}

bool Python::blitToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    // Get the render texture view for this widget
    WGPUTextureView texView = yetty_wgpu_get_render_texture_view(widgetId_);
    if (!texView) {
        return false;
    }

    // Create blit pipeline if needed
    if (!blitInitialized_) {
        if (!createBlitPipeline(ctx)) {
            return false;
        }
    }

    // Create bind group (recreate each frame in case texture changed)
    if (blitBindGroup_) {
        wgpuBindGroupRelease(blitBindGroup_);
        blitBindGroup_ = nullptr;
    }

    // Get bind group layout from pipeline
    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(blitPipeline_, 0);

    WGPUBindGroupEntry bgEntries[2] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = texView;

    bgEntries[1].binding = 1;
    bgEntries[1].sampler = blitSampler_;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;

    blitBindGroup_ = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    if (!blitBindGroup_) {
        yerror("Python: Failed to create blit bind group");
        return false;
    }

    // Calculate pixel position from render context
    const auto& rc = _renderCtx;
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    // Adjust for scroll offset if relative positioning
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
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

    wgpuRenderPassEncoderSetPipeline(pass, blitPipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, blitBindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    // Reset viewport to full screen for next layer
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, (float)rc.screenWidth, (float)rc.screenHeight, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, rc.screenWidth, rc.screenHeight);

    return true;
}

bool Python::blitRenderTexture(WebGPUContext& ctx) {
    // Legacy method - creates own render pass (slow, don't use)
    // Use blitToPass() instead
    (void)ctx;
    return false;
}

bool Python::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;

    if (action != 1) return false; // GLFW_PRESS only

    // Enter key - execute input buffer
    if (key == 257) { // GLFW_KEY_ENTER
        if (!inputBuffer_.empty()) {
            auto result = plugin_->execute(inputBuffer_);
            if (result) {
                output_ += ">>> " + inputBuffer_ + "\n" + *result;
            } else {
                output_ += ">>> " + inputBuffer_ + "\nError: " + result.error().message() + "\n";
            }
            inputBuffer_.clear();
            return true;
        }
    }

    // Backspace - remove last character
    if (key == 259) { // GLFW_KEY_BACKSPACE
        if (!inputBuffer_.empty()) {
            inputBuffer_.pop_back();
            return true;
        }
    }

    return false;
}

bool Python::onChar(unsigned int codepoint) {
    if (codepoint < 128) {
        inputBuffer_ += static_cast<char>(codepoint);
        return true;
    }
    return false;
}

bool Python::onMouseMove(float localX, float localY) {
    mouseX_ = localX;
    mouseY_ = localY;

    if (!pygfxInitialized_) return false;

    // Forward to pygfx via Python callback
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* pygfx = PyImport_ImportModule("yetty_pygfx");
    if (pygfx) {
        PyObject* func = PyObject_GetAttrString(pygfx, "on_mouse_move");
        if (func && PyCallable_Check(func)) {
            PyObject* args = Py_BuildValue("(ffii)", localX, localY,
                                           mouseDown_ ? mouseButton_ : -1, 0);
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

bool Python::onMouseButton(int button, bool pressed) {
    mouseDown_ = pressed;
    mouseButton_ = button;

    if (!pygfxInitialized_) return false;

    // Forward to pygfx via Python callback
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* pygfx = PyImport_ImportModule("yetty_pygfx");
    if (pygfx) {
        PyObject* func = PyObject_GetAttrString(pygfx, "on_mouse_button");
        if (func && PyCallable_Check(func)) {
            PyObject* args = Py_BuildValue("(ffii)", mouseX_, mouseY_, button, pressed ? 1 : 0);
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

bool Python::onMouseScroll(float xoffset, float yoffset, int mods) {
    if (!pygfxInitialized_) return false;

    // Forward to pygfx via Python callback
    PyGILState_STATE gstate = PyGILState_Ensure();

    PyObject* pygfx = PyImport_ImportModule("yetty_pygfx");
    if (pygfx) {
        PyObject* func = PyObject_GetAttrString(pygfx, "on_mouse_scroll");
        if (func && PyCallable_Check(func)) {
            PyObject* args = Py_BuildValue("(ffffi)", mouseX_, mouseY_, xoffset, yoffset, mods);
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
    yetty::Result<yetty::PluginPtr> create() {
        return yetty::PythonPlugin::create();
    }
}
