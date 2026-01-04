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

class YDrawLayer;

class YDrawPlugin : public Plugin {
public:
    ~YDrawPlugin() override;

    static Result<PluginPtr> create(YettyPtr engine) noexcept;

    const char* pluginName() const override { return "ydraw"; }

    Result<void> dispose() override;

    Result<PluginLayerPtr> createLayer(const std::string& payload) override;

private:
    explicit YDrawPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override;
};

//-----------------------------------------------------------------------------
// YDrawLayer - Plugin layer that wraps YDrawRenderer
//-----------------------------------------------------------------------------

class YDrawLayer : public PluginLayer {
public:
    YDrawLayer();
    ~YDrawLayer() override;

    Result<void> init(const std::string& payload) override;
    Result<void> dispose() override;

    // Renderable interface
    Result<void> render(WebGPUContext& ctx) override;
    bool renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) override;

    bool onMouseMove(float localX, float localY) override;
    bool onMouseButton(int button, bool pressed) override;
    bool onKey(int key, int scancode, int action, int mods) override;
    bool onChar(unsigned int codepoint) override;
    bool wantsMouse() const override { return true; }
    bool wantsKeyboard() const override { return true; }

private:
    std::unique_ptr<YDrawRenderer> _renderer;
    bool _failed = false;
};

} // namespace yetty

extern "C" {
    const char* name();
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine);
}
