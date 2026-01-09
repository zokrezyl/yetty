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

class YDrawW;

class YDrawPlugin : public Plugin {
public:
    ~YDrawPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "ydraw"; }

    Result<void> dispose() override;

    Result<WidgetPtr> createWidget(const std::string& payload) override;

private:
    explicit YDrawPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> pluginInit() noexcept;
};

//-----------------------------------------------------------------------------
// YDrawW - Plugin widget that wraps YDrawRenderer
//-----------------------------------------------------------------------------

class YDrawW : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<YDrawW>(new YDrawW(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init YDrawW", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~YDrawW() override;

    Result<void> dispose() override;

    // Renderable interface
    Result<void> render(WebGPUContext& ctx) override;
    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    explicit YDrawW(const std::string& payload) {
        payload_ = payload;
    }
    Result<void> init() override;

    std::unique_ptr<YDrawRenderer> renderer_;
    bool failed_ = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
