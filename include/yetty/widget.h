#pragma once

#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <cstdint>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>

// Forward declare libuv types (must be outside yetty namespace)
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

namespace yetty {

// Forward declarations
class WebGPUContext;
class Widget;
class Plugin;

using WidgetPtr = std::shared_ptr<Widget>;

//-----------------------------------------------------------------------------
// Position and Screen types
//-----------------------------------------------------------------------------

enum class PositionMode {
    Absolute,  // Fixed position, doesn't scroll
    Relative   // Relative to cursor when created, scrolls with content
};

enum class ScreenType {
    Main,       // Normal/primary screen
    Alternate   // Alternate screen (vim, less, htop, etc.)
};

//-----------------------------------------------------------------------------
// WidgetParams - parameters for widget creation
//-----------------------------------------------------------------------------

struct WidgetParams {
    PositionMode mode = PositionMode::Relative;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t widthCells = 10;
    uint32_t heightCells = 10;
    uint32_t cellWidth = 0;   // pixels per cell
    uint32_t cellHeight = 0;  // pixels per cell
    uv_loop_t* loop = nullptr;  // libuv event loop
};


//-----------------------------------------------------------------------------
// Widget - base class for all renderable widgets
//
// ## Two-Phase Construction
//
// Widgets use two-phase construction to handle fallible initialization:
//   1. Constructor (PRIVATE) - stores parameters as members, CANNOT fail
//   2. init() (PRIVATE) - NO ARGUMENTS, works on members, CAN fail
//   3. create() (PUBLIC) - static factory, calls constructor + init()
//
// Example subclass:
//
//   class Lottie : public Widget {
//   public:
//       static Result<WidgetPtr> create(WebGPUContext* ctx, const WidgetParams& p,
//                                       const std::string& args, const std::string& payload) {
//           auto w = std::shared_ptr<Lottie>(new Lottie(ctx, p, args, payload));
//           TRY(w->init());
//           return Ok(std::static_pointer_cast<Widget>(w));
//       }
//
//   private:
//       Lottie(WebGPUContext* ctx, const WidgetParams& p,
//              const std::string& args, const std::string& payload)
//           : Widget(ctx, p), _args(args), _payload(payload) {}
//
//       Result<void> init() override;  // NO ARGUMENTS - uses members
//   };
//
//-----------------------------------------------------------------------------
class Widget : public std::enable_shared_from_this<Widget> {
public:
    using Ptr = std::shared_ptr<Widget>;

    virtual ~Widget() = default;

    //-------------------------------------------------------------------------
    // Identity
    //-------------------------------------------------------------------------

    uint32_t id() const { return _id; }
    const std::string& hashId() const { return _hashId; }
    virtual const std::string& name() const { return _name; }
    virtual uint32_t zOrder() const { return _zOrder; }

    //-------------------------------------------------------------------------
    // Lifecycle
    //-------------------------------------------------------------------------

    virtual void start() { _running.store(true); }
    virtual void stop() { _running.store(false); }
    virtual bool isRunning() const { return _running.load(); }
    virtual Result<void> dispose() { return Ok(); }

    //-------------------------------------------------------------------------
    // Rendering
    //-------------------------------------------------------------------------

    /// Phase 1: Pre-render (before shared pass)
    /// @param on - false if widget should release GPU resources (scrolled out or explicitly disabled)
    virtual void prepareFrame(WebGPUContext& ctx, bool on) { (void)ctx; (void)on; }

    /// Render to the given render pass
    /// @param on - false if widget should skip rendering (scrolled out or explicitly disabled)
    virtual Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) = 0;

    virtual void update(double deltaTime) { (void)deltaTime; }

    //-------------------------------------------------------------------------
    // Input handling
    //-------------------------------------------------------------------------

    virtual bool onMouseMove(float localX, float localY) {
        (void)localX; (void)localY; return false;
    }
    virtual bool onMouseButton(int button, bool pressed) {
        (void)button; (void)pressed; return false;
    }
    virtual bool onMouseScroll(float xoffset, float yoffset, int mods) {
        (void)xoffset; (void)yoffset; (void)mods; return false;
    }
    virtual bool onKey(int key, int scancode, int action, int mods) {
        (void)key; (void)scancode; (void)action; (void)mods; return false;
    }
    virtual bool onChar(unsigned int codepoint) {
        (void)codepoint; return false;
    }
    virtual bool wantsKeyboard() const { return false; }
    virtual bool wantsMouse() const { return false; }

    //-------------------------------------------------------------------------
    // Position and size (public getters/setters)
    //-------------------------------------------------------------------------

    // Cell-based position (for grid alignment)
    int32_t getX() const { return _x; }
    int32_t getY() const { return _y; }
    void setPosition(int32_t x, int32_t y) { _x = x; _y = y; _dirty = true; }

    // Pixel-based position (screen coordinates)
    float getPixelX() const { return _pixelX; }
    float getPixelY() const { return _pixelY; }
    void setPixelPosition(float x, float y) { _pixelX = x; _pixelY = y; _dirty = true; }

    uint32_t getWidthCells() const { return _widthCells; }
    uint32_t getHeightCells() const { return _heightCells; }
    void setCellSize(uint32_t w, uint32_t h) { _widthCells = w; _heightCells = h; _dirty = true; }

    uint32_t getPixelWidth() const { return _pixelWidth; }
    uint32_t getPixelHeight() const { return _pixelHeight; }
    void setPixelSize(uint32_t w, uint32_t h) { _pixelWidth = w; _pixelHeight = h; _dirty = true; }

    virtual void onResize(uint32_t newW, uint32_t newH) { _pixelWidth = newW; _pixelHeight = newH; }

    PositionMode getPositionMode() const { return _positionMode; }
    void setPositionMode(PositionMode mode) { _positionMode = mode; }

    ScreenType getScreenType() const { return _screenType; }
    void setScreenType(ScreenType type) { _screenType = type; }

    //-------------------------------------------------------------------------
    // State flags
    //-------------------------------------------------------------------------

    bool isVisible() const { return _visible; }
    void setVisible(bool v) { _visible = v; }

    bool isDirty() const { return _dirty; }
    void setDirty(bool d = true) { _dirty = d; }
    void clearDirty() { _dirty = false; }

    bool hasFocus() const { return _hasFocus; }
    virtual void setFocus(bool f) { _hasFocus = f; }

    //-------------------------------------------------------------------------
    // Setters for factory use
    //-------------------------------------------------------------------------

    void setHashId(const std::string& id) { _hashId = id; }
    void setId(uint32_t id) { _id = id; }
    void setZOrder(uint32_t z) { _zOrder = z; }
    void setName(const std::string& n) { _name = n; }

    const std::string& getPayload() const { return _payload; }
    void setPayload(const std::string& p) { _payload = p; }

    // For re-initialization after setPayload()
    Result<void> reinit() { return init(); }

    Plugin* getParent() const { return _parent; }
    void setParent(Plugin* p) { _parent = p; }

protected:
    //-------------------------------------------------------------------------
    // Constructor (protected - only subclasses can call)
    // Stores parameters, CANNOT fail
    //-------------------------------------------------------------------------
    Widget(WebGPUContext* ctx, const WidgetParams& params)
        : _ctx(ctx)
        , _x(params.x)
        , _y(params.y)
        , _widthCells(params.widthCells)
        , _heightCells(params.heightCells)
        , _pixelWidth(params.widthCells * params.cellWidth)
        , _pixelHeight(params.heightCells * params.cellHeight)
        , _positionMode(params.mode)
        , _id(_nextId++)
    {}

    // Default constructor for subclasses that don't need params yet
    Widget() : _id(_nextId++) {}

    //-------------------------------------------------------------------------
    // init() - NO ARGUMENTS, works on members, CAN fail
    // Override in subclasses to do actual initialization
    //-------------------------------------------------------------------------
    virtual Result<void> init() { return Ok(); }

    //-------------------------------------------------------------------------
    // Member variables (protected - accessible to subclasses)
    //-------------------------------------------------------------------------

    WebGPUContext* _ctx = nullptr;

    uint32_t _id = 0;
    std::string _hashId;
    uint32_t _zOrder = 200;
    std::string _name = "Widget";

    int32_t _x = 0;          // Cell-based X position
    int32_t _y = 0;          // Cell-based Y position
    float _pixelX = 0.0f;    // Pixel-based X position (screen coords)
    float _pixelY = 0.0f;    // Pixel-based Y position (screen coords)
    uint32_t _widthCells = 1;
    uint32_t _heightCells = 1;
    uint32_t _pixelWidth = 0;
    uint32_t _pixelHeight = 0;

    PositionMode _positionMode = PositionMode::Absolute;
    ScreenType _screenType = ScreenType::Main;

    std::atomic<bool> _running{false};
    bool _visible = true;
    bool _hasFocus = false;
    bool _dirty = true;

    std::string _payload;
    std::string _args;
    std::mutex _mutex;

    Plugin* _parent = nullptr;

private:
    static inline uint32_t _nextId = 1;
};

} // namespace yetty
