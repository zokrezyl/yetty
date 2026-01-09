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
//
// Two-phase construction:
//   1. Constructor (private) - stores payload
//   2. init() (private) - no args, returns Result
//   3. create() (public) - factory
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
        dispose_called_ = true;
        return Ok();
    }

    Result<void> render(WebGPUContext& ctx) override {
        (void)ctx;
        render_count_++;
        return Ok();
    }

    bool render(WGPURenderPassEncoder pass, WebGPUContext& ctx) override {
        (void)pass;
        (void)ctx;
        render_count_++;
        return true;
    }

    bool onMouseMove(float localX, float localY) override {
        last_mouse_x_ = localX;
        last_mouse_y_ = localY;
        mouse_move_count_++;
        return wants_mouse_;
    }

    bool onMouseButton(int button, bool pressed) override {
        last_button_ = button;
        last_pressed_ = pressed;
        mouse_button_count_++;
        return wants_mouse_;
    }

    bool wantsKeyboard() const override { return wants_keyboard_; }
    bool wantsMouse() const override { return wants_mouse_; }

    // Test inspection
    bool initCalled() const { return init_called_; }
    bool disposeCalled() const { return dispose_called_; }
    int renderCount() const { return render_count_; }
    float lastMouseX() const { return last_mouse_x_; }
    float lastMouseY() const { return last_mouse_y_; }
    int mouseButtonCount() const { return mouse_button_count_; }
    int mouseMoveCount() const { return mouse_move_count_; }

    // Test control
    void setWantsKeyboard(bool v) { wants_keyboard_ = v; }
    void setWantsMouse(bool v) { wants_mouse_ = v; }

private:
    explicit MockPluginWidget(const std::string& payload) {
        payload_ = payload;
    }

    Result<void> init() override {
        init_called_ = true;
        return Ok();
    }

    bool init_called_ = false;
    bool dispose_called_ = false;
    int render_count_ = 0;
    float last_mouse_x_ = 0;
    float last_mouse_y_ = 0;
    int last_button_ = 0;
    bool last_pressed_ = false;
    int mouse_button_count_ = 0;
    int mouse_move_count_ = 0;
    bool wants_keyboard_ = false;
    bool wants_mouse_ = false;
};

//-----------------------------------------------------------------------------
// MockPlugin - Simple plugin for testing
//-----------------------------------------------------------------------------
class MockPlugin : public Plugin {
public:
    ~MockPlugin() override = default;

    static Result<PluginPtr> create(YettyPtr engine) noexcept {
        auto p = PluginPtr(new MockPlugin(std::move(engine)));
        if (auto res = static_cast<MockPlugin*>(p.get())->pluginInit(); !res) {
            return Err<PluginPtr>("Failed to init MockPlugin", res);
        }
        return Ok(p);
    }

    const char* pluginName() const override { return "mock"; }

    Result<WidgetPtr> createWidget(const std::string& payload) override {
        auto result = MockPluginWidget::create(payload);
        if (!result) {
            return Err<WidgetPtr>("Failed to create mock widget", result);
        }
        auto layer = std::static_pointer_cast<MockPluginWidget>(*result);
        created_layers_.push_back(layer);
        return Ok<WidgetPtr>(layer);
    }

    Result<void> render(WebGPUContext& ctx) override {
        (void)ctx;
        render_count_++;
        return Ok();
    }

    // Test inspection
    int initCount() const { return init_count_; }
    int renderCount() const { return render_count_; }
    const std::vector<std::shared_ptr<MockPluginWidget>>& createdWidgets() const {
        return created_layers_;
    }

private:
    explicit MockPlugin(YettyPtr engine) noexcept : Plugin(std::move(engine)) {}

    Result<void> pluginInit() noexcept {
        initialized_ = true;
        init_count_++;
        return Ok();
    }

    int init_count_ = 0;
    int render_count_ = 0;
    std::vector<std::shared_ptr<MockPluginWidget>> created_layers_;
};

} // namespace yetty::test
