#pragma once

#include "../plugin.h"
#include <webgpu/webgpu.h>
#include <memory>

#ifdef YETTY_YMERY_ENABLED
struct ImGuiContext;
struct ImPlotContext;
namespace ymery {
    class EmbeddedApp;
}
#endif

namespace yetty {

class YmeryLayer;

//-----------------------------------------------------------------------------
// YmeryPlugin - holds shared ImGui context and ymery::EmbeddedApp
//-----------------------------------------------------------------------------
class YmeryPlugin : public Plugin {
public:
    YmeryPlugin();
    ~YmeryPlugin() override;

    static Result<PluginPtr> create();

    const char* pluginName() const override { return "ymery"; }

    Result<void> init(WebGPUContext* ctx) override;
    void dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    void renderAll(WebGPUContext& ctx,
                   WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                   uint32_t screenWidth, uint32_t screenHeight,
                   float cellWidth, float cellHeight,
                   int scrollOffset, uint32_t termRows) override;

#ifdef YETTY_YMERY_ENABLED
    ImGuiContext* imguiContext() const { return imguiCtx_; }
    std::shared_ptr<ymery::EmbeddedApp> app() const { return app_; }

    // For input coordinate calculation
    float cellWidth_ = 0;
    float cellHeight_ = 0;
#endif

private:
#ifdef YETTY_YMERY_ENABLED
    Result<void> initImGui(uint32_t screenWidth, uint32_t screenHeight);

    std::shared_ptr<ymery::EmbeddedApp> app_;
    ImGuiContext* imguiCtx_ = nullptr;
    ImPlotContext* implotCtx_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTextureFormat format_ = WGPUTextureFormat_Undefined;
#endif
    double lastTime_ = 0.0;
};

//-----------------------------------------------------------------------------
// YmeryLayer - per-layer position/size, forwards input to shared ImGui context
//-----------------------------------------------------------------------------
class YmeryLayer : public PluginLayer {
public:
    YmeryLayer();
    ~YmeryLayer() override;

    Result<void> init(const std::string& payload) override;
    void dispose() override;

    bool onMouseMove(float x, float y) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;

    bool wantsKeyboard() const override;
    bool wantsMouse() const override;

    void setFocus(bool f) override;

    const std::string& getLayoutPath() const { return layoutPath_; }
    const std::string& getPluginPath() const { return pluginPath_; }
    const std::string& getMainModule() const { return mainModule_; }

private:
    Result<void> parsePayload(const std::string& payload);

    std::string layoutPath_;
    std::string pluginPath_;
    std::string mainModule_ = "app";
};

using Ymery = YmeryPlugin;

} // namespace yetty

extern "C" {
    const char* ymery_plugin_name();
    yetty::Result<yetty::PluginPtr> ymery_plugin_create();
}
