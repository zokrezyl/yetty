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
    if (auto res = static_cast<PythonPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init PythonPlugin", res);
    }
    return Ok(p);
}

Result<void> PythonPlugin::pluginInit() noexcept {
    // Setup packages with pygfx/fastplotlib
    if (!setupPythonPackages()) {
        spdlog::warn("Failed to setup Python packages - pygfx features may not work");
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

    initialized_ = true;
    spdlog::info("PythonPlugin initialized");

    // Release GIL so render thread can acquire it later
    mainThreadState_ = PyEval_SaveThread();
    spdlog::debug("GIL released after init");

    return Ok();
}

Result<void> PythonPlugin::initPython() {
    if (pyInitialized_) {
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
    spdlog::info("Calling init_plugin()...");
    PyObject* result = PyObject_CallObject(initPluginFunc_, nullptr);
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
    if (pyInitialized_) {
        mainModule_ = nullptr;
        mainDict_ = nullptr;
        // Py_Finalize();  // Causes segfault - skip for now
        pyInitialized_ = false;
        spdlog::info("Python interpreter cleanup complete");
    }

    initialized_ = false;
    return Ok();
}

Result<WidgetPtr> PythonPlugin::createWidget(const std::string& payload) {
    return PythonW::create(payload, this);
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

    spdlog::info("Python file executed: {}", path);
    return Ok();
}

//-----------------------------------------------------------------------------
// PythonW
//-----------------------------------------------------------------------------

PythonW::~PythonW() {
    (void)dispose();
}

Result<void> PythonW::init() {
    // Store the payload but DON'T execute yet
    // We need to wait for render() to be called so we have WebGPU context
    // to call init_widget() first

    if (payload_.empty()) {
        return Err("Empty payload");
    }

    // Check if it's inline content (prefixed with "inline:")
    if (payload_.compare(0, 7, "inline:") == 0) {
        // Inline code - extract content after "inline:" prefix
        payload_ = payload_.substr(7);
        spdlog::info("PythonW: inline code provided ({} bytes)", payload_.size());
    } else {
        // File path - verify it exists and load it
        std::ifstream file(payload_);
        if (!file.good()) {
            return Err("Failed to open Python script file: " + payload_);
        }

        // Read file content
        std::string content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();

        if (content.empty()) {
            return Err("Empty Python script file: " + payload_);
        }

        scriptPath_ = payload_;
        payload_ = content;
        spdlog::info("PythonW: loaded script from file: {} ({} bytes)",
                    scriptPath_, payload_.size());
    }

    return Ok();
}

Result<void> PythonW::dispose() {
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

    // Cleanup pygfx resources (only if Python is still initialized)
    if (plugin_ && plugin_->isInitialized()) {
        if (userRenderFunc_) {
            Py_DECREF(userRenderFunc_);
            userRenderFunc_ = nullptr;
        }
        if (renderFrameFunc_) {
            Py_DECREF(renderFrameFunc_);
            renderFrameFunc_ = nullptr;
        }
        if (pygfxModule_) {
            // Call cleanup
            PyObject* cleanupFunc = PyObject_GetAttrString(pygfxModule_, "cleanup");
            if (cleanupFunc) {
                PyObject* result = PyObject_CallObject(cleanupFunc, nullptr);
                Py_XDECREF(result);
                Py_DECREF(cleanupFunc);
            }
            Py_DECREF(pygfxModule_);
            pygfxModule_ = nullptr;
        }
    } else {
        // Python already finalized, just null out pointers
        userRenderFunc_ = nullptr;
        renderFrameFunc_ = nullptr;
        pygfxModule_ = nullptr;
    }
    pygfxInitialized_ = false;
    wgpuHandlesSet_ = false;

    return Ok();
}

Result<void> PythonW::render(WebGPUContext& ctx) {
    (void)ctx;
    // Legacy render - not used. Use render() instead.
    return Ok();
}

void PythonW::prepareFrame(WebGPUContext& ctx) {
    // This is called BEFORE the shared render pass begins
    // Here we initialize and render pygfx content to our texture

    if (failed_ || !visible_) {
        return;
    }

    // First time: call init_layer, then execute user script
    if (!wgpuHandlesSet_) {
        uint32_t width = getPixelWidth();
        uint32_t height = getPixelHeight();

        spdlog::info("PythonW: First prepareFrame - layer dimensions: {}x{}", width, height);

        // Use defaults if not set
        if (width == 0) width = 1024;
        if (height == 0) height = 768;

        spdlog::info("PythonW: Initializing layer with dimensions: {}x{}", width, height);

        // Call init_widget() callback with WebGPU context
        if (!callInitWidget(ctx, width, height)) {
            failed_ = true;
            return;
        }

        // Now execute the user script (init.py has already been called)
        if (!scriptPath_.empty()) {
            spdlog::info("PythonW: Executing user script: {}", scriptPath_);
            auto result = plugin_->runFile(scriptPath_);
            if (!result) {
                output_ = "Error: " + result.error().message();
                spdlog::error("PythonW: failed to run script: {}", scriptPath_);
                failed_ = true;
                return;
            }
            output_ = "Script executed: " + scriptPath_;
            spdlog::info("PythonW: User script executed successfully");
        } else if (!payload_.empty()) {
            // Inline code
            spdlog::info("PythonW: Executing inline code");
            auto result = plugin_->execute(payload_);
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

bool PythonW::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    // This is called INSIDE the shared render pass
    // We only blit our pre-rendered texture here - NO Python rendering!

    if (failed_) return false;
    if (!visible_) return false;
    if (!wgpuHandlesSet_) return false;  // prepareFrame() hasn't run yet

    // Blit the rendered texture to the layer rectangle in the pass
    if (!blitToPass(pass, ctx)) {
        spdlog::error("PythonW: Failed to blit render texture");
        return false;
    }

    return true;
}

bool PythonW::callInitWidget(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    spdlog::info("PythonW: calling init_widget({}, {})", width, height);

    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    auto initFunc = plugin_->getInitWidgetFunc();
    if (!initFunc) {
        spdlog::error("PythonW: init_layer function not available");
        PyGILState_Release(gstate);
        return false;
    }

    // Create context dict with WebGPU handles
    PyObject* ctxDict = PyDict_New();
    if (!ctxDict) {
        spdlog::error("PythonW: Failed to create ctx dict");
        PyGILState_Release(gstate);
        return false;
    }

    void* device = (void*)ctx.getDevice();
    void* queue = (void*)ctx.getQueue();

    PyObject* deviceObj = PyLong_FromVoidPtr(device);
    PyObject* queueObj = PyLong_FromVoidPtr(queue);
    PyObject* widthObj = PyLong_FromUnsignedLong(width);
    PyObject* heightObj = PyLong_FromUnsignedLong(height);

    PyDict_SetItemString(ctxDict, "device", deviceObj);
    PyDict_SetItemString(ctxDict, "queue", queueObj);
    PyDict_SetItemString(ctxDict, "width", widthObj);
    PyDict_SetItemString(ctxDict, "height", heightObj);

    Py_DECREF(deviceObj);
    Py_DECREF(queueObj);
    Py_DECREF(widthObj);
    Py_DECREF(heightObj);

    // Call init_widget(ctx, width, height)
    PyObject* args = Py_BuildValue("(OII)", ctxDict, width, height);
    if (!args) {
        Py_DECREF(ctxDict);
        spdlog::error("PythonW: Failed to build args");
        PyErr_Print();
        PyGILState_Release(gstate);
        return false;
    }

    spdlog::info("PythonW: Calling Python init_layer...");
    PyObject* result = PyObject_CallObject(initFunc, args);

    Py_DECREF(ctxDict);
    Py_DECREF(args);

    if (!result) {
        spdlog::error("PythonW: init_widget() raised exception");
        PyErr_Print();
        PyGILState_Release(gstate);
        return false;
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);

    wgpuHandlesSet_ = true;
    textureWidth_ = width;
    textureHeight_ = height;

    spdlog::info("PythonW: init_widget() completed successfully");
    return true;
}

bool PythonW::callRender(WebGPUContext& ctx, uint32_t frameNum, uint32_t width, uint32_t height) {
    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    if (!userRenderFunc_) {
        // Try to get render function from main dict
        PyObject* mainModule = PyImport_AddModule("__main__");
        PyObject* mainDict = PyModule_GetDict(mainModule);
        userRenderFunc_ = PyDict_GetItemString(mainDict, "render");

        if (!userRenderFunc_) {
            spdlog::warn("PythonW: No render() function found in user script");
            PyGILState_Release(gstate);
            return false;
        }

        Py_INCREF(userRenderFunc_);  // Keep reference
        spdlog::info("PythonW: Found user render() function");
    }

    // Create context dict
    PyObject* ctxDict = PyDict_New();
    PyDict_SetItemString(ctxDict, "device", PyLong_FromVoidPtr((void*)ctx.getDevice()));
    PyDict_SetItemString(ctxDict, "queue", PyLong_FromVoidPtr((void*)ctx.getQueue()));

    // Call render(ctx, frame_num, width, height)
    PyObject* args = Py_BuildValue("(Oiii)", ctxDict, frameNum, width, height);
    PyObject* result = PyObject_CallObject(userRenderFunc_, args);

    Py_DECREF(ctxDict);
    Py_DECREF(args);

    if (!result) {
        PyErr_Print();
        spdlog::error("PythonW: render() failed");
        PyGILState_Release(gstate);
        return false;
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);
    return true;
}

bool PythonW::callDisposeWidget() {
    spdlog::info("PythonW: calling dispose_widget()");

    // Acquire GIL
    PyGILState_STATE gstate = PyGILState_Ensure();

    auto disposeFunc = plugin_->getDisposeWidgetFunc();
    if (!disposeFunc) {
        PyGILState_Release(gstate);
        return true;  // Not an error if not available
    }

    PyObject* result = PyObject_CallObject(disposeFunc, nullptr);
    if (!result) {
        PyErr_Print();
        spdlog::warn("PythonW: dispose_widget() failed");
        PyGILState_Release(gstate);
        return false;
    }

    Py_DECREF(result);
    PyGILState_Release(gstate);
    return true;
}

bool PythonW::initPygfx(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    if (pygfxInitialized_) {
        return true;
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

    // Create render texture
    if (!yetty_wgpu_create_render_texture(width, height)) {
        spdlog::error("PythonW: Failed to create render texture");
        return false;
    }
    textureWidth_ = width;
    textureHeight_ = height;

    // Import and initialize yetty_pygfx module
    // First, add the build/python directory to sys.path
    auto result = plugin_->execute(
        "import sys\n"
        "sys.path.insert(0, '" + std::string(CMAKE_BINARY_DIR) + "/python')\n"
    );
    if (!result) {
        spdlog::error("PythonW: Failed to set Python path");
        return false;
    }

    // Import yetty_pygfx
    result = plugin_->execute(
        "import yetty_pygfx\n"
        "yetty_pygfx.init_pygfx()\n"
    );
    if (!result) {
        spdlog::error("PythonW: Failed to import yetty_pygfx: {}", result.error().message());
        return false;
    }

    // Create the figure
    std::string createFigCode =
        "fig = yetty_pygfx.create_figure(" + std::to_string(width) + ", " + std::to_string(height) + ")\n";
    result = plugin_->execute(createFigCode);
    if (!result) {
        spdlog::error("PythonW: Failed to create figure: {}", result.error().message());
        return false;
    }

    // Get the render_frame function for later use
    pygfxModule_ = PyImport_ImportModule("yetty_pygfx");
    if (pygfxModule_) {
        renderFrameFunc_ = PyObject_GetAttrString(pygfxModule_, "render_frame");
    }

    pygfxInitialized_ = true;
    spdlog::info("PythonW: pygfx initialized with {}x{} render target", width, height);

    return true;
}

bool PythonW::renderPygfx() {
    if (!pygfxInitialized_ || !renderFrameFunc_) {
        return false;
    }

    // Call render_frame()
    PyObject* result = PyObject_CallObject(renderFrameFunc_, nullptr);
    if (!result) {
        PyErr_Print();
        PyErr_Clear();
        return false;
    }

    bool success = PyObject_IsTrue(result);
    Py_DECREF(result);

    return success;
}

bool PythonW::createBlitPipeline(WebGPUContext& ctx) {
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
        spdlog::error("PythonW: Failed to create blit sampler");
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
        spdlog::error("PythonW: Failed to create blit shader");
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
        spdlog::error("PythonW: Failed to create blit pipeline");
        return false;
    }

    blitInitialized_ = true;
    spdlog::info("PythonW: Blit pipeline created");
    return true;
}

bool PythonW::blitToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    // Get the render texture view
    WGPUTextureView texView = yetty_wgpu_get_render_texture_view();
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
        spdlog::error("PythonW: Failed to create blit bind group");
        return false;
    }

    // Calculate pixel position from render context
    const auto& rc = renderCtx_;
    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    // Adjust for scroll offset if relative positioning
    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
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

bool PythonW::blitRenderTexture(WebGPUContext& ctx) {
    // Legacy method - creates own render pass (slow, don't use)
    // Use blitToPass() instead
    (void)ctx;
    return false;
}

bool PythonW::onKey(int key, int scancode, int action, int mods) {
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

bool PythonW::onChar(unsigned int codepoint) {
    if (codepoint < 128) {
        inputBuffer_ += static_cast<char>(codepoint);
        return true;
    }
    return false;
}

bool PythonW::onMouseMove(float localX, float localY) {
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

bool PythonW::onMouseButton(int button, bool pressed) {
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

bool PythonW::onMouseScroll(float xoffset, float yoffset, int mods) {
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
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::PythonPlugin::create(std::move(engine));
    }
}
