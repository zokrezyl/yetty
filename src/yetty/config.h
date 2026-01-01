#pragma once

#include <yetty/result.hpp>
#include <yaml-cpp/yaml.h>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace yetty {

class Config {
public:
    Config();
    ~Config() = default;

    // Factory method following the create pattern
    static Result<Config*> create();

    // Initialize with optional config file, env vars, and command line overrides
    Result<void> init(const std::string& configPath = "",
                      const YAML::Node& cmdOverrides = YAML::Node());

    // Get a value by dotted path (e.g., "plugins.path" or "rendering.damage-tracking")
    // Returns nullopt if key doesn't exist
    template<typename T>
    std::optional<T> get(const std::string& path) const;

    // Get a value with default fallback
    template<typename T>
    T get(const std::string& path, const T& defaultValue) const;

    // Get string list (for colon-separated paths like plugins.path)
    std::vector<std::string> getPathList(const std::string& path) const;

    // Check if a key exists
    bool has(const std::string& path) const;

    // Get the raw YAML node for advanced queries
    const YAML::Node& root() const { return _config; }

    // Helper to get executable directory
    static std::filesystem::path getExecutableDir();

    // Helper to get XDG config path
    static std::filesystem::path getXDGConfigPath();

    // Helper to get default plugin paths (relative to executable)
    static std::vector<std::string> getDefaultPluginPaths();

    // Environment variable prefix
    static constexpr const char* ENV_PREFIX = "YETTY_";

    // Common config keys
    static constexpr const char* KEY_PLUGINS_PATH = "plugins.path";
    static constexpr const char* KEY_RENDERING_DAMAGE_TRACKING = "rendering.damage-tracking";
    static constexpr const char* KEY_RENDERING_SHOW_FPS = "rendering.show-fps";
    static constexpr const char* KEY_SCROLLBACK_LINES = "scrollback.lines";
    static constexpr const char* KEY_DEBUG_DAMAGE_RECTS = "debug.damage-rects";

    // Legacy accessors for backward compatibility
    bool useDamageTracking() const;
    bool showFps() const;
    bool debugDamageRects() const;
    uint32_t scrollbackLines() const;
    std::vector<std::string> pluginPaths() const;

private:
    // Load config from file
    Result<void> loadFile(const std::string& path);

    // Apply environment variable overrides
    void applyEnvOverrides();

    // Apply command line overrides
    void applyOverrides(const YAML::Node& overrides);

    // Get YAML node by dotted path
    YAML::Node getNode(const std::string& path) const;

    // Convert dotted path to env var name (e.g., "plugins.path" -> "YETTY_PLUGINS_PATH")
    static std::string pathToEnvVar(const std::string& path);

    // Parse colon-separated string into vector
    static std::vector<std::string> parsePathList(const std::string& pathStr);

    // Merge YAML nodes (source into target)
    static void mergeNodes(YAML::Node& target, const YAML::Node& source);

    YAML::Node _config;
    bool _initialized = false;
};

// Template implementations
template<typename T>
std::optional<T> Config::get(const std::string& path) const {
    YAML::Node node = getNode(path);
    if (!node || node.IsNull()) {
        return std::nullopt;
    }
    try {
        return node.as<T>();
    } catch (...) {
        return std::nullopt;
    }
}

template<typename T>
T Config::get(const std::string& path, const T& defaultValue) const {
    auto value = get<T>(path);
    return value.value_or(defaultValue);
}

} // namespace yetty
