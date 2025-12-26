#pragma once

#include "Plugin.h"
#include "result.hpp"
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

namespace yetty {

// Forward declarations
class Grid;
struct WebGPUContext;

// Our vendor ID for OSC sequences
constexpr int YETTY_OSC_VENDOR_ID = 99999;

// Factory function type for built-in plugins
using BuiltinPluginFactory = std::function<Result<PluginPtr>()>;

// Metadata for a registered plugin type
struct PluginMeta {
    std::string name;
    BuiltinPluginFactory factory;
    void* handle = nullptr;        // For dynamically loaded plugins
    PluginCreateFn createFn = nullptr;
};

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    // Register a built-in plugin factory
    void registerPlugin(const std::string& name, BuiltinPluginFactory factory);

    // Load plugins from directory (.so files)
    void loadPluginsFromDirectory(const std::string& path);

    // Create a new layer for a plugin type
    Result<PluginLayerPtr> createLayer(const std::string& pluginName,
                                        PositionMode mode,
                                        int32_t x, int32_t y,
                                        uint32_t widthCells, uint32_t heightCells,
                                        const std::string& payload,
                                        Grid* grid,
                                        uint32_t cellWidth, uint32_t cellHeight);

    // Update a layer
    Result<void> updateLayer(uint32_t id, const std::string& payload);

    // Remove a layer
    Result<void> removeLayer(uint32_t id, Grid* grid);

    // Get a layer by ID
    PluginLayerPtr getLayer(uint32_t id);

    // Get all layers (across all plugins)
    std::vector<PluginLayerPtr> getAllLayers() const;

    // Parse OSC sequence
    bool handleOSCSequence(const std::string& sequence,
                          Grid* grid,
                          int cursorCol, int cursorRow,
                          uint32_t cellWidth, uint32_t cellHeight,
                          std::string* response = nullptr,
                          uint32_t* linesToAdvance = nullptr);

    // Update all plugins
    void update(double deltaTime);

    // Render all plugins
    void render(WebGPUContext& ctx, WGPUTextureView targetView,
                uint32_t screenWidth, uint32_t screenHeight,
                float cellWidth, float cellHeight,
                int scrollOffset = 0, uint32_t termRows = 0);

    // Handle terminal resize
    void onTerminalResize(uint32_t newCols, uint32_t newRows,
                          uint32_t cellWidth, uint32_t cellHeight);

    // Handle scroll
    void onScroll(int lines, Grid* grid = nullptr);

    // Input routing
    bool onMouseMove(float pixelX, float pixelY, const Grid* grid,
                     float cellWidth, float cellHeight, int scrollOffset);
    bool onMouseButton(int button, bool pressed, float pixelX, float pixelY,
                       const Grid* grid, float cellWidth, float cellHeight, int scrollOffset);
    bool onMouseScroll(float xoffset, float yoffset, int mods, float pixelX, float pixelY,
                       const Grid* grid, float cellWidth, float cellHeight, int scrollOffset);
    bool onKey(int key, int scancode, int action, int mods);
    bool onChar(unsigned int codepoint);

    void clearFocus();
    PluginLayerPtr getFocusedLayer() const { return focusedLayer_; }

    // Base94 encode/decode
    static std::string base94Decode(const std::string& encoded);
    static std::string base94Encode(const std::string& data);

private:
    // Get or create plugin instance for a type
    Result<PluginPtr> getOrCreatePlugin(const std::string& name);

    // Find layer at grid cell
    PluginLayerPtr layerAtCell(int col, int row, const Grid* grid);
    void markGridCells(Grid* grid, PluginLayer* layer);
    void clearGridCells(Grid* grid, PluginLayer* layer);

    std::unordered_map<std::string, PluginMeta> pluginMetas_;
    std::unordered_map<std::string, PluginPtr> plugins_;  // Active plugin instances
    uint32_t nextLayerId_ = 1;
    std::vector<void*> handles_;
    PluginLayerPtr focusedLayer_;
    float lastMouseX_ = 0;
    float lastMouseY_ = 0;
};

} // namespace yetty
