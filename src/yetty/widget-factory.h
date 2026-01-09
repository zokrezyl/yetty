#pragma once

#include <yetty/plugin.h>  // Plugin, PluginPtr, WidgetPtr, WidgetParams
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace yetty {

// Forward declarations
class Yetty;
class WebGPUContext;

//-----------------------------------------------------------------------------
// InternalWidgetFactory - function type for creating internal widgets
//-----------------------------------------------------------------------------
using InternalWidgetFactory = std::function<Result<WidgetPtr>(
    WebGPUContext* ctx,
    const WidgetParams& params,
    const std::string& pluginArgs,
    const std::string& payload
)>;

//-----------------------------------------------------------------------------
// WidgetFactory - single entry point for creating all widgets
//
// Usage:
//   factory->createWidget("thorvg.lottie", "-x 0 -y 0 -w 10 -h 10", "--loop", payload)
//   factory->createWidget("plot", "-x 0 -y 0 -w 20 -h 10", "", payload)
//-----------------------------------------------------------------------------
class WidgetFactory {
public:
    explicit WidgetFactory(Yetty* engine);
    ~WidgetFactory();

    Result<void> init();

    //-------------------------------------------------------------------------
    // Registration
    //-------------------------------------------------------------------------

    // Register internal widget type (built into binary)
    void registerInternal(const std::string& name, InternalWidgetFactory factory);

    // Register plugin for lazy loading
    void registerPlugin(const std::string& name, const std::string& path = "");

    // Scan directory for dynamic plugins
    void loadPluginsFromDirectory(const std::string& path);

    //-------------------------------------------------------------------------
    // Widget creation
    //-------------------------------------------------------------------------

    // Create widget
    // name: "widgetType" for internal, "plugin.widgetType" for plugins
    // genericArgs: "-x 0 -y 0 -w 10 -h 10 --relative" etc.
    // pluginArgs: plugin-specific args (passed through)
    // payload: base64-encoded data (passed through)
    Result<WidgetPtr> createWidget(
        const std::string& name,
        const std::string& genericArgs,
        const std::string& pluginArgs,
        const std::string& payload
    );

    //-------------------------------------------------------------------------
    // Queries
    //-------------------------------------------------------------------------

    std::vector<std::string> getAvailableWidgets() const;
    std::vector<std::string> getAvailablePlugins() const;
    bool hasWidget(const std::string& name) const;

    //-------------------------------------------------------------------------
    // Plugin access (for shared resources)
    //-------------------------------------------------------------------------

    Result<PluginPtr> getOrLoadPlugin(const std::string& name);

private:
    Yetty* engine_;
    WebGPUContext* ctx_ = nullptr;

    // Internal widget factories
    std::unordered_map<std::string, InternalWidgetFactory> internalFactories_;

    // Plugin registry (lazy loaded)
    struct PluginEntry {
        std::string path;           // Empty for built-in
        PluginPtr instance;         // nullptr until loaded
        bool isLoaded = false;
    };
    std::unordered_map<std::string, PluginEntry> plugins_;

    // Parse "plugin.widget" or "widget"
    std::pair<std::string, std::string> parseName(const std::string& name) const;

    // Parse generic args into WidgetParams
    WidgetParams parseGenericArgs(const std::string& args) const;

    // Load plugin
    Result<PluginPtr> loadPlugin(const std::string& name);
    Result<PluginPtr> loadDynamicPlugin(const std::string& path);
};

} // namespace yetty
