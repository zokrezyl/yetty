//-----------------------------------------------------------------------------
// yetty-plugin-tester - Test application for yetty plugins
//-----------------------------------------------------------------------------
// Usage:
//   yetty-plugin-tester run <plugin-name> [options]
//   yetty-plugin-tester list
//   yetty-plugin-tester info <plugin-name>
//
// Examples:
//   yetty-plugin-tester run pdf --file document.pdf --rect 0,0,800,600
//   yetty-plugin-tester run video --file video.mp4 --rect 0,0,1280,720
//   yetty-plugin-tester run python --code "print('hello')"
//   yetty-plugin-tester run python --file script.py --pygfx
//-----------------------------------------------------------------------------

#include <yetty/plugin.h>
#include <yetty/webgpu-context.h>
#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <ytrace/ytrace.hpp>
#include <args.hxx>

#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

//-----------------------------------------------------------------------------
// Plugin loading
//-----------------------------------------------------------------------------

struct PluginHandle {
    void* handle = nullptr;
    const char* (*name_func)() = nullptr;
    yetty::Result<yetty::PluginPtr> (*create_func)() = nullptr;
    yetty::PluginPtr plugin;
    std::string path;

    ~PluginHandle() {
        plugin.reset();
        if (handle) {
            dlclose(handle);
        }
    }
};

std::unique_ptr<PluginHandle> loadPlugin(const std::string& pluginDir, const std::string& pluginName) {
    auto handle = std::make_unique<PluginHandle>();

    // Construct plugin path
    std::string pluginPath = pluginDir + "/" + pluginName + ".so";
    if (!fs::exists(pluginPath)) {
        yerror("Plugin not found: {}", pluginPath);
        return nullptr;
    }
    handle->path = pluginPath;

    // Load the shared library
    // Use RTLD_GLOBAL so Python extension modules can find libpython symbols
    handle->handle = dlopen(pluginPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle->handle) {
        yerror("Failed to load plugin: {}", dlerror());
        return nullptr;
    }

    // Get the name function
    handle->name_func = reinterpret_cast<const char*(*)()>(dlsym(handle->handle, "name"));
    if (!handle->name_func) {
        yerror("Plugin missing 'name' function: {}", dlerror());
        return nullptr;
    }

    // Get the create function (new signature: no args)
    handle->create_func = reinterpret_cast<yetty::Result<yetty::PluginPtr>(*)()>(
        dlsym(handle->handle, "create"));
    if (!handle->create_func) {
        yerror("Plugin missing 'create' function: {}", dlerror());
        return nullptr;
    }

    yinfo("Loaded plugin '{}' from {}", handle->name_func(), pluginPath);
    return handle;
}

std::vector<std::string> listPlugins(const std::string& pluginDir) {
    std::vector<std::string> plugins;
    if (!fs::exists(pluginDir)) {
        return plugins;
    }

    for (const auto& entry : fs::directory_iterator(pluginDir)) {
        if (entry.path().extension() == ".so") {
            plugins.push_back(entry.path().stem().string());
        }
    }
    return plugins;
}

//-----------------------------------------------------------------------------
// GLFW callbacks
//-----------------------------------------------------------------------------

static bool g_shouldClose = false;
static int g_width = 1024;
static int g_height = 768;
static bool g_resized = false;

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)window; (void)scancode; (void)mods;
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        g_shouldClose = true;
    }
    if (key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL) && action == GLFW_PRESS) {
        g_shouldClose = true;
    }
}

void resizeCallback(GLFWwindow* window, int width, int height) {
    (void)window;
    g_width = width;
    g_height = height;
    g_resized = true;
}

//-----------------------------------------------------------------------------
// Commands
//-----------------------------------------------------------------------------

int cmdList(const std::string& pluginDir) {
    auto plugins = listPlugins(pluginDir);
    if (plugins.empty()) {
        std::cout << "No plugins found in " << pluginDir << std::endl;
        return 1;
    }

    std::cout << "Available plugins:" << std::endl;
    for (const auto& name : plugins) {
        std::cout << "  - " << name << std::endl;
    }
    return 0;
}

int cmdInfo(const std::string& pluginDir, const std::string& pluginName) {
    auto handle = loadPlugin(pluginDir, pluginName);
    if (!handle) {
        return 1;
    }

    std::cout << "Plugin: " << handle->name_func() << std::endl;
    std::cout << "Path: " << handle->path << std::endl;

    return 0;
}

int cmdRun(const std::string& pluginDir,
           const std::string& pluginName,
           const std::string& payload,
           int x, int y, int width, int height,
           bool headless,
           int durationMs) {

    // Load plugin
    auto handle = loadPlugin(pluginDir, pluginName);
    if (!handle) {
        return 1;
    }

    // Initialize GLFW
    if (!glfwInit()) {
        yerror("Failed to initialize GLFW");
        return 1;
    }

    // Create window
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(width, height,
        ("Plugin Tester - " + pluginName).c_str(), nullptr, nullptr);
    if (!window) {
        yerror("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }

    glfwSetKeyCallback(window, keyCallback);
    glfwSetFramebufferSizeCallback(window, resizeCallback);
    
    // Get actual framebuffer size (may differ from window size on HiDPI displays)
    int fb_width, fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    if (fb_width != width || fb_height != height) {
        yinfo("HiDPI scaling: window {}x{} -> framebuffer {}x{}", 
                     width, height, fb_width, fb_height);
        width = fb_width;
        height = fb_height;
    }
    g_width = width;
    g_height = height;

    // Create WebGPU context
    auto ctxResult = yetty::WebGPUContext::create(window, width, height);
    if (!ctxResult) {
        yerror("Failed to create WebGPU context: {}", ctxResult.error().message());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    auto ctx = *ctxResult;
    
    yinfo("Surface format: {} (BGRA8Unorm=23, BGRA8UnormSrgb=24)", static_cast<int>(ctx->getSurfaceFormat()));

    // Create plugin instance
    auto pluginResult = handle->create_func();
    if (!pluginResult) {
        yerror("Failed to create plugin: {}", pluginResult.error().message());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    handle->plugin = *pluginResult;

    // For Python plugin: set WebGPU handles before creating layer
    // This allows yetty_pygfx to work during payload execution
    if (pluginName == "python") {
        auto set_handles_fn = reinterpret_cast<void(*)(void*, void*, WGPUDevice, WGPUQueue)>(
            dlsym(handle->handle, "yetty_wgpu_set_handles"));
        if (set_handles_fn) {
            set_handles_fn(nullptr, nullptr, ctx->getDevice(), ctx->getQueue());
            yinfo("Set WebGPU handles for Python plugin");
        }
        // NOTE: Don't create render texture - we want direct render pass rendering!
    }

    // Create layer with payload (using new signature with all explicit params)
    auto layerResult = handle->plugin->createWidget(
        "",           // widgetName
        nullptr,      // factory
        nullptr,      // fontManager
        nullptr,      // loop
        x, y,         // position
        static_cast<uint32_t>(width / 10),  // widthCells (approx based on cell size)
        static_cast<uint32_t>(height / 20), // heightCells
        "",           // pluginArgs
        payload       // payload
    );
    if (!layerResult) {
        yerror("Failed to create layer: {}", layerResult.error().message());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    auto layer = *layerResult;
    
    // Debug: print struct sizes to detect ODR violations
    yinfo("Tester: sizeof(RenderContext)={} sizeof(Widget)={}", 
                 sizeof(yetty::RenderContext), sizeof(yetty::Widget));

    // Set pixel size for the layer (fills the whole window)
    yinfo("Tester: layer ptr = {}", (void*)layer.get());
    yinfo("Tester: Setting pixel size to {}x{}", width, height);
    layer->setPixelSize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    yinfo("Tester: After setPixelSize, getPixelWidth={}", layer->getPixelWidth());

    // Set render context
    yetty::RenderContext renderCtx;
    renderCtx.screenWidth = static_cast<uint32_t>(width);
    renderCtx.screenHeight = static_cast<uint32_t>(height);
    renderCtx.cellWidth = 10.0f;  // Default cell size
    renderCtx.cellHeight = 20.0f;
    renderCtx.deltaTime = 0.016; // ~60fps
    renderCtx.targetFormat = ctx->getSurfaceFormat();
    layer->setRenderContext(renderCtx);
    yinfo("Tester: After setRenderContext, screenWidth={}", layer->getRenderContext().screenWidth);

    // Store position for layer (not used in RenderContext but may be needed)
    (void)x; (void)y;

    yinfo("Running plugin '{}' with payload: {}", pluginName,
                 payload.empty() ? "(empty)" : payload.substr(0, 50));
    yinfo("Tester: Before main loop, getPixelWidth={}", layer->getPixelWidth());

    // Main loop
    auto startTime = std::chrono::steady_clock::now();
    auto lastFrameTime = startTime;
    int frameCount = 0;

    while (!glfwWindowShouldClose(window) && !g_shouldClose) {
        if (frameCount == 0) {
            yinfo("Tester: First frame start, getPixelWidth={}", layer->getPixelWidth());
        }
        glfwPollEvents();
        if (frameCount == 0) {
            yinfo("Tester: After glfwPollEvents, getPixelWidth={}", layer->getPixelWidth());
        }

        // Check duration limit
        if (durationMs > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed >= durationMs) {
                yinfo("Duration limit reached ({} ms)", durationMs);
                break;
            }
        }

        // Handle resize
        if (g_resized) {
            yinfo("Tester: RESIZE to {}x{}", g_width, g_height);
            ctx->resize(g_width, g_height);
            layer->setPixelSize(static_cast<uint32_t>(g_width), static_cast<uint32_t>(g_height));
            renderCtx.screenWidth = static_cast<uint32_t>(g_width);
            renderCtx.screenHeight = static_cast<uint32_t>(g_height);
            layer->setRenderContext(renderCtx);
            g_resized = false;
        }

        // Calculate delta time
        auto now = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;
        renderCtx.deltaTime = deltaTime;
        yinfo("Tester: BEFORE setRenderContext1, getPixelWidth={} renderCtx.screenWidth={}", 
                     layer->getPixelWidth(), renderCtx.screenWidth);
        layer->setRenderContext(renderCtx);
        if (frameCount == 0) {
            yinfo("Tester: After setRenderContext1, getPixelWidth={}", layer->getPixelWidth());
        }

        // Get current texture view
        auto viewResult = ctx->getCurrentTextureView();
        if (!viewResult) {
            yerror("Failed to get texture view: {}", viewResult.error().message());
            continue;
        }
        if (frameCount == 0) {
            yinfo("Tester: After getCurrentTextureView, getPixelWidth={}", layer->getPixelWidth());
        }
        renderCtx.targetView = *viewResult;
        layer->setRenderContext(renderCtx);
        if (frameCount == 0) {
            yinfo("Tester: After setRenderContext2, getPixelWidth={}", layer->getPixelWidth());
        }

        // Create command encoder and render pass
        WGPUCommandEncoderDescriptor encoderDesc = {};
        encoderDesc.label = {.data = "tester_encoder", .length = 14};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx->getDevice(), &encoderDesc);

        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = *viewResult;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.1, 0.1, 0.2, 1.0};  // Dark blue background

        WGPURenderPassDescriptor passDesc = {};
        passDesc.label = {.data = "tester_pass", .length = 11};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = nullptr;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        // Render using render
        ydebug("Tester: Before render, getPixelWidth={}", layer->getPixelWidth());
        layer->render(pass, *ctx);

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(ctx->getQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);

        ctx->present();
        frameCount++;

        // Limit frame rate in headless mode
        if (headless) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }

    // Calculate stats
    auto totalTime = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - startTime).count();
    yinfo("Rendered {} frames in {:.2f}s ({:.1f} fps)",
                 frameCount, totalTime, frameCount / totalTime);

    // Cleanup - order matters!
    // First, clear std::expected's references so reset() calls are final drops
    if (layerResult) {
        layerResult->reset();  // Clear the std::expected's internal shared_ptr
    }
    layer.reset();  // Now this actually destroys the layer

    // Also clear pluginResult before plugin.reset()
    if (pluginResult) {
        pluginResult->reset();
    }
    handle->plugin.reset();
    handle.reset();
    ctx.reset();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(int argc, char* argv[]) {

    args::ArgumentParser parser("yetty-plugin-tester - Test yetty plugins");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

    args::Group commands(parser, "commands");
    args::Command listCmd(commands, "list", "List available plugins");
    args::Command infoCmd(commands, "info", "Show plugin information");
    args::Command runCmd(commands, "run", "Run a plugin");

    // Global options
    args::ValueFlag<std::string> pluginDir(parser, "dir", "Plugin directory",
                                           {'d', "dir"}, "./plugins");
    args::Flag verbose(parser, "verbose", "Verbose output", {'v', "verbose"});

    // Run command options
    args::Positional<std::string> pluginName(runCmd, "plugin", "Plugin name to run");
    args::ValueFlag<std::string> payload(runCmd, "payload", "Payload string (file path or inline data)",
                                         {'p', "payload"}, "");
    args::ValueFlag<std::string> fileArg(runCmd, "file", "File to open with plugin",
                                         {'f', "file"}, "");
    args::ValueFlag<std::string> codeArg(runCmd, "code", "Code to execute (python plugin)",
                                         {'c', "code"}, "");
    args::ValueFlag<std::string> rectArg(runCmd, "rect", "Rectangle x,y,w,h",
                                         {'r', "rect"}, "0,0,1024,768");
    args::Flag headless(runCmd, "headless", "Run without visible window", {"headless"});
    args::ValueFlag<int> duration(runCmd, "ms", "Run for specified duration in milliseconds",
                                  {'t', "duration"}, 0);
    args::Flag pygfx(runCmd, "pygfx", "Initialize pygfx for Python plugin", {"pygfx"});

    // Info command options
    args::Positional<std::string> infoPluginName(infoCmd, "plugin", "Plugin name");

    try {
        parser.ParseCLI(argc, argv);
    } catch (const args::Help&) {
        std::cout << parser;
        return 0;
    } catch (const args::ParseError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    } catch (const args::ValidationError& e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    if (verbose) {
    }

    std::string dir = args::get(pluginDir);

    if (listCmd) {
        return cmdList(dir);
    }

    if (infoCmd) {
        if (!infoPluginName) {
            std::cerr << "Plugin name required for info command" << std::endl;
            return 1;
        }
        return cmdInfo(dir, args::get(infoPluginName));
    }

    if (runCmd) {
        if (!pluginName) {
            std::cerr << "Plugin name required for run command" << std::endl;
            return 1;
        }

        // Determine payload
        std::string payloadStr;
        if (fileArg) {
            payloadStr = args::get(fileArg);
        } else if (codeArg) {
            payloadStr = args::get(codeArg);
        } else if (payload) {
            payloadStr = args::get(payload);
        }

        // Parse rectangle
        int x = 0, y = 0, w = 1024, h = 768;
        std::string rectStr = args::get(rectArg);
        if (sscanf(rectStr.c_str(), "%d,%d,%d,%d", &x, &y, &w, &h) != 4) {
            ywarn("Invalid rect format, using defaults");
        }

        // Handle pygfx flag for python plugin
        if (pygfx && args::get(pluginName) == "python") {
            // Prepend sys.path setup and pygfx initialization to payload
            // Use the plugin directory's parent to find the python modules
            std::string pythonPath = fs::path(dir).parent_path().string() + "/python";
            payloadStr = "import sys; sys.path.insert(0, '" + pythonPath + "'); "
                         "import yetty_pygfx; yetty_pygfx.init_pygfx(); " + payloadStr;
        }

        return cmdRun(dir, args::get(pluginName), payloadStr,
                      x, y, w, h, headless, args::get(duration));
    }

    // No command specified
    std::cout << parser;
    return 0;
}
