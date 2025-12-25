#include "Ymery.h"
#include "../renderer/WebGPUContext.h"

#ifdef YETTY_YMERY_ENABLED
#include <ymery/embedded.hpp>
#endif

#include <GLFW/glfw3.h>
#include <algorithm>
#include <iostream>
#include <sstream>


namespace yetty {

Ymery::Ymery() = default;

Ymery::~Ymery() {
    dispose();
}

Result<PluginPtr> Ymery::create() {
    return std::make_shared<Ymery>();
}

Result<void> Ymery::parsePayload(const std::string& payload) {
    // Parse payload format: "layout_path=/path;plugin_path=/path;main=module"
    // Simple key=value;key=value parser

    std::istringstream stream(payload);
    std::string pair;

    while (std::getline(stream, pair, ';')) {
        auto eqPos = pair.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = pair.substr(0, eqPos);
        std::string value = pair.substr(eqPos + 1);

        // Trim whitespace
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
        return Err<void>("Ymery: layout_path is required in payload");
    }

    return Ok();
}

Result<void> Ymery::init(const std::string& payload) {
#ifndef YETTY_YMERY_ENABLED
    return Err<void>("Ymery plugin not enabled (YETTY_YMERY_ENABLED not defined)");
#else
    payload_ = payload;

    // Parse the payload
    if (auto res = parsePayload(payload); !res) {
        return res;
    }

    // Note: We defer actual ymery initialization until first render
    // because we need the WebGPU context which isn't available in init()

    std::cout << "Ymery plugin initialized (layout: " << layoutPath_
              << ", main: " << mainModule_ << ")" << std::endl;

    return Ok();
#endif
}

void Ymery::dispose() {
#ifdef YETTY_YMERY_ENABLED
    if (app_) {
        app_->dispose();
        app_.reset();
    }
#endif
    initialized_ = false;
}

void Ymery::update(double deltaTime) {
    (void)deltaTime;
    // Nothing to do here - rendering happens in render()
}

void Ymery::render(WebGPUContext& ctx,
                   WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                   uint32_t screenWidth, uint32_t screenHeight,
                   float pixelX, float pixelY, float pixelW, float pixelH) {
#ifndef YETTY_YMERY_ENABLED
    (void)ctx; (void)targetView; (void)targetFormat;
    (void)screenWidth; (void)screenHeight;
    (void)pixelX; (void)pixelY; (void)pixelW; (void)pixelH;
    return;
#else
    // Lazy initialization - create ymery app on first render
    if (!initialized_ && !app_) {
        ymery::EmbeddedConfig config;
        config.device = ctx.getDevice();
        config.queue = ctx.getQueue();
        config.format = static_cast<int>(targetFormat);
        config.layout_paths.push_back(layoutPath_);
        if (!pluginPath_.empty()) {
            config.plugin_paths.push_back(pluginPath_);
        }
        config.main_module = mainModule_;
        config.width = static_cast<uint32_t>(pixelW);
        config.height = static_cast<uint32_t>(pixelH);

        auto res = ymery::EmbeddedApp::create(config);
        if (!res) {
            std::cerr << "Ymery: failed to create EmbeddedApp: "
                      << ymery::error_msg(res) << std::endl;
            initialized_ = true;  // Don't retry
            return;
        }
        app_ = *res;
        initialized_ = true;
        std::cout << "Ymery: EmbeddedApp created successfully" << std::endl;
    }

    if (!app_) return;

    // Calculate delta time
    double currentTime = glfwGetTime();
    float deltaTime = static_cast<float>(currentTime - lastTime_);
    if (deltaTime <= 0.0f) deltaTime = 1.0f / 60.0f;
    lastTime_ = currentTime;

    // Update size if changed
    uint32_t w = static_cast<uint32_t>(pixelW);
    uint32_t h = static_cast<uint32_t>(pixelH);
    app_->resize(w, h);

    // Begin ImGui frame
    app_->begin_frame(deltaTime);

    // Render widgets
    app_->render_widgets();

    // Create render pass for this plugin's area
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;  // Preserve terminal content
    colorAttachment.storeOp = WGPUStoreOp_Store;
// depthSlice is only in newer wgpu-native versions
#if defined(WGPU_DEPTH_SLICE_UNDEFINED)
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Clamp viewport to screen boundaries
    float clampedX = std::max(0.0f, pixelX);
    float clampedY = std::max(0.0f, pixelY);
    float clampedW = std::min(pixelW, static_cast<float>(screenWidth) - clampedX);
    float clampedH = std::min(pixelH, static_cast<float>(screenHeight) - clampedY);

    // Skip if completely off-screen
    if (clampedW <= 0 || clampedH <= 0) {
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        return;
    }

    // Set viewport and scissor to clamped plugin area
    wgpuRenderPassEncoderSetViewport(pass, clampedX, clampedY, clampedW, clampedH, 0.0f, 1.0f);
    wgpuRenderPassEncoderSetScissorRect(pass,
        static_cast<uint32_t>(clampedX), static_cast<uint32_t>(clampedY),
        static_cast<uint32_t>(clampedW), static_cast<uint32_t>(clampedH));

    // Set display position for correct screen placement
    app_->set_display_pos(pixelX, pixelY);

    // Render ImGui to the pass
    app_->end_frame(pass);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
#endif
}

void Ymery::onResize(uint32_t newWidth, uint32_t newHeight) {
    Plugin::onResize(newWidth, newHeight);
#ifdef YETTY_YMERY_ENABLED
    if (app_) {
        app_->resize(newWidth, newHeight);
    }
#endif
}

bool Ymery::onMouseMove(float x, float y) {
#ifdef YETTY_YMERY_ENABLED
    if (!app_) return false;
    app_->on_mouse_pos(x, y);
    return true;  // Always consume mouse events - ymery is ImGui-based
#else
    (void)x; (void)y;
    return false;
#endif
}

bool Ymery::onMouseButton(int button, bool pressed) {
#ifdef YETTY_YMERY_ENABLED
    if (!app_) return false;
    app_->on_mouse_button(button, pressed);
    return true;  // Always consume mouse events - ymery is ImGui-based
#else
    (void)button; (void)pressed;
    return false;
#endif
}

bool Ymery::onMouseScroll(float xoffset, float yoffset, int mods) {
#ifdef YETTY_YMERY_ENABLED
    if (!app_) return false;
    (void)mods;  // ymery doesn't use mods yet
    app_->on_mouse_scroll(xoffset, yoffset);
    return true;  // Always consume scroll events - ymery is ImGui-based
#else
    (void)xoffset; (void)yoffset; (void)mods;
    return false;
#endif
}

bool Ymery::onKey(int key, int scancode, int action, int mods) {
#ifdef YETTY_YMERY_ENABLED
    if (!app_) return false;
    app_->on_key(key, scancode, action, mods);
    return app_->wants_keyboard();  // Only consume if ImGui wants keyboard
#else
    (void)key; (void)scancode; (void)action; (void)mods;
    return false;
#endif
}

bool Ymery::onChar(unsigned int codepoint) {
#ifdef YETTY_YMERY_ENABLED
    if (!app_) return false;
    app_->on_char(codepoint);
    return app_->wants_keyboard();
#else
    (void)codepoint;
    return false;
#endif
}

bool Ymery::wantsKeyboard() const {
#ifdef YETTY_YMERY_ENABLED
    if (!app_) return false;
    return app_->wants_keyboard();
#else
    return false;
#endif
}

bool Ymery::wantsMouse() const {
#ifdef YETTY_YMERY_ENABLED
    // Always want mouse - ymery is ImGui-based and needs mouse input
    return app_ != nullptr;
#else
    return false;
#endif
}

void Ymery::setFocus(bool f) {
    Plugin::setFocus(f);
#ifdef YETTY_YMERY_ENABLED
    if (app_) {
        app_->on_focus(f);
    }
#endif
}

} // namespace yetty

// C exports for dynamic loading
extern "C" {
    const char* ymery_plugin_name() {
        return "ymery";
    }

    yetty::Result<yetty::PluginPtr> ymery_plugin_create() {
        return yetty::Ymery::create();
    }
}
