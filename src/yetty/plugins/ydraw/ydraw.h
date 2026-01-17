#pragma once

#include <yetty/plugin.h>
#include <yetty/ydraw.h>
#include <webgpu/webgpu.h>
#include <memory>

namespace yetty {

//-----------------------------------------------------------------------------
// YDrawPlugin - Plugin wrapper for YDrawRenderer
// Demonstrates how to use the core ydraw library in a plugin
//-----------------------------------------------------------------------------

class YDraw;

class YDrawPlugin : public Plugin {
public:
    ~YDrawPlugin() override;

    static Result<PluginPtr> create() noexcept;

    const char* pluginName() const override { return "ydraw"; }

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

private:
    YDrawPlugin() noexcept = default;
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// YDraw - Plugin widget that wraps YDrawRenderer
//-----------------------------------------------------------------------------

class YDraw : public Widget {
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
        (void)pluginArgs;
        auto w = std::shared_ptr<YDraw>(new YDraw(payload));
        w->_x = x;
        w->_y = y;
        w->_widthCells = widthCells;
        w->_heightCells = heightCells;
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init YDraw", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~YDraw() override;

    Result<void> dispose() override;

    void prepareFrame(WebGPUContext& ctx, bool on) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) override;

    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    explicit YDraw(const std::string& payload) {
        _payload = payload;
    }
    Result<void> init() override;

    std::unique_ptr<YDrawRenderer> renderer_;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create();
}
