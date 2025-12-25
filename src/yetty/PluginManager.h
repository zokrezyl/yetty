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

// Our vendor ID for OSC sequences (to avoid conflicts with other terminals)
// Format: ESC ] 99999 ; <plugin> ; <mode> ; <params> ; <payload> ST
constexpr int YETTY_OSC_VENDOR_ID = 99999;

// Factory function type for built-in plugins
using BuiltinPluginFactory = std::function<Result<PluginPtr>()>;

// Metadata for a loaded/registered plugin
struct PluginMeta {
    std::string name;
    BuiltinPluginFactory factory;  // For built-in plugins
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

    // Create a new plugin instance
    Result<PluginPtr> createInstance(const std::string& pluginName,
                                     PositionMode mode,
                                     int32_t x, int32_t y,
                                     uint32_t widthCells, uint32_t heightCells,
                                     const std::string& payload,
                                     Grid* grid,
                                     uint32_t cellWidth, uint32_t cellHeight);

    // Update a plugin instance
    Result<void> updateInstance(uint32_t id, const std::string& payload);

    // Remove a plugin instance
    Result<void> removeInstance(uint32_t id, Grid* grid);

    // Get a plugin instance by ID
    PluginPtr getInstance(uint32_t id);

    // Get all instances
    const std::vector<PluginPtr>& getInstances() const { return instances_; }

    // Parse OSC sequence and create/update/delete/query plugin
    // Returns true if sequence was handled
    // If response is non-null and a query is made, response is filled with the reply
    bool handleOSCSequence(const std::string& sequence,
                          Grid* grid,
                          int cursorCol, int cursorRow,
                          uint32_t cellWidth, uint32_t cellHeight,
                          std::string* response = nullptr);

    // Update all plugin instances (called each frame)
    void update(double deltaTime);

    // Render all plugin instances
    // scrollOffset: lines scrolled back (0 = at bottom)
    // termRows: number of visible terminal rows
    void render(WebGPUContext& ctx, WGPUTextureView targetView,
                uint32_t screenWidth, uint32_t screenHeight,
                float cellWidth, float cellHeight,
                int scrollOffset = 0, uint32_t termRows = 0);

    // Handle terminal resize
    void onTerminalResize(uint32_t newCols, uint32_t newRows,
                          uint32_t cellWidth, uint32_t cellHeight);

    // Handle scroll (for relative positioned plugins)
    void onScroll(int lines);

    // Input routing - returns true if event was consumed by a plugin
    // pixelX/Y are screen coordinates, cellWidth/Height for grid lookup
    bool onMouseMove(float pixelX, float pixelY, const Grid* grid,
                     float cellWidth, float cellHeight, int scrollOffset);
    bool onMouseButton(int button, bool pressed, float pixelX, float pixelY,
                       const Grid* grid, float cellWidth, float cellHeight, int scrollOffset);
    bool onMouseScroll(float xoffset, float yoffset, int mods, float pixelX, float pixelY,
                       const Grid* grid, float cellWidth, float cellHeight, int scrollOffset);
    bool onKey(int key, int scancode, int action, int mods);
    bool onChar(unsigned int codepoint);

    // Clear focus from all plugins
    void clearFocus();

    // Get currently focused plugin
    PluginPtr getFocusedPlugin() const { return focusedPlugin_; }

    // Utility: Base94 encode/decode
    static std::string base94Decode(const std::string& encoded);
    static std::string base94Encode(const std::string& data);

private:
    // Find plugin at grid cell (returns nullptr if no plugin)
    PluginPtr pluginAtCell(int col, int row, const Grid* grid);
    void markGridCells(Grid* grid, Plugin* plugin);
    void clearGridCells(Grid* grid, Plugin* plugin);

    std::unordered_map<std::string, PluginMeta> plugins_;
    std::vector<PluginPtr> instances_;
    uint32_t nextInstanceId_ = 1;  // 0 is reserved for "no plugin"
    std::vector<void*> handles_;    // For cleanup of dlopen handles
    PluginPtr focusedPlugin_;       // Plugin that receives keyboard input
    float lastMouseX_ = 0;          // Last mouse position for hover tracking
    float lastMouseY_ = 0;
};

} // namespace yetty
