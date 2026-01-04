#pragma once

//=============================================================================
// Mock Plugin for Testing
//
// Lightweight mock plugin and layer for testing plugin manager behavior
// without GPU dependencies.
//=============================================================================

#include <yetty/plugin.h>
#include <string>
#include <vector>

namespace yetty::test {

//-----------------------------------------------------------------------------
// MockPluginLayer - Simple layer for testing
//-----------------------------------------------------------------------------
class MockPluginLayer : public PluginLayer {
public:
    MockPluginLayer() = default;
    ~MockPluginLayer() override = default;

    Result<void> init(const std::string& payload) override {
        _payload = payload;
        _init_called = true;
        return Ok();
    }

    Result<void> dispose() override {
        _dispose_called = true;
        return Ok();
    }

    Result<void> render(WebGPUContext& ctx) override {
        (void)ctx;
        _render_count++;
        return Ok();
    }

    bool renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) override {
        (void)pass;
        (void)ctx;
        _render_count++;
        return true;
    }

    bool onMouseMove(float localX, float localY) override {
        _last_mouse_x = localX;
        _last_mouse_y = localY;
        _mouse_move_count++;
        return _wants_mouse;
    }

    bool onMouseButton(int button, bool pressed) override {
        _last_button = button;
        _last_pressed = pressed;
        _mouse_button_count++;
        return _wants_mouse;
    }

    bool wantsKeyboard() const override { return _wants_keyboard; }
    bool wantsMouse() const override { return _wants_mouse; }

    // Test inspection
    bool initCalled() const { return _init_called; }
    bool disposeCalled() const { return _dispose_called; }
    int renderCount() const { return _render_count; }
    float lastMouseX() const { return _last_mouse_x; }
    float lastMouseY() const { return _last_mouse_y; }
    int mouseButtonCount() const { return _mouse_button_count; }
    int mouseMoveCount() const { return _mouse_move_count; }

    // Test control
    void setWantsKeyboard(bool v) { _wants_keyboard = v; }
    void setWantsMouse(bool v) { _wants_mouse = v; }

private:
    bool _init_called = false;
    bool _dispose_called = false;
    int _render_count = 0;
    float _last_mouse_x = 0;
    float _last_mouse_y = 0;
    int _last_button = 0;
    bool _last_pressed = false;
    int _mouse_button_count = 0;
    int _mouse_move_count = 0;
    bool _wants_keyboard = false;
    bool _wants_mouse = false;
};

//-----------------------------------------------------------------------------
// MockPlugin - Simple plugin for testing
//-----------------------------------------------------------------------------
class MockPlugin : public Plugin {
public:
    ~MockPlugin() override = default;

    static Result<PluginPtr> create(YettyPtr engine) noexcept {
        auto p = PluginPtr(new MockPlugin(std::move(engine)));
        if (auto res = static_cast<MockPlugin*>(p.get())->init(); !res) {
            return Err<PluginPtr>("Failed to init MockPlugin", res);
        }
        return Ok(p);
    }

    const char* pluginName() const override { return "mock"; }

    Result<PluginLayerPtr> createLayer(const std::string& payload) override {
        auto layer = std::make_shared<MockPluginLayer>();
        if (auto res = layer->init(payload); !res) {
            return Err<PluginLayerPtr>("Failed to init mock layer", res);
        }
        _created_layers.push_back(layer);
        return Ok<PluginLayerPtr>(layer);
    }

    Result<void> render(WebGPUContext& ctx) override {
        (void)ctx;
        _render_count++;
        return Ok();
    }

    // Test inspection
    int initCount() const { return _init_count; }
    int renderCount() const { return _render_count; }
    const std::vector<std::shared_ptr<MockPluginLayer>>& createdLayers() const {
        return _created_layers;
    }

private:
    explicit MockPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}
    Result<void> init() noexcept override {
        _initialized = true;
        _init_count++;
        return Ok();
    }

    int _init_count = 0;
    int _render_count = 0;
    std::vector<std::shared_ptr<MockPluginLayer>> _created_layers;
};

} // namespace yetty::test
