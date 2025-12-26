#pragma once

#include "result.hpp"
#include <webgpu/webgpu.h>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

namespace yetty {

// Forward declarations
struct WebGPUContext;
class Grid;
class Plugin;
class PluginLayer;

using PluginPtr = std::shared_ptr<Plugin>;
using PluginLayerPtr = std::shared_ptr<PluginLayer>;

// Position mode for plugin layers
enum class PositionMode {
    Absolute,  // Fixed position, doesn't scroll
    Relative   // Relative to cursor when created, scrolls with content
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
    virtual void dispose() {}

    // Called every frame (for animation, etc.)
    virtual void update(double deltaTime) { (void)deltaTime; }

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
    bool hasFocus() const { return hasFocus_; }
    virtual void setFocus(bool f) { hasFocus_ = f; }

    // Handle resize (called when cell dimensions change)
    virtual void onResize(uint32_t newPixelWidth, uint32_t newPixelHeight) {
        pixelWidth_ = newPixelWidth;
        pixelHeight_ = newPixelHeight;
    }

    // Accessors
    uint32_t getId() const { return id_; }
    void setId(uint32_t id) { id_ = id; }

    Plugin* getParent() const { return parent_; }
    void setParent(Plugin* p) { parent_ = p; }

    PositionMode getPositionMode() const { return positionMode_; }
    void setPositionMode(PositionMode mode) { positionMode_ = mode; }

    int32_t getX() const { return x_; }
    int32_t getY() const { return y_; }
    void setPosition(int32_t x, int32_t y) { x_ = x; y_ = y; }

    uint32_t getWidthCells() const { return widthCells_; }
    uint32_t getHeightCells() const { return heightCells_; }
    void setCellSize(uint32_t w, uint32_t h) { widthCells_ = w; heightCells_ = h; }

    uint32_t getPixelWidth() const { return pixelWidth_; }
    uint32_t getPixelHeight() const { return pixelHeight_; }
    void setPixelSize(uint32_t w, uint32_t h) { pixelWidth_ = w; pixelHeight_ = h; }

    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    const std::string& getPayload() const { return payload_; }
    void setPayload(const std::string& p) { payload_ = p; }

    bool needsRender() const { return needsRender_; }
    void setNeedsRender(bool v) { needsRender_ = v; }

protected:
    uint32_t id_ = 0;
    Plugin* parent_ = nullptr;
    PositionMode positionMode_ = PositionMode::Absolute;
    int32_t x_ = 0;
    int32_t y_ = 0;
    uint32_t widthCells_ = 1;
    uint32_t heightCells_ = 1;
    uint32_t pixelWidth_ = 0;
    uint32_t pixelHeight_ = 0;
    bool visible_ = true;
    bool needsRender_ = true;
    bool hasFocus_ = false;
    std::string payload_;
};

//-----------------------------------------------------------------------------
// Plugin - represents a plugin TYPE (e.g., "ymery", "shadertoy")
// Holds shared resources (e.g., single ImGui context for all ymery layers)
// Lazily initialized when first layer is created
//-----------------------------------------------------------------------------
class Plugin : public std::enable_shared_from_this<Plugin> {
public:
    Plugin() = default;
    virtual ~Plugin() = default;

    // Plugin identification
    virtual const char* pluginName() const = 0;

    // Initialize shared resources (called lazily on first layer creation)
    // ctx may be nullptr if called before first render
    virtual Result<void> init(WebGPUContext* ctx) { (void)ctx; return Ok(); }

    // Dispose shared resources
    virtual void dispose() {
        for (auto& layer : layers_) {
            if (layer) layer->dispose();
        }
        layers_.clear();
    }

    // Create a new layer for this plugin
    virtual Result<PluginLayerPtr> createLayer(const std::string& payload) = 0;

    // Add a layer to this plugin (called by PluginManager after createLayer)
    void addLayer(PluginLayerPtr layer) {
        layer->setParent(this);
        layers_.push_back(layer);
    }

    // Remove a layer by ID
    bool removeLayer(uint32_t id) {
        for (auto it = layers_.begin(); it != layers_.end(); ++it) {
            if ((*it)->getId() == id) {
                (*it)->dispose();
                layers_.erase(it);
                return true;
            }
        }
        return false;
    }

    // Get a layer by ID
    PluginLayerPtr getLayer(uint32_t id) {
        for (auto& layer : layers_) {
            if (layer->getId() == id) return layer;
        }
        return nullptr;
    }

    // Get all layers
    const std::vector<PluginLayerPtr>& getLayers() const { return layers_; }

    // Update all layers
    virtual void update(double deltaTime) {
        for (auto& layer : layers_) {
            if (layer->isVisible()) {
                layer->update(deltaTime);
            }
        }
    }

    // Render all layers of this plugin
    // Subclasses can override to batch rendering (e.g., single ImGui frame for all ymery layers)
    virtual void renderAll(WebGPUContext& ctx,
                           WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset, uint32_t termRows) = 0;

    // Handle terminal resize - notify all layers
    virtual void onTerminalResize(uint32_t cellWidth, uint32_t cellHeight) {
        for (auto& layer : layers_) {
            uint32_t newW = layer->getWidthCells() * cellWidth;
            uint32_t newH = layer->getHeightCells() * cellHeight;
            layer->onResize(newW, newH);
            layer->setNeedsRender(true);
        }
    }

    // Check if plugin is initialized
    bool isInitialized() const { return initialized_; }
    void setInitialized(bool v) { initialized_ = v; }

protected:
    std::vector<PluginLayerPtr> layers_;
    bool initialized_ = false;
};

// C function types for dynamic loading
using PluginNameFn = const char* (*)();
using PluginCreateFn = Result<PluginPtr> (*)();

} // namespace yetty
