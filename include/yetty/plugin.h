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
class Grid;
class Font;
class FontManager;
class Plugin;
class PluginLayer;
class Yetty;

// Yetty shared pointer type (defined here to avoid circular include)
using YettyPtr = std::shared_ptr<Yetty>;

using PluginPtr = std::shared_ptr<Plugin>;
using PluginLayerPtr = std::shared_ptr<PluginLayer>;

// Position mode for plugin layers
enum class PositionMode {
    Absolute,  // Fixed position, doesn't scroll
    Relative   // Relative to cursor when created, scrolls with content
};

// Screen type for plugin layers (main vs alternate screen)
enum class ScreenType {
    Main,       // Normal/primary screen
    Alternate   // Alternate screen (vim, less, htop, etc.)
};

//-----------------------------------------------------------------------------
// PluginLayer - represents a specific layer at a position in the terminal
// Each layer has its own position, size, payload, and per-layer state
//-----------------------------------------------------------------------------
class PluginLayer : public std::enable_shared_from_this<PluginLayer> {
public:
    PluginLayer() = default;
    virtual ~PluginLayer() = default;

    // Initialize this layer with its payload
    virtual Result<void> init(const std::string& payload) = 0;

    // Dispose layer-specific resources
    virtual Result<void> dispose() { return Ok(); }

    // Called every frame (for animation, etc.)
    virtual Result<void> update(double deltaTime) { (void)deltaTime; return Ok(); }

    // Input handling - coordinates are relative to layer's top-left (in screen pixels)
    // Return true if event was consumed
    virtual bool onMouseMove(float localX, float localY) { (void)localX; (void)localY; return false; }
    virtual bool onMouseButton(int button, bool pressed) { (void)button; (void)pressed; return false; }
    virtual bool onMouseScroll(float xoffset, float yoffset, int mods) { (void)xoffset; (void)yoffset; (void)mods; return false; }
    virtual bool onKey(int key, int scancode, int action, int mods) {
        (void)key; (void)scancode; (void)action; (void)mods; return false;
    }
    virtual bool onChar(unsigned int codepoint) { (void)codepoint; return false; }

    // Query if layer wants input
    virtual bool wantsKeyboard() const { return false; }
    virtual bool wantsMouse() const { return false; }

    // Focus state
    bool hasFocus() const { return _has_focus; }
    virtual void setFocus(bool f) { _has_focus = f; }

    // Handle resize (called when cell dimensions change)
    virtual void onResize(uint32_t newPixelWidth, uint32_t newPixelHeight) {
        _pixel_width = newPixelWidth;
        _pixel_height = newPixelHeight;
    }

    // Accessors
    uint32_t getId() const { return _id; }
    void setId(uint32_t id) { _id = id; }

    Plugin* getParent() const { return _parent; }
    void setParent(Plugin* p) { _parent = p; }

    PositionMode getPositionMode() const { return _position_mode; }
    void setPositionMode(PositionMode mode) { _position_mode = mode; }

    ScreenType getScreenType() const { return _screen_type; }
    void setScreenType(ScreenType type) { _screen_type = type; }

    int32_t getX() const { return _x; }
    int32_t getY() const { return _y; }
    void setPosition(int32_t x, int32_t y) { _x = x; _y = y; }

    uint32_t getWidthCells() const { return _width_cells; }
    uint32_t getHeightCells() const { return _height_cells; }
    void setCellSize(uint32_t w, uint32_t h) { _width_cells = w; _height_cells = h; }

    uint32_t getPixelWidth() const { return _pixel_width; }
    uint32_t getPixelHeight() const { return _pixel_height; }
    void setPixelSize(uint32_t w, uint32_t h) { _pixel_width = w; _pixel_height = h; }

    bool isVisible() const { return _visible; }
    void setVisible(bool v) { _visible = v; }

    const std::string& getPayload() const { return _payload; }
    void setPayload(const std::string& p) { _payload = p; }

    bool needsRender() const { return _needs_render; }
    void setNeedsRender(bool v) { _needs_render = v; }

protected:
    uint32_t _id = 0;
    Plugin* _parent = nullptr;
    PositionMode _position_mode = PositionMode::Absolute;
    ScreenType _screen_type = ScreenType::Main;
    int32_t _x = 0;
    int32_t _y = 0;
    uint32_t _width_cells = 1;
    uint32_t _height_cells = 1;
    uint32_t _pixel_width = 0;
    uint32_t _pixel_height = 0;
    bool _visible = true;
    bool _needs_render = true;
    bool _has_focus = false;
    std::string _payload;
};

//-----------------------------------------------------------------------------
// Plugin - represents a plugin TYPE (e.g., "ymery", "shadertoy")
// Holds shared resources (e.g., single ImGui context for all ymery layers)
// Lazily initialized when first layer is created
//-----------------------------------------------------------------------------
class Plugin : public std::enable_shared_from_this<Plugin> {
public:
    virtual ~Plugin() = default;

    // Plugin identification
    virtual const char* pluginName() const = 0;

protected:
    // Constructor - PROTECTED, takes engine shared_ptr which is stored
    // Derived classes pass engine from their create() to constructor
    // Using shared_ptr ensures engine stays alive while plugin exists
    explicit Plugin(YettyPtr engine) noexcept : engine_(std::move(engine)) {}

    // init() - PROTECTED, takes NO arguments, uses stored engine_
    // Called by derived create() after construction
    virtual Result<void> init() noexcept { return Ok(); }

public:

    // Dispose shared resources
    virtual Result<void> dispose() {
        for (auto& layer : _layers) {
            if (layer) {
                if (auto res = layer->dispose(); !res) {
                    return Err<void>("Failed to dispose layer", res);
                }
            }
        }
        _layers.clear();
        return Ok();
    }

    // Create a new layer for this plugin
    virtual Result<PluginLayerPtr> createLayer(const std::string& payload) = 0;

    // Add a layer to this plugin (called by PluginManager after createLayer)
    void addLayer(PluginLayerPtr layer) {
        layer->setParent(this);
        _layers.push_back(layer);
    }

    // Remove a layer by ID
    Result<void> removeLayer(uint32_t id) {
        for (auto it = _layers.begin(); it != _layers.end(); ++it) {
            if ((*it)->getId() == id) {
                if (auto res = (*it)->dispose(); !res) {
                    return Err<void>("Failed to dispose layer " + std::to_string(id), res);
                }
                _layers.erase(it);
                return Ok();
            }
        }
        return Err<void>("Layer not found: " + std::to_string(id));
    }

    // Get a layer by ID
    PluginLayerPtr getLayer(uint32_t id) {
        for (auto& layer : _layers) {
            if (layer->getId() == id) return layer;
        }
        return nullptr;
    }

    // Get all layers
    const std::vector<PluginLayerPtr>& getLayers() const { return _layers; }

    // Update all layers
    virtual Result<void> update(double deltaTime) {
        for (auto& layer : _layers) {
            if (layer->isVisible()) {
                if (auto res = layer->update(deltaTime); !res) {
                    return Err<void>("Failed to update layer", res);
                }
            }
        }
        return Ok();
    }

    // Render all layers of this plugin
    // Subclasses can override to batch rendering (e.g., single ImGui frame for all ymery layers)
    // Uses stored ctx_ from init()
    // isAltScreen: true if currently on alternate screen (vim, less, etc.)
    virtual Result<void> renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                   uint32_t screenWidth, uint32_t screenHeight,
                                   float cellWidth, float cellHeight,
                                   int scrollOffset, uint32_t termRows,
                                   bool isAltScreen = false) = 0;

    // Handle terminal resize - notify all layers
    virtual Result<void> onTerminalResize(uint32_t cellWidth, uint32_t cellHeight) {
        for (auto& layer : _layers) {
            uint32_t newW = layer->getWidthCells() * cellWidth;
            uint32_t newH = layer->getHeightCells() * cellHeight;
            layer->onResize(newW, newH);
            layer->setNeedsRender(true);
        }
        return Ok();
    }

    // Check if plugin is initialized
    bool isInitialized() const { return _initialized; }
    void setInitialized(bool v) { _initialized = v; }

    // Font access for text rendering plugins
    void setFont(Font* font) { _font = font; }
    Font* getFont() const { return _font; }

    // Engine access (for context, fontManager, config, etc.)
    YettyPtr getEngine() const { return engine_; }

protected:
    YettyPtr engine_;
    std::vector<PluginLayerPtr> _layers;
    bool _initialized = false;
    Font* _font = nullptr;
};

// C function types for dynamic loading
using PluginNameFn = const char* (*)();
using PluginCreateFn = Result<PluginPtr> (*)(YettyPtr);

} // namespace yetty
