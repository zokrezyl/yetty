#pragma once

#include <yetty/renderable.h>
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>

namespace yetty {

// Forward declarations
class WebGPUContext;
class Grid;
class Font;
class FontManager;
class Plugin;
class Widget;
class Yetty;

// Yetty shared pointer type (defined here to avoid circular include)
using YettyPtr = std::shared_ptr<Yetty>;

using PluginPtr = std::shared_ptr<Plugin>;
using WidgetPtr = std::shared_ptr<Widget>;

// Legacy alias for backward compatibility during migration
using PluginLayer = Widget;
using PluginLayerPtr = WidgetPtr;

//-----------------------------------------------------------------------------
// PluginMeta - metadata returned by plugins via meta() function
//-----------------------------------------------------------------------------
struct PluginMeta {
    std::string name;         // Plugin name (required)
    std::string version;      // Version string (e.g., "1.0.0")
    std::string author;       // Author name/email
    std::string description;  // Short description
    std::unordered_map<std::string, std::string> extra;  // Additional metadata
};

//-----------------------------------------------------------------------------
// RenderContext - rendering parameters passed to PluginLayer::render()
// Set by the owner (Terminal/PluginManager) before calling render()
//-----------------------------------------------------------------------------
struct RenderContext {
    WGPUTextureView targetView = nullptr;
    WGPUTextureFormat targetFormat = WGPUTextureFormat_BGRA8Unorm;
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
    int scrollOffset = 0;
    uint32_t termRows = 0;
    bool isAltScreen = false;
    double deltaTime = 0.0;  // Time since last frame
};

// Position mode for widgets
enum class PositionMode {
    Absolute,  // Fixed position, doesn't scroll
    Relative   // Relative to cursor when created, scrolls with content
};

// Screen type for widgets (main vs alternate screen)
enum class ScreenType {
    Main,       // Normal/primary screen
    Alternate   // Alternate screen (vim, less, htop, etc.)
};

//-----------------------------------------------------------------------------
// Widget - a plugin instance rendered at a position in the terminal
//
// Widgets decide internally how to render:
// - Simple drawing: draw directly to the provided render pass
// - Need texture: create/manage own texture, render to it, blit to pass
// - Complex pipeline: manage own render passes, blit result to pass
//-----------------------------------------------------------------------------
class Widget : public Renderable, public std::enable_shared_from_this<Widget> {
public:
    Widget() = default;
    ~Widget() override = default;

    // Renderable interface
    uint32_t id() const override { return _id; }
    uint32_t zOrder() const override { return _zOrder; }
    const std::string& name() const override { return _name; }
    void start() override { _running.store(true); }
    void stop() override { _running.store(false); }
    bool isRunning() const override { return _running.load(); }

    // Legacy render - creates own command encoder (slow, avoid!)
    Result<void> render(WebGPUContext& ctx) override = 0;

    // Batched render - draws into existing render pass
    // Widget decides internally: draw directly, or render to texture and blit
    // Returns true if drew something, false if skipped (off-screen, etc.)
    virtual bool renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) = 0;

    // Initialize this widget with its payload
    virtual Result<void> init(const std::string& payload) = 0;

    // Dispose widget-specific resources
    virtual Result<void> dispose() { return Ok(); }

    // Input handling - coordinates are relative to widget's top-left (in screen pixels)
    virtual bool onMouseMove(float localX, float localY) { (void)localX; (void)localY; return false; }
    virtual bool onMouseButton(int button, bool pressed) { (void)button; (void)pressed; return false; }
    virtual bool onMouseScroll(float xoffset, float yoffset, int mods) { (void)xoffset; (void)yoffset; (void)mods; return false; }
    virtual bool onKey(int key, int scancode, int action, int mods) {
        (void)key; (void)scancode; (void)action; (void)mods; return false;
    }
    virtual bool onChar(unsigned int codepoint) { (void)codepoint; return false; }

    // Query if widget wants input
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
    void setId(uint32_t id) { _id = id; }
    void setZOrder(uint32_t z) { _zOrder = z; }
    void setName(const std::string& n) { _name = n; }

    // Hash ID (8 char nix-style: [a-z0-9]{8})
    const std::string& hashId() const { return _hashId; }
    void setHashId(const std::string& id) { _hashId = id; }

    Plugin* getParent() const { return _parent; }
    void setParent(Plugin* p) { _parent = p; }

    PositionMode getPositionMode() const { return _position_mode; }
    void setPositionMode(PositionMode mode) { _position_mode = mode; }

    ScreenType getScreenType() const { return _screen_type; }
    void setScreenType(ScreenType type) { _screen_type = type; }

    int32_t getX() const { return _x; }
    int32_t getY() const { return _y; }
    void setPosition(int32_t x, int32_t y) {
        if (_x != x || _y != y) {
            _x = x;
            _y = y;
            _dirty = true;  // Mark dirty when position changes
        }
    }

    // Dirty flag for "quiet widgets" optimization
    // Quiet widgets (e.g., static images) only re-render when dirty
    // Animated widgets (e.g., shaders) should mark dirty every frame
    bool isDirty() const { return _dirty; }
    void setDirty(bool d = true) { _dirty = d; }
    void clearDirty() { _dirty = false; }

    uint32_t getWidthCells() const { return _width_cells; }
    uint32_t getHeightCells() const { return _height_cells; }
    void setCellSize(uint32_t w, uint32_t h) {
        if (_width_cells != w || _height_cells != h) {
            _width_cells = w;
            _height_cells = h;
            _dirty = true;
        }
    }

    uint32_t getPixelWidth() const { return _pixel_width; }
    uint32_t getPixelHeight() const { return _pixel_height; }
    void setPixelSize(uint32_t w, uint32_t h) {
        if (_pixel_width != w || _pixel_height != h) {
            _pixel_width = w;
            _pixel_height = h;
            _dirty = true;
        }
    }

    bool isVisible() const { return _visible; }
    void setVisible(bool v) { _visible = v; }

    const std::string& getPayload() const { return _payload; }
    void setPayload(const std::string& p) { _payload = p; }

    // RenderContext - set by owner before calling render()
    const RenderContext& getRenderContext() const { return _render_context; }
    void setRenderContext(const RenderContext& ctx) { _render_context = ctx; }

protected:
    uint32_t _id = 0;
    std::string _hashId;     // 8 char nix-style ID: [a-z0-9]{8}
    uint32_t _zOrder = 200;  // Widgets render above terminal (0)
    std::string _name = "Widget";
    std::atomic<bool> _running{false};
    std::mutex _mutex;

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
    bool _has_focus = false;
    bool _dirty = true;  // Start dirty (needs initial render)
    std::string _payload;
    RenderContext _render_context;
};

//-----------------------------------------------------------------------------
// Plugin - represents a plugin TYPE (e.g., "simple-plot", "shadertoy")
// Plugin is a Renderable for shared resources (shaders, etc.)
// Widgets are separate Renderables for per-instance rendering
//-----------------------------------------------------------------------------
class Plugin : public Renderable, public std::enable_shared_from_this<Plugin> {
public:
    ~Plugin() override = default;

    // Plugin metadata - new API
    // Override this to provide full metadata, or just override pluginName() for legacy
    virtual PluginMeta pluginMeta() const {
        PluginMeta meta;
        meta.name = pluginName();
        return meta;
    }
    
    // Legacy plugin identification - override this or pluginMeta()
    virtual const char* pluginName() const { return "UnnamedPlugin"; }

    // Renderable interface
    uint32_t id() const override { return _pluginId; }
    uint32_t zOrder() const override { return _pluginZOrder; }
    const std::string& name() const override { return _pluginName; }
    void start() override { _running.store(true); }
    void stop() override { _running.store(false); }
    bool isRunning() const override { return _running.load(); }

    // Plugin's render() for shared resources - default does nothing
    Result<void> render(WebGPUContext& ctx) override {
        (void)ctx;
        return Ok();
    }

protected:
    explicit Plugin(YettyPtr engine) noexcept : engine_(std::move(engine)) {}
    virtual Result<void> init() noexcept { return Ok(); }

public:
    // Dispose shared resources
    virtual Result<void> dispose() {
        for (auto& widget : _widgets) {
            if (widget) {
                if (auto res = widget->dispose(); !res) {
                    return Err<void>("Failed to dispose widget", res);
                }
            }
        }
        _widgets.clear();
        return Ok();
    }

    // Create a new widget for this plugin
    // Legacy name is createLayer - both work
    virtual Result<WidgetPtr> createWidget(const std::string& payload) {
        // Default implementation calls legacy createLayer if overridden
        // Subclasses should override createWidget (preferred) or createLayer (legacy)
        return Err<WidgetPtr>("createWidget not implemented");
    }
    
    // Legacy name - override this or createWidget
    virtual Result<WidgetPtr> createLayer(const std::string& payload) {
        return createWidget(payload);
    }

    // Add a widget to this plugin
    void addWidget(WidgetPtr widget) {
        widget->setParent(this);
        _widgets.push_back(widget);
    }
    
    // Legacy alias
    void addLayer(WidgetPtr widget) { addWidget(widget); }

    // Remove a widget by ID
    Result<void> removeWidget(uint32_t id) {
        for (auto it = _widgets.begin(); it != _widgets.end(); ++it) {
            if ((*it)->id() == id) {
                if (auto res = (*it)->dispose(); !res) {
                    return Err<void>("Failed to dispose widget " + std::to_string(id), res);
                }
                _widgets.erase(it);
                return Ok();
            }
        }
        return Err<void>("Widget not found: " + std::to_string(id));
    }
    
    // Legacy alias
    Result<void> removeLayer(uint32_t id) { return removeWidget(id); }

    // Get a widget by ID
    WidgetPtr getWidget(uint32_t id) {
        for (auto& widget : _widgets) {
            if (widget->id() == id) return widget;
        }
        return nullptr;
    }
    
    // Legacy alias
    WidgetPtr getLayer(uint32_t id) { return getWidget(id); }

    // Get all widgets
    const std::vector<WidgetPtr>& getWidgets() const { return _widgets; }
    
    // Legacy alias
    const std::vector<WidgetPtr>& getLayers() const { return _widgets; }

    // Handle terminal resize - notify all widgets
    virtual Result<void> onTerminalResize(uint32_t cellWidth, uint32_t cellHeight) {
        for (auto& widget : _widgets) {
            uint32_t newW = widget->getWidthCells() * cellWidth;
            uint32_t newH = widget->getHeightCells() * cellHeight;
            widget->onResize(newW, newH);
        }
        return Ok();
    }

    // Check if plugin is initialized
    bool isInitialized() const { return _initialized; }
    void setInitialized(bool v) { _initialized = v; }

    // Font access for text rendering plugins
    void setFont(Font* font) { _font = font; }
    Font* getFont() const { return _font; }

    // Engine access
    YettyPtr getEngine() const { return engine_; }

protected:
    YettyPtr engine_;
    std::vector<WidgetPtr> _widgets;
    bool _initialized = false;
    Font* _font = nullptr;

    uint32_t _pluginId = 0;
    uint32_t _pluginZOrder = 150;  // Plugin shared resources render before widgets
    std::string _pluginName = "Plugin";
    std::atomic<bool> _running{false};
    std::mutex _mutex;
};

// C function types for dynamic loading
using PluginMetaFn = PluginMeta (*)();
using PluginCreateFn = Result<PluginPtr> (*)(YettyPtr);

// Legacy alias
using PluginNameFn = const char* (*)();

} // namespace yetty
