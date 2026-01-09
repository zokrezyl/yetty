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

class YmeryW;

//-----------------------------------------------------------------------------
// YmeryPlugin - holds shared ImGui context and ymery::EmbeddedApp
//-----------------------------------------------------------------------------
class YmeryPlugin : public Plugin {
public:
    ~YmeryPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "ymery"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(const std::string& payload) override;

    // Shared render for ImGui - called once per frame for all layers
    Result<void> render(WebGPUContext& ctx) override;

#ifdef YETTY_YMERY_ENABLED
    ImGuiContext* imguiContext() const { return _imgui_ctx; }
    std::shared_ptr<ymery::EmbeddedApp> app() const { return _app; }

    // For input coordinate calculation
    float _cell_width = 0;
    float _cell_height = 0;
#endif

private:
    explicit YmeryPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> pluginInit() noexcept;

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
// YmeryW - per-layer position/size, forwards input to shared ImGui context
//-----------------------------------------------------------------------------
class YmeryW : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<YmeryW>(new YmeryW(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init YmeryW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~YmeryW() override;

    Result<void> dispose() override;

    // Render is handled by the plugin's shared ImGui context
    Result<void> render(WebGPUContext& ctx) override { (void)ctx; return Ok(); }
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override {
        (void)pass; (void)ctx; return true;
    }

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
    explicit YmeryW(const std::string& payload) {
        payload_ = payload;
    }

    Result<void> init() override;
    Result<void> parsePayload();

    std::string _layout_path;
    std::string _plugin_path;
    std::string _main_module = "app";
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
