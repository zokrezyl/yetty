#pragma once

#include "plugin.h"
#include "custom-glyph-plugin.h"
#include "terminal/grid.h"
#include "result.hpp"
#include <webgpu/webgpu.h>
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
    std::string _name;
    BuiltinPluginFactory _factory;
    void* _handle = nullptr;        // For dynamically loaded plugins
    PluginCreateFn _createFn = nullptr;
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
    // widthCells/heightCells: 0 = stretch to edge, negative = termSize - abs(value)
    Result<PluginLayerPtr> createLayer(const std::string& pluginName,
                                        PositionMode mode,
                                        int32_t x, int32_t y,
                                        int32_t widthCells, int32_t heightCells,
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
    void markGridCells(Grid* grid, PluginLayer* layer);
    void clearGridCells(Grid* grid, PluginLayer* layer);

    std::unordered_map<std::string, PluginMeta> pluginMetas_;
    std::unordered_map<std::string, PluginPtr> plugins_;  // Active plugin instances
    uint32_t nextLayerId_ = 1;
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
