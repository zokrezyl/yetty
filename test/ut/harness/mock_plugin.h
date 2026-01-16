#pragma once

//=============================================================================
// Mock Plugin for Testing
//
// Lightweight mock plugin and widget for testing plugin manager behavior
// without GPU dependencies.
//=============================================================================

#include <yetty/plugin.h>
#include <string>
#include <vector>

namespace yetty::test {

//-----------------------------------------------------------------------------
// MockPluginWidget - Simple widget for testing
//-----------------------------------------------------------------------------
class MockPluginWidget : public Widget {
public:
    static Result<WidgetPtr> create(const std::string& payload) {
        auto w = std::shared_ptr<MockPluginWidget>(new MockPluginWidget(payload));
        if (auto res = w->init(); !res) {
            return Err<WidgetPtr>("Failed to init MockPluginWidget", res);
        }
        return Ok(std::static_pointer_cast<Widget>(w));
    }

    ~MockPluginWidget() override = default;

    Result<void> dispose() override {
        _disposeCalled = true;
        return Ok();
    }

    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override {
        (void)pass;
        (void)ctx;
        _renderCount++;
        return Ok();
    }

    bool onMouseMove(float localX, float localY) override {
        _lastMouseX = localX;
        _lastMouseY = localY;
        _mouseMoveCount++;
        return _wantsMouse;
    }

    bool onMouseButton(int button, bool pressed) override {
        _lastButton = button;
        _lastPressed = pressed;
        _mouseButtonCount++;
        return _wantsMouse;
    }

    bool wantsKeyboard() const override { return _wantsKeyboard; }
    bool wantsMouse() const override { return _wantsMouse; }

    // Test inspection
    bool initCalled() const { return _initCalled; }
    bool disposeCalled() const { return _disposeCalled; }
    int renderCount() const { return _renderCount; }
    float lastMouseX() const { return _lastMouseX; }
    float lastMouseY() const { return _lastMouseY; }
    int mouseMoveCount() const { return _mouseMoveCount; }
    int mouseButtonCount() const { return _mouseButtonCount; }

    // Test control
    void setWantsKeyboard(bool v) { _wantsKeyboard = v; }
    void setWantsMouse(bool v) { _wantsMouse = v; }

private:
    explicit MockPluginWidget(const std::string& payload) {
        _payload = payload;
    }

    Result<void> init() override {
        _initCalled = true;
        return Ok();
    }

    bool _initCalled = false;
    bool _disposeCalled = false;
    int _renderCount = 0;
    float _lastMouseX = 0;
    float _lastMouseY = 0;
    int _lastButton = 0;
    bool _lastPressed = false;
    int _mouseMoveCount = 0;
    int _mouseButtonCount = 0;
    bool _wantsKeyboard = false;
    bool _wantsMouse = false;
};

//-----------------------------------------------------------------------------
// MockPlugin - Simple plugin for testing
//-----------------------------------------------------------------------------
class MockPlugin : public Plugin {
public:
    ~MockPlugin() override = default;

    static Result<PluginPtr> create() noexcept {
        auto p = PluginPtr(new MockPlugin());
        if (auto res = p->init(nullptr); !res) {
            return Err<PluginPtr>("Failed to init MockPlugin", res);
        }
        return Ok(p);
    }

    const char* pluginName() const override { return "mock"; }

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
    ) override {
        (void)widgetName;
        (void)factory;
        (void)fontManager;
        (void)loop;
        (void)x;
        (void)y;
        (void)widthCells;
        (void)heightCells;
        (void)pluginArgs;
        auto result = MockPluginWidget::create(payload);
        if (!result) {
            return Err<WidgetPtr>("Failed to create mock widget", result);
        }
        auto widget = std::static_pointer_cast<MockPluginWidget>(*result);
        _createdWidgets.push_back(widget);
        return Ok<WidgetPtr>(widget);
    }

    Result<void> init(WebGPUContext* ctx) override {
        (void)ctx;
        _initialized = true;
        _initCount++;
        return Ok();
    }

    // Test inspection
    int initCount() const { return _initCount; }
    const std::vector<std::shared_ptr<MockPluginWidget>>& createdWidgets() const {
        return _createdWidgets;
    }

private:
    MockPlugin() noexcept = default;

    int _initCount = 0;
    std::vector<std::shared_ptr<MockPluginWidget>> _createdWidgets;
};

} // namespace yetty::test
