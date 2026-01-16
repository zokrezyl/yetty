#pragma once

//=============================================================================
// Yetty Engine Class
//
// Main application engine that owns all core components and manages the
// application lifecycle. Replaces the monolithic main.cpp AppState.
//
// Supports multiple platforms via conditional compilation:
// - Desktop (Linux/macOS/Windows): GLFW-based windowing
// - Android: Native Activity with android_native_app_glue
// - Web: Emscripten with GLFW (emulated)
//=============================================================================

#include <yetty/webgpu-context.h>
#include <yetty/result.hpp>
#include <yetty/font.h>
#include <yetty/font-manager.h>
#include <yetty/widget.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <webgpu/webgpu.h>

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#else
#include <GLFW/glfw3.h>
#endif

#if YETTY_WEB
#include <emscripten.h>
#endif

#if !YETTY_WEB && !defined(__ANDROID__)
#include <uv.h>
#endif

namespace yetty {

// Forward declarations
class Config;
class GridRenderer;
class Terminal;
class RemoteTerminal;
class WidgetFactory;
class Grid;
class InputHandler;
class ShaderManager;
class CursorRenderer;

//-----------------------------------------------------------------------------
// GPU Resource entries (per-renderable namespace)
//-----------------------------------------------------------------------------
struct ShaderResource {
    WGPUShaderModule module = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPUPipelineLayout pipelineLayout = nullptr;
    std::string vertexEntry;
    std::string fragmentEntry;
};

struct TextureResource {
    WGPUTexture texture = nullptr;
    WGPUTextureView view = nullptr;
    WGPUSampler sampler = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
};

struct BufferResource {
    WGPUBuffer buffer = nullptr;
    uint64_t size = 0;
    uint32_t usage = 0;
};

//-----------------------------------------------------------------------------
// Yetty - Main application engine
//-----------------------------------------------------------------------------
class Yetty : public std::enable_shared_from_this<Yetty> {
public:
    using Ptr = std::shared_ptr<Yetty>;

    ~Yetty();

    // Factory methods - the only way to create
#if defined(__ANDROID__)
    static Result<Ptr> create(struct android_app* app) noexcept;
#else
    static Result<Ptr> create(int argc, char* argv[]) noexcept;
#endif

    // Main loop - returns exit code
    int run() noexcept;

    // Service accessors (for plugins and InputHandler)
    std::shared_ptr<Config> config() const noexcept { return _config; }
    WebGPUContext::Ptr context() const noexcept { return _ctx; }
    std::shared_ptr<GridRenderer> renderer() const noexcept { return _renderer; }
    Font* font() const noexcept { return _font; }
    FontManager::Ptr fontManager() const noexcept { return _fontManager; }

#if !YETTY_WEB
    std::shared_ptr<Terminal> terminal() const noexcept { return _terminal; }
    std::shared_ptr<RemoteTerminal> remoteTerminal() const noexcept { return _remoteTerminal; }
    std::shared_ptr<WidgetFactory> widgetFactory() const noexcept { return _widgetFactory; }
    std::shared_ptr<ShaderManager> shaderManager() const noexcept { return _shaderManager; }
#endif

#if !YETTY_WEB
    uv_loop_t* getLoop() const noexcept { return _uvLoop; }
#endif

    // Window info
#if defined(__ANDROID__)
    struct android_app* androidApp() const noexcept { return _androidApp; }
    ANativeWindow* nativeWindow() const noexcept;
#else
    GLFWwindow* window() const noexcept { return _window; }
#endif
    uint32_t windowWidth() const noexcept;
    uint32_t windowHeight() const noexcept;
    float cellWidth() const noexcept { return _baseCellWidth * _zoomLevel; }
    float cellHeight() const noexcept { return _baseCellHeight * _zoomLevel; }
    float baseCellWidth() const noexcept { return _baseCellWidth; }
    float baseCellHeight() const noexcept { return _baseCellHeight; }
    float zoomLevel() const noexcept { return _zoomLevel; }
    uint32_t cols() const noexcept { return _cols; }
    uint32_t rows() const noexcept { return _rows; }

    // State modifiers (called by InputHandler)
    void setZoomLevel(float zoom) noexcept;
    void updateGridSize(uint32_t cols, uint32_t rows) noexcept;

    // Request a render (called by Terminal when it has new output)
    void requestRender() noexcept;

    // Get elapsed time since start (for shader uniforms)
    double getElapsedTime() const noexcept;

    //=========================================================================
    // Render state (active during command execution)
    //=========================================================================
    WGPUCommandEncoder currentEncoder() const noexcept { return _currentEncoder; }
    WGPURenderPassEncoder currentRenderPass() const noexcept { return _currentRenderPass; }
    void setCurrentEncoder(WGPUCommandEncoder enc) noexcept { _currentEncoder = enc; }
    void setCurrentRenderPass(WGPURenderPassEncoder pass) noexcept { _currentRenderPass = pass; }

private:
    Yetty() noexcept = default;

    // Initialization phases
#if defined(__ANDROID__)
    Result<void> init(struct android_app* app) noexcept;
#else
    Result<void> init(int argc, char* argv[]) noexcept;
    Result<void> parseArgs(int argc, char* argv[]) noexcept;
#endif
    Result<void> initWindow() noexcept;
    Result<void> initGraphics() noexcept;
    Result<void> initFont() noexcept;
    Result<void> initRenderer() noexcept;
    Result<void> initTerminal() noexcept;
    Result<void> initCallbacks() noexcept;
    void shutdown() noexcept;

    // Main loop
    void mainLoopIteration() noexcept;
    void handleResize(int width, int height) noexcept;

#if defined(__ANDROID__)
    // Android-specific initialization
    Result<void> setupToybox() noexcept;
    Result<void> extractAssets() noexcept;
    bool extractAsset(const char* assetName, const char* destPath) noexcept;
    std::string getNativeLibraryDir() noexcept;

    // Android input handling
    static int32_t handleInput(struct android_app* app, AInputEvent* event);
    static void handleCmd(struct android_app* app, int32_t cmd);
    void showSoftKeyboard() noexcept;
    void hideSoftKeyboard() noexcept;
#endif

#if YETTY_WEB
    static void emscriptenMainLoop() noexcept;
#endif

    // Core components
    std::shared_ptr<Config> _config;
    WebGPUContext::Ptr _ctx;
    std::shared_ptr<GridRenderer> _renderer;
    Font* _font = nullptr;                 // Pointer to font (owned by FontManager)
    FontManager::Ptr _fontManager;

#if YETTY_WEB
    std::shared_ptr<class WebDisplay> _webDisplay;  // Demo display for web builds
#else
    std::shared_ptr<Terminal> _terminal;
    std::shared_ptr<RemoteTerminal> _remoteTerminal;  // For --mux mode
    std::shared_ptr<WidgetFactory> _widgetFactory;
    std::shared_ptr<InputHandler> _inputHandler;
    std::shared_ptr<ShaderManager> _shaderManager;
    std::shared_ptr<CursorRenderer> _cursorRenderer;
#endif

    // Root widgets (Terminal or RemoteTerminal, more in future)
    std::vector<WidgetPtr> _rootWidgets;

    // Active render state (during command execution)
    WGPUCommandEncoder _currentEncoder = nullptr;
    WGPURenderPassEncoder _currentRenderPass = nullptr;

    // Window / Platform
#if defined(__ANDROID__)
    struct android_app* _androidApp = nullptr;
    std::string _dataDir;
    std::string _toyboxPath;

    // Android touch state
    float _touchX = 0.0f;
    float _touchY = 0.0f;
    bool _touching = false;
    double _touchDownTime = 0.0;
    bool _selecting = false;
    bool _androidInitialized = false;
    bool _androidRunning = false;
#else
    GLFWwindow* _window = nullptr;
#endif

    // Grid dimensions
    uint32_t _cols = 80;
    uint32_t _rows = 24;
    float _baseCellWidth = 0.0f;
    float _baseCellHeight = 0.0f;
    float _zoomLevel = 1.0f;

    // Command line options
    std::string _fontPath;
    std::string _executeCommand;
    uint32_t _initialWidth = 1024;
    uint32_t _initialHeight = 768;
    bool _generateAtlasOnly = false;
    bool _useMux = false;  // Use multiplexed terminal (connect to yetty-server)

public:
    // Check if multiplexed terminal mode is enabled
    bool useMux() const noexcept { return _useMux; }

private:

    // FPS tracking
    double _lastFpsTime = 0.0;
    uint32_t _frameCount = 0;

#if !YETTY_WEB
    // libuv event loop (needed for Terminal PTY operations)
    uv_loop_t* _uvLoop = nullptr;
#if !defined(__ANDROID__)
    // Desktop-only: timer-driven rendering at 50Hz
    uv_timer_t* _frameTimer = nullptr;
    uv_async_t* _wakeAsync = nullptr;  // For Terminal to wake up main loop
    bool _needsRender = true;          // Flag to trigger render
    double _lastRenderTime = 0.0;      // For time-based shader uniforms

    // libuv callbacks
    static void onFrameTimer(uv_timer_t* handle);
    static void onWakeAsync(uv_async_t* handle);
    void initEventLoop() noexcept;
    void shutdownEventLoop() noexcept;
#endif
#endif

    // For Emscripten
    static Yetty* s_instance;
};

} // namespace yetty
