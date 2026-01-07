#pragma once

#include <yetty/plugin.h>
#include <yetty/custom-glyph-plugin.h>
#include <yetty/osc-command.h>
#include "yetty/grid.h"
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

namespace yetty {

// Forward declarations
class Grid;
class WebGPUContext;

// Factory function type for built-in plugins
using BuiltinPluginFactory = std::function<Result<PluginPtr>(YettyPtr)>;

// Internal registry entry for a plugin type (not the same as PluginMeta from plugin.h)
struct PluginRegistryEntry {
    std::string name;
    BuiltinPluginFactory factory;
    void* handle = nullptr;        // For dynamically loaded plugins
    PluginCreateFn createFn = nullptr;
};

class PluginManager {
public:
    using Ptr = std::shared_ptr<PluginManager>;

    static Result<Ptr> create() noexcept;

    ~PluginManager();

    // Non-copyable
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // Register a built-in plugin factory
    void registerPlugin(const std::string& name, BuiltinPluginFactory factory);

    // Load plugins from directory (.so files)
    void loadPluginsFromDirectory(const std::string& path);

    // Create a new widget for a plugin type
    // widthCells/heightCells: 0 = stretch to edge, negative = termSize - abs(value)
    Result<WidgetPtr> createWidget(const std::string& pluginName,
                                    PositionMode mode,
                                    int32_t x, int32_t y,
                                    int32_t widthCells, int32_t heightCells,
                                    const std::string& payload,
                                    Grid* grid,
                                    uint32_t cellWidth, uint32_t cellHeight);
    
    // Legacy alias
    Result<WidgetPtr> createLayer(const std::string& pluginName,
                                   PositionMode mode,
                                   int32_t x, int32_t y,
                                   int32_t widthCells, int32_t heightCells,
                                   const std::string& payload,
                                   Grid* grid,
                                   uint32_t cellWidth, uint32_t cellHeight) {
        return createWidget(pluginName, mode, x, y, widthCells, heightCells, payload, grid, cellWidth, cellHeight);
    }

    // Update a widget (by hash ID)
    Result<void> updateLayer(const std::string& hashId, const std::string& payload);

    // Remove a layer (by hash ID)
    Result<void> removeLayer(const std::string& hashId, Grid* grid);

    // Get a layer by hash ID
    PluginLayerPtr getLayer(const std::string& hashId);

    // Get a layer by numeric ID (internal use)
    PluginLayerPtr getLayerById(uint32_t id);

    // Stop a layer (pause rendering)
    Result<void> stopLayer(const std::string& hashId);
    Result<void> stopLayersByPlugin(const std::string& pluginName);

    // Start a layer (resume rendering)
    Result<void> startLayer(const std::string& hashId);
    Result<void> startLayersByPlugin(const std::string& pluginName);

    // Kill (destroy) layers by plugin type
    Result<void> killLayersByPlugin(const std::string& pluginName, Grid* grid);

    // Get list of available plugin names
    std::vector<std::string> getAvailablePlugins() const;

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
    Result<void> update(double deltaTime);

    // Render all plugins
    Result<void> render(WebGPUContext& ctx, WGPUTextureView targetView,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float cellWidth, float cellHeight,
                        int scrollOffset = 0, uint32_t termRows = 0);

    // Handle terminal resize
    Result<void> onTerminalResize(uint32_t newCols, uint32_t newRows,
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
    PluginLayerPtr getHoveredLayer() const { return hoveredLayer_; }

    // Alternate screen handling
    void onAltScreenChange(bool isAltScreen);
    bool isAltScreen() const { return isAltScreen_; }

    // Font access for text-rendering plugins
    void setFont(Font* font) { font_ = font; }
    Font* getFont() const { return font_; }

    // Engine for plugin initialization
    void setEngine(YettyPtr engine) { engine_ = std::move(engine); }
    YettyPtr getEngine() const { return engine_; }

    // Base94 encode/decode
    static std::string base94Decode(const std::string& encoded);
    static std::string base94Encode(const std::string& data);

    //-------------------------------------------------------------------------
    // Custom Glyph Plugin Support
    //-------------------------------------------------------------------------

    // Register a custom glyph plugin (handles codepoint ranges)
    void registerCustomGlyphPlugin(CustomGlyphPluginPtr plugin);

    // Check if a codepoint has a registered custom glyph plugin
    CustomGlyphPluginPtr getCustomGlyphPluginForCodepoint(uint32_t codepoint);

    // Called when terminal syncs a cell - creates layer if codepoint has custom glyph
    // Returns the reserved glyph index to use, or 0 if no custom glyph
    uint16_t onCellSync(uint32_t col, uint32_t row, uint32_t codepoint, uint32_t widthCells);

    // Called when a cell is cleared/overwritten - removes custom glyph layer
    void onCellClear(uint32_t col, uint32_t row);

    // Render all custom glyph layers (called after grid, before decorator plugins)
    Result<void> renderCustomGlyphs(WebGPUContext& ctx, WGPUTextureView targetView,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset = 0);

    // Update custom glyph plugins (for animation)
    void updateCustomGlyphs(double deltaTime);

private:
    PluginManager() noexcept;
    Result<void> init() noexcept;

    // Debug frame rendering
    Result<void> initFrameRenderer(WGPUDevice device, WGPUTextureFormat format);
    void renderFrame(WebGPUContext& ctx, WGPUTextureView targetView,
                     uint32_t screenWidth, uint32_t screenHeight,
                     float x, float y, float w, float h,
                     float r, float g, float b, float a);
    // Get or create plugin instance for a type
    Result<PluginPtr> getOrCreatePlugin(const std::string& name);

    // Find layer at grid cell
    PluginLayerPtr layerAtCell(int col, int row, const Grid* grid);
    void markGridCells(Grid* grid, Widget* widget);
    void clearGridCells(Grid* grid, Widget* widget);

    std::unordered_map<std::string, PluginRegistryEntry> pluginRegistry_;
    std::unordered_map<std::string, PluginPtr> plugins_;  // Active plugin instances
    std::unordered_map<std::string, WidgetPtr> widgetsByHashId_;  // Hash ID -> widget
    uint32_t nextLayerId_ = 1;
    OscCommandParser oscParser_;  // OSC command parser
    Font* font_ = nullptr;
    YettyPtr engine_;
    std::vector<void*> handles_;
    PluginLayerPtr focusedLayer_;
    PluginLayerPtr hoveredLayer_;
    float lastMouseX_ = 0;
    float lastMouseY_ = 0;
    bool isAltScreen_ = false;

    // Debug frame rendering resources
    WGPURenderPipeline framePipeline_ = nullptr;
    WGPUBuffer frameUniformBuffer_ = nullptr;
    WGPUBindGroup frameBindGroup_ = nullptr;
    WGPUBindGroupLayout frameBindGroupLayout_ = nullptr;
    bool frameRendererInitialized_ = false;

    // Custom glyph plugin support
    std::vector<CustomGlyphPluginPtr> customGlyphPlugins_;
    uint16_t nextCustomGlyphIndex_ = GLYPH_CUSTOM_START;

    // Map from grid position to custom glyph index for tracking
    std::unordered_map<uint64_t, uint16_t> customGlyphPositions_;

    // Helper to make position key
    static uint64_t makePositionKey(uint32_t col, uint32_t row) {
        return (static_cast<uint64_t>(row) << 32) | col;
    }
};

} // namespace yetty
