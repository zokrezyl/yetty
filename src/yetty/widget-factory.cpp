#include "widget-factory.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <sstream>
#include <algorithm>

namespace yetty {

//-----------------------------------------------------------------------------
// Construction
//-----------------------------------------------------------------------------

WidgetFactory::WidgetFactory(Yetty* engine)
    : engine_(engine)
{
}

WidgetFactory::~WidgetFactory() {
    // Dispose all loaded plugins
    for (auto& [name, entry] : plugins_) {
        if (entry.instance) {
            entry.instance->dispose();
        }
    }
}

Result<void> WidgetFactory::init() {
    ctx_ = engine_->context().get();
    if (!ctx_) {
        return Err<void>("WebGPU context not available");
    }

    // Register built-in plugins (lazy loaded)
    registerPlugin("thorvg");
    registerPlugin("shader");

    // Internal widgets will be registered by the engine/terminal

    return Ok();
}

//-----------------------------------------------------------------------------
// Registration
//-----------------------------------------------------------------------------

void WidgetFactory::registerInternal(const std::string& name, InternalWidgetFactory factory) {
    internalFactories_[name] = std::move(factory);
}

void WidgetFactory::registerPlugin(const std::string& name, const std::string& path) {
    plugins_[name] = PluginEntry{
        .path = path,
        .instance = nullptr,
        .isLoaded = false
    };
}

void WidgetFactory::loadPluginsFromDirectory(const std::string& path) {
    // TODO: Scan directory for .so/.dylib files and register them
    (void)path;
}

//-----------------------------------------------------------------------------
// Widget creation
//-----------------------------------------------------------------------------

std::pair<std::string, std::string> WidgetFactory::parseName(const std::string& name) const {
    auto dot = name.find('.');
    if (dot == std::string::npos) {
        return {"", name};  // Internal widget
    }
    return {name.substr(0, dot), name.substr(dot + 1)};
}

WidgetParams WidgetFactory::parseGenericArgs(const std::string& args) const {
    WidgetParams params;

    // Simple argument parser for: -x N -y N -w N -h N --relative --absolute
    std::istringstream iss(args);
    std::string token;

    while (iss >> token) {
        if (token == "-x" && iss >> token) {
            params.x = std::stoi(token);
        } else if (token == "-y" && iss >> token) {
            params.y = std::stoi(token);
        } else if (token == "-w" && iss >> token) {
            params.widthCells = std::stoul(token);
        } else if (token == "-h" && iss >> token) {
            params.heightCells = std::stoul(token);
        } else if (token == "--relative") {
            params.mode = PositionMode::Relative;
        } else if (token == "--absolute") {
            params.mode = PositionMode::Absolute;
        }
    }

    // Get cell dimensions from engine if available
    // TODO: Get from terminal context
    if (params.cellWidth == 0) params.cellWidth = 10;
    if (params.cellHeight == 0) params.cellHeight = 20;

    return params;
}

Result<WidgetPtr> WidgetFactory::createWidget(
    const std::string& name,
    const std::string& genericArgs,
    const std::string& pluginArgs,
    const std::string& payload
) {
    auto [pluginName, widgetType] = parseName(name);
    auto params = parseGenericArgs(genericArgs);

    if (pluginName.empty()) {
        // Internal widget
        auto it = internalFactories_.find(widgetType);
        if (it == internalFactories_.end()) {
            return Err<WidgetPtr>("Unknown internal widget: " + widgetType);
        }
        return it->second(ctx_, params, pluginArgs, payload);
    }

    // Plugin widget - lazy load plugin
    auto pluginResult = getOrLoadPlugin(pluginName);
    if (!pluginResult.has_value()) {
        return Err<WidgetPtr>("Failed to load plugin: " + pluginName, pluginResult);
    }

    return pluginResult.value()->createWidget(widgetType, params, pluginArgs, payload);
}

//-----------------------------------------------------------------------------
// Queries
//-----------------------------------------------------------------------------

std::vector<std::string> WidgetFactory::getAvailableWidgets() const {
    std::vector<std::string> result;

    // Internal widgets
    for (const auto& [name, _] : internalFactories_) {
        result.push_back(name);
    }

    // Plugin widgets
    for (const auto& [pluginName, entry] : plugins_) {
        if (entry.isLoaded && entry.instance) {
            for (const auto& widgetType : entry.instance->getWidgetTypes()) {
                result.push_back(pluginName + "." + widgetType);
            }
        } else {
            // Plugin not loaded yet, just show plugin name
            result.push_back(pluginName + ".*");
        }
    }

    return result;
}

std::vector<std::string> WidgetFactory::getAvailablePlugins() const {
    std::vector<std::string> result;
    for (const auto& [name, _] : plugins_) {
        result.push_back(name);
    }
    return result;
}

bool WidgetFactory::hasWidget(const std::string& name) const {
    auto [pluginName, widgetType] = parseName(name);

    if (pluginName.empty()) {
        return internalFactories_.contains(widgetType);
    }

    return plugins_.contains(pluginName);
}

//-----------------------------------------------------------------------------
// Plugin loading
//-----------------------------------------------------------------------------

Result<PluginPtr> WidgetFactory::getOrLoadPlugin(const std::string& name) {
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return Err<PluginPtr>("Unknown plugin: " + name);
    }

    if (!it->second.isLoaded) {
        auto loadResult = loadPlugin(name);
        if (!loadResult.has_value()) {
            return Err<PluginPtr>("Failed to load plugin: " + name, loadResult);
        }
        it->second.instance = loadResult.value();
        it->second.isLoaded = true;
    }

    return Ok(it->second.instance);
}

Result<PluginPtr> WidgetFactory::loadPlugin(const std::string& name) {
    // Built-in plugins - these will be implemented as separate includes
    // For now, return error as plugins need to be migrated

    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return Err<PluginPtr>("Unknown plugin: " + name);
    }

    // Dynamic loading from path
    if (!it->second.path.empty()) {
        return loadDynamicPlugin(it->second.path);
    }

    // Built-in plugin creation will be added here after migration
    // if (name == "thorvg") {
    //     return ThorvgPlugin::create(ctx_);
    // }

    return Err<PluginPtr>("Plugin not yet migrated: " + name);
}

Result<PluginPtr> WidgetFactory::loadDynamicPlugin(const std::string& path) {
    // TODO: Implement dynamic plugin loading via dlopen/LoadLibrary
    (void)path;
    return Err<PluginPtr>("Dynamic plugin loading not yet implemented");
}

} // namespace yetty
