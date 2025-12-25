#include "PluginManager.h"
#include "terminal/Grid.h"
#include "renderer/WebGPUContext.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <filesystem>
#include <dlfcn.h>

namespace yetty {

PluginManager::PluginManager() {
}

PluginManager::~PluginManager() {
    // Clean up instances first
    for (auto& instance : instances_) {
        if (instance) {
            instance->dispose();
        }
    }
    instances_.clear();
    plugins_.clear();

    // Close dynamic library handles
    for (void* handle : handles_) {
        if (handle) {
            dlclose(handle);
        }
    }
    handles_.clear();
}

void PluginManager::registerPlugin(const std::string& name, BuiltinPluginFactory factory) {
    PluginMeta meta;
    meta.name = name;
    meta.factory = std::move(factory);
    plugins_[name] = std::move(meta);
    std::cout << "Registered built-in plugin: " << name << std::endl;
}

void PluginManager::loadPluginsFromDirectory(const std::string& path) {
    namespace fs = std::filesystem;

    if (!fs::exists(path) || !fs::is_directory(path)) {
        std::cerr << "Plugin directory not found: " << path << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file()) {
            const auto& filePath = entry.path();
            std::string ext = filePath.extension().string();

            // Load shared libraries (.so on Linux, .dylib on macOS)
#ifdef __APPLE__
            if (ext != ".dylib") continue;
#else
            if (ext != ".so") continue;
#endif

            void* handle = dlopen(filePath.c_str(), RTLD_LAZY);
            if (!handle) {
                std::cerr << "Failed to load plugin " << filePath << ": " << dlerror() << std::endl;
                continue;
            }

            auto nameFn = reinterpret_cast<PluginNameFn>(dlsym(handle, "name"));
            auto createFn = reinterpret_cast<PluginCreateFn>(dlsym(handle, "create"));

            if (!nameFn || !createFn) {
                std::cerr << "Plugin " << filePath << " missing name() or create() symbol" << std::endl;
                dlclose(handle);
                continue;
            }

            PluginMeta meta;
            meta.name = nameFn();
            meta.handle = handle;
            meta.createFn = createFn;

            std::cout << "Loaded plugin: " << meta.name << " from " << filePath << std::endl;
            plugins_[meta.name] = std::move(meta);
            handles_.push_back(handle);
        }
    }
}

Result<PluginPtr> PluginManager::createInstance(const std::string& pluginName,
                                                 PositionMode mode,
                                                 int32_t x, int32_t y,
                                                 uint32_t widthCells, uint32_t heightCells,
                                                 const std::string& payload,
                                                 Grid* grid,
                                                 uint32_t cellWidth, uint32_t cellHeight) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) {
        return Err<PluginPtr>("Unknown plugin: " + pluginName);
    }

    const auto& meta = it->second;
    Result<PluginPtr> result;

    // Create instance using factory or create function
    if (meta.factory) {
        result = meta.factory();
    } else if (meta.createFn) {
        result = meta.createFn();
    } else {
        return Err<PluginPtr>("Plugin has no factory: " + pluginName);
    }

    if (!result) {
        return Err<PluginPtr>("Failed to create plugin instance: " + pluginName, result);
    }

    PluginPtr instance = *result;

    // Configure the instance
    instance->setId(nextInstanceId_++);
    instance->setPositionMode(mode);
    instance->setPosition(x, y);
    instance->setCellSize(widthCells, heightCells);
    instance->setPixelSize(widthCells * cellWidth, heightCells * cellHeight);
    instance->setPayload(payload);

    // Initialize with payload
    if (auto initResult = instance->init(payload); !initResult) {
        return Err<PluginPtr>("Plugin initialization failed: " + pluginName, initResult);
    }

    // Mark grid cells
    if (grid) {
        markGridCells(grid, instance.get());
    }

    instances_.push_back(instance);
    return Ok(instance);
}

Result<void> PluginManager::updateInstance(uint32_t id, const std::string& payload) {
    PluginPtr instance = getInstance(id);
    if (!instance) {
        return Err<void>("Plugin instance not found: " + std::to_string(id));
    }

    instance->setPayload(payload);
    instance->setNeedsRender(true);

    // Re-initialize with new payload
    instance->dispose();
    if (auto initResult = instance->init(payload); !initResult) {
        return Err<void>("Plugin re-initialization failed: " + std::to_string(id), initResult);
    }

    return Ok();
}

Result<void> PluginManager::removeInstance(uint32_t id, Grid* grid) {
    for (auto it = instances_.begin(); it != instances_.end(); ++it) {
        if ((*it)->getId() == id) {
            if (grid) {
                clearGridCells(grid, it->get());
            }
            (*it)->dispose();
            instances_.erase(it);
            return Ok();
        }
    }
    return Err<void>("Plugin instance not found: " + std::to_string(id));
}

PluginPtr PluginManager::getInstance(uint32_t id) {
    for (auto& instance : instances_) {
        if (instance->getId() == id) {
            return instance;
        }
    }
    return nullptr;
}

// Helper: find the Nth semicolon and return position after it (or npos if not found)
static size_t findNthSemicolon(const std::string& s, int n) {
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        pos = s.find(';', pos);
        if (pos == std::string::npos) return pos;
        pos++;  // Move past the semicolon
    }
    return pos;
}

// Helper: extract field between semicolon positions
static std::string getField(const std::string& s, int fieldIndex) {
    size_t start = (fieldIndex == 0) ? 0 : findNthSemicolon(s, fieldIndex);
    if (start == std::string::npos) return "";
    size_t end = s.find(';', start);
    if (end == std::string::npos) return s.substr(start);
    return s.substr(start, end - start);
}

bool PluginManager::handleOSCSequence(const std::string& sequence,
                                       Grid* grid,
                                       int cursorCol, int cursorRow,
                                       uint32_t cellWidth, uint32_t cellHeight,
                                       std::string* response) {
    // Format: <vendor_id>;<plugin_id>;<mode>;...
    // Base94 payload may contain ';', so we use fixed field positions.

    std::string vendorStr = getField(sequence, 0);
    if (vendorStr.empty()) return false;

    int vendorId = 0;
    try {
        vendorId = std::stoi(vendorStr);
    } catch (...) {
        return false;
    }

    if (vendorId != YETTY_OSC_VENDOR_ID) {
        return false;
    }

    std::string pluginId = getField(sequence, 1);
    std::string mode = getField(sequence, 2);

    if (pluginId.empty() || mode.empty()) {
        return false;
    }

    if (mode == "D") {
        // Delete: 99999;<plugin>;D;<id>
        std::string idStr = getField(sequence, 3);
        if (idStr.empty()) return false;
        uint32_t instanceId = std::stoul(idStr);
        auto result = removeInstance(instanceId, grid);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        return true;
    }

    if (mode == "U") {
        // Update: 99999;<plugin>;U;<id>;<payload>
        std::string idStr = getField(sequence, 3);
        if (idStr.empty()) return false;
        uint32_t instanceId = std::stoul(idStr);

        size_t payloadStart = findNthSemicolon(sequence, 4);
        std::string encodedPayload = (payloadStart != std::string::npos)
            ? sequence.substr(payloadStart) : "";
        std::string payload = base94Decode(encodedPayload);
        auto result = updateInstance(instanceId, payload);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        return true;
    }

    if (mode == "Q") {
        // Query: 99999;?;Q - returns list of active plugin instances
        if (response) {
            // Build response: OSC sequence with plugin list
            // Format: ESC ] 99999;!;Q;<id>;<plugin>;<x>;<y>;<w>;<h>; ... ST
            std::string result = "\033]";
            result += std::to_string(YETTY_OSC_VENDOR_ID);
            result += ";!;Q";  // '!' indicates this is a response
            for (const auto& instance : instances_) {
                result += ";";
                result += std::to_string(instance->getId());
                result += ",";
                result += instance->pluginName();
                result += ",";
                result += std::to_string(instance->getX());
                result += ",";
                result += std::to_string(instance->getY());
                result += ",";
                result += std::to_string(instance->getWidthCells());
                result += ",";
                result += std::to_string(instance->getHeightCells());
            }
            result += "\033\\";  // String terminator
            *response = result;
        }
        std::cout << "PluginManager: Query - " << instances_.size() << " active instances" << std::endl;
        return true;
    }

    if (mode == "A" || mode == "R") {
        // Create: 99999;<plugin>;<A|R>;<x>;<y>;<w>;<h>;<payload>
        std::string xStr = getField(sequence, 3);
        std::string yStr = getField(sequence, 4);
        std::string wStr = getField(sequence, 5);
        std::string hStr = getField(sequence, 6);

        if (xStr.empty() || yStr.empty() || wStr.empty() || hStr.empty()) {
            return false;
        }

        PositionMode posMode = (mode == "A") ? PositionMode::Absolute : PositionMode::Relative;
        int32_t x = std::stoi(xStr);
        int32_t y = std::stoi(yStr);
        uint32_t w = std::stoul(wStr);
        uint32_t h = std::stoul(hStr);

        size_t payloadStart = findNthSemicolon(sequence, 7);
        std::string encodedPayload = (payloadStart != std::string::npos)
            ? sequence.substr(payloadStart) : "";
        std::string payload = base94Decode(encodedPayload);

        if (posMode == PositionMode::Relative) {
            x += cursorCol;
            y += cursorRow;
        }

        auto result = createInstance(pluginId, posMode, x, y, w, h, payload,
                                      grid, cellWidth, cellHeight);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        std::cout << "Created plugin instance id=" << (*result)->getId() << " plugin=" << pluginId
                  << " at (" << x << "," << y << ") size " << w << "x" << h << std::endl;
        return true;
    }

    return false;
}

void PluginManager::update(double deltaTime) {
    for (auto& instance : instances_) {
        if (instance->isVisible()) {
            instance->update(deltaTime);
        }
    }
}

void PluginManager::render(WebGPUContext& ctx, WGPUTextureView targetView,
                            uint32_t screenWidth, uint32_t screenHeight,
                            float cellWidth, float cellHeight,
                            int scrollOffset, uint32_t termRows) {
    if (!targetView) return;

    for (auto& instance : instances_) {
        if (!instance->isVisible()) continue;

        // Calculate base position
        float pixelX = instance->getX() * cellWidth;
        float pixelY = instance->getY() * cellHeight;
        float pixelW = instance->getWidthCells() * cellWidth;
        float pixelH = instance->getHeightCells() * cellHeight;

        // For Relative plugins, adjust position when viewing scrollback
        // They should appear shifted down by scrollOffset rows
        if (instance->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        // Skip rendering if plugin is entirely off-screen
        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;  // Off-screen, skip render but keep alive
            }
        }

        instance->render(ctx, targetView, ctx.getSurfaceFormat(),
                        screenWidth, screenHeight,
                        pixelX, pixelY, pixelW, pixelH);
    }
}

void PluginManager::onTerminalResize(uint32_t newCols, uint32_t newRows,
                                      uint32_t cellWidth, uint32_t cellHeight) {
    (void)newCols;
    (void)newRows;

    for (auto& instance : instances_) {
        uint32_t newW = instance->getWidthCells() * cellWidth;
        uint32_t newH = instance->getHeightCells() * cellHeight;
        instance->onResize(newW, newH);
        instance->setNeedsRender(true);
    }
}

void PluginManager::onScroll(int lines) {
    for (auto& instance : instances_) {
        if (instance->getPositionMode() == PositionMode::Relative) {
            instance->setPosition(instance->getX(), instance->getY() - lines);
        }
    }
}

void PluginManager::markGridCells(Grid* grid, Plugin* plugin) {
    if (!grid || !plugin) return;

    int32_t startX = plugin->getX();
    int32_t startY = plugin->getY();
    uint32_t w = plugin->getWidthCells();
    uint32_t h = plugin->getHeightCells();
    uint16_t id = static_cast<uint16_t>(plugin->getId());

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            int32_t gridCol = startX + col;
            int32_t gridRow = startY + row;

            if (gridCol >= 0 && gridCol < (int32_t)grid->getCols() &&
                gridRow >= 0 && gridRow < (int32_t)grid->getRows()) {
                grid->setPluginId(gridCol, gridRow, id);
            }
        }
    }
}

void PluginManager::clearGridCells(Grid* grid, Plugin* plugin) {
    if (!grid || !plugin) return;

    int32_t startX = plugin->getX();
    int32_t startY = plugin->getY();
    uint32_t w = plugin->getWidthCells();
    uint32_t h = plugin->getHeightCells();

    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            int32_t gridCol = startX + col;
            int32_t gridRow = startY + row;

            if (gridCol >= 0 && gridCol < (int32_t)grid->getCols() &&
                gridRow >= 0 && gridRow < (int32_t)grid->getRows()) {
                grid->clearPluginId(gridCol, gridRow);
            }
        }
    }
}

// Base94 encoding uses printable ASCII from '!' (33) to '~' (126)
std::string PluginManager::base94Decode(const std::string& encoded) {
    if (encoded.empty()) return "";

    const size_t len = encoded.size();
    std::string result;
    result.resize(len / 2);  // Pre-size for direct access

    const char* src = encoded.data();
    char* dst = &result[0];
    size_t outIdx = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
        unsigned char c1 = src[i] - '!';
        unsigned char c2 = src[i + 1] - '!';
        dst[outIdx++] = static_cast<char>(c1 * 94 + c2);
    }

    result.resize(outIdx);
    return result;
}

std::string PluginManager::base94Encode(const std::string& data) {
    std::string result;
    result.reserve(data.size() * 2);

    for (unsigned char byte : data) {
        int value = byte;
        char c1 = '!' + (value / 94);
        char c2 = '!' + (value % 94);
        result.push_back(c1);
        result.push_back(c2);
    }

    return result;
}

//-----------------------------------------------------------------------------
// Input routing
//-----------------------------------------------------------------------------

PluginPtr PluginManager::pluginAtCell(int col, int row, const Grid* grid) {
    if (!grid) return nullptr;
    if (col < 0 || row < 0) return nullptr;
    if ((uint32_t)col >= grid->getCols() || (uint32_t)row >= grid->getRows()) return nullptr;

    uint16_t pluginId = grid->getPluginId(col, row);
    if (pluginId == 0) {
        return nullptr;
    }

    spdlog::debug("pluginAtCell({},{}): FOUND plugin ID={}", col, row, pluginId);

    return getInstance(pluginId);
}

void PluginManager::clearFocus() {
    if (focusedPlugin_) {
        focusedPlugin_->setFocus(false);
        focusedPlugin_ = nullptr;
    }
}

bool PluginManager::onMouseMove(float pixelX, float pixelY, const Grid* grid,
                                 float cellWidth, float cellHeight, int scrollOffset) {
    lastMouseX_ = pixelX;
    lastMouseY_ = pixelY;

    // Convert pixel coordinates to grid cell
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);

    // Adjust for scroll offset (when viewing scrollback, grid row 0 might be scrollback line N)
    // For input purposes, we're hitting the visible grid, so no adjustment needed here
    // The scrollOffset affects rendering position, not grid lookup

    PluginPtr plugin = pluginAtCell(col, row, grid);
    if (plugin) {
        spdlog::debug("onMouseMove: plugin found at cell ({},{}), wantsMouse={}",
                      col, row, plugin->wantsMouse());
        if (plugin->wantsMouse()) {
            // Calculate local coordinates relative to plugin's top-left
            float pluginPixelX = plugin->getX() * cellWidth;
            float pluginPixelY = plugin->getY() * cellHeight;

            // For Relative plugins, adjust for scroll offset (same as in render)
            if (plugin->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
                pluginPixelY += scrollOffset * cellHeight;
            }

            float localX = pixelX - pluginPixelX;
            float localY = pixelY - pluginPixelY;

            spdlog::debug("onMouseMove: forwarding to plugin, local=({},{})", localX, localY);
            return plugin->onMouseMove(localX, localY);
        }
    }

    return false;
}

bool PluginManager::onMouseButton(int button, bool pressed, float pixelX, float pixelY,
                                   const Grid* grid, float cellWidth, float cellHeight, int scrollOffset) {

    // Convert pixel coordinates to grid cell
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);

    spdlog::debug("onMouseButton: button={} pressed={} at pixel ({},{}) -> cell ({},{})",
                  button, pressed, pixelX, pixelY, col, row);

    PluginPtr plugin = pluginAtCell(col, row, grid);

    // Handle focus changes on mouse press
    if (pressed && button == 0) {  // Left click
        spdlog::debug("onMouseButton: left click, plugin={}, focusedPlugin_={}",
                      (void*)plugin.get(), (void*)focusedPlugin_.get());
        if (plugin != focusedPlugin_) {
            // Notify old focused plugin about focus loss (button -1 signals click outside)
            if (focusedPlugin_) {
                spdlog::debug("onMouseButton: notifying old plugin {} of focus loss", focusedPlugin_->getId());
                focusedPlugin_->onMouseButton(-1, false);
            }
            clearFocus();
            // Set focus for plugins that want keyboard OR mouse
            if (plugin && (plugin->wantsKeyboard() || plugin->wantsMouse())) {
                focusedPlugin_ = plugin;
                plugin->setFocus(true);
                spdlog::debug("onMouseButton: set focus to plugin {}", plugin->getId());
            }
        }
    }

    if (plugin) {
        spdlog::debug("onMouseButton: plugin found, wantsMouse={}", plugin->wantsMouse());
        if (plugin->wantsMouse()) {
            // Calculate local coordinates
            float pluginPixelX = plugin->getX() * cellWidth;
            float pluginPixelY = plugin->getY() * cellHeight;

            // For Relative plugins, adjust for scroll offset (same as in render)
            if (plugin->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
                pluginPixelY += scrollOffset * cellHeight;
            }

            float localX = pixelX - pluginPixelX;
            float localY = pixelY - pluginPixelY;

            // Forward mouse position first (ymery needs this)
            plugin->onMouseMove(localX, localY);
            bool consumed = plugin->onMouseButton(button, pressed);
            spdlog::debug("onMouseButton: plugin consumed={}", consumed);
            return consumed;
        }
    }

    return false;
}

bool PluginManager::onMouseScroll(float xoffset, float yoffset, int mods, float pixelX, float pixelY,
                                   const Grid* grid, float cellWidth, float cellHeight, int scrollOffset) {

    // Convert pixel coordinates to grid cell
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);

    spdlog::debug("onMouseScroll: offset=({},{}) mods={} at pixel ({},{}) -> cell ({},{})",
                  xoffset, yoffset, mods, pixelX, pixelY, col, row);

    PluginPtr plugin = pluginAtCell(col, row, grid);
    if (plugin && plugin->wantsMouse()) {
        spdlog::debug("onMouseScroll: forwarding to plugin {}", plugin->getId());
        bool consumed = plugin->onMouseScroll(xoffset, yoffset, mods);
        spdlog::debug("onMouseScroll: plugin consumed={}", consumed);
        return consumed;
    }

    return false;
}

bool PluginManager::onKey(int key, int scancode, int action, int mods) {
    if (focusedPlugin_ && focusedPlugin_->wantsKeyboard()) {
        return focusedPlugin_->onKey(key, scancode, action, mods);
    }
    return false;
}

bool PluginManager::onChar(unsigned int codepoint) {
    if (focusedPlugin_ && focusedPlugin_->wantsKeyboard()) {
        return focusedPlugin_->onChar(codepoint);
    }
    return false;
}

} // namespace yetty
