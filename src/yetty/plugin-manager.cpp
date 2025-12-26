#include "plugin-manager.h"
#include "terminal/Grid.h"
#include "renderer/WebGPUContext.h"
#include <spdlog/spdlog.h>
#include <iostream>
#include <filesystem>
#include <dlfcn.h>

namespace yetty {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
    // Dispose all plugins (which disposes their layers)
    for (auto& [name, plugin] : plugins_) {
        if (plugin) plugin->dispose();
    }
    plugins_.clear();
    pluginMetas_.clear();

    // Close dynamic library handles
    for (void* handle : handles_) {
        if (handle) dlclose(handle);
    }
    handles_.clear();
}

void PluginManager::registerPlugin(const std::string& name, BuiltinPluginFactory factory) {
    PluginMeta meta;
    meta.name = name;
    meta.factory = std::move(factory);
    pluginMetas_[name] = std::move(meta);
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
            pluginMetas_[meta.name] = std::move(meta);
            handles_.push_back(handle);
        }
    }
}

Result<PluginPtr> PluginManager::getOrCreatePlugin(const std::string& name) {
    // Check if already created
    auto it = plugins_.find(name);
    if (it != plugins_.end()) {
        return Ok(it->second);
    }

    // Find meta
    auto metaIt = pluginMetas_.find(name);
    if (metaIt == pluginMetas_.end()) {
        return Err<PluginPtr>("Unknown plugin: " + name);
    }

    const auto& meta = metaIt->second;
    Result<PluginPtr> result;

    if (meta.factory) {
        result = meta.factory();
    } else if (meta.createFn) {
        result = meta.createFn();
    } else {
        return Err<PluginPtr>("Plugin has no factory: " + name);
    }

    if (!result) {
        return Err<PluginPtr>("Failed to create plugin: " + name, result);
    }

    PluginPtr plugin = *result;
    plugins_[name] = plugin;
    return Ok(plugin);
}

Result<PluginLayerPtr> PluginManager::createLayer(const std::string& pluginName,
                                                   PositionMode mode,
                                                   int32_t x, int32_t y,
                                                   uint32_t widthCells, uint32_t heightCells,
                                                   const std::string& payload,
                                                   Grid* grid,
                                                   uint32_t cellWidth, uint32_t cellHeight) {
    // Get or create the plugin
    auto pluginResult = getOrCreatePlugin(pluginName);
    if (!pluginResult) {
        return Err<PluginLayerPtr>("Failed to get plugin", pluginResult);
    }
    PluginPtr plugin = *pluginResult;

    // Create the layer
    auto layerResult = plugin->createLayer(payload);
    if (!layerResult) {
        return Err<PluginLayerPtr>("Failed to create layer", layerResult);
    }
    PluginLayerPtr layer = *layerResult;

    // Configure the layer
    layer->setId(nextLayerId_++);
    layer->setPositionMode(mode);
    layer->setPosition(x, y);
    layer->setCellSize(widthCells, heightCells);
    layer->setPixelSize(widthCells * cellWidth, heightCells * cellHeight);

    // Add to plugin
    plugin->addLayer(layer);

    // Mark grid cells
    if (grid) {
        markGridCells(grid, layer.get());
    }

    return Ok(layer);
}

Result<void> PluginManager::updateLayer(uint32_t id, const std::string& payload) {
    PluginLayerPtr layer = getLayer(id);
    if (!layer) {
        return Err<void>("Layer not found: " + std::to_string(id));
    }

    layer->setPayload(payload);
    layer->setNeedsRender(true);

    // Re-init
    layer->dispose();
    if (auto res = layer->init(payload); !res) {
        return Err<void>("Layer re-init failed", res);
    }

    return Ok();
}

Result<void> PluginManager::removeLayer(uint32_t id, Grid* grid) {
    for (auto& [name, plugin] : plugins_) {
        PluginLayerPtr layer = plugin->getLayer(id);
        if (layer) {
            if (grid) {
                clearGridCells(grid, layer.get());
            }
            plugin->removeLayer(id);
            return Ok();
        }
    }
    return Err<void>("Layer not found: " + std::to_string(id));
}

PluginLayerPtr PluginManager::getLayer(uint32_t id) {
    for (auto& [name, plugin] : plugins_) {
        if (auto layer = plugin->getLayer(id)) {
            return layer;
        }
    }
    return nullptr;
}

std::vector<PluginLayerPtr> PluginManager::getAllLayers() const {
    std::vector<PluginLayerPtr> layers;
    for (auto& [name, plugin] : plugins_) {
        for (auto& layer : plugin->getLayers()) {
            layers.push_back(layer);
        }
    }
    return layers;
}

// Helper functions for parsing
static size_t findNthSemicolon(const std::string& s, int n) {
    size_t pos = 0;
    for (int i = 0; i < n; i++) {
        pos = s.find(';', pos);
        if (pos == std::string::npos) return pos;
        pos++;
    }
    return pos;
}

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
                                       std::string* response,
                                       uint32_t* linesToAdvance) {
    std::string vendorStr = getField(sequence, 0);
    if (vendorStr.empty()) return false;

    int vendorId = 0;
    try {
        vendorId = std::stoi(vendorStr);
    } catch (...) {
        return false;
    }

    if (vendorId != YETTY_OSC_VENDOR_ID) return false;

    std::string pluginId = getField(sequence, 1);
    std::string mode = getField(sequence, 2);

    if (pluginId.empty() || mode.empty()) return false;

    if (mode == "D") {
        std::string idStr = getField(sequence, 3);
        if (idStr.empty()) return false;
        uint32_t layerId = std::stoul(idStr);
        auto result = removeLayer(layerId, grid);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        return true;
    }

    if (mode == "U") {
        std::string idStr = getField(sequence, 3);
        if (idStr.empty()) return false;
        uint32_t layerId = std::stoul(idStr);

        size_t payloadStart = findNthSemicolon(sequence, 4);
        std::string encodedPayload = (payloadStart != std::string::npos)
            ? sequence.substr(payloadStart) : "";
        std::string payload = base94Decode(encodedPayload);
        auto result = updateLayer(layerId, payload);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        return true;
    }

    if (mode == "Q") {
        if (response) {
            std::string result = "\033]";
            result += std::to_string(YETTY_OSC_VENDOR_ID);
            result += ";!;Q";
            for (auto& layer : getAllLayers()) {
                result += ";";
                result += std::to_string(layer->getId());
                result += ",";
                result += layer->getParent()->pluginName();
                result += ",";
                result += std::to_string(layer->getX());
                result += ",";
                result += std::to_string(layer->getY());
                result += ",";
                result += std::to_string(layer->getWidthCells());
                result += ",";
                result += std::to_string(layer->getHeightCells());
            }
            result += "\033\\";
            *response = result;
        }
        return true;
    }

    if (mode == "A" || mode == "R") {
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

        auto result = createLayer(pluginId, posMode, x, y, w, h, payload,
                                  grid, cellWidth, cellHeight);
        if (!result) {
            std::cerr << "PluginManager: " << error_msg(result) << std::endl;
            return false;
        }
        std::cout << "Created layer id=" << (*result)->getId() << " plugin=" << pluginId
                  << " at (" << x << "," << y << ") size " << w << "x" << h << std::endl;

        if (linesToAdvance && posMode == PositionMode::Relative) {
            *linesToAdvance = h;
        }
        return true;
    }

    return false;
}

void PluginManager::update(double deltaTime) {
    for (auto& [name, plugin] : plugins_) {
        plugin->update(deltaTime);
    }
}

void PluginManager::render(WebGPUContext& ctx, WGPUTextureView targetView,
                            uint32_t screenWidth, uint32_t screenHeight,
                            float cellWidth, float cellHeight,
                            int scrollOffset, uint32_t termRows) {
    if (!targetView) return;

    for (auto& [name, plugin] : plugins_) {
        // Lazy init plugin if needed
        if (!plugin->isInitialized()) {
            if (auto res = plugin->init(&ctx); !res) {
                std::cerr << "Failed to init plugin " << name << ": " << error_msg(res) << std::endl;
                continue;
            }
        }

        plugin->renderAll(ctx, targetView, ctx.getSurfaceFormat(),
                          screenWidth, screenHeight,
                          cellWidth, cellHeight,
                          scrollOffset, termRows);
    }
}

void PluginManager::onTerminalResize(uint32_t newCols, uint32_t newRows,
                                      uint32_t cellWidth, uint32_t cellHeight) {
    (void)newCols;
    (void)newRows;

    for (auto& [name, plugin] : plugins_) {
        plugin->onTerminalResize(cellWidth, cellHeight);
    }
}

void PluginManager::onScroll(int lines, Grid* grid) {
    for (auto& [name, plugin] : plugins_) {
        for (auto& layer : plugin->getLayers()) {
            if (layer->getPositionMode() == PositionMode::Relative) {
                if (grid) {
                    clearGridCells(grid, layer.get());
                }
                layer->setPosition(layer->getX(), layer->getY() - lines);
                if (grid) {
                    markGridCells(grid, layer.get());
                }
            }
        }
    }
}

void PluginManager::markGridCells(Grid* grid, PluginLayer* layer) {
    if (!grid || !layer) return;

    int32_t startX = layer->getX();
    int32_t startY = layer->getY();
    uint32_t w = layer->getWidthCells();
    uint32_t h = layer->getHeightCells();
    uint16_t id = static_cast<uint16_t>(layer->getId());

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

void PluginManager::clearGridCells(Grid* grid, PluginLayer* layer) {
    if (!grid || !layer) return;

    int32_t startX = layer->getX();
    int32_t startY = layer->getY();
    uint32_t w = layer->getWidthCells();
    uint32_t h = layer->getHeightCells();

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

std::string PluginManager::base94Decode(const std::string& encoded) {
    if (encoded.empty()) return "";

    const size_t len = encoded.size();
    std::string result;
    result.resize(len / 2);

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

PluginLayerPtr PluginManager::layerAtCell(int col, int row, const Grid* grid) {
    if (!grid) return nullptr;
    if (col < 0 || row < 0) return nullptr;
    if ((uint32_t)col >= grid->getCols() || (uint32_t)row >= grid->getRows()) return nullptr;

    uint16_t layerId = grid->getPluginId(col, row);
    if (layerId == 0) return nullptr;

    spdlog::debug("layerAtCell({},{}): FOUND layer ID={}", col, row, layerId);
    return getLayer(layerId);
}

void PluginManager::clearFocus() {
    if (focusedLayer_) {
        focusedLayer_->setFocus(false);
        focusedLayer_ = nullptr;
    }
}

bool PluginManager::onMouseMove(float pixelX, float pixelY, const Grid* grid,
                                 float cellWidth, float cellHeight, int scrollOffset) {
    lastMouseX_ = pixelX;
    lastMouseY_ = pixelY;

    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);
    int logicalRow = row - scrollOffset;

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);
    if (layer && layer->wantsMouse()) {
        float layerPixelX = layer->getX() * cellWidth;
        float layerPixelY = layer->getY() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            layerPixelY += scrollOffset * cellHeight;
        }

        float localX = pixelX - layerPixelX;
        float localY = pixelY - layerPixelY;

        return layer->onMouseMove(localX, localY);
    }

    return false;
}

bool PluginManager::onMouseButton(int button, bool pressed, float pixelX, float pixelY,
                                   const Grid* grid, float cellWidth, float cellHeight, int scrollOffset) {
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);
    int logicalRow = row - scrollOffset;

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);

    // Handle focus changes on left click
    if (pressed && button == 0) {
        if (layer != focusedLayer_) {
            if (focusedLayer_) {
                focusedLayer_->onMouseButton(-1, false);
            }
            clearFocus();
            if (layer && (layer->wantsKeyboard() || layer->wantsMouse())) {
                focusedLayer_ = layer;
                layer->setFocus(true);
            }
        }
    }

    if (layer && layer->wantsMouse()) {
        float layerPixelX = layer->getX() * cellWidth;
        float layerPixelY = layer->getY() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            layerPixelY += scrollOffset * cellHeight;
        }

        float localX = pixelX - layerPixelX;
        float localY = pixelY - layerPixelY;

        layer->onMouseMove(localX, localY);
        return layer->onMouseButton(button, pressed);
    }

    return false;
}

bool PluginManager::onMouseScroll(float xoffset, float yoffset, int mods, float pixelX, float pixelY,
                                   const Grid* grid, float cellWidth, float cellHeight, int scrollOffset) {
    int col = static_cast<int>(pixelX / cellWidth);
    int row = static_cast<int>(pixelY / cellHeight);
    int logicalRow = row - scrollOffset;

    PluginLayerPtr layer = layerAtCell(col, logicalRow, grid);
    if (layer && layer->wantsMouse()) {
        return layer->onMouseScroll(xoffset, yoffset, mods);
    }

    return false;
}

bool PluginManager::onKey(int key, int scancode, int action, int mods) {
    if (focusedLayer_ && focusedLayer_->wantsKeyboard()) {
        return focusedLayer_->onKey(key, scancode, action, mods);
    }
    return false;
}

bool PluginManager::onChar(unsigned int codepoint) {
    if (focusedLayer_ && focusedLayer_->wantsKeyboard()) {
        return focusedLayer_->onChar(codepoint);
    }
    return false;
}

} // namespace yetty
