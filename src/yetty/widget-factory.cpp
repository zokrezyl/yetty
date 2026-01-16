#include "widget-factory.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/font-manager.h>
#include <yetty/config.h>
#include <ytrace/ytrace.hpp>
#include <ytrace/ytrace.hpp>
#include <filesystem>
#include <dlfcn.h>

// Built-in plugins (linked into main executable)
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__)
#include "plugins/python/python.h"
#endif

namespace yetty {

//-----------------------------------------------------------------------------
// Factory
//-----------------------------------------------------------------------------

std::shared_ptr<WidgetFactory> WidgetFactory::create(
    Yetty* engine,
    const std::vector<std::string>& searchPaths
) {
    return std::shared_ptr<WidgetFactory>(new WidgetFactory(engine, searchPaths));
}

WidgetFactory::WidgetFactory(Yetty* engine, const std::vector<std::string>& searchPaths)
    : _engine(engine)
    , _searchPaths(searchPaths)
{
    // Register built-in plugins as available (lazy - not created until used)
#if (defined(__unix__) || defined(__APPLE__)) && !defined(__ANDROID__)
    _builtinPlugins["python"] = []() -> Result<PluginPtr> {
        return PythonPlugin::create();
    };
    yinfo("Registered built-in plugin: python (lazy)");
#endif
}

WidgetFactory::~WidgetFactory() {
    for (auto& [name, plugin] : _loadedPlugins) {
        if (plugin) {
            plugin->dispose();
        }
    }
}

//-----------------------------------------------------------------------------
// Shared resource access
//-----------------------------------------------------------------------------

FontManager* WidgetFactory::getFontManager() const {
    return _engine ? _engine->fontManager().get() : nullptr;
}

WebGPUContext* WidgetFactory::getContext() const {
    return _engine ? _engine->context().get() : nullptr;
}

Config* WidgetFactory::getConfig() const {
    return _engine ? _engine->config().get() : nullptr;
}

uv_loop_t* WidgetFactory::getLoop() const {
    return _engine ? _engine->getLoop() : nullptr;
}

ShaderManager* WidgetFactory::getShaderManager() const {
    return _engine ? _engine->shaderManager().get() : nullptr;
}

//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------

void WidgetFactory::registerWidget(const std::string& name, WidgetCreateFn factory) {
    _widgetFactories[name] = std::move(factory);
}

//-----------------------------------------------------------------------------
// Widget creation
//-----------------------------------------------------------------------------

std::pair<std::string, std::string> WidgetFactory::parseName(const std::string& name) const {
    auto dot = name.find('.');
    if (dot == std::string::npos) {
        return {"", name};
    }
    return {name.substr(0, dot), name.substr(dot + 1)};
}

Result<WidgetPtr> WidgetFactory::createWidget(
    const std::string& name,
    uv_loop_t* loop,
    int32_t x,
    int32_t y,
    uint32_t widthCells,
    uint32_t heightCells,
    const std::string& pluginArgs,
    const std::string& payload
) {
    yfunc();
    ydebug("createWidget: name='{}' x={} y={} w={} h={} args='{}' payload_size={}",
           name, x, y, widthCells, heightCells, pluginArgs, payload.size());

    auto [pluginName, widgetName] = parseName(name);
    ydebug("createWidget: parsed pluginName='{}' widgetName='{}'", pluginName, widgetName);

    if (pluginName.empty()) {
        // No dot - first check widget factories
        ydebug("createWidget: no dot, checking widget factories for '{}'", widgetName);
        auto it = _widgetFactories.find(widgetName);
        if (it != _widgetFactories.end()) {
            ydebug("createWidget: found in widget factories, calling factory");
            return it->second(widgetName, this, getFontManager(), loop, x, y, widthCells, heightCells, pluginArgs, payload);
        }

        // Try loading as plugin (e.g., "shader" -> shader.so with default widget)
        ydebug("createWidget: not in factories, trying to load as plugin '{}'", widgetName);
        auto pluginResult = getOrLoadPlugin(widgetName);
        if (!pluginResult.has_value()) {
            ydebug("createWidget: plugin load FAILED for '{}'", widgetName);
            return Err<WidgetPtr>("Unknown widget: " + widgetName, pluginResult);
        }

        // Use default widget type (empty string lets plugin decide)
        ydebug("createWidget: plugin loaded, calling plugin->createWidget with widgetName=''");
        ydebug("createWidget: this={} getFontManager()={} loop={}", (void*)this, (void*)getFontManager(), (void*)loop);
        auto result = pluginResult.value()->createWidget("", this, getFontManager(), loop, x, y, widthCells, heightCells, pluginArgs, payload);
        if (!result) {
            ydebug("createWidget: plugin->createWidget FAILED: {}", result.error().message());
        } else {
            ydebug("createWidget: plugin->createWidget SUCCESS");
        }
        return result;
    }

    // Has dot - format is "plugin.widgetName"
    ydebug("createWidget: has dot, loading plugin '{}'", pluginName);
    auto pluginResult = getOrLoadPlugin(pluginName);
    if (!pluginResult.has_value()) {
        ydebug("createWidget: plugin load FAILED for '{}'", pluginName);
        return Err<WidgetPtr>("Failed to load plugin: " + pluginName, pluginResult);
    }

    ydebug("createWidget: plugin loaded, calling plugin->createWidget with widgetName='{}'", widgetName);
    return pluginResult.value()->createWidget(widgetName, this, getFontManager(), loop, x, y, widthCells, heightCells, pluginArgs, payload);
}

Result<WidgetPtr> WidgetFactory::createWidget(
    const std::string& name,
    int32_t x,
    int32_t y,
    uint32_t widthCells,
    uint32_t heightCells,
    const std::string& pluginArgs,
    const std::string& payload
) {
    return createWidget(name, getLoop(), x, y, widthCells, heightCells, pluginArgs, payload);
}

//-----------------------------------------------------------------------------
// Queries
//-----------------------------------------------------------------------------

std::vector<std::string> WidgetFactory::getAvailableWidgets() const {
    std::vector<std::string> result;

    for (const auto& [name, _] : _widgetFactories) {
        result.push_back(name);
    }

    for (const auto& [pluginName, plugin] : _loadedPlugins) {
        if (plugin) {
            for (const auto& widgetType : plugin->getWidgetTypes()) {
                result.push_back(pluginName + "." + widgetType);
            }
        }
    }

    return result;
}

bool WidgetFactory::hasWidget(const std::string& name) const {
    auto [pluginName, widgetName] = parseName(name);

    if (pluginName.empty()) {
        return _widgetFactories.contains(widgetName);
    }

    // Check if plugin is loaded, but don't load it just to check
    return _loadedPlugins.contains(pluginName);
}

//-----------------------------------------------------------------------------
// Plugin loading (lazy)
//-----------------------------------------------------------------------------

Result<PluginPtr> WidgetFactory::getOrLoadPlugin(const std::string& name) {
    yfunc();
    ydebug("getOrLoadPlugin: name='{}'", name);

    // Check if already loaded
    auto it = _loadedPlugins.find(name);
    if (it != _loadedPlugins.end()) {
        ydebug("getOrLoadPlugin: already loaded, returning cached plugin");
        return Ok(it->second);
    }

    // Check if it's a built-in plugin (lazy creation)
    auto builtinIt = _builtinPlugins.find(name);
    if (builtinIt != _builtinPlugins.end()) {
        ydebug("getOrLoadPlugin: found built-in plugin '{}', creating lazily", name);
        auto createResult = builtinIt->second();
        if (!createResult.has_value()) {
            ydebug("getOrLoadPlugin: built-in plugin creation FAILED: {}", createResult.error().message());
            return createResult;
        }
        // Initialize the plugin
        auto plugin = createResult.value();
        if (auto res = plugin->init(getContext()); !res) {
            ydebug("getOrLoadPlugin: built-in plugin init FAILED: {}", res.error().message());
            return Err<PluginPtr>("Plugin init failed: " + name, res);
        }
        _loadedPlugins[name] = plugin;
        yinfo("Loaded built-in plugin: {}", name);
        return Ok(plugin);
    }

    // Find and load dynamic plugin
    ydebug("getOrLoadPlugin: not cached, calling findAndLoadPlugin");
    auto loadResult = findAndLoadPlugin(name);
    if (!loadResult.has_value()) {
        ydebug("getOrLoadPlugin: findAndLoadPlugin FAILED: {}", loadResult.error().message());
        return loadResult;
    }

    // Cache it
    ydebug("getOrLoadPlugin: caching plugin");
    _loadedPlugins[name] = loadResult.value();
    return loadResult;
}

Result<PluginPtr> WidgetFactory::findAndLoadPlugin(const std::string& name) {
    yfunc();
    ydebug("findAndLoadPlugin: name='{}' searchPaths.size={}", name, _searchPaths.size());
    namespace fs = std::filesystem;

    // Search for plugin in all search paths
    for (const auto& dir : _searchPaths) {
        ydebug("findAndLoadPlugin: searching in dir='{}'", dir);
        // Try different extensions
        for (const char* ext : {".so", ".dylib", ".dll"}) {
            fs::path pluginPath = fs::path(dir) / (name + ext);
            ydebug("findAndLoadPlugin: checking path='{}'", pluginPath.string());
            if (fs::exists(pluginPath)) {
                ydebug("findAndLoadPlugin: FOUND at {}", pluginPath.string());
                yinfo("Found plugin '{}' at {}", name, pluginPath.string());
                return loadDynamicPlugin(pluginPath.string());
            }
        }
    }

    ydebug("findAndLoadPlugin: NOT FOUND in any search path");
    return Err<PluginPtr>("Plugin not found: " + name);
}

Result<PluginPtr> WidgetFactory::loadDynamicPlugin(const std::string& path) {
    yfunc();
    ydebug("loadDynamicPlugin: path='{}'", path);

    // Use RTLD_GLOBAL for python plugin so libpython symbols are available to numpy extensions
    int flags = RTLD_NOW;
    if (path.find("python") != std::string::npos) {
        flags |= RTLD_GLOBAL;
        yinfo("loadDynamicPlugin: using RTLD_GLOBAL for python plugin");
    } else {
        flags |= RTLD_LOCAL;
    }

    // Load shared library
    ydebug("loadDynamicPlugin: calling dlopen with flags={}", flags);
    void* handle = dlopen(path.c_str(), flags);
    if (!handle) {
        const char* err = dlerror();
        ydebug("loadDynamicPlugin: dlopen FAILED: {}", err ? err : "unknown");
        return Err<PluginPtr>("Failed to load: " + std::string(err ? err : "unknown"));
    }
    ydebug("loadDynamicPlugin: dlopen SUCCESS, handle={}", handle);

    // Look for create function - plugins export: Result<PluginPtr> create()
    ydebug("loadDynamicPlugin: looking for 'create' symbol");
    using CreateFn = Result<PluginPtr> (*)();
    auto createFn = reinterpret_cast<CreateFn>(dlsym(handle, "create"));
    if (!createFn) {
        ydebug("loadDynamicPlugin: 'create' symbol NOT FOUND");
        dlclose(handle);
        return Err<PluginPtr>("Missing create(): " + path);
    }
    ydebug("loadDynamicPlugin: 'create' symbol found at {}", (void*)createFn);

    // Create plugin instance
    ydebug("loadDynamicPlugin: calling create()");
    auto result = createFn();
    if (!result) {
        ydebug("loadDynamicPlugin: create() FAILED: {}", result.error().message());
        dlclose(handle);
        return Err<PluginPtr>("create() failed: " + path, result);
    }
    ydebug("loadDynamicPlugin: create() SUCCESS");

    PluginPtr plugin = *result;
    ydebug("loadDynamicPlugin: plugin name='{}' initialized={}", plugin->pluginName(), plugin->isInitialized());

    // Initialize plugin
    ydebug("loadDynamicPlugin: calling plugin->init(getContext())");
    ydebug("loadDynamicPlugin: getContext()={}", (void*)getContext());
    if (auto res = plugin->init(getContext()); !res) {
        ydebug("loadDynamicPlugin: plugin->init() FAILED: {}", res.error().message());
        dlclose(handle);
        return Err<PluginPtr>("Plugin init failed: " + path, res);
    }
    ydebug("loadDynamicPlugin: plugin->init() SUCCESS");

    yinfo("Loaded plugin: {}", plugin->pluginName());
    ydebug("loadDynamicPlugin: returning plugin successfully");
    return Ok(plugin);
}

} // namespace yetty
