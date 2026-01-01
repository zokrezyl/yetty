#include "ymery.h"
#include <yetty/webgpu-context.h>

#include <ymery/embedded.hpp>
#include <imgui.h>
#include <imgui_impl_wgpu.h>
#include <implot.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <spdlog/spdlog.h>

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace yetty {

// Get the directory containing the executable
static std::string getExecutableDir() {
#ifdef __linux__
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::filesystem::path(path).parent_path().string();
    }
#endif
    // Fallback to current working directory
    return std::filesystem::current_path().string();
}

// GLFW key to ImGui key translation
static ImGuiKey glfw_key_to_imgui_key(int key) {
    switch (key) {
        case 256: return ImGuiKey_Escape;
        case 257: return ImGuiKey_Enter;
        case 258: return ImGuiKey_Tab;
        case 259: return ImGuiKey_Backspace;
        case 260: return ImGuiKey_Insert;
        case 261: return ImGuiKey_Delete;
        case 262: return ImGuiKey_RightArrow;
        case 263: return ImGuiKey_LeftArrow;
        case 264: return ImGuiKey_DownArrow;
        case 265: return ImGuiKey_UpArrow;
        case 266: return ImGuiKey_PageUp;
        case 267: return ImGuiKey_PageDown;
        case 268: return ImGuiKey_Home;
        case 269: return ImGuiKey_End;
        case 340: return ImGuiKey_LeftShift;
        case 341: return ImGuiKey_LeftCtrl;
        case 342: return ImGuiKey_LeftAlt;
        case 343: return ImGuiKey_LeftSuper;
        case 344: return ImGuiKey_RightShift;
        case 345: return ImGuiKey_RightCtrl;
        case 346: return ImGuiKey_RightAlt;
        case 347: return ImGuiKey_RightSuper;
        case 32: return ImGuiKey_Space;
        case 48: return ImGuiKey_0;
        case 49: return ImGuiKey_1;
        case 50: return ImGuiKey_2;
        case 51: return ImGuiKey_3;
        case 52: return ImGuiKey_4;
        case 53: return ImGuiKey_5;
        case 54: return ImGuiKey_6;
        case 55: return ImGuiKey_7;
        case 56: return ImGuiKey_8;
        case 57: return ImGuiKey_9;
        case 65: return ImGuiKey_A;
        case 66: return ImGuiKey_B;
        case 67: return ImGuiKey_C;
        case 68: return ImGuiKey_D;
        case 69: return ImGuiKey_E;
        case 70: return ImGuiKey_F;
        case 71: return ImGuiKey_G;
        case 72: return ImGuiKey_H;
        case 73: return ImGuiKey_I;
        case 74: return ImGuiKey_J;
        case 75: return ImGuiKey_K;
        case 76: return ImGuiKey_L;
        case 77: return ImGuiKey_M;
        case 78: return ImGuiKey_N;
        case 79: return ImGuiKey_O;
        case 80: return ImGuiKey_P;
        case 81: return ImGuiKey_Q;
        case 82: return ImGuiKey_R;
        case 83: return ImGuiKey_S;
        case 84: return ImGuiKey_T;
        case 85: return ImGuiKey_U;
        case 86: return ImGuiKey_V;
        case 87: return ImGuiKey_W;
        case 88: return ImGuiKey_X;
        case 89: return ImGuiKey_Y;
        case 90: return ImGuiKey_Z;
        default: return ImGuiKey_None;
    }
}

//-----------------------------------------------------------------------------
// YmeryPlugin
//-----------------------------------------------------------------------------

YmeryPlugin::YmeryPlugin() = default;

YmeryPlugin::~YmeryPlugin() {
    (void)dispose();
}

Result<PluginPtr> YmeryPlugin::create() {
    return std::make_shared<YmeryPlugin>();
}

Result<void> YmeryPlugin::init(WebGPUContext* ctx) {
    if (!ctx) {
        return Err<void>("YmeryPlugin::init: WebGPUContext required");
    }

    _device = ctx->getDevice();
    _queue = ctx->getQueue();
    _format = ctx->getSurfaceFormat();

    _initialized = true;
    spdlog::info("YmeryPlugin initialized");
    return Ok();
}

Result<void> YmeryPlugin::dispose() {
    // Order matters: clear layers first, then app, then ImGui
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose YmeryPlugin base", res);
    }

    // FIXME: ymery-cpp has a bug in WidgetFactory destructor
    // Don't reset app - let it leak for now until ymery-cpp is fixed
    // _app.reset();

    // Finally clean up ImGui
    if (_imgui_ctx) {
        ImGui::SetCurrentContext(_imgui_ctx);
        ImGui_ImplWGPU_Shutdown();

        if (_implot_ctx) {
            ImPlot::DestroyContext(_implot_ctx);
            _implot_ctx = nullptr;
        }

        ImGui::DestroyContext(_imgui_ctx);
        _imgui_ctx = nullptr;
    }

    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> YmeryPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<YmeryLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize YmeryLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> YmeryPlugin::initImGui(uint32_t screenWidth, uint32_t screenHeight) {
    if (_imgui_ctx) return Ok();  // Already initialized

    // Create ImGui context
    _imgui_ctx = ImGui::CreateContext();
    if (!_imgui_ctx) {
        return Err<void>("Failed to create ImGui context");
    }
    ImGui::SetCurrentContext(_imgui_ctx);

    // Create ImPlot context
    _implot_ctx = ImPlot::CreateContext();
    if (!_implot_ctx) {
        ImGui::DestroyContext(_imgui_ctx);
        _imgui_ctx = nullptr;
        return Err<void>("Failed to create ImPlot context");
    }
    ImPlot::SetCurrentContext(_implot_ctx);

    // Configure ImGui
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(screenWidth), static_cast<float>(screenHeight));

    ImGui::StyleColorsDark();

    // Initialize WebGPU backend
    ImGui_ImplWGPU_InitInfo wgpu_info = {};
    wgpu_info.Device = _device;
    wgpu_info.NumFramesInFlight = 3;
    wgpu_info.RenderTargetFormat = _format;
    wgpu_info.DepthStencilFormat = WGPUTextureFormat_Undefined;

    if (!ImGui_ImplWGPU_Init(&wgpu_info)) {
        ImPlot::DestroyContext(_implot_ctx);
        ImGui::DestroyContext(_imgui_ctx);
        _implot_ctx = nullptr;
        _imgui_ctx = nullptr;
        return Err<void>("Failed to init ImGui WebGPU backend");
    }

    spdlog::info("YmeryPlugin: ImGui initialized ({}x{})", screenWidth, screenHeight);
    return Ok();
}

Result<void> YmeryPlugin::renderAll(WebGPUContext& ctx,
                                     WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset, uint32_t termRows,
                                     bool isAltScreen) {
    (void)targetFormat;

    if (_layers.empty()) return Ok();

    // Filter layers for current screen
    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    bool hasVisibleLayers = false;
    for (auto& layer : _layers) {
        if (layer->isVisible() && layer->getScreenType() == currentScreen) {
            hasVisibleLayers = true;
            break;
        }
    }
    if (!hasVisibleLayers) return Ok();

    // Initialize ImGui on first render
    if (!_imgui_ctx) {
        if (auto res = initImGui(screenWidth, screenHeight); !res) {
            return Err<void>("Failed to initialize ImGui", res);
        }
    }

    // Lazy create ymery app on first render
    if (!_app) {
        auto firstLayer = std::static_pointer_cast<YmeryLayer>(_layers[0]);

        ymery::EmbeddedConfig config;
        config.layout_paths.push_back(firstLayer->getLayoutPath());

        // Determine plugins directory
        std::string pluginsDir;
        std::string passedPath = firstLayer->getPluginPath();

        if (!passedPath.empty() && std::filesystem::exists(passedPath)) {
            // Use passed path if it exists
            pluginsDir = passedPath;
        } else {
            // Default to plugins directory relative to executable
            pluginsDir = getExecutableDir() + "/plugins";
        }

        spdlog::info("YmeryPlugin: using plugins dir: {}", pluginsDir);
        if (std::filesystem::exists(pluginsDir)) {
            config.plugin_paths.push_back(pluginsDir);
        } else {
            spdlog::warn("YmeryPlugin: plugins dir does not exist: {}", pluginsDir);
        }
        config.main_module = firstLayer->getMainModule();

        auto res = ymery::EmbeddedApp::create(config);
        if (!res) {
            return Err<void>("Failed to create EmbeddedApp: " + ymery::error_msg(res));
        }
        _app = *res;
        spdlog::info("YmeryPlugin: EmbeddedApp created");
    }

    if (!_app) return Err<void>("YmeryPlugin: app not initialized");

    // Store cell dimensions for input coordinate calculation
    _cell_width = cellWidth;
    _cell_height = cellHeight;

    // Set ImGui context
    ImGui::SetCurrentContext(_imgui_ctx);
    ImPlot::SetCurrentContext(_implot_ctx);

    // Calculate delta time
    double currentTime = glfwGetTime();
    float deltaTime = static_cast<float>(currentTime - _last_time);
    if (deltaTime <= 0.0f) deltaTime = 1.0f / 60.0f;
    _last_time = currentTime;

    // Update display size
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(screenWidth), static_cast<float>(screenHeight));
    io.DeltaTime = deltaTime;

    // Begin ImGui frame
    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();

    // Render each layer at its position
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<YmeryLayer>(layerBase);

        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        // Adjust for scroll offset
        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        // Skip if off-screen
        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;
            }
        }

        // Position the ImGui window for this layer
        ImGui::SetNextWindowPos(ImVec2(pixelX, pixelY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(pixelW, pixelH), ImGuiCond_Always);
    }

    // Render ymery widgets
    _app->render_widgets();

    // End ImGui frame
    ImGui::Render();

    // Create render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
#if defined(WGPU_DEPTH_SLICE_UNDEFINED)
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Render ImGui
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

//-----------------------------------------------------------------------------
// YmeryLayer
//-----------------------------------------------------------------------------

YmeryLayer::YmeryLayer() = default;
YmeryLayer::~YmeryLayer() = default;

Result<void> YmeryLayer::parsePayload(const std::string& payload) {
    std::istringstream stream(payload);
    std::string pair;

    while (std::getline(stream, pair, ';')) {
        auto eqPos = pair.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = pair.substr(0, eqPos);
        std::string value = pair.substr(eqPos + 1);

        // Trim
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();

        if (key == "layout_path" || key == "layout") {
            _layout_path = value;
        } else if (key == "plugin_path" || key == "plugins") {
            _plugin_path = value;
        } else if (key == "main" || key == "module") {
            _main_module = value;
        }
    }

    if (_layout_path.empty()) {
        return Err<void>("YmeryLayer: layout_path is required");
    }

    return Ok();
}

Result<void> YmeryLayer::init(const std::string& payload) {
    _payload = payload;
    return parsePayload(payload);
}

Result<void> YmeryLayer::dispose() {
    return Ok();
}

bool YmeryLayer::onMouseMove(float x, float y) {
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        // Add layer offset to get absolute position
        float absX = x + getX() * plugin->_cell_width;
        float absY = y + getY() * plugin->_cell_height;
        io.AddMousePosEvent(absX, absY);
        return true;
    }
    return false;
}

bool YmeryLayer::onMouseButton(int button, bool pressed) {
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        if (button >= 0 && button < ImGuiMouseButton_COUNT) {
            io.AddMouseButtonEvent(button, pressed);
        }
        return true;
    }
    return false;
}

bool YmeryLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)mods;
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseWheelEvent(xoffset, yoffset);
        return true;
    }
    return false;
}

bool YmeryLayer::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();

        ImGuiKey imgui_key = glfw_key_to_imgui_key(key);
        bool pressed = (action != 0);
        io.AddKeyEvent(imgui_key, pressed);

        io.AddKeyEvent(ImGuiMod_Ctrl, (mods & 0x0002) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (mods & 0x0001) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (mods & 0x0004) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (mods & 0x0008) != 0);

        return io.WantCaptureKeyboard;
    }
    return false;
}

bool YmeryLayer::onChar(unsigned int codepoint) {
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(codepoint);
        return io.WantCaptureKeyboard;
    }
    return false;
}

bool YmeryLayer::wantsKeyboard() const {
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        return ImGui::GetIO().WantCaptureKeyboard;
    }
    return false;
}

bool YmeryLayer::wantsMouse() const {
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    return plugin && plugin->imguiContext();
}

void YmeryLayer::setFocus(bool f) {
    PluginLayer::setFocus(f);
    auto plugin = static_cast<YmeryPlugin*>(_parent);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(f);
    }
}

} // namespace yetty

extern "C" {
    const char* name() { return "ymery"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::YmeryPlugin::create(); }
}
