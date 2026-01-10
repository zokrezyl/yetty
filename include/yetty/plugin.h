#pragma once

#include <yetty/widget.h>
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <uv.h>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <mutex>
#include <unordered_map>

namespace yetty {

// Forward declarations
class WebGPUContext;
class Grid;
class Font;
class FontManager;
class Plugin;
class WidgetFactory;

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
// Plugin - Factory that creates widget types
//
// Plugins:
//   - Hold shared resources (GPU pipelines, fonts, etc.)
//   - Know how to create their widget types
//   - Parse plugin-specific arguments
//
// Widgets get everything they need via:
//   - WidgetFactory* (gives FontManager, WebGPUContext, Config)
//   - WidgetParams (gives uv_loop_t*, position, size, cell dimensions)
//   - payload (widget-specific data)
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
    const std::string& name() const { return _name; }

    // Create widget
    // widgetName: e.g., "lottie", "svg" for ThorvgPlugin, or "" for single-widget plugins
    // factory: for creating other widgets
    // fontManager: for drawing fonts
    // loop: for async callbacks
    // x, y: position in cells
    // widthCells, heightCells: size in cells
    // pluginArgs: plugin-specific command line args (e.g., "--loop")
    // payload: base64-encoded data
    virtual Result<WidgetPtr> createWidget(
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
    ) {
        (void)widgetName;
        (void)factory;
        (void)fontManager;
        (void)loop;
        (void)x;
        (void)y;
        (void)widthCells;
        (void)heightCells;
        (void)pluginArgs;
        (void)payload;
        return Err<WidgetPtr>("createWidget not implemented");
    }

    // List available widget types (e.g., ["lottie", "svg"])
    virtual std::vector<std::string> getWidgetTypes() const { return {}; }

    // Initialize plugin with WebGPU context
    virtual Result<void> init(WebGPUContext* ctx) {
        _ctx = ctx;
        _initialized = true;
        return Ok();
    }

    // Cleanup
    virtual Result<void> dispose() { return Ok(); }

    // Accessors
    WebGPUContext* getContext() const { return _ctx; }
    bool isInitialized() const { return _initialized; }

protected:
    Plugin() noexcept = default;

    WebGPUContext* _ctx = nullptr;
    std::string _name;
    bool _initialized = false;
    std::mutex _mutex;
};

// C function types for dynamic loading
using PluginMetaFn = PluginMeta (*)();
using PluginCreateFn = Result<PluginPtr> (*)();
using PluginNameFn = const char* (*)();

} // namespace yetty
