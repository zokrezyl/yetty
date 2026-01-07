#include "grid-renderer.h"
#include "grid.h"
#include <yetty/yetty.h>

#if defined(__ANDROID__)
#include <sys/stat.h>
#include <unistd.h>
#include <vterm.h>
#define LOG_TAG "yetty"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include "input-handler.h"
#endif

#if !YETTY_WEB && !defined(__ANDROID__)
#include "plugin-manager.h"
#include <yetty/shader-glyph-renderer.h>
#include <yetty/shader-manager.h>
#include <yetty/cursor-renderer.h>
#include "terminal.h"
#include <args.hxx>
#elif defined(__ANDROID__)
#include "terminal.h"
#elif YETTY_WEB
#include <yetty/config.h>
#include "web-display.h"
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <thread>

#if !defined(__ANDROID__)
#include <filesystem>
#endif

namespace yetty {

// Helper macro to check Result in callbacks where we can't return errors
#define CHECK_RESULT(expr)                                                     \
  do {                                                                         \
    if (auto _res = (expr); !_res) {                                           \
      spdlog::error("{}: {}", #expr, _res.error().message());                  \
    }                                                                          \
  } while (0)

// Static instance for Emscripten
Yetty *Yetty::s_instance = nullptr;

// Default paths
#if YETTY_WEB
static const char *DEFAULT_FONT = "/assets/DejaVuSansMNerdFontMono-Regular.ttf";
static const char *DEFAULT_ATLAS = "/assets/atlas.png";
static const char *DEFAULT_METRICS = "/assets/atlas.json";
#elif defined(__ANDROID__)
// Android paths are set dynamically based on app data directory
static const char *DEFAULT_FONT = nullptr;         // Not used on Android
static const char *DEFAULT_ATLAS = "atlas.png";    // Asset name
static const char *DEFAULT_METRICS = "atlas.json"; // Asset name
#elif defined(_WIN32)
static const char *DEFAULT_FONT = "C:/Windows/Fonts/consola.ttf";
static const char *DEFAULT_ATLAS = "assets/atlas.png";
static const char *DEFAULT_METRICS = "assets/atlas.json";
#elif defined(__APPLE__)
static const char *DEFAULT_FONT = "/System/Library/Fonts/Monaco.ttf";
static const char *DEFAULT_ATLAS = "assets/atlas.png";
static const char *DEFAULT_METRICS = "assets/atlas.json";
#else
static const char *DEFAULT_FONT =
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
static const char *DEFAULT_ATLAS = "assets/atlas.png";
static const char *DEFAULT_METRICS = "assets/atlas.json";
#endif

//-----------------------------------------------------------------------------
// Yetty Implementation
//-----------------------------------------------------------------------------

Yetty::~Yetty() { shutdown(); }

#if defined(__ANDROID__)
Result<Yetty::Ptr> Yetty::create(struct android_app *app) noexcept {
  auto p = Ptr(new Yetty());
  if (auto res = p->init(app); !res) {
    return Err<Ptr>("Failed to init Yetty", res);
  }
  return Ok(p);
}
#else
Result<Yetty::Ptr> Yetty::create(int argc, char *argv[]) noexcept {
  auto p = Ptr(new Yetty());
  if (auto res = p->init(argc, argv); !res) {
    return Err<Ptr>("Failed to init Yetty", res);
  }
  return Ok(p);
}
#endif

#if defined(__ANDROID__)
Result<void> Yetty::init(struct android_app *app) noexcept {
  LOGI("yetty starting...");
  spdlog::set_level(spdlog::level::debug);

  _androidApp = app;
  _dataDir = std::string(app->activity->internalDataPath);

  // Set up Toybox
  if (auto res = setupToybox(); !res) {
    return res;
  }

  // Create default config (no command line on Android)
  auto configResult = Config::create();
  if (!configResult) {
    return Err<void>("Failed to create config", configResult);
  }
  _config = *configResult;

  _initialWidth = 1024; // Will be set when window is created
  _initialHeight = 768;

  // Note: actual initialization happens in handleCmd when window is ready
  s_instance = this;
  return Ok();
}
#else
Result<void> Yetty::init(int argc, char *argv[]) noexcept {
  spdlog::set_level(spdlog::level::debug);
  spdlog::info("yetty starting...");

  // Parse command line arguments
  if (auto res = parseArgs(argc, argv); !res) {
    return res;
  }

  // Initialize window
  if (auto res = initWindow(); !res) {
    return res;
  }

  // Initialize graphics (WebGPU)
  if (auto res = initGraphics(); !res) {
    return res;
  }

  // Initialize font manager
  if (auto fmRes = FontManager::create(_ctx); !fmRes) {
    return Err<void>("Failed to create FontManager", fmRes);
  } else {
    _fontManager = *fmRes;
  }

  // Initialize font
  if (auto res = initFont(); !res) {
    return res;
  }

  // Initialize renderer
  if (auto res = initRenderer(); !res) {
    return res;
  }

  // Initialize libuv loop before terminal (terminal uses it)
  initEventLoop();

  // Initialize terminal
  if (auto res = initTerminal(); !res) {
    return res;
  }

  // Set up callbacks
  if (auto res = initCallbacks(); !res) {
    return res;
  }

  s_instance = this;
  _lastFpsTime = glfwGetTime();

  return Ok();
}
#endif

#if !defined(__ANDROID__)
Result<void> Yetty::parseArgs(int argc, char *argv[]) noexcept {
#if !YETTY_WEB
  args::ArgumentParser parser("yetty - WebGPU Terminal Emulator");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

  // Config and plugin options
  args::ValueFlag<std::string> configFile(parser, "path", "Config file path",
                                          {'c', "config"});
  args::ValueFlag<std::string> pluginPath(
      parser, "path", "Plugin search path (colon-separated)",
      {'p', "plugin-path"});

  // Display options
  args::ValueFlag<std::string> fontPathArg(parser, "font", "Path to TTF font",
                                           {'f', "font"});
  args::ValueFlag<uint32_t> widthArg(parser, "width", "Window width in pixels",
                                     {'W', "width"});
  args::ValueFlag<uint32_t> heightArg(
      parser, "height", "Window height in pixels", {'H', "height"});

  // Mode options
  args::Flag generateAtlasFlag(parser, "generate-atlas",
                               "Generate font atlas and exit",
                               {"generate-atlas"});
  args::Flag noDamageFlag(parser, "no-damage", "Disable damage tracking",
                          {"no-damage"});
  args::Flag debugDamageFlag(parser, "debug-damage",
                             "Log damage rectangle updates", {"debug-damage"});
  args::ValueFlag<std::string> executeArg(
      parser, "command", "Execute command instead of shell", {'e'});

  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Help &) {
    std::cout << parser;
    return Err<void>("Help requested");
  } catch (const args::ParseError &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return Err<void>(std::string("Parse error: ") + e.what());
  }

  // Build command line overrides for config
  YAML::Node cmdOverrides;
  if (pluginPath) {
    cmdOverrides["plugins"]["path"] = args::get(pluginPath);
  }
  if (noDamageFlag) {
    cmdOverrides["rendering"]["damage-tracking"] = false;
  }
  if (debugDamageFlag) {
    cmdOverrides["debug"]["damage-rects"] = true;
  }

  // Create and initialize config
  std::string configPath = configFile ? args::get(configFile) : "";
  auto configResult = Config::create(configPath, cmdOverrides);
  if (!configResult) {
    return Err<void>("Failed to create config", configResult);
  }
  _config = *configResult;

  // Extract final values
  _generateAtlasOnly = generateAtlasFlag;
  _fontPath = fontPathArg ? args::get(fontPathArg) : std::string(DEFAULT_FONT);
  _executeCommand = executeArg ? args::get(executeArg) : "";
  _initialWidth = widthArg ? args::get(widthArg) : 1024;
  _initialHeight = heightArg ? args::get(heightArg) : 768;

  if (_initialWidth == 0)
    _initialWidth = 1024;
  if (_initialHeight == 0)
    _initialHeight = 768;

  if (!_executeCommand.empty()) {
    spdlog::info("Execute command: {}", _executeCommand);
  }
#else
  (void)argc;
  (void)argv;

  // Web build: simplified config without command line parsing
  auto configResult = Config::create();
  if (!configResult) {
    return Err<void>("Failed to create config");
  }
  _config = *configResult;

  _generateAtlasOnly = false;
  _fontPath = DEFAULT_FONT;
  _initialWidth = 1024;
  _initialHeight = 768;
#endif

  return Ok();
}
#endif // !defined(__ANDROID__)

Result<void> Yetty::initWindow() noexcept {
#if defined(__ANDROID__)
  // On Android, window is managed by native activity
  // Get window dimensions from ANativeWindow
  ANativeWindow *window = _androidApp->window;
  if (!window) {
    return Err<void>("Android native window not available");
  }
  _initialWidth = static_cast<uint32_t>(ANativeWindow_getWidth(window));
  _initialHeight = static_cast<uint32_t>(ANativeWindow_getHeight(window));
  LOGI("Window size: %dx%d", _initialWidth, _initialHeight);
  return Ok();
#else
#if !YETTY_USE_PREBUILT_ATLAS
  // Generate atlas only mode (no window needed)
  if (_generateAtlasOnly) {
    std::cout << "Generating font atlas from: " << _fontPath << std::endl;

    Font font;
    float fontSize = 32.0f;
    if (!font.generate(_fontPath, fontSize)) {
      return Err<void>("Failed to generate font atlas");
    }

    // Save to assets directory
    std::string atlasDir = std::string(CMAKE_SOURCE_DIR) + "/assets";
    std::string atlasPath = atlasDir + "/atlas.lz4";
    std::string metricsPath = atlasDir + "/atlas.json";

    if (!font.saveAtlas(atlasPath, metricsPath)) {
      return Err<void>("Failed to save atlas");
    }

    std::cout << "Atlas saved to:" << std::endl;
    std::cout << "  " << atlasPath << std::endl;
    std::cout << "  " << metricsPath << std::endl;

    return Err<void>("Atlas generation complete"); // Exit signal
  }
#endif

  // Initialize GLFW
  if (!glfwInit()) {
    return Err<void>("Failed to initialize GLFW");
  }

  // Don't create OpenGL context - we're using WebGPU
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  _window = glfwCreateWindow(_initialWidth, _initialHeight,
                             "yetty - WebGPU Terminal", nullptr, nullptr);
  if (!_window) {
    glfwTerminate();
    return Err<void>("Failed to create window");
  }

  return Ok();
#endif // __ANDROID__
}

Result<void> Yetty::initGraphics() noexcept {
#if defined(__ANDROID__)
  auto ctxResult =
      WebGPUContext::create(_androidApp->window, _initialWidth, _initialHeight);
  if (!ctxResult) {
    return Err<void>("Failed to initialize WebGPU", ctxResult);
  }
  _ctx = *ctxResult;
#else
  auto ctxResult =
      WebGPUContext::create(_window, _initialWidth, _initialHeight);
  if (!ctxResult) {
    glfwDestroyWindow(_window);
    glfwTerminate();
    return Err<void>("Failed to initialize WebGPU", ctxResult);
  }
  _ctx = *ctxResult;
#endif

  return Ok();
}

Result<void> Yetty::initFont() noexcept {
#if defined(__ANDROID__)
  // Android: extract assets and load prebuilt atlas via FontManager
  if (auto res = extractAssets(); !res) {
    return res;
  }

  std::string atlasPath = _dataDir + "/atlas.lz4";
  std::string metricsPath = _dataDir + "/atlas.json";

  LOGI("Loading atlas via FontManager from: %s", atlasPath.c_str());
  auto fontResult =
      _fontManager->loadFromAtlas(atlasPath, metricsPath, "default");
  if (!fontResult) {
    return Err<void>("Failed to load font atlas", fontResult);
  }
  _font = *fontResult;
  LOGI("Font atlas loaded via FontManager");

  float fontSize = _font->getFontSize();
  _baseCellWidth = fontSize * 0.6f;
  _baseCellHeight = fontSize * 1.2f;
#elif YETTY_USE_PREBUILT_ATLAS
  // Web build: load prebuilt atlas via FontManager
  spdlog::info("Loading pre-built atlas via FontManager...");
  auto fontResult =
      _fontManager->loadFromAtlas(DEFAULT_ATLAS, DEFAULT_METRICS, "default");
  if (!fontResult) {
    return Err<void>("Failed to load pre-built atlas", fontResult);
  }
  _font = *fontResult;

  float fontSize = _font->getFontSize();
  _baseCellWidth = fontSize * 0.6f;
  _baseCellHeight = fontSize * 1.2f;
#else
  // Native build: FontManager handles everything in initRenderer()
  // Nothing to do here
#endif

  return Ok();
}

Result<void> Yetty::initRenderer() noexcept {
  // Get font family - use config or default bundled Nerd Font
  std::string fontFamily = _config ? _config->fontFamily() : "default";
  if (fontFamily == "default") {
    // Use bundled Nerd Font
    fontFamily = Config::getExecutableDir().string() + "/assets/DejaVuSansMNerdFontMono-Regular.ttf";
  }
  spdlog::info("Using font: {}", fontFamily);

  auto rendererResult = GridRenderer::create(_ctx, _fontManager, fontFamily);
  if (!rendererResult) {
    return Err<void>("Failed to create text renderer", rendererResult);
  }
  _renderer = *rendererResult;

  // Get font for cell size calculation
  auto fontResult = _fontManager->getFont(fontFamily, Font::Regular, 32.0f);
  if (!fontResult) {
    return Err<void>("Failed to get font for cell size", fontResult);
  }
  _font = *fontResult;
  float fontSize = _font->getFontSize();
  _baseCellWidth = fontSize * 0.6f;
  _baseCellHeight = fontSize * 1.2f;

  _renderer->setCellSize(_baseCellWidth, _baseCellHeight);
  _renderer->resize(_initialWidth, _initialHeight);
  _renderer->setConfig(_config.get());

  // Calculate grid size
  _cols = static_cast<uint32_t>(_initialWidth / _baseCellWidth);
  _rows = static_cast<uint32_t>(_initialHeight / _baseCellHeight);

  return Ok();
}

Result<void> Yetty::initTerminal() noexcept {
#if defined(__ANDROID__)
  // Android: Terminal mode (no plugins)
  auto terminalResult =
      Terminal::create(nextRenderableId(), _cols, _rows, _font, _uvLoop);
  if (!terminalResult) {
    return Err<void>("Failed to create terminal", terminalResult);
  }
  _terminal = *terminalResult;
  _terminal->setConfig(_config.get());

  // Set up environment for Android
  setenv("TERM", "xterm-256color", 1);
  setenv("COLORTERM", "truecolor", 1);
  setenv("HOME", _dataDir.c_str(), 1);
  setenv("PATH", "/system/bin:/system/xbin", 1);
  setenv("SHELL", "/system/bin/sh", 1);
  setenv("TOYBOX", _toyboxPath.c_str(), 1);

  // Start shell
  _terminal->setShell("/system/bin/sh");
  _terminal->start();
  addRenderable(_terminal);
  LOGI("Terminal started with shell: /system/bin/sh");
  LOGI("Toybox available at: %s", _toyboxPath.c_str());
#elif !YETTY_WEB
  // Desktop: Terminal mode with plugins
  auto terminalResult =
      Terminal::create(nextRenderableId(), _cols, _rows, _font, _uvLoop);
  if (!terminalResult) {
    return Err<void>("Failed to create terminal", terminalResult);
  }
  _terminal = *terminalResult;
  _terminal->setConfig(_config.get());

  // Create and configure PluginManager
  auto pluginMgrResult = PluginManager::create();
  if (!pluginMgrResult) {
    return Err<void>("Failed to create plugin manager", pluginMgrResult);
  }
  _pluginManager = *pluginMgrResult;

  // Pass font and engine to plugins
  _pluginManager->setFont(_font);
  _pluginManager->setEngine(shared_from_this());

  // Note: ShaderGlyphRenderer is now integrated via Terminal, not as a plugin

  // Load plugins from configured paths
  auto pluginPaths = _config->pluginPaths();
  for (const auto &path : pluginPaths) {
    spdlog::info("Loading plugins from: {}", path);
    _pluginManager->loadPluginsFromDirectory(path);
  }

  // Wire up plugin manager to terminal
  _terminal->setPluginManager(_pluginManager.get());
  _terminal->setCellSize(static_cast<uint32_t>(_baseCellWidth),
                         static_cast<uint32_t>(_baseCellHeight));

  // Wire up emoji atlas for dynamic emoji loading
  if (_renderer) {
    _terminal->setEmojiAtlas(_renderer->getEmojiAtlas());
    _terminal->setRenderer(_renderer.get());
  }

  // Set up zoom handling on terminal
  _terminal->setBaseCellSize(_baseCellWidth, _baseCellHeight);

  // Set shell command and start terminal
  if (!_executeCommand.empty()) {
    _terminal->setShell(_executeCommand);
  }
  _terminal->start();

  // Add terminal to renderables
  addRenderable(_terminal);

  // Create ShaderManager for shader-based rendering (cursor, custom glyphs)
  _shaderManager = std::make_shared<ShaderManager>();
  std::string shaderPath = std::string(CMAKE_SOURCE_DIR) + "/src/yetty/shaders/";
  if (auto res = _shaderManager->init(_ctx->getDevice(), shaderPath); !res) {
    spdlog::warn("Failed to init ShaderManager: {} - cursor shader disabled", error_msg(res));
    _shaderManager.reset();
  }

  // CursorRenderer disabled - GridRenderer handles cursor via shaders.wgsl
  // The separate CursorRenderer causes issues because when terminal has no
  // damage, it doesn't render, leaving swapchain texture with undefined
  // content. CursorRenderer's LoadOp_Load then loads garbage.
  // TODO: Implement GPU-driven cursor blinking in GridRenderer's shader
  // using a time uniform, so cursor blinks even without damage.
  (void)_shaderManager;  // Keep ShaderManager for future use

  std::cout << "Terminal mode: Grid " << _cols << "x" << _rows
            << " (damage tracking: "
            << (_config->useDamageTracking() ? "on" : "off") << ")"
            << std::endl;

  // Create input handler
  auto inputResult = InputHandler::create(this);
  if (!inputResult) {
    return Err<void>("Failed to create input handler", inputResult);
  }
  _inputHandler = *inputResult;
#else
  // Web build: Create WebDisplay renderable
  spdlog::info("Web build: Creating WebDisplay");

  auto displayResult = WebDisplay::create(nextRenderableId(), _cols, _rows, _ctx, _fontManager);
  if (!displayResult) {
    return Err<void>("Failed to create WebDisplay", displayResult);
  }
  _webDisplay = *displayResult;
  addRenderable(_webDisplay);

  // Fill the grid with a welcome message
  Grid& grid = _webDisplay->grid();
  Font* font = _webDisplay->font();

  const char* lines[] = {
    "+---------------------------------------------------------------------------+",
    "|                                                                           |",
    "|                     YETTY - WebGPU Terminal Emulator                      |",
    "|                                                                           |",
    "+---------------------------------------------------------------------------+",
    "|                                                                           |",
    "|  Welcome to the yetty web demo!                                           |",
    "|                                                                           |",
    "|  This terminal is rendered using WebGPU with MSDF fonts.                  |",
    "|                                                                           |",
    "|  Features:                                                                |",
    "|    * GPU-accelerated text rendering                                       |",
    "|    * Smooth scaling via MSDF (Multi-channel Signed Distance Fields)       |",
    "|    * Full Unicode support including emoji                                 |",
    "|    * 24-bit true color support                                            |",
    "|                                                                           |",
    "|  Use the Toybox shell below to run commands.                              |",
    "|                                                                           |",
    "+---------------------------------------------------------------------------+",
  };

  // Default colors
  uint8_t fgR = 0xCC, fgG = 0xCC, fgB = 0xCC;  // Light gray
  uint8_t bgR = 0x0F, bgG = 0x0F, bgB = 0x23;  // Dark background

  // Write each line
  for (size_t row = 0; row < sizeof(lines)/sizeof(lines[0]) && row < _rows; row++) {
    const char* line = lines[row];
    uint32_t col = 0;

    // UTF-8 decode and write
    const uint8_t* p = reinterpret_cast<const uint8_t*>(line);
    while (*p && col < _cols) {
      uint32_t codepoint;
      if ((*p & 0x80) == 0) {
        codepoint = *p++;
      } else if ((*p & 0xE0) == 0xC0) {
        codepoint = (*p++ & 0x1F) << 6;
        codepoint |= (*p++ & 0x3F);
      } else if ((*p & 0xF0) == 0xE0) {
        codepoint = (*p++ & 0x0F) << 12;
        codepoint |= (*p++ & 0x3F) << 6;
        codepoint |= (*p++ & 0x3F);
      } else if ((*p & 0xF8) == 0xF0) {
        codepoint = (*p++ & 0x07) << 18;
        codepoint |= (*p++ & 0x3F) << 12;
        codepoint |= (*p++ & 0x3F) << 6;
        codepoint |= (*p++ & 0x3F);
      } else {
        codepoint = '?';
        p++;
      }

      // Get glyph index from font
      uint16_t glyphIndex = font ? font->getGlyphIndex(codepoint) : static_cast<uint16_t>(codepoint);

      // Set colors based on character type
      uint8_t r = fgR, g = fgG, b = fgB;
      if (codepoint == '+' || codepoint == '-' || codepoint == '|') {
        // Box drawing characters - use accent color
        r = 0xE9; g = 0x45; b = 0x60;  // Red accent
      } else if (codepoint == '*') {
        r = 0x4A; g = 0xDE; b = 0x80;  // Green for bullets
      }

      grid.setCell(col, row, glyphIndex, r, g, b, bgR, bgG, bgB);
      col++;
    }

    // Fill rest of line with spaces
    uint16_t spaceGlyph = font ? font->getGlyphIndex(' ') : ' ';
    while (col < _cols) {
      grid.setCell(col, row, spaceGlyph, fgR, fgG, fgB, bgR, bgG, bgB);
      col++;
    }
  }

  // Fill remaining rows with spaces
  uint16_t spaceGlyph = font ? font->getGlyphIndex(' ') : ' ';
  for (uint32_t row = sizeof(lines)/sizeof(lines[0]); row < _rows; row++) {
    for (uint32_t col = 0; col < _cols; col++) {
      grid.setCell(col, row, spaceGlyph, fgR, fgG, fgB, bgR, bgG, bgB);
    }
  }

  _webDisplay->start();
  spdlog::info("Web demo: WebDisplay {}x{} initialized", _cols, _rows);
#endif

  return Ok();
}

Result<void> Yetty::initCallbacks() noexcept {
#if defined(__ANDROID__)
  // Android callbacks are set up in run() via android_app handlers
  return Ok();
#else
  glfwSetWindowUserPointer(_window, this);

#if !YETTY_WEB
  if (_inputHandler) {
    // Set up keyboard and mouse callbacks via lambdas that call InputHandler
    glfwSetKeyCallback(_window, [](GLFWwindow *w, int key, int scancode,
                                   int action, int mods) {
      auto *engine = static_cast<Yetty *>(glfwGetWindowUserPointer(w));
      if (engine && engine->_inputHandler) {
        engine->_inputHandler->onKey(key, scancode, action, mods);
      }
    });

    glfwSetCharCallback(_window, [](GLFWwindow *w, unsigned int codepoint) {
      auto *engine = static_cast<Yetty *>(glfwGetWindowUserPointer(w));
      if (engine && engine->_inputHandler) {
        engine->_inputHandler->onChar(codepoint);
      }
    });

    glfwSetCursorPosCallback(
        _window, [](GLFWwindow *w, double xpos, double ypos) {
          auto *engine = static_cast<Yetty *>(glfwGetWindowUserPointer(w));
          if (engine && engine->_inputHandler) {
            engine->_inputHandler->onMouseMove(xpos, ypos);
          }
        });

    glfwSetMouseButtonCallback(
        _window, [](GLFWwindow *w, int button, int action, int mods) {
          auto *engine = static_cast<Yetty *>(glfwGetWindowUserPointer(w));
          if (engine && engine->_inputHandler) {
            engine->_inputHandler->onMouseButton(button, action, mods);
          }
        });
  }
#endif

  // Window resize callback
  glfwSetFramebufferSizeCallback(
      _window, [](GLFWwindow *w, int newWidth, int newHeight) {
        auto *engine = static_cast<Yetty *>(glfwGetWindowUserPointer(w));
        if (engine) {
          engine->handleResize(newWidth, newHeight);
        }
      });

  // Scroll callback
  glfwSetScrollCallback(
      _window, [](GLFWwindow *w, double xoffset, double yoffset) {
        auto *engine = static_cast<Yetty *>(glfwGetWindowUserPointer(w));
#if !YETTY_WEB
        if (engine && engine->_inputHandler) {
          engine->_inputHandler->onScroll(xoffset, yoffset);
        }
#else
        (void)engine; (void)xoffset; (void)yoffset;
#endif
      });

  return Ok();
#endif // __ANDROID__
}

void Yetty::shutdown() noexcept {
#if defined(__ANDROID__)
  LOGI("Shutting down...");
  // Reset in reverse order of creation
  _terminal.reset();
  _renderer.reset();
  _fontManager.reset(); // FontManager owns all fonts
  _font = nullptr;
  _ctx.reset();
  _androidInitialized = false;
#elif YETTY_WEB
  // Web: just clean up shared_ptrs (handled automatically)
  // GLFW termination handled by Emscripten runtime
#else
  // Desktop: Shutdown libuv event loop
  shutdownEventLoop();

  // shared_ptrs are cleaned up automatically

  if (_window) {
    glfwDestroyWindow(_window);
    _window = nullptr;
  }
  glfwTerminate();
#endif

  s_instance = nullptr;
}

//-----------------------------------------------------------------------------
// libuv Event Loop (Desktop only)
//-----------------------------------------------------------------------------

#if !YETTY_WEB && !defined(__ANDROID__)
void Yetty::initEventLoop() noexcept {
  _uvLoop = new uv_loop_t;
  uv_loop_init(_uvLoop);

  // Frame timer - fires at 50Hz (every 20ms)
  _frameTimer = new uv_timer_t;
  uv_timer_init(_uvLoop, _frameTimer);
  _frameTimer->data = this;
  uv_timer_start(_frameTimer, onFrameTimer, 0, 20); // 50Hz

  // Async handle for Terminal to wake up main loop
  _wakeAsync = new uv_async_t;
  uv_async_init(_uvLoop, _wakeAsync, onWakeAsync);
  _wakeAsync->data = this;

  _lastRenderTime = glfwGetTime();
  spdlog::debug("libuv event loop initialized (50Hz timer)");
}

void Yetty::shutdownEventLoop() noexcept {
  if (_frameTimer) {
    uv_timer_stop(_frameTimer);
    uv_close(reinterpret_cast<uv_handle_t *>(_frameTimer), nullptr);
  }
  if (_wakeAsync) {
    uv_close(reinterpret_cast<uv_handle_t *>(_wakeAsync), nullptr);
  }
  if (_uvLoop) {
    // Run loop once to process close callbacks
    uv_run(_uvLoop, UV_RUN_NOWAIT);
    uv_loop_close(_uvLoop);
    delete _uvLoop;
    _uvLoop = nullptr;
  }
  if (_frameTimer) {
    delete _frameTimer;
    _frameTimer = nullptr;
  }
  if (_wakeAsync) {
    delete _wakeAsync;
    _wakeAsync = nullptr;
  }
}

void Yetty::onFrameTimer(uv_timer_t *handle) {
  Yetty *self = static_cast<Yetty *>(handle->data);

  // Poll GLFW events (non-blocking)
  glfwPollEvents();

  // Check if window should close
  if (glfwWindowShouldClose(self->_window)) {
    uv_stop(self->_uvLoop);
    return;
  }

  // Render frame
  self->mainLoopIteration();
}

void Yetty::onWakeAsync(uv_async_t *handle) {
  Yetty *self = static_cast<Yetty *>(handle->data);
  self->_needsRender = true;
  // The next timer tick will render
}

void Yetty::requestRender() noexcept {
  _needsRender = true;
  if (_wakeAsync) {
    uv_async_send(_wakeAsync);
  }
}

double Yetty::getElapsedTime() const noexcept {
  return glfwGetTime() - _lastRenderTime;
}
#else
void Yetty::requestRender() noexcept {
  // No-op on web/Android
}

double Yetty::getElapsedTime() const noexcept { return 0.0; }
#endif

int Yetty::run() noexcept {
#if defined(__ANDROID__)
  LOGI("Starting Android main loop...");

  _androidApp->onAppCmd = handleCmd;
  _androidApp->onInputEvent = handleInput;

  while (true) {
    int events;
    struct android_poll_source *source;

    // Poll for events: 0 timeout when running (non-blocking), -1 when paused
    // (blocking)
    int timeout = _androidRunning ? 0 : -1;

    int pollResult;
    while ((pollResult = ALooper_pollAll(timeout, nullptr, &events,
                                         reinterpret_cast<void **>(&source))) >=
           0) {
      if (source != nullptr) {
        source->process(_androidApp, source);
      }

      if (_androidApp->destroyRequested) {
        LOGI("Destroy requested, cleaning up...");
        shutdown();
        return 0;
      }

      // Recalculate timeout in case running state changed
      timeout = _androidRunning ? 0 : -1;
    }

    // Render frame
    if (_androidRunning) {
      mainLoopIteration();
    }
  }
#elif YETTY_WEB
  std::cout << "Starting render loop... (use mouse scroll to zoom, ESC to exit)"
            << std::endl;
  emscripten_set_main_loop(emscriptenMainLoop, 0, false);
  return 0;
#else
  std::cout << "Starting render loop... (use mouse scroll to zoom, ESC to exit)"
            << std::endl;

  // Run libuv event loop - blocks until uv_stop() is called
  uv_run(_uvLoop, UV_RUN_DEFAULT);

  std::cout << "Shutting down..." << std::endl;
  return 0;
#endif
}

#if YETTY_WEB
void Yetty::emscriptenMainLoop() noexcept {
  if (s_instance) {
    s_instance->mainLoopIteration();
  }
}
#endif

void Yetty::mainLoopIteration() noexcept {
#if defined(__ANDROID__)
  if (!_androidInitialized || !_androidRunning) {
    return;
  }
  if (!_ctx) {
    return;
  }

  // Render all renderables
  renderAll();

  // Present
  _ctx->present();
#elif YETTY_WEB
  // Web build: simplified rendering loop
  // Collect and execute render commands from all renderables
  renderAll();

  // Present the frame
  _ctx->present();
#else
  // Desktop build: full rendering with plugins and shader manager
  // Note: glfwPollEvents() is called in onFrameTimer() before this

  // Check if any renderable is still running
  bool anyRunning = false;
  for (const auto &r : _renderables) {
    if (r->isRunning()) {
      anyRunning = true;
      break;
    }
  }
  if (!anyRunning && !_renderables.empty()) {
    glfwSetWindowShouldClose(_window, GLFW_TRUE);
    return;
  }

  // Upload any pending fallback glyphs to GPU
  if (_font && _font->hasPendingGlyphs()) {
    _font->uploadPendingGlyphs(_ctx->getDevice(), _ctx->getQueue());
    _renderer->updateFontBindings(*_font);
  }

  // Update global uniforms (time, mouse, screen) once per frame
  if (_shaderManager) {
    double now = glfwGetTime();
    float deltaTime = static_cast<float>(now - _lastRenderTime);
    _lastRenderTime = now;

    // Get mouse position (normalized 0-1)
    double mouseX = 0, mouseY = 0;
    glfwGetCursorPos(_window, &mouseX, &mouseY);
    float normMouseX = static_cast<float>(mouseX) / windowWidth();
    float normMouseY = static_cast<float>(mouseY) / windowHeight();

    // TODO: track mouse click position separately
    float clickX = normMouseX;
    float clickY = normMouseY;

    _shaderManager->updateGlobalUniforms(_ctx->getQueue(), deltaTime,
                                          normMouseX, normMouseY,
                                          clickX, clickY,
                                          windowWidth(), windowHeight());
  }

  // Collect and execute render commands from all renderables
  // Each renderable returns nullptr if no damage (skip frame efficiently)
  renderAll();

  // Render plugin layers on top (only if there are layers)
  if (_pluginManager && !_pluginManager->getAllLayers().empty()) {
    auto viewResult = _ctx->getCurrentTextureView();
    if (viewResult) {
      CHECK_RESULT(_pluginManager->render(*_ctx, *viewResult,
                                          windowWidth(), windowHeight(),
                                          cellWidth(), cellHeight(),
                                          _terminal ? _terminal->getScrollOffset() : 0,
                                          _rows));
    }
  }

  // Present the frame
  _ctx->present();

  // FPS counter
  double currentTime = glfwGetTime();
  _frameCount++;
  if (currentTime - _lastFpsTime >= 1.0) {
    std::cout << "FPS: " << _frameCount << std::endl;
    _frameCount = 0;
    _lastFpsTime = currentTime;
  }
#endif // __ANDROID__
}

void Yetty::handleResize(int newWidth, int newHeight) noexcept {
  if (newWidth == 0 || newHeight == 0)
    return;

  // Resize WebGPU context
  if (_ctx) {
    _ctx->resize(static_cast<uint32_t>(newWidth),
                 static_cast<uint32_t>(newHeight));
  }

  // Recalculate grid size based on current cell size
  float cellW = cellWidth();
  float cellH = cellHeight();
  uint32_t newCols = static_cast<uint32_t>(newWidth / cellW);
  uint32_t newRows = static_cast<uint32_t>(newHeight / cellH);

  // Ensure minimum grid size
  if (newCols < 1)
    newCols = 1;
  if (newRows < 1)
    newRows = 1;

  if (newCols != _cols || newRows != _rows) {
    updateGridSize(newCols, newRows);
    std::cout << "Window resize -> Grid " << newCols << "x" << newRows
              << std::endl;
  }
}

uint32_t Yetty::windowWidth() const noexcept {
#if defined(__ANDROID__)
  if (_androidApp && _androidApp->window) {
    return static_cast<uint32_t>(ANativeWindow_getWidth(_androidApp->window));
  }
  return _initialWidth;
#else
  int w, h;
  glfwGetFramebufferSize(_window, &w, &h);
  return static_cast<uint32_t>(w);
#endif
}

uint32_t Yetty::windowHeight() const noexcept {
#if defined(__ANDROID__)
  if (_androidApp && _androidApp->window) {
    return static_cast<uint32_t>(ANativeWindow_getHeight(_androidApp->window));
  }
  return _initialHeight;
#else
  int w, h;
  glfwGetFramebufferSize(_window, &w, &h);
  return static_cast<uint32_t>(h);
#endif
}

void Yetty::setZoomLevel(float zoom) noexcept {
  _zoomLevel = zoom;

#if !YETTY_WEB
  // Delegate to terminal which owns the zoom state and updates renderer
  if (_terminal) {
    _terminal->setZoomLevel(zoom);
  }
#endif
}

void Yetty::updateGridSize(uint32_t cols, uint32_t rows) noexcept {
  _cols = cols;
  _rows = rows;

#if !YETTY_WEB && !defined(__ANDROID__)
  if (_terminal) {
    _terminal->resize(cols, rows);
  }
#elif defined(__ANDROID__)
  if (_terminal) {
    _terminal->resize(cols, rows);
  }
#endif
}

//-----------------------------------------------------------------------------
// Renderable management
//-----------------------------------------------------------------------------

void Yetty::addRenderable(Renderable::Ptr renderable) noexcept {
  if (!renderable)
    return;

  // Insert sorted by zOrder
  auto it =
      std::lower_bound(_renderables.begin(), _renderables.end(), renderable,
                       [](const Renderable::Ptr &a, const Renderable::Ptr &b) {
                         return a->zOrder() < b->zOrder();
                       });
  _renderables.insert(it, renderable);

  spdlog::debug("Added renderable '{}' (id={}, zOrder={})", renderable->name(),
                renderable->id(), renderable->zOrder());
}

void Yetty::removeRenderable(uint32_t id) noexcept {
  auto it =
      std::find_if(_renderables.begin(), _renderables.end(),
                   [id](const Renderable::Ptr &r) { return r->id() == id; });

  if (it != _renderables.end()) {
    spdlog::debug("Removing renderable '{}' (id={})", (*it)->name(), id);
    (*it)->stop();
    _renderables.erase(it);
  }
}

void Yetty::renderAll() noexcept {
  // Render all renderables in zOrder
  for (auto &renderable : _renderables) {
    if (!renderable->isRunning())
      continue;

    // Set current renderable context
    _currentRenderableId = renderable->id();

    // Call render - each renderable manages its own GPU resources
    renderable->render(*_ctx);
  }

  // Reset context
  _currentRenderableId = 0;
}

//-----------------------------------------------------------------------------
// Per-Renderable Resource Management
//-----------------------------------------------------------------------------

RenderableResources &Yetty::currentResources() noexcept {
  return _resources[_currentRenderableId];
}

RenderableResources &Yetty::getResources(uint32_t renderableId) noexcept {
  return _resources[renderableId];
}

void Yetty::cleanupResources(uint32_t renderableId) noexcept {
  auto it = _resources.find(renderableId);
  if (it == _resources.end())
    return;

  auto &res = it->second;

  // Release all GPU resources
  for (auto &[name, shader] : res.shaders) {
    if (shader.pipeline)
      wgpuRenderPipelineRelease(shader.pipeline);
    if (shader.pipelineLayout)
      wgpuPipelineLayoutRelease(shader.pipelineLayout);
    if (shader.bindGroupLayout)
      wgpuBindGroupLayoutRelease(shader.bindGroupLayout);
    if (shader.module)
      wgpuShaderModuleRelease(shader.module);
  }

  for (auto &[name, tex] : res.textures) {
    if (tex.sampler)
      wgpuSamplerRelease(tex.sampler);
    if (tex.view)
      wgpuTextureViewRelease(tex.view);
    if (tex.texture)
      wgpuTextureRelease(tex.texture);
  }

  for (auto &[name, buf] : res.buffers) {
    if (buf.buffer)
      wgpuBufferRelease(buf.buffer);
  }

  _resources.erase(it);
  spdlog::debug("Cleaned up resources for renderable {}", renderableId);
}

//-----------------------------------------------------------------------------
// Android-specific implementations
//-----------------------------------------------------------------------------
#if defined(__ANDROID__)

ANativeWindow *Yetty::nativeWindow() const noexcept {
  return _androidApp ? _androidApp->window : nullptr;
}

Result<void> Yetty::setupToybox() noexcept {
  // Toybox is in the native library directory as libtoybox.so
  // This directory has execute permissions (unlike the files directory)
  std::string nativeLibDir = getNativeLibraryDir();
  _toyboxPath = nativeLibDir + "/libtoybox.so";

  LOGI("Looking for Toybox at %s", _toyboxPath.c_str());

  // Check if toybox exists and is executable
  if (access(_toyboxPath.c_str(), X_OK) == 0) {
    LOGI("Toybox found at %s", _toyboxPath.c_str());
    return Ok();
  }

  LOGW("Toybox not found at %s", _toyboxPath.c_str());
  return Ok(); // Not a fatal error
}

Result<void> Yetty::extractAssets() noexcept {
  std::string atlasPath = _dataDir + "/atlas.png";
  std::string metricsPath = _dataDir + "/atlas.json";
  std::string shaderPath = _dataDir + "/shaders.wgsl";

  // Extract atlas if not present
  if (access(atlasPath.c_str(), R_OK) != 0) {
    if (!extractAsset("atlas.png", atlasPath.c_str())) {
      return Err<void>("Failed to extract atlas.png");
    }
  }

  // Extract metrics if not present
  if (access(metricsPath.c_str(), R_OK) != 0) {
    if (!extractAsset("atlas.json", metricsPath.c_str())) {
      return Err<void>("Failed to extract atlas.json");
    }
  }

  // Always extract shader (may have been updated)
  if (!extractAsset("shaders.wgsl", shaderPath.c_str())) {
    LOGW("Failed to extract shaders.wgsl (may not be needed)");
  } else {
    // Set environment variable for GridRenderer to find the shader
    setenv("YETTY_SHADER_PATH", shaderPath.c_str(), 1);
    LOGI("Shader extracted to %s", shaderPath.c_str());
  }

  return Ok();
}

bool Yetty::extractAsset(const char *assetName, const char *destPath) noexcept {
  AAssetManager *assetManager = _androidApp->activity->assetManager;
  AAsset *asset =
      AAssetManager_open(assetManager, assetName, AASSET_MODE_BUFFER);

  if (!asset) {
    LOGE("Failed to open asset: %s", assetName);
    return false;
  }

  off_t size = AAsset_getLength(asset);
  const void *buffer = AAsset_getBuffer(asset);

  FILE *file = fopen(destPath, "wb");
  if (!file) {
    LOGE("Failed to create file: %s", destPath);
    AAsset_close(asset);
    return false;
  }

  fwrite(buffer, 1, size, file);
  fclose(file);
  AAsset_close(asset);

  // Make executable (for shader files, not strictly necessary but harmless)
  chmod(destPath, 0755);

  LOGI("Extracted asset %s to %s", assetName, destPath);
  return true;
}

std::string Yetty::getNativeLibraryDir() noexcept {
  JNIEnv *env;
  _androidApp->activity->vm->AttachCurrentThread(&env, nullptr);

  // Get the NativeActivity class
  jclass activityClass = env->GetObjectClass(_androidApp->activity->clazz);

  // Get getApplicationInfo() method
  jmethodID getAppInfo =
      env->GetMethodID(activityClass, "getApplicationInfo",
                       "()Landroid/content/pm/ApplicationInfo;");
  jobject appInfo =
      env->CallObjectMethod(_androidApp->activity->clazz, getAppInfo);

  // Get nativeLibraryDir field
  jclass appInfoClass = env->GetObjectClass(appInfo);
  jfieldID nativeLibDirField =
      env->GetFieldID(appInfoClass, "nativeLibraryDir", "Ljava/lang/String;");
  jstring nativeLibDir =
      (jstring)env->GetObjectField(appInfo, nativeLibDirField);

  // Convert to C++ string
  const char *nativeLibDirCStr = env->GetStringUTFChars(nativeLibDir, nullptr);
  std::string result(nativeLibDirCStr);
  env->ReleaseStringUTFChars(nativeLibDir, nativeLibDirCStr);

  // Clean up local refs
  env->DeleteLocalRef(nativeLibDir);
  env->DeleteLocalRef(appInfoClass);
  env->DeleteLocalRef(appInfo);
  env->DeleteLocalRef(activityClass);

  _androidApp->activity->vm->DetachCurrentThread();

  return result;
}

// Static helper to get VTerm modifier flags from Android meta state
static VTermModifier getModifiers(int32_t metaState) {
  int mod = VTERM_MOD_NONE;
  if (metaState & AMETA_SHIFT_ON)
    mod |= VTERM_MOD_SHIFT;
  if (metaState & AMETA_CTRL_ON)
    mod |= VTERM_MOD_CTRL;
  if (metaState & AMETA_ALT_ON)
    mod |= VTERM_MOD_ALT;
  return static_cast<VTermModifier>(mod);
}

int32_t Yetty::handleInput(struct android_app *app, AInputEvent *event) {
  auto *engine = static_cast<Yetty *>(s_instance);
  if (!engine)
    return 0;

  int32_t eventType = AInputEvent_getType(event);

  if (eventType == AINPUT_EVENT_TYPE_MOTION) {
    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    float x = AMotionEvent_getX(event, 0);
    float y = AMotionEvent_getY(event, 0);

    // Convert touch position to terminal cell
    int col = static_cast<int>(x / engine->cellWidth());
    int row = static_cast<int>(y / engine->cellHeight());

    switch (action) {
    case AMOTION_EVENT_ACTION_DOWN:
      engine->_touchX = x;
      engine->_touchY = y;
      engine->_touching = true;

      // Show soft keyboard on tap
      engine->showSoftKeyboard();

      // Start selection
      if (engine->_terminal) {
        engine->_terminal->startSelection(row, col);
        engine->_selecting = true;
      }
      break;

    case AMOTION_EVENT_ACTION_MOVE:
      if (engine->_selecting && engine->_terminal) {
        engine->_terminal->extendSelection(row, col);
      }
      engine->_touchX = x;
      engine->_touchY = y;
      break;

    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_CANCEL:
      engine->_touching = false;
      engine->_selecting = false;
      break;
    }

    return 1;
  }

  if (eventType == AINPUT_EVENT_TYPE_KEY) {
    int32_t keyCode = AKeyEvent_getKeyCode(event);
    int32_t action = AKeyEvent_getAction(event);
    int32_t metaState = AKeyEvent_getMetaState(event);
    VTermModifier mod = getModifiers(metaState);

    LOGI("Key event: keyCode=%d action=%d meta=0x%x", keyCode, action,
         metaState);

    if (action == AKEY_EVENT_ACTION_DOWN && engine->_terminal) {
      // Handle special keys first
      switch (keyCode) {
      case AKEYCODE_ENTER:
        engine->_terminal->sendKey('\r', mod);
        return 1;
      case AKEYCODE_DEL: // Backspace
        engine->_terminal->sendSpecialKey(VTERM_KEY_BACKSPACE, mod);
        return 1;
      case AKEYCODE_FORWARD_DEL: // Delete
        engine->_terminal->sendSpecialKey(VTERM_KEY_DEL, mod);
        return 1;
      case AKEYCODE_TAB:
        engine->_terminal->sendKey('\t', mod);
        return 1;
      case AKEYCODE_ESCAPE:
        engine->_terminal->sendSpecialKey(VTERM_KEY_ESCAPE, mod);
        return 1;
      case AKEYCODE_DPAD_UP:
        engine->_terminal->sendSpecialKey(VTERM_KEY_UP, mod);
        return 1;
      case AKEYCODE_DPAD_DOWN:
        engine->_terminal->sendSpecialKey(VTERM_KEY_DOWN, mod);
        return 1;
      case AKEYCODE_DPAD_LEFT:
        engine->_terminal->sendSpecialKey(VTERM_KEY_LEFT, mod);
        return 1;
      case AKEYCODE_DPAD_RIGHT:
        engine->_terminal->sendSpecialKey(VTERM_KEY_RIGHT, mod);
        return 1;
      case AKEYCODE_MOVE_HOME:
        engine->_terminal->sendSpecialKey(VTERM_KEY_HOME, mod);
        return 1;
      case AKEYCODE_MOVE_END:
        engine->_terminal->sendSpecialKey(VTERM_KEY_END, mod);
        return 1;
      case AKEYCODE_PAGE_UP:
        engine->_terminal->sendSpecialKey(VTERM_KEY_PAGEUP, mod);
        return 1;
      case AKEYCODE_PAGE_DOWN:
        engine->_terminal->sendSpecialKey(VTERM_KEY_PAGEDOWN, mod);
        return 1;
      case AKEYCODE_INSERT:
        engine->_terminal->sendSpecialKey(VTERM_KEY_INS, mod);
        return 1;
      // Skip modifier-only keys
      case AKEYCODE_SHIFT_LEFT:
      case AKEYCODE_SHIFT_RIGHT:
      case AKEYCODE_CTRL_LEFT:
      case AKEYCODE_CTRL_RIGHT:
      case AKEYCODE_ALT_LEFT:
      case AKEYCODE_ALT_RIGHT:
      case AKEYCODE_META_LEFT:
      case AKEYCODE_META_RIGHT:
      case AKEYCODE_CAPS_LOCK:
      case AKEYCODE_NUM_LOCK:
      case AKEYCODE_SCROLL_LOCK:
        return 1;
      }

      // For regular characters, get the Unicode character via JNI
      JNIEnv *env;
      app->activity->vm->AttachCurrentThread(&env, nullptr);

      jclass keyEventClass = env->FindClass("android/view/KeyEvent");
      jmethodID constructor =
          env->GetMethodID(keyEventClass, "<init>", "(II)V");
      jobject keyEvent =
          env->NewObject(keyEventClass, constructor, action, keyCode);

      jmethodID getUnicodeChar =
          env->GetMethodID(keyEventClass, "getUnicodeChar", "(I)I");
      jint unicodeChar =
          env->CallIntMethod(keyEvent, getUnicodeChar, metaState);

      env->DeleteLocalRef(keyEvent);
      env->DeleteLocalRef(keyEventClass);
      app->activity->vm->DetachCurrentThread();

      if (unicodeChar > 0) {
        LOGI("Unicode char: %d ('%c')", unicodeChar, (char)unicodeChar);
        // Handle Ctrl+key combinations
        if (mod & VTERM_MOD_CTRL) {
          if (unicodeChar >= 'a' && unicodeChar <= 'z') {
            engine->_terminal->sendKey(unicodeChar - 'a' + 1, VTERM_MOD_NONE);
          } else if (unicodeChar >= 'A' && unicodeChar <= 'Z') {
            engine->_terminal->sendKey(unicodeChar - 'A' + 1, VTERM_MOD_NONE);
          } else {
            engine->_terminal->sendKey(unicodeChar, mod);
          }
        } else {
          engine->_terminal->sendKey(unicodeChar, VTERM_MOD_NONE);
        }
        return 1;
      }
    }

    return 1;
  }

  return 0;
}

void Yetty::handleCmd(struct android_app *app, int32_t cmd) {
  auto *engine = static_cast<Yetty *>(s_instance);
  if (!engine)
    return;

  switch (cmd) {
  case APP_CMD_INIT_WINDOW:
    LOGI("APP_CMD_INIT_WINDOW");
    if (app->window != nullptr && !engine->_androidInitialized) {
      // Initialize window
      if (auto res = engine->initWindow(); !res) {
        LOGE("Failed to init window: %s", res.error().message().c_str());
        return;
      }

      // Initialize graphics (WebGPU)
      if (auto res = engine->initGraphics(); !res) {
        LOGE("Failed to init graphics: %s", res.error().message().c_str());
        return;
      }

      // Initialize font manager
      if (auto fmRes = FontManager::create(engine->_ctx); !fmRes) {
        LOGE("Failed to create FontManager: %s",
             fmRes.error().message().c_str());
        return;
      } else {
        engine->_fontManager = *fmRes;
      }

      // Initialize font
      if (auto res = engine->initFont(); !res) {
        LOGE("Failed to init font: %s", res.error().message().c_str());
        return;
      }

      // Initialize renderer
      if (auto res = engine->initRenderer(); !res) {
        LOGE("Failed to init renderer: %s", res.error().message().c_str());
        return;
      }

      // Initialize terminal
      if (auto res = engine->initTerminal(); !res) {
        LOGE("Failed to init terminal: %s", res.error().message().c_str());
        return;
      }

      engine->_androidInitialized = true;
      engine->_androidRunning = true;
      LOGI("Yetty initialized successfully");
    }
    break;

  case APP_CMD_TERM_WINDOW:
    LOGI("APP_CMD_TERM_WINDOW");
    engine->_androidRunning = false;
    engine->shutdown();
    break;

  case APP_CMD_GAINED_FOCUS:
    LOGI("APP_CMD_GAINED_FOCUS");
    break;

  case APP_CMD_LOST_FOCUS:
    LOGI("APP_CMD_LOST_FOCUS");
    break;

  case APP_CMD_CONFIG_CHANGED:
    LOGI("APP_CMD_CONFIG_CHANGED");
    if (engine->_androidInitialized && engine->_ctx && app->window) {
      int32_t newWidth = ANativeWindow_getWidth(app->window);
      int32_t newHeight = ANativeWindow_getHeight(app->window);
      LOGI("New window size: %dx%d", newWidth, newHeight);
      engine->handleResize(newWidth, newHeight);
    }
    break;

  case APP_CMD_CONTENT_RECT_CHANGED:
    LOGI("APP_CMD_CONTENT_RECT_CHANGED: rect=[%d,%d,%d,%d]",
         app->contentRect.left, app->contentRect.top, app->contentRect.right,
         app->contentRect.bottom);
    if (engine->_androidInitialized && engine->_ctx) {
      int32_t newWidth = app->contentRect.right - app->contentRect.left;
      int32_t newHeight = app->contentRect.bottom - app->contentRect.top;

      if (newWidth > 0 && newHeight > 0) {
        LOGI("Resizing to content rect: %dx%d", newWidth, newHeight);
        engine->handleResize(newWidth, newHeight);
      }
    }
    break;
  }
}

void Yetty::showSoftKeyboard() noexcept {
  JNIEnv *env;
  _androidApp->activity->vm->AttachCurrentThread(&env, nullptr);

  jclass activityClass = env->GetObjectClass(_androidApp->activity->clazz);

  jmethodID getSystemService =
      env->GetMethodID(activityClass, "getSystemService",
                       "(Ljava/lang/String;)Ljava/lang/Object;");

  jstring serviceName = env->NewStringUTF("input_method");
  jobject imm = env->CallObjectMethod(_androidApp->activity->clazz,
                                      getSystemService, serviceName);
  env->DeleteLocalRef(serviceName);

  if (imm) {
    jclass immClass = env->GetObjectClass(imm);

    jmethodID getWindow =
        env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    jobject window =
        env->CallObjectMethod(_androidApp->activity->clazz, getWindow);

    if (window) {
      jclass windowClass = env->GetObjectClass(window);
      jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView",
                                                "()Landroid/view/View;");
      jobject decorView = env->CallObjectMethod(window, getDecorView);

      if (decorView) {
        jmethodID showSoftInput = env->GetMethodID(immClass, "showSoftInput",
                                                   "(Landroid/view/View;I)Z");
        env->CallBooleanMethod(imm, showSoftInput, decorView, 0);
        LOGI("Requested soft keyboard");
        env->DeleteLocalRef(decorView);
      }
      env->DeleteLocalRef(windowClass);
      env->DeleteLocalRef(window);
    }

    env->DeleteLocalRef(immClass);
    env->DeleteLocalRef(imm);
  }

  env->DeleteLocalRef(activityClass);
  _androidApp->activity->vm->DetachCurrentThread();
}

void Yetty::hideSoftKeyboard() noexcept {
  JNIEnv *env;
  _androidApp->activity->vm->AttachCurrentThread(&env, nullptr);

  jclass activityClass = env->GetObjectClass(_androidApp->activity->clazz);
  jmethodID getSystemService =
      env->GetMethodID(activityClass, "getSystemService",
                       "(Ljava/lang/String;)Ljava/lang/Object;");
  jstring serviceName = env->NewStringUTF("input_method");
  jobject imm = env->CallObjectMethod(_androidApp->activity->clazz,
                                      getSystemService, serviceName);
  env->DeleteLocalRef(serviceName);

  if (imm) {
    jclass immClass = env->GetObjectClass(imm);
    jmethodID getWindow =
        env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
    jobject window =
        env->CallObjectMethod(_androidApp->activity->clazz, getWindow);

    if (window) {
      jclass windowClass = env->GetObjectClass(window);
      jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView",
                                                "()Landroid/view/View;");
      jobject decorView = env->CallObjectMethod(window, getDecorView);

      if (decorView) {
        jclass viewClass = env->GetObjectClass(decorView);
        jmethodID getWindowToken = env->GetMethodID(viewClass, "getWindowToken",
                                                    "()Landroid/os/IBinder;");
        jobject token = env->CallObjectMethod(decorView, getWindowToken);

        if (token) {
          jmethodID hideSoftInputFromWindow = env->GetMethodID(
              immClass, "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
          env->CallBooleanMethod(imm, hideSoftInputFromWindow, token, 0);
          env->DeleteLocalRef(token);
        }

        env->DeleteLocalRef(viewClass);
        env->DeleteLocalRef(decorView);
      }
      env->DeleteLocalRef(windowClass);
      env->DeleteLocalRef(window);
    }

    env->DeleteLocalRef(immClass);
    env->DeleteLocalRef(imm);
  }

  env->DeleteLocalRef(activityClass);
  _androidApp->activity->vm->DetachCurrentThread();
}

#endif // __ANDROID__

} // namespace yetty
