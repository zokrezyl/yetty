#pragma once

#include <yetty/plugin.h>  // Plugin, PluginPtr, WidgetPtr
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare libuv types
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace yetty {

// Forward declarations
class Yetty;
class WebGPUContext;
class FontManager;
class Config;
class WidgetFactory;

//-----------------------------------------------------------------------------
// WidgetCreateFn - function type for creating widgets
//-----------------------------------------------------------------------------
using WidgetCreateFn = std::function<Result<WidgetPtr>(
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
)>;

//-----------------------------------------------------------------------------
// WidgetFactory - lazy-loads plugins and creates widgets
//
// Plugins are discovered from search paths but only loaded when first accessed.
//-----------------------------------------------------------------------------
class WidgetFactory {
public:
    // Factory method - search paths passed at creation
    static std::shared_ptr<WidgetFactory> create(
        Yetty* engine,
        const std::vector<std::string>& searchPaths
    );

    ~WidgetFactory();

    //-------------------------------------------------------------------------
    // Shared resource access (for widgets that need them)
    //-------------------------------------------------------------------------
    FontManager* getFontManager() const;
    WebGPUContext* getContext() const;
    Config* getConfig() const;
    uv_loop_t* getLoop() const;
    class ShaderManager* getShaderManager() const;

    //-------------------------------------------------------------------------
    // Widget registration (for non-plugin widgets)
    //-------------------------------------------------------------------------
    void registerWidget(const std::string& name, WidgetCreateFn factory);

    //-------------------------------------------------------------------------
    // Widget creation - explicit parameters
    //-------------------------------------------------------------------------
    Result<WidgetPtr> createWidget(
        const std::string& name,
        uv_loop_t* loop,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload
    );

    // Convenience version using factory's default loop
    Result<WidgetPtr> createWidget(
        const std::string& name,
        int32_t x,
        int32_t y,
        uint32_t widthCells,
        uint32_t heightCells,
        const std::string& pluginArgs,
        const std::string& payload
    );

    //-------------------------------------------------------------------------
    // Queries
    //-------------------------------------------------------------------------
    std::vector<std::string> getAvailableWidgets() const;
    bool hasWidget(const std::string& name) const;

    //-------------------------------------------------------------------------
    // Plugin access (lazy-loaded)
    //-------------------------------------------------------------------------
    Result<PluginPtr> getOrLoadPlugin(const std::string& name);

private:
    WidgetFactory(Yetty* engine, const std::vector<std::string>& searchPaths);

    Yetty* _engine;
    std::vector<std::string> _searchPaths;

    // Direct widget factories (non-plugin widgets)
    std::unordered_map<std::string, WidgetCreateFn> _widgetFactories;

    // Loaded plugins (lazy-loaded on first access)
    std::unordered_map<std::string, PluginPtr> _loadedPlugins;

    // Built-in plugin factories (lazy - created on first use)
    using BuiltinPluginFactory = std::function<Result<PluginPtr>()>;
    std::unordered_map<std::string, BuiltinPluginFactory> _builtinPlugins;

    std::pair<std::string, std::string> parseName(const std::string& name) const;
    Result<PluginPtr> findAndLoadPlugin(const std::string& name);
    Result<PluginPtr> loadDynamicPlugin(const std::string& path);
};

} // namespace yetty
