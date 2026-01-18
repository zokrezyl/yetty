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

class Ymery;

//-----------------------------------------------------------------------------
// YmeryPlugin - holds shared ImGui context and ymery::EmbeddedApp
//-----------------------------------------------------------------------------
class YmeryPlugin : public Plugin {
public:
    ~YmeryPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "ymery"; }

    Result<void> init(WebGPUContext* ctx) override;
    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(
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
    ) override;

    // Initialize ImGui and ymery app (called lazily from widget render)
    Result<void> ensureInitialized(WebGPUContext& ctx, const std::string& layoutPath,
                                    const std::string& pluginPath, const std::string& mainModule);

#ifdef YETTY_YMERY_ENABLED
    ImGuiContext* imguiContext() const { return _imgui_ctx; }
    ImPlotContext* implotContext() const { return _implot_ctx; }
    std::shared_ptr<ymery::EmbeddedApp> app() const { return _app; }
#endif

private:
    YmeryPlugin() noexcept = default;
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
    std::vector<WidgetPtr> _widgets;  // Track created widgets for this plugin
};

//-----------------------------------------------------------------------------
// Ymery - self-contained widget with own ImGui context and drawing queue
//-----------------------------------------------------------------------------
class Ymery : public Widget {
public:
    static Result<WidgetPtr> create(
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
        (void)factory;
        (void)fontManager;
        (void)loop;
        (void)payload;
        auto w = std::shared_ptr<Ymery>(new Ymery(pluginArgs));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init Ymery", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~Ymery() override;

    Result<void> dispose() override;

    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) override;

    bool onMouseMove(float x, float y) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onMouseScroll(float xoffset, float yoffset, int mods) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;

    bool wantsKeyboard() const override;
    bool wantsMouse() const override;

    void setFocus(bool f) override;

private:
    explicit Ymery(const std::string& args) : _args_str(args) {}

    Result<void> init() override;
    Result<void> parseArgs();
    Result<void> ensureImGuiInitialized(WebGPUContext& ctx);

    std::string _args_str;
    std::vector<std::string> _layout_paths;  // -p arguments
    std::string _main_module;                 // -m argument

#ifdef YETTY_YMERY_ENABLED
    ImGuiContext* _imgui_ctx = nullptr;
    ImPlotContext* _implot_ctx = nullptr;
    std::shared_ptr<ymery::EmbeddedApp> _app;
#endif
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
