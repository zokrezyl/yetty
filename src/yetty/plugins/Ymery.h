#pragma once

#include "../Plugin.h"
#include <webgpu/webgpu.h>
#include <memory>

// Forward declare ymery types
namespace ymery {
    class EmbeddedApp;
}

namespace yetty {

// Ymery plugin - renders ymery YAML-based ImGui widgets
class Ymery : public Plugin {
public:
    Ymery();
    ~Ymery() override;

    // Factory method for plugin creation
    static Result<PluginPtr> create();

    // Plugin interface
    const char* pluginName() const override { return "ymery"; }

    Result<void> init(const std::string& payload) override;
    void dispose() override;
    void update(double deltaTime) override;
    void render(WebGPUContext& ctx,
               WGPUTextureView targetView, WGPUTextureFormat targetFormat,
               uint32_t screenWidth, uint32_t screenHeight,
               float pixelX, float pixelY, float pixelW, float pixelH) override;
    void onResize(uint32_t newWidth, uint32_t newHeight) override;

    // Input forwarding - returns true if event was consumed
    bool onMouseMove(float x, float y) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;

    // Query if ymery wants input
    bool wantsKeyboard() const override;
    bool wantsMouse() const override;

private:
    Result<void> parsePayload(const std::string& payload);

    std::shared_ptr<ymery::EmbeddedApp> app_;
    double lastTime_ = 0.0;

    // Parsed from payload
    std::string layoutPath_;
    std::string pluginPath_;
    std::string mainModule_ = "app";

    // WebGPU resources for render pass
    bool initialized_ = false;
};

} // namespace yetty

// C exports for dynamic loading (when compiled as separate .so)
extern "C" {
    const char* ymery_plugin_name();
    yetty::Result<yetty::PluginPtr> ymery_plugin_create();
}
