#include "yetty/config.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace yetty {

Config::Config() {
    // Set defaults
    _config["plugins"]["path"] = "";  // Will be filled with default paths
    _config["rendering"]["damage-tracking"] = true;
    _config["rendering"]["show-fps"] = true;
    _config["scrollback"]["lines"] = 10000;
    _config["debug"]["damage-rects"] = false;
}

Result<Config*> Config::create() {
    Config* config = new Config();
    // Constructor doesn't throw, init() will be called separately
    return Ok(config);
}

Result<void> Config::init(const std::string& configPath, const YAML::Node& cmdOverrides) {
    // Priority (lowest to highest):
    // 1. Defaults (already set in constructor)
    // 2. Config file
    // 3. Environment variables
    // 4. Command line arguments

    // Determine config file path
    std::string effectivePath = configPath;
    if (effectivePath.empty()) {
        // Check XDG config
        auto xdgPath = getXDGConfigPath();
        if (std::filesystem::exists(xdgPath)) {
            effectivePath = xdgPath.string();
        }
    }

    // Load config file if exists
    if (!effectivePath.empty()) {
        if (auto res = loadFile(effectivePath); !res) {
            // Log warning but continue - config file is optional
            spdlog::warn("Failed to load config file {}: {}", effectivePath, error_msg(res));
        } else {
            spdlog::info("Loaded config from: {}", effectivePath);
        }
    }

    // Apply environment variable overrides
    applyEnvOverrides();

    // Apply command line overrides
    if (cmdOverrides && !cmdOverrides.IsNull()) {
        applyOverrides(cmdOverrides);
    }

    // If no plugin paths configured, use defaults
    auto paths = pluginPaths();
    if (paths.empty()) {
        auto defaults = getDefaultPluginPaths();
        std::string pathStr;
        for (size_t i = 0; i < defaults.size(); ++i) {
            if (i > 0) pathStr += ":";
            pathStr += defaults[i];
        }
        _config["plugins"]["path"] = pathStr;
    }

    _initialized = true;
    return Ok();
}

Result<void> Config::loadFile(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return Err<void>("Cannot open config file: " + path);
        }

        YAML::Node fileConfig = YAML::Load(file);
        if (fileConfig && !fileConfig.IsNull()) {
            mergeNodes(_config, fileConfig);
        }
        return Ok();
    } catch (const YAML::Exception& e) {
        return Err<void>("YAML parse error: " + std::string(e.what()));
    }
}

void Config::applyEnvOverrides() {
    // Map of config paths to their environment variable overrides
    const std::vector<std::pair<std::string, std::string>> envMappings = {
        {"plugins.path", "YETTY_PLUGINS_PATH"},
        {"rendering.damage-tracking", "YETTY_RENDERING_DAMAGE_TRACKING"},
        {"rendering.show-fps", "YETTY_RENDERING_SHOW_FPS"},
        {"scrollback.lines", "YETTY_SCROLLBACK_LINES"},
        {"debug.damage-rects", "YETTY_DEBUG_DAMAGE_RECTS"},
    };

    for (const auto& [path, envVar] : envMappings) {
        const char* value = std::getenv(envVar.c_str());
        if (value) {
            // Parse the path and set the value
            std::vector<std::string> parts;
            std::stringstream ss(path);
            std::string part;
            while (std::getline(ss, part, '.')) {
                parts.push_back(part);
            }

            // Navigate to the parent node
            YAML::Node current = _config;
            for (size_t i = 0; i < parts.size() - 1; ++i) {
                current = current[parts[i]];
            }

            // Try to infer the type from existing value
            YAML::Node existing = current[parts.back()];
            if (existing && existing.IsScalar()) {
                try {
                    // Try bool first
                    if (std::string(value) == "true" || std::string(value) == "1") {
                        current[parts.back()] = true;
                    } else if (std::string(value) == "false" || std::string(value) == "0") {
                        current[parts.back()] = false;
                    } else {
                        // Try int
                        try {
                            int intVal = std::stoi(value);
                            current[parts.back()] = intVal;
                        } catch (...) {
                            // Fallback to string
                            current[parts.back()] = std::string(value);
                        }
                    }
                } catch (...) {
                    current[parts.back()] = std::string(value);
                }
            } else {
                current[parts.back()] = std::string(value);
            }

            spdlog::debug("Config override from env: {}={}", envVar, value);
        }
    }
}

void Config::applyOverrides(const YAML::Node& overrides) {
    if (overrides && !overrides.IsNull()) {
        mergeNodes(_config, overrides);
    }
}

YAML::Node Config::getNode(const std::string& path) const {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string part;
    while (std::getline(ss, part, '.')) {
        parts.push_back(part);
    }

    YAML::Node current = YAML::Clone(_config);
    for (const auto& p : parts) {
        if (!current || !current[p]) {
            return YAML::Node();
        }
        current = current[p];
    }
    return current;
}

std::string Config::pathToEnvVar(const std::string& path) {
    std::string result = ENV_PREFIX;
    for (char c : path) {
        if (c == '.' || c == '-') {
            result += '_';
        } else {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    return result;
}

std::vector<std::string> Config::parsePathList(const std::string& pathStr) {
    std::vector<std::string> result;
    if (pathStr.empty()) {
        return result;
    }

    std::stringstream ss(pathStr);
    std::string item;
    while (std::getline(ss, item, ':')) {
        if (!item.empty()) {
            result.push_back(item);
        }
    }
    return result;
}

void Config::mergeNodes(YAML::Node& target, const YAML::Node& source) {
    if (!source || source.IsNull()) {
        return;
    }

    if (source.IsMap()) {
        for (const auto& pair : source) {
            std::string key = pair.first.as<std::string>();
            if (pair.second.IsMap() && target[key] && target[key].IsMap()) {
                // Recursively merge maps
                YAML::Node targetChild = target[key];
                mergeNodes(targetChild, pair.second);
            } else {
                // Override the value
                target[key] = YAML::Clone(pair.second);
            }
        }
    } else {
        target = YAML::Clone(source);
    }
}

std::filesystem::path Config::getExecutableDir() {
#ifdef __linux__
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::filesystem::path(path).parent_path();
    }
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        char realPath[PATH_MAX];
        if (realpath(path, realPath)) {
            return std::filesystem::path(realPath).parent_path();
        }
    }
#elif defined(_WIN32)
    char path[MAX_PATH];
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) > 0) {
        return std::filesystem::path(path).parent_path();
    }
#endif
    // Fallback to current directory
    return std::filesystem::current_path();
}

std::filesystem::path Config::getXDGConfigPath() {
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path configDir;

    if (xdgConfig && xdgConfig[0] != '\0') {
        configDir = xdgConfig;
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            configDir = std::filesystem::path(home) / ".config";
        } else {
            configDir = "/tmp";
        }
    }

    return configDir / "yetty" / "config.yaml";
}

std::vector<std::string> Config::getDefaultPluginPaths() {
    auto exeDir = getExecutableDir();
    return {
        (exeDir / "plugins").string()
    };
}

std::vector<std::string> Config::getPathList(const std::string& path) const {
    auto node = getNode(path);
    if (!node || node.IsNull()) {
        return {};
    }

    if (node.IsSequence()) {
        std::vector<std::string> result;
        for (const auto& item : node) {
            result.push_back(item.as<std::string>());
        }
        return result;
    }

    if (node.IsScalar()) {
        return parsePathList(node.as<std::string>());
    }

    return {};
}

bool Config::has(const std::string& path) const {
    YAML::Node node = getNode(path);
    return node && !node.IsNull();
}

// Legacy accessors
bool Config::useDamageTracking() const {
    return get<bool>(KEY_RENDERING_DAMAGE_TRACKING, true);
}

bool Config::showFps() const {
    return get<bool>(KEY_RENDERING_SHOW_FPS, true);
}

bool Config::debugDamageRects() const {
    return get<bool>(KEY_DEBUG_DAMAGE_RECTS, false);
}

uint32_t Config::scrollbackLines() const {
    return get<uint32_t>(KEY_SCROLLBACK_LINES, 10000);
}

std::vector<std::string> Config::pluginPaths() const {
    return getPathList(KEY_PLUGINS_PATH);
}

} // namespace yetty
