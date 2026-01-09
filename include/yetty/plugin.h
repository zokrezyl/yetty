#pragma once

#include <yetty/widget.h>
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace yetty {

// Forward declarations
class WebGPUContext;
class Grid;
class Font;
class FontManager;
class Plugin;
class Yetty;

// Yetty shared pointer type
using YettyPtr = std::shared_ptr<Yetty>;
using PluginPtr = std::shared_ptr<Plugin>;

//-----------------------------------------------------------------------------
// PluginMeta - metadata returned by plugins
//-----------------------------------------------------------------------------
struct PluginMeta {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::vector<std::string> widgetTypes;  // e.g., ["lottie", "svg"]
    std::unordered_map<std::string, std::string> extra;
};

//-----------------------------------------------------------------------------
// Plugin - Factory helper that knows how to create its widget types
//
// Plugins are NOT Widgets/Renderables. They:
//   - Hold shared resources (GPU pipelines, fonts, etc.)
//   - Know how to create their widget types
//   - Parse plugin-specific arguments
//
// Two-phase construction:
//   - Private constructor (cannot fail)
//   - init() method (can fail, returns Result<>)
//   - Static create() calls constructor + init()
//-----------------------------------------------------------------------------
class Plugin : public std::enable_shared_from_this<Plugin> {
public:
    virtual ~Plugin() = default;

    // Metadata
    virtual PluginMeta meta() const {
        PluginMeta m;
        m.name = pluginName();
        return m;
    }
    virtual const char* pluginName() const { return "UnnamedPlugin"; }
    const std::string& name() const { return name_; }

    // Create widget by type name
    // widgetType: e.g., "lottie", "svg" for ThorvgPlugin, or "" for single-widget plugins
    // params: generic widget parameters (position, size)
    // pluginArgs: plugin-specific command line args (e.g., "--loop")
    // payload: base64-encoded data
    virtual Result<WidgetPtr> createWidget(
        const std::string& widgetType,
        const WidgetParams& params,
        const std::string& pluginArgs,
        const std::string& payload
    ) {
        (void)widgetType;
        (void)params;
        (void)pluginArgs;
        (void)payload;
        return Err<WidgetPtr>("createWidget not implemented");
    }

    // Legacy createWidget for backward compatibility during migration
    virtual Result<WidgetPtr> createWidget(const std::string& payload) {
        return createWidget("", WidgetParams{}, "", payload);
    }

    // List available widget types (e.g., ["lottie", "svg"])
    virtual std::vector<std::string> getWidgetTypes() const { return {}; }

    // Initialize plugin (load shared resources)
    virtual Result<void> init(WebGPUContext* ctx) {
        ctx_ = ctx;
        return Ok();
    }

    // Legacy init for backward compatibility
    virtual Result<void> init() { return Ok(); }

    // Cleanup - disposes all widgets owned by this plugin
    virtual Result<void> dispose() {
        for (auto& widget : widgets_) {
            if (auto res = widget->dispose(); !res) {
                // Log error but continue disposing other widgets
            }
        }
        widgets_.clear();
        return Ok();
    }

    // Optional: render shared resources (called once per frame before widgets)
    virtual void renderSharedResources(WebGPUContext& ctx) { (void)ctx; }

    // Engine access
    YettyPtr getEngine() const { return engine_; }
    WebGPUContext* getContext() const { return ctx_; }

    // Font access for text rendering plugins
    void setFont(Font* font) { font_ = font; }
    Font* getFont() const { return font_; }

    // Check if initialized
    bool isInitialized() const { return initialized_; }
    void setInitialized(bool v) { initialized_ = v; }

    //-------------------------------------------------------------------------
    // Legacy API for backward compatibility with PluginManager
    // These will be removed after migration to WidgetFactory
    //-------------------------------------------------------------------------

    // Renderable-like interface (Plugin used to inherit from Renderable)
    uint32_t id() const { return pluginId_; }
    uint32_t zOrder() const { return pluginZOrder_; }
    void start() { running_ = true; }
    void stop() { running_ = false; }
    bool isRunning() const { return running_; }

    // Widget management (used to be owned by Plugin)
    void addWidget(WidgetPtr widget) {
        widget->setParent(this);
        widgets_.push_back(widget);
    }

    Result<void> removeWidget(uint32_t id) {
        for (auto it = widgets_.begin(); it != widgets_.end(); ++it) {
            if ((*it)->id() == id) {
                if (auto res = (*it)->dispose(); !res) {
                    return Err<void>("Failed to dispose widget " + std::to_string(id), res);
                }
                widgets_.erase(it);
                return Ok();
            }
        }
        return Err<void>("Widget not found: " + std::to_string(id));
    }

    WidgetPtr getWidget(uint32_t id) {
        for (auto& widget : widgets_) {
            if (widget->id() == id) return widget;
        }
        return nullptr;
    }

    const std::vector<WidgetPtr>& getWidgets() const { return widgets_; }

    // Plugin render (for shared resources)
    virtual Result<void> render(WebGPUContext& ctx) {
        (void)ctx;
        return Ok();
    }

    // Terminal resize handling
    virtual Result<void> onTerminalResize(uint32_t cellWidth, uint32_t cellHeight) {
        for (auto& widget : widgets_) {
            uint32_t newW = widget->getWidthCells() * cellWidth;
            uint32_t newH = widget->getHeightCells() * cellHeight;
            widget->onResize(newW, newH);
        }
        return Ok();
    }

protected:
    explicit Plugin(YettyPtr engine) noexcept : engine_(std::move(engine)) {}
    Plugin() noexcept = default;

    YettyPtr engine_;
    WebGPUContext* ctx_ = nullptr;
    std::string name_;
    Font* font_ = nullptr;
    bool initialized_ = false;
    bool& _initialized = initialized_;  // Legacy alias
    std::mutex mutex_;

    // Legacy Renderable-like state
    uint32_t pluginId_ = 0;
    uint32_t pluginZOrder_ = 150;
    bool running_ = false;

    // Widget storage (legacy - will move to Terminal)
    std::vector<WidgetPtr> widgets_;
    std::vector<WidgetPtr>& _widgets = widgets_;  // Legacy alias
};

// C function types for dynamic loading
using PluginMetaFn = PluginMeta (*)();
using PluginCreateFn = Result<PluginPtr> (*)(YettyPtr);
using PluginNameFn = const char* (*)();

} // namespace yetty
