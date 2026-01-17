#include "ymery.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <ytrace/ytrace.hpp>

#include <ymery/embedded.hpp>
#include <imgui.h>
#include <imgui_impl_wgpu.h>
#include <implot.h>

#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <ytrace/ytrace.hpp>

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

YmeryPlugin::~YmeryPlugin() {
    (void)dispose();
}

Result<PluginPtr> YmeryPlugin::create() noexcept {
    auto p = PluginPtr(new YmeryPlugin());
    if (auto res = static_cast<YmeryPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init YmeryPlugin", res);
    }
    return Ok(p);
}

Result<void> YmeryPlugin::pluginInit() noexcept {
    // Note: Will be initialized properly in init() with WebGPUContext
    _initialized = false;
    return Ok();
}

Result<void> YmeryPlugin::init(WebGPUContext* ctx) {
    auto res = Plugin::init(ctx);
    if (!res) return res;

    if (!_ctx) {
        return Err<void>("YmeryPlugin: WebGPUContext required");
    }

    _device = _ctx->getDevice();
    _queue = _ctx->getQueue();
    _format = _ctx->getSurfaceFormat();

    _initialized = true;
    yinfo("YmeryPlugin initialized");
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

Result<WidgetPtr> YmeryPlugin::createWidget(
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
    yinfo("payload={} x={} y={} w={} h={}", payload, x, y, widthCells, heightCells);
    return Ymery::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload);
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

    yinfo("YmeryPlugin: ImGui initialized ({}x{})", screenWidth, screenHeight);
    return Ok();
}

Result<void> YmeryPlugin::ensureInitialized(const RenderContext& rc, const std::string& layoutPath,
                                            const std::string& pluginPath, const std::string& mainModule) {
    // Initialize ImGui on first call
    if (!_imgui_ctx) {
        if (auto res = initImGui(rc.screenWidth, rc.screenHeight); !res) {
            return Err<void>("Failed to initialize ImGui", res);
        }
    }

    // Create ymery app on first call
    if (!_app) {
        ymery::EmbeddedConfig config;
        config.layout_paths.push_back(layoutPath);

        // Determine plugins directory
        std::string pluginsDir;
        if (!pluginPath.empty() && std::filesystem::exists(pluginPath)) {
            pluginsDir = pluginPath;
        } else {
            pluginsDir = getExecutableDir() + "/plugins";
        }

        yinfo("YmeryPlugin: using plugins dir: {}", pluginsDir);
        if (std::filesystem::exists(pluginsDir)) {
            config.plugin_paths.push_back(pluginsDir);
        } else {
            ywarn("YmeryPlugin: plugins dir does not exist: {}", pluginsDir);
        }
        config.main_module = mainModule;

        auto res = ymery::EmbeddedApp::create(config);
        if (!res) {
            return Err<void>("Failed to create EmbeddedApp: " + ymery::error_msg(res));
        }
        _app = *res;
        yinfo("YmeryPlugin: EmbeddedApp created");
    }

    // Store cell dimensions for input coordinate calculation
    _cell_width = rc.cellWidth;
    _cell_height = rc.cellHeight;

    return Ok();
}

//-----------------------------------------------------------------------------
// Ymery - self-contained widget
//-----------------------------------------------------------------------------

Ymery::~Ymery() {
    (void)dispose();
}

Result<void> Ymery::parseArgs() {
    // Parse command-line style args: -p <layout_path> -m <main_module>
    std::istringstream stream(_args_str);
    std::string token;
    std::vector<std::string> tokens;

    // Tokenize by whitespace
    while (stream >> token) {
        tokens.push_back(token);
    }

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "-p" && i + 1 < tokens.size()) {
            _layout_paths.push_back(tokens[++i]);
        } else if (tokens[i] == "-m" && i + 1 < tokens.size()) {
            _main_module = tokens[++i];
        }
    }

    if (_main_module.empty()) {
        return Err<void>("Ymery: -m <main_module> is required");
    }

    return Ok();
}

Result<void> Ymery::init() {
    return parseArgs();
}

Result<void> Ymery::dispose() {
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
    _app.reset();
    return Ok();
}

Result<void> Ymery::ensureImGuiInitialized(WebGPUContext& ctx) {
    if (_imgui_ctx) return Ok();

    const auto& rc = _renderCtx;

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
    io.DisplaySize = ImVec2(static_cast<float>(rc.screenWidth), static_cast<float>(rc.screenHeight));

    ImGui::StyleColorsDark();

    // Initialize WebGPU backend
    ImGui_ImplWGPU_InitInfo wgpu_info = {};
    wgpu_info.Device = ctx.getDevice();
    wgpu_info.NumFramesInFlight = 3;
    wgpu_info.RenderTargetFormat = ctx.getSurfaceFormat();
    wgpu_info.DepthStencilFormat = WGPUTextureFormat_Undefined;

    if (!ImGui_ImplWGPU_Init(&wgpu_info)) {
        ImPlot::DestroyContext(_implot_ctx);
        ImGui::DestroyContext(_imgui_ctx);
        _implot_ctx = nullptr;
        _imgui_ctx = nullptr;
        return Err<void>("Failed to init ImGui WebGPU backend");
    }

    // Create ymery app
    ymery::EmbeddedConfig config;

    // Add layout paths from -p arguments
    for (const auto& path : _layout_paths) {
        if (std::filesystem::exists(path)) {
            config.layout_paths.push_back(path);
        }
    }

    // If -m is a file path, add its directory and parent to layout paths
    std::filesystem::path mainPath(_main_module);
    if (mainPath.has_parent_path()) {
        auto mainDir = mainPath.parent_path();
        if (std::filesystem::exists(mainDir)) {
            config.layout_paths.push_back(mainDir.string());
        }
    }

    // Set main module (convert path to module name if needed)
    // e.g., "demo/layouts/open-all/app.yaml" -> "app"
    if (mainPath.has_extension()) {
        config.main_module = mainPath.stem().string();
    } else {
        config.main_module = _main_module;
    }

    // Plugin paths
    std::string pluginsDir = getExecutableDir() + "/plugins";
    std::string ymeryPluginsDir = pluginsDir + "/ymery";
    if (std::filesystem::exists(ymeryPluginsDir)) {
        config.plugin_paths.push_back(ymeryPluginsDir);
    }
    if (std::filesystem::exists(pluginsDir)) {
        config.plugin_paths.push_back(pluginsDir);
    }

    yinfo("Ymery: layout_paths={}, main_module={}, plugin_paths={}",
          config.layout_paths.size(), config.main_module, config.plugin_paths.size());

    auto res = ymery::EmbeddedApp::create(config);
    if (!res) {
        ImGui_ImplWGPU_Shutdown();
        ImPlot::DestroyContext(_implot_ctx);
        ImGui::DestroyContext(_imgui_ctx);
        _implot_ctx = nullptr;
        _imgui_ctx = nullptr;
        return Err<void>("Failed to create EmbeddedApp: " + ymery::error_msg(res));
    }
    _app = *res;

    yinfo("Ymery: initialized ImGui and app");
    return Ok();
}

Result<void> Ymery::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    if (!on || !_visible) return Ok();

    // Lazy initialize
    if (auto res = ensureImGuiInitialized(ctx); !res) {
        return res;
    }

    const auto& rc = _renderCtx;

    // Calculate pixel position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    // Adjust for scroll offset
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();
        }
    }

    // Set contexts
    ImGui::SetCurrentContext(_imgui_ctx);
    ImPlot::SetCurrentContext(_implot_ctx);

    // Build ImGui frame
    float deltaTime = static_cast<float>(rc.deltaTime);
    if (deltaTime <= 0.0f) deltaTime = 1.0f / 60.0f;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(rc.screenWidth), static_cast<float>(rc.screenHeight));
    io.DeltaTime = deltaTime;

    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();

    // Position widget's ImGui window
    ImGui::SetNextWindowPos(ImVec2(pixelX, pixelY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(pixelW, pixelH), ImGuiCond_Always);

    // Render ymery widgets
    _app->render_widgets();

    // End frame and render to provided pass
    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), pass);

    // Reset scissor rect to full screen - ImGui leaves it set to its last clip rect
    wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, rc.screenWidth, rc.screenHeight);

    return Ok();
}

bool Ymery::onMouseMove(float x, float y) {
    if (!_imgui_ctx) return false;
    ImGui::SetCurrentContext(_imgui_ctx);
    ImGuiIO& io = ImGui::GetIO();
    const auto& rc = _renderCtx;
    float absX = x + _x * rc.cellWidth;
    float absY = y + _y * rc.cellHeight;
    // Adjust for scroll offset (same as in render)
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        absY += rc.scrollOffset * rc.cellHeight;
    }
    io.AddMousePosEvent(absX, absY);
    return true;
}

bool Ymery::onMouseButton(int button, bool pressed) {
    if (!_imgui_ctx) return false;
    ImGui::SetCurrentContext(_imgui_ctx);
    ImGuiIO& io = ImGui::GetIO();
    if (button >= 0 && button < ImGuiMouseButton_COUNT) {
        io.AddMouseButtonEvent(button, pressed);
    }
    return true;
}

bool Ymery::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)mods;
    if (!_imgui_ctx) return false;
    ImGui::SetCurrentContext(_imgui_ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(xoffset, yoffset);
    return true;
}

bool Ymery::onKey(int key, int scancode, int action, int mods) {
    (void)scancode;
    if (!_imgui_ctx) return false;
    ImGui::SetCurrentContext(_imgui_ctx);
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

bool Ymery::onChar(unsigned int codepoint) {
    if (!_imgui_ctx) return false;
    ImGui::SetCurrentContext(_imgui_ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharacter(codepoint);
    return io.WantCaptureKeyboard;
}

bool Ymery::wantsKeyboard() const {
    if (!_imgui_ctx) return false;
    ImGui::SetCurrentContext(_imgui_ctx);
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool Ymery::wantsMouse() const {
    return _imgui_ctx != nullptr;
}

void Ymery::setFocus(bool f) {
    Widget::setFocus(f);
    if (_imgui_ctx) {
        ImGui::SetCurrentContext(_imgui_ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(f);
    }
}

} // namespace yetty

extern "C" {
    const char* name() { return "ymery"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::YmeryPlugin::create(); }
}
