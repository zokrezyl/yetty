#pragma once

#include "result.hpp"
#include <webgpu/webgpu.h>
#include <string>
#include <cstdint>
#include <memory>

namespace yetty {

// Forward declarations
struct WebGPUContext;
class Grid;

// Position mode for plugins
enum class PositionMode {
    Absolute,  // Fixed position, doesn't scroll
    Relative   // Relative to cursor when created, scrolls with content
};

// Plugin base class - each instance renders itself
// Concrete plugins (ShaderToy, Image, Video, etc.) inherit from this
class Plugin : public std::enable_shared_from_this<Plugin> {
public:
    Plugin() = default;
    virtual ~Plugin() = default;

    // Plugin identification (override in derived classes)
    virtual const char* pluginName() const = 0;

    // Lifecycle
    virtual Result<void> init(const std::string& payload) = 0;
    virtual void dispose() {}

    // Called every frame
    virtual void update(double deltaTime) { (void)deltaTime; }

    // Render the plugin - each instance renders itself
    virtual void render(WebGPUContext& ctx,
                       WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                       uint32_t screenWidth, uint32_t screenHeight,
                       float pixelX, float pixelY, float pixelW, float pixelH) = 0;

    // Handle resize
    virtual void onResize(uint32_t newWidth, uint32_t newHeight) {
        pixelWidth_ = newWidth;
        pixelHeight_ = newHeight;
    }

    // Input handling - coordinates are relative to plugin's top-left
    // Return true if event was consumed
    virtual bool onMouseMove(float localX, float localY) { (void)localX; (void)localY; return false; }
    virtual bool onMouseButton(int button, bool pressed) { (void)button; (void)pressed; return false; }
    virtual bool onMouseScroll(float xoffset, float yoffset) { (void)xoffset; (void)yoffset; return false; }
    virtual bool onKey(int key, int scancode, int action, int mods) {
        (void)key; (void)scancode; (void)action; (void)mods; return false;
    }
    virtual bool onChar(unsigned int codepoint) { (void)codepoint; return false; }

    // Query if plugin wants input (for focus management)
    virtual bool wantsKeyboard() const { return false; }
    virtual bool wantsMouse() const { return false; }

    // Focus state
    bool hasFocus() const { return hasFocus_; }
    void setFocus(bool f) { hasFocus_ = f; }

    // Accessors
    uint32_t getId() const { return id_; }
    void setId(uint32_t id) { id_ = id; }

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

using PluginPtr = std::shared_ptr<Plugin>;

// C function types for dynamic loading
using PluginNameFn = const char* (*)();
using PluginCreateFn = Result<PluginPtr> (*)();

} // namespace yetty
