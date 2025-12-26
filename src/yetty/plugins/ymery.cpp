#include "ymery.h"
#include "../renderer/webgpu-context.h"

#ifdef YETTY_YMERY_ENABLED
#include <ymery/embedded.hpp>
#include <imgui.h>
#include <imgui_impl_wgpu.h>
#include <implot.h>
#endif

#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <sstream>

namespace yetty {

#ifdef YETTY_YMERY_ENABLED
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
#endif

//-----------------------------------------------------------------------------
// YmeryPlugin
//-----------------------------------------------------------------------------

YmeryPlugin::YmeryPlugin() = default;

YmeryPlugin::~YmeryPlugin() {
    dispose();
}

Result<PluginPtr> YmeryPlugin::create() {
    return std::make_shared<YmeryPlugin>();
}

Result<void> YmeryPlugin::init(WebGPUContext* ctx) {
#ifndef YETTY_YMERY_ENABLED
    (void)ctx;
    return Err<void>("Ymery plugin not enabled (YETTY_YMERY_ENABLED not defined)");
#else
    if (!ctx) {
        return Err<void>("YmeryPlugin::init: WebGPUContext required");
    }

    device_ = ctx->getDevice();
    queue_ = ctx->getQueue();
    format_ = ctx->getSurfaceFormat();

    initialized_ = true;
    std::cout << "YmeryPlugin initialized" << std::endl;
    return Ok();
#endif
}

void YmeryPlugin::dispose() {
#ifdef YETTY_YMERY_ENABLED
    if (app_) {
        app_->dispose();
        app_.reset();
    }

    if (imguiCtx_) {
        ImGui::SetCurrentContext(imguiCtx_);
        ImGui_ImplWGPU_Shutdown();

        if (implotCtx_) {
            ImPlot::DestroyContext(implotCtx_);
            implotCtx_ = nullptr;
        }

        ImGui::DestroyContext(imguiCtx_);
        imguiCtx_ = nullptr;
    }
#endif
    Plugin::dispose();
    initialized_ = false;
}

Result<PluginLayerPtr> YmeryPlugin::createLayer(const std::string& payload) {
#ifndef YETTY_YMERY_ENABLED
    (void)payload;
    return Err<PluginLayerPtr>("Ymery plugin not enabled");
#else
    auto layer = std::make_shared<YmeryLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize YmeryLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
#endif
}

#ifdef YETTY_YMERY_ENABLED
Result<void> YmeryPlugin::initImGui(uint32_t screenWidth, uint32_t screenHeight) {
    if (imguiCtx_) return Ok();  // Already initialized

    // Create ImGui context
    imguiCtx_ = ImGui::CreateContext();
    if (!imguiCtx_) {
        return Err<void>("Failed to create ImGui context");
    }
    ImGui::SetCurrentContext(imguiCtx_);

    // Create ImPlot context
    implotCtx_ = ImPlot::CreateContext();
    if (!implotCtx_) {
        ImGui::DestroyContext(imguiCtx_);
        imguiCtx_ = nullptr;
        return Err<void>("Failed to create ImPlot context");
    }
    ImPlot::SetCurrentContext(implotCtx_);

    // Configure ImGui
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(static_cast<float>(screenWidth), static_cast<float>(screenHeight));

    ImGui::StyleColorsDark();

    // Initialize WebGPU backend
    ImGui_ImplWGPU_InitInfo wgpu_info = {};
    wgpu_info.Device = device_;
    wgpu_info.NumFramesInFlight = 3;
    wgpu_info.RenderTargetFormat = format_;
    wgpu_info.DepthStencilFormat = WGPUTextureFormat_Undefined;

    if (!ImGui_ImplWGPU_Init(&wgpu_info)) {
        ImPlot::DestroyContext(implotCtx_);
        ImGui::DestroyContext(imguiCtx_);
        implotCtx_ = nullptr;
        imguiCtx_ = nullptr;
        return Err<void>("Failed to init ImGui WebGPU backend");
    }

    std::cout << "YmeryPlugin: ImGui initialized (" << screenWidth << "x" << screenHeight << ")" << std::endl;
    return Ok();
}
#endif

void YmeryPlugin::renderAll(WebGPUContext& ctx,
                             WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                             uint32_t screenWidth, uint32_t screenHeight,
                             float cellWidth, float cellHeight,
                             int scrollOffset, uint32_t termRows) {
#ifndef YETTY_YMERY_ENABLED
    (void)ctx; (void)targetView; (void)targetFormat;
    (void)screenWidth; (void)screenHeight;
    (void)cellWidth; (void)cellHeight;
    (void)scrollOffset; (void)termRows;
    return;
#else
    if (layers_.empty()) return;

    // Initialize ImGui on first render
    if (!imguiCtx_) {
        if (auto res = initImGui(screenWidth, screenHeight); !res) {
            std::cerr << "YmeryPlugin: " << error_msg(res) << std::endl;
            return;
        }
    }

    // Lazy create ymery app on first render
    if (!app_) {
        auto firstLayer = std::static_pointer_cast<YmeryLayer>(layers_[0]);

        ymery::EmbeddedConfig config;
        config.layout_paths.push_back(firstLayer->getLayoutPath());
        if (!firstLayer->getPluginPath().empty()) {
            config.plugin_paths.push_back(firstLayer->getPluginPath());
        }
        config.main_module = firstLayer->getMainModule();

        auto res = ymery::EmbeddedApp::create(config);
        if (!res) {
            std::cerr << "YmeryPlugin: failed to create EmbeddedApp: "
                      << ymery::error_msg(res) << std::endl;
            return;
        }
        app_ = *res;
        std::cout << "YmeryPlugin: EmbeddedApp created" << std::endl;
    }

    if (!app_) return;

    // Store cell dimensions for input coordinate calculation
    cellWidth_ = cellWidth;
    cellHeight_ = cellHeight;

    // Set ImGui context
    ImGui::SetCurrentContext(imguiCtx_);
    ImPlot::SetCurrentContext(implotCtx_);

    // Calculate delta time
    double currentTime = glfwGetTime();
    float deltaTime = static_cast<float>(currentTime - lastTime_);
    if (deltaTime <= 0.0f) deltaTime = 1.0f / 60.0f;
    lastTime_ = currentTime;

    // Update display size
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(screenWidth), static_cast<float>(screenHeight));
    io.DeltaTime = deltaTime;

    // Begin ImGui frame
    ImGui_ImplWGPU_NewFrame();
    ImGui::NewFrame();

    // Render each layer at its position
    for (auto& layerBase : layers_) {
        if (!layerBase->isVisible()) continue;

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
    app_->render_widgets();

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
#endif
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
            layoutPath_ = value;
        } else if (key == "plugin_path" || key == "plugins") {
            pluginPath_ = value;
        } else if (key == "main" || key == "module") {
            mainModule_ = value;
        }
    }

    if (layoutPath_.empty()) {
        return Err<void>("YmeryLayer: layout_path is required");
    }

    return Ok();
}

Result<void> YmeryLayer::init(const std::string& payload) {
    payload_ = payload;
    return parsePayload(payload);
}

void YmeryLayer::dispose() {
}

bool YmeryLayer::onMouseMove(float x, float y) {
#ifdef YETTY_YMERY_ENABLED
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        // Add layer offset to get absolute position
        float absX = x + getX() * plugin->cellWidth_;
        float absY = y + getY() * plugin->cellHeight_;
        io.AddMousePosEvent(absX, absY);
        return true;
    }
#else
    (void)x; (void)y;
#endif
    return false;
}

bool YmeryLayer::onMouseButton(int button, bool pressed) {
#ifdef YETTY_YMERY_ENABLED
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        if (button >= 0 && button < ImGuiMouseButton_COUNT) {
            io.AddMouseButtonEvent(button, pressed);
        }
        return true;
    }
#else
    (void)button; (void)pressed;
#endif
    return false;
}

bool YmeryLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
#ifdef YETTY_YMERY_ENABLED
    (void)mods;
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseWheelEvent(xoffset, yoffset);
        return true;
    }
#else
    (void)xoffset; (void)yoffset; (void)mods;
#endif
    return false;
}

bool YmeryLayer::onKey(int key, int scancode, int action, int mods) {
#ifdef YETTY_YMERY_ENABLED
    (void)scancode;
    auto plugin = static_cast<YmeryPlugin*>(parent_);
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
#else
    (void)key; (void)scancode; (void)action; (void)mods;
#endif
    return false;
}

bool YmeryLayer::onChar(unsigned int codepoint) {
#ifdef YETTY_YMERY_ENABLED
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharacter(codepoint);
        return io.WantCaptureKeyboard;
    }
#else
    (void)codepoint;
#endif
    return false;
}

bool YmeryLayer::wantsKeyboard() const {
#ifdef YETTY_YMERY_ENABLED
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        return ImGui::GetIO().WantCaptureKeyboard;
    }
#endif
    return false;
}

bool YmeryLayer::wantsMouse() const {
#ifdef YETTY_YMERY_ENABLED
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    return plugin && plugin->imguiContext();
#else
    return false;
#endif
}

void YmeryLayer::setFocus(bool f) {
    PluginLayer::setFocus(f);
#ifdef YETTY_YMERY_ENABLED
    auto plugin = static_cast<YmeryPlugin*>(parent_);
    if (plugin && plugin->imguiContext()) {
        ImGui::SetCurrentContext(plugin->imguiContext());
        ImGuiIO& io = ImGui::GetIO();
        io.AddFocusEvent(f);
    }
#endif
}

} // namespace yetty

extern "C" {
    const char* ymery_plugin_name() { return "ymery"; }
    yetty::Result<yetty::PluginPtr> ymery_plugin_create() { return yetty::YmeryPlugin::create(); }
}
