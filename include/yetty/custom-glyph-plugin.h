#pragma once

#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

namespace yetty {

// Forward declarations
class WebGPUContext;

//-----------------------------------------------------------------------------
// CodepointRange - defines a range of Unicode codepoints a plugin handles
//-----------------------------------------------------------------------------
struct CodepointRange {
    uint32_t start;
    uint32_t end;  // inclusive

    bool contains(uint32_t codepoint) const {
        return codepoint >= start && codepoint <= end;
    }
};

//-----------------------------------------------------------------------------
// CustomGlyphWidget - represents a single glyph instance at a grid position
// Created automatically when a registered codepoint appears in the terminal
//-----------------------------------------------------------------------------
class CustomGlyphWidget {
public:
    virtual ~CustomGlyphWidget() = default;

    // Initialize this widget for a specific codepoint
    virtual Result<void> init(uint32_t codepoint) = 0;

    // Called each frame for animation
    virtual void update(double deltaTime) = 0;

    // Render the glyph at the given pixel position
    // Note: The widget itself doesn't create WebGPU resources - uses shared resources from plugin
    virtual Result<void> render(WebGPUContext& ctx,
                                 WGPUTextureView targetView,
                                 WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float pixelX, float pixelY,
                                 float pixelW, float pixelH) = 0;

    // Cleanup
    virtual void dispose() = 0;

    // Position accessors
    uint32_t getCol() const { return _col; }
    uint32_t getRow() const { return _row; }
    void setPosition(uint32_t col, uint32_t row) { _col = col; _row = row; }

    // Codepoint this widget renders
    uint32_t getCodepoint() const { return _codepoint; }
    void setCodepoint(uint32_t cp) { _codepoint = cp; }

    // Size in cells (usually 1x1 or 2x1 for wide chars)
    uint32_t getWidthCells() const { return _widthCells; }
    uint32_t getHeightCells() const { return _heightCells; }
    void setCellSize(uint32_t w, uint32_t h) { _widthCells = w; _heightCells = h; }

    // Visibility (for scrollback handling)
    bool isVisible() const { return _visible; }
    void setVisible(bool v) { _visible = v; }

    // Animation time
    float getTime() const { return _time; }

protected:
    uint32_t _col = 0;
    uint32_t _row = 0;
    uint32_t _codepoint = 0;
    uint32_t _widthCells = 1;
    uint32_t _heightCells = 1;
    bool _visible = true;
    float _time = 0.0f;
};

using CustomGlyphWidgetPtr = std::shared_ptr<CustomGlyphWidget>;

//-----------------------------------------------------------------------------
// CustomGlyphPlugin - base class for custom glyph rendering plugins
// Manages shared resources and widgets for a set of codepoint ranges
//-----------------------------------------------------------------------------
class CustomGlyphPlugin : public std::enable_shared_from_this<CustomGlyphPlugin> {
public:
    virtual ~CustomGlyphPlugin() = default;

    // Plugin identification
    virtual const char* pluginName() const = 0;

    // Which codepoint ranges this plugin handles
    virtual std::vector<CodepointRange> getCodepointRanges() const = 0;

    // Initialize shared resources (pipelines, etc.)
    virtual Result<void> init(WebGPUContext* ctx) = 0;

    // Create a new widget for a codepoint
    virtual Result<CustomGlyphWidgetPtr> createWidget(uint32_t codepoint) = 0;

    // Render all widgets (can batch for efficiency)
    // Uses stored _ctx from init()
    virtual Result<void> renderAll(WGPUTextureView targetView,
                                    WGPUTextureFormat targetFormat,
                                    uint32_t screenWidth, uint32_t screenHeight,
                                    float cellWidth, float cellHeight,
                                    int scrollOffset = 0) = 0;

    // Update all widgets (for animation)
    virtual void update(double deltaTime) = 0;

    // Cleanup
    virtual void dispose() = 0;

    // Check if this plugin handles a codepoint
    bool handlesCodepoint(uint32_t codepoint) const {
        for (const auto& range : getCodepointRanges()) {
            if (range.contains(codepoint)) {
                return true;
            }
        }
        return false;
    }

    // Widget management
    void addWidget(CustomGlyphWidgetPtr widget);
    void removeWidgetAt(uint32_t col, uint32_t row);
    CustomGlyphWidgetPtr getWidgetAt(uint32_t col, uint32_t row) const;
    const std::vector<CustomGlyphWidgetPtr>& getWidgets() const { return _widgets; }
    void clearWidgets();

    // Check if initialized
    bool isInitialized() const { return _initialized; }
    void setInitialized(bool v) { _initialized = v; }

    // Context access
    WebGPUContext* getContext() const { return _ctx; }

protected:
    WebGPUContext* _ctx = nullptr;
    std::vector<CustomGlyphWidgetPtr> _widgets;
    bool _initialized = false;
};

using CustomGlyphPluginPtr = std::shared_ptr<CustomGlyphPlugin>;

} // namespace yetty
