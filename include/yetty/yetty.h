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
#include <yetty/config.h>
#include <yetty/font.h>
#include <yetty/font-manager.h>

#include <memory>
#include <string>
#include <vector>

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

namespace yetty {

// Forward declarations
class TextRenderer;
class Terminal;
class PluginManager;
class Grid;
class InputHandler;

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
    Config::Ptr config() const noexcept { return _config; }
    WebGPUContext::Ptr context() const noexcept { return _ctx; }
    std::shared_ptr<TextRenderer> renderer() const noexcept { return _renderer; }
    Font* font() const noexcept { return _font; }
    FontManager::Ptr fontManager() const noexcept { return _fontManager; }

#if !YETTY_WEB
    std::shared_ptr<Terminal> terminal() const noexcept { return _terminal; }
    std::shared_ptr<PluginManager> pluginManager() const noexcept { return _pluginManager; }
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

    // Demo mode
    bool isDemoMode() const noexcept { return _demoMode; }
    Grid* demoGrid() const noexcept { return _demoGrid; }

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
    Result<void> initTerminalOrDemo() noexcept;
    Result<void> initCallbacks() noexcept;
    void shutdown() noexcept;

    // Main loop
    void mainLoopIteration() noexcept;
    void handleResize(int width, int height) noexcept;

#if defined(__ANDROID__)
    // Android-specific initialization
    Result<void> setupBusybox() noexcept;
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
    Config::Ptr _config;
    WebGPUContext::Ptr _ctx;
    std::shared_ptr<TextRenderer> _renderer;
    std::unique_ptr<Font> _font_storage;  // Owns the font
    Font* _font = nullptr;                 // Raw pointer for compatibility
    FontManager::Ptr _fontManager;

#if !YETTY_WEB
    std::shared_ptr<Terminal> _terminal;
    std::shared_ptr<PluginManager> _pluginManager;
    std::shared_ptr<InputHandler> _inputHandler;
#endif

    // Window / Platform
#if defined(__ANDROID__)
    struct android_app* _androidApp = nullptr;
    std::string _dataDir;
    std::string _busyboxPath;

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

    // Demo mode
    bool _demoMode = false;
    Grid* _demoGrid = nullptr;
    int _scrollMs = 50;
    double _lastScrollTime = 0.0;
    std::vector<std::string> _dictionary;

    // Command line options
    std::string _fontPath;
    std::string _executeCommand;
    uint32_t _initialWidth = 1024;
    uint32_t _initialHeight = 768;
    bool _generateAtlasOnly = false;
    bool _usePrebuiltAtlas = false;

    // FPS tracking
    double _lastFpsTime = 0.0;
    uint32_t _frameCount = 0;

    // For Emscripten
    static Yetty* s_instance;
};

} // namespace yetty
