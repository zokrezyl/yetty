#pragma once

#include <yetty/plugin.h>
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
    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

    Result<void> renderAll(WebGPUContext& ctx,
                           WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows,
                           bool isAltScreen = false) override;

#ifdef YETTY_YMERY_ENABLED
    ImGuiContext* imguiContext() const { return _imgui_ctx; }
    std::shared_ptr<ymery::EmbeddedApp> app() const { return _app; }

    // For input coordinate calculation
    float _cell_width = 0;
    float _cell_height = 0;
#endif

private:
#ifdef YETTY_YMERY_ENABLED
    Result<void> initImGui(uint32_t screenWidth, uint32_t screenHeight);

    std::shared_ptr<ymery::EmbeddedApp> _app;
    ImGuiContext* _imgui_ctx = nullptr;
    ImPlotContext* _implot_ctx = nullptr;
    WGPUDevice _device = nullptr;
    WGPUQueue _queue = nullptr;
    WGPUTextureFormat _format = WGPUTextureFormat_Undefined;
#endif
    double _last_time = 0.0;
};

//-----------------------------------------------------------------------------
// YmeryLayer - per-layer position/size, forwards input to shared ImGui context
//-----------------------------------------------------------------------------
class YmeryLayer : public PluginLayer {
public:
    YmeryLayer();
    ~YmeryLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    bool onMouseMove(float x, float y) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;

    bool wantsKeyboard() const override;
    bool wantsMouse() const override;

    void setFocus(bool f) override;

    const std::string& getLayoutPath() const { return _layout_path; }
    const std::string& getPluginPath() const { return _plugin_path; }
    const std::string& getMainModule() const { return _main_module; }

private:
    Result<void> parsePayload(const std::string& payload);

    std::string _layout_path;
    std::string _plugin_path;
    std::string _main_module = "app";
};

using Ymery = YmeryPlugin;

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
