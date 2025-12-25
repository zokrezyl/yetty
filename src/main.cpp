#include "yetty/renderer/WebGPUContext.h"
#include "yetty/renderer/TextRenderer.h"
#include "yetty/terminal/Grid.h"
#include "yetty/terminal/Font.h"
#include "yetty/Config.h"

#if !YETTY_WEB
#include "yetty/terminal/Terminal.h"
#include "yetty/PluginManager.h"
#include "yetty/plugins/ShaderToy.h"
#include "yetty/plugins/Image.h"
#ifdef YETTY_YMERY_ENABLED
#include "yetty/plugins/Ymery.h"
#endif
#include <termios.h>
#include <unistd.h>
#endif

#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cstring>

#if YETTY_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace yetty;

// Application state for callbacks and main loop
struct AppState {
    GLFWwindow* window = nullptr;
    WebGPUContext* ctx = nullptr;
    TextRenderer* renderer = nullptr;
    Font* font = nullptr;
    Config* config = nullptr;
    float zoomLevel = 1.0f;
    float baseCellWidth = 0.0f;
    float baseCellHeight = 0.0f;

    // Mouse tracking for plugin input
    double mouseX = 0.0;
    double mouseY = 0.0;

#if !YETTY_WEB
    Terminal* terminal = nullptr;
    PluginManager* pluginManager = nullptr;
    struct termios originalTermios;
    bool termiosSet = false;
#endif

    // Demo mode (scrolling text)
    bool demoMode = false;
    Grid* demoGrid = nullptr;
    int scrollMs = 50;
    double lastScrollTime = 0.0;
    std::vector<std::string>* dictionary = nullptr;
    uint32_t cols = 80;
    uint32_t rows = 24;

    // FPS tracking
    double lastFpsTime = 0.0;
    uint32_t frameCount = 0;
};

// Global state for Emscripten main loop
static AppState* g_appState = nullptr;

// Colors for random text (RGB uint8)
struct RGB { uint8_t r, g, b; };
static RGB g_colors[] = {
    {255, 255, 255},  // white
    {0, 255, 0},      // green
    {0, 255, 255},    // cyan
    {255, 255, 0}     // yellow
};

// Generate random line from dictionary
static std::string generateLine(const std::vector<std::string>& dict, uint32_t maxCols) {
    std::string line;
    while (line.length() < maxCols - 10) {
        const std::string& word = dict[std::rand() % dict.size()];
        if (!line.empty()) line += " ";
        line += word;
    }
    return line;
}

#if !YETTY_WEB
// Mouse cursor position callback
void cursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state) return;

    state->mouseX = xpos;
    state->mouseY = ypos;

    // Route to plugins
    if (state->pluginManager && state->terminal) {
        float cellWidth = state->baseCellWidth * state->zoomLevel;
        float cellHeight = state->baseCellHeight * state->zoomLevel;
        int scrollOffset = state->terminal->getScrollOffset();

        state->pluginManager->onMouseMove(
            static_cast<float>(xpos), static_cast<float>(ypos),
            &state->terminal->getGrid(), cellWidth, cellHeight, scrollOffset);
    }
}

// Mouse button callback
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    (void)mods;
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state) return;

    // Route to plugins
    if (state->pluginManager && state->terminal) {
        float cellWidth = state->baseCellWidth * state->zoomLevel;
        float cellHeight = state->baseCellHeight * state->zoomLevel;
        int scrollOffset = state->terminal->getScrollOffset();

        bool consumed = state->pluginManager->onMouseButton(
            button, action == GLFW_PRESS,
            static_cast<float>(state->mouseX), static_cast<float>(state->mouseY),
            &state->terminal->getGrid(), cellWidth, cellHeight, scrollOffset);

        if (consumed) return;
    }

    // TODO: Handle terminal mouse events if not consumed by plugin
}

// Key callback for terminal mode
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->terminal) return;

    // Route to focused plugin first
    if (state->pluginManager) {
        if (state->pluginManager->onKey(key, scancode, action, mods)) {
            return;  // Plugin consumed the event
        }
    }

    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    VTermModifier vtermMod = VTERM_MOD_NONE;
    if (mods & GLFW_MOD_CONTROL) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_CTRL);
    if (mods & GLFW_MOD_ALT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_ALT);
    if (mods & GLFW_MOD_SHIFT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_SHIFT);

    // Handle Ctrl/Alt combinations - use glfwGetKeyName for correct keyboard layout
    if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) {
        const char* keyName = glfwGetKeyName(key, scancode);
        if (keyName && keyName[0] != '\0') {
            // Single character key name - send it with modifier
            if (keyName[1] == '\0') {
                uint32_t ch = keyName[0];
                std::cerr << "  -> Sending Ctrl/Alt+'" << (char)ch << "'" << std::endl;
                state->terminal->sendKey(ch, vtermMod);
                return;
            }
        }
    }

    // Map GLFW keys to VTerm keys
    switch (key) {
        case GLFW_KEY_ENTER:
            state->terminal->sendSpecialKey(VTERM_KEY_ENTER, vtermMod);
            break;
        case GLFW_KEY_BACKSPACE:
            state->terminal->sendSpecialKey(VTERM_KEY_BACKSPACE, vtermMod);
            break;
        case GLFW_KEY_TAB:
            state->terminal->sendSpecialKey(VTERM_KEY_TAB, vtermMod);
            break;
        case GLFW_KEY_ESCAPE:
            state->terminal->sendSpecialKey(VTERM_KEY_ESCAPE, vtermMod);
            break;
        case GLFW_KEY_UP:
            state->terminal->sendSpecialKey(VTERM_KEY_UP, vtermMod);
            break;
        case GLFW_KEY_DOWN:
            state->terminal->sendSpecialKey(VTERM_KEY_DOWN, vtermMod);
            break;
        case GLFW_KEY_LEFT:
            state->terminal->sendSpecialKey(VTERM_KEY_LEFT, vtermMod);
            break;
        case GLFW_KEY_RIGHT:
            state->terminal->sendSpecialKey(VTERM_KEY_RIGHT, vtermMod);
            break;
        case GLFW_KEY_HOME:
            state->terminal->sendSpecialKey(VTERM_KEY_HOME, vtermMod);
            break;
        case GLFW_KEY_END:
            state->terminal->sendSpecialKey(VTERM_KEY_END, vtermMod);
            break;
        case GLFW_KEY_PAGE_UP:
            state->terminal->sendSpecialKey(VTERM_KEY_PAGEUP, vtermMod);
            break;
        case GLFW_KEY_PAGE_DOWN:
            state->terminal->sendSpecialKey(VTERM_KEY_PAGEDOWN, vtermMod);
            break;
        case GLFW_KEY_INSERT:
            state->terminal->sendSpecialKey(VTERM_KEY_INS, vtermMod);
            break;
        case GLFW_KEY_DELETE:
            state->terminal->sendSpecialKey(VTERM_KEY_DEL, vtermMod);
            break;
        default:
            break;
    }
}

// Character callback for regular text input
void charCallback(GLFWwindow* window, unsigned int codepoint) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->terminal) return;

    // Route to focused plugin first
    if (state->pluginManager) {
        if (state->pluginManager->onChar(codepoint)) {
            return;  // Plugin consumed the event
        }
    }

    state->terminal->sendKey(codepoint);
}
#endif

// Main loop iteration (called by Emscripten or native loop)
static void mainLoopIteration() {
    if (!g_appState) return;

    auto& state = *g_appState;

    glfwPollEvents();

#if !YETTY_WEB
    // Terminal mode: update terminal and render its grid
    if (!state.demoMode && state.terminal) {
        state.terminal->update();

        // Update decorators
        static double lastTime = glfwGetTime();
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        if (state.pluginManager) {
            state.pluginManager->update(deltaTime);
        }

        if (!state.terminal->isRunning()) {
            glfwSetWindowShouldClose(state.window, GLFW_TRUE);
            return;
        }

        // Check if we need to render this frame
        auto cursorBlinkResult = state.terminal->updateCursorBlink(currentTime);
        bool cursorChanged = cursorBlinkResult && *cursorBlinkResult;
        bool hasDamage = state.terminal->hasDamage();
        bool hasPlugins = state.pluginManager && !state.pluginManager->getInstances().empty();

        // Skip rendering if nothing needs update
        if (!cursorChanged && !hasDamage && !hasPlugins) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60fps idle
            return;
        }

        // Get current window size
        int w, h;
        glfwGetFramebufferSize(state.window, &w, &h);
        if (w > 0 && h > 0) {
            state.renderer->resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }

        // Render terminal grid with cursor and damage tracking
        if (state.config && state.config->useDamageTracking) {
            state.renderer->render(*state.ctx, state.terminal->getGrid(),
                                   state.terminal->getDamageRects(),
                                   state.terminal->hasFullDamage(),
                                   state.terminal->getCursorCol(),
                                   state.terminal->getCursorRow(),
                                   state.terminal->isCursorVisible());
            // Clear damage after rendering
            state.terminal->clearDamageRects();
            state.terminal->clearFullDamage();
        } else {
            state.renderer->render(*state.ctx, state.terminal->getGrid(),
                                   state.terminal->getCursorCol(),
                                   state.terminal->getCursorRow(),
                                   state.terminal->isCursorVisible());
        }

        // Render plugin overlays (after terminal, before present)
        if (hasPlugins) {
            auto targetViewResult = state.ctx->getCurrentTextureView();
            if (targetViewResult) {
                float cellWidth = state.baseCellWidth * state.zoomLevel;
                float cellHeight = state.baseCellHeight * state.zoomLevel;
                int scrollOffset = state.terminal ? state.terminal->getScrollOffset() : 0;
                uint32_t termRows = state.terminal ? state.terminal->getGrid().getRows() : 0;
                state.pluginManager->render(*state.ctx, *targetViewResult,
                    static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                    cellWidth, cellHeight, scrollOffset, termRows);
            }
        }

        // Present the frame (releases cached texture view)
        state.ctx->present();
    } else
#endif
    {
        // Demo mode: scrolling text
        if (state.demoMode && state.demoGrid) {
            // Check for ESC key
            if (glfwGetKey(state.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(state.window, GLFW_TRUE);
#if YETTY_WEB
                emscripten_cancel_main_loop();
#endif
                return;
            }

            // Scrolling logic
            double currentTime = glfwGetTime();
            if (state.scrollMs > 0 && state.dictionary &&
                (currentTime - state.lastScrollTime) * 1000.0 >= state.scrollMs) {
                state.demoGrid->scrollUp();
                std::string newLine = generateLine(*state.dictionary, state.cols);
                RGB color = g_colors[std::rand() % 4];
                state.demoGrid->writeString(0, state.rows - 1, newLine.c_str(),
                                            color.r, color.g, color.b, state.font);
                state.lastScrollTime = currentTime;
            }

            // Get current window size
            int w, h;
            glfwGetFramebufferSize(state.window, &w, &h);
            if (w > 0 && h > 0) {
                state.renderer->resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }

            // Render demo grid
            state.renderer->render(*state.ctx, *state.demoGrid);
        }
    }

    // FPS counter
    double currentTime = glfwGetTime();
    state.frameCount++;
    if (currentTime - state.lastFpsTime >= 1.0) {
        std::cout << "FPS: " << state.frameCount << std::endl;
        state.frameCount = 0;
        state.lastFpsTime = currentTime;
    }
}

// Scroll callback - wheel=scroll, Ctrl+wheel=zoom
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->renderer) return;

    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;

#if !YETTY_WEB
    // Check if scroll is over a plugin first
    if (!ctrlPressed && state->pluginManager && state->terminal) {
        float cellWidth = state->baseCellWidth * state->zoomLevel;
        float cellHeight = state->baseCellHeight * state->zoomLevel;
        int scrollOffset = state->terminal->getScrollOffset();

        bool consumed = state->pluginManager->onMouseScroll(
            static_cast<float>(xoffset), static_cast<float>(yoffset),
            static_cast<float>(state->mouseX), static_cast<float>(state->mouseY),
            &state->terminal->getGrid(), cellWidth, cellHeight, scrollOffset);

        if (consumed) return;
    }

    // Scroll through scrollback (no Ctrl, not over plugin)
    if (!ctrlPressed && state->terminal) {
        int lines = static_cast<int>(yoffset * 3);  // 3 lines per scroll notch
        if (lines > 0) {
            state->terminal->scrollUp(lines);
        } else if (lines < 0) {
            state->terminal->scrollDown(-lines);
        }
        return;
    }
#endif

    // Zoom (Ctrl + wheel)
    state->zoomLevel += static_cast<float>(yoffset) * 0.1f;
    state->zoomLevel = glm::clamp(state->zoomLevel, 0.2f, 5.0f);

    // Update cell size and scale
    float newCellWidth = state->baseCellWidth * state->zoomLevel;
    float newCellHeight = state->baseCellHeight * state->zoomLevel;
    state->renderer->setCellSize(newCellWidth, newCellHeight);
    state->renderer->setScale(state->zoomLevel);

    // Get current window size
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    // Recalculate grid size based on new cell size
    uint32_t newCols = static_cast<uint32_t>(w / newCellWidth);
    uint32_t newRows = static_cast<uint32_t>(h / newCellHeight);

    if (newCols != state->cols || newRows != state->rows) {
        state->cols = newCols;
        state->rows = newRows;

#if !YETTY_WEB
        if (state->terminal) {
            state->terminal->resize(newCols, newRows);
        }
#endif
        if (state->demoGrid) {
            state->demoGrid->resize(newCols, newRows);
        }

        std::cout << "Zoom: " << (state->zoomLevel * 100.0f) << "% -> Grid "
                  << newCols << "x" << newRows << std::endl;
    }
}

// Default paths
#if YETTY_WEB
const char* DEFAULT_FONT = "/assets/DejaVuSansMono.ttf";
const char* DEFAULT_ATLAS = "/assets/atlas.png";
const char* DEFAULT_METRICS = "/assets/atlas.json";
#elif defined(_WIN32)
const char* DEFAULT_FONT = "C:/Windows/Fonts/consola.ttf";
const char* DEFAULT_ATLAS = "assets/atlas.png";
const char* DEFAULT_METRICS = "assets/atlas.json";
#elif defined(__APPLE__)
const char* DEFAULT_FONT = "/System/Library/Fonts/Monaco.ttf";
const char* DEFAULT_ATLAS = "assets/atlas.png";
const char* DEFAULT_METRICS = "assets/atlas.json";
#else
const char* DEFAULT_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
const char* DEFAULT_ATLAS = "assets/atlas.png";
const char* DEFAULT_METRICS = "assets/atlas.json";
#endif

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options] [font.ttf] [width] [height]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
#if !YETTY_USE_PREBUILT_ATLAS
    std::cerr << "  --generate-atlas   Generate atlas.png and atlas.json in assets/" << std::endl;
#endif
    std::cerr << "  --load-atlas       Use pre-built atlas instead of generating" << std::endl;
    std::cerr << "  --demo [scroll_ms] Run scrolling text demo (default: terminal mode)" << std::endl;
    std::cerr << "  --no-damage        Disable damage tracking (update full screen each frame)" << std::endl;
    std::cerr << "  --debug-damage     Log damage rectangle updates" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Arguments:" << std::endl;
    std::cerr << "  font.ttf   - Path to TTF font (default: system monospace)" << std::endl;
    std::cerr << "  width      - Window width in pixels (default: 1024)" << std::endl;
    std::cerr << "  height     - Window height in pixels (default: 768)" << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command line
    bool generateAtlasOnly = false;
    bool usePrebuiltAtlas = YETTY_USE_PREBUILT_ATLAS;
    bool demoMode = YETTY_WEB ? true : false;  // Web always uses demo mode
    bool useDamageTracking = true;
    bool debugDamageRects = false;
    int scrollMs = 50;
    const char* fontPath = DEFAULT_FONT;
    uint32_t width = 1024;
    uint32_t height = 768;

    int argIndex = 1;
    while (argIndex < argc && argv[argIndex][0] == '-') {
        if (std::strcmp(argv[argIndex], "--generate-atlas") == 0) {
            generateAtlasOnly = true;
        } else if (std::strcmp(argv[argIndex], "--load-atlas") == 0) {
            usePrebuiltAtlas = true;
        } else if (std::strcmp(argv[argIndex], "--demo") == 0) {
            demoMode = true;
            if (argIndex + 1 < argc && argv[argIndex + 1][0] != '-') {
                scrollMs = std::atoi(argv[++argIndex]);
            }
        } else if (std::strcmp(argv[argIndex], "--no-damage") == 0) {
            useDamageTracking = false;
        } else if (std::strcmp(argv[argIndex], "--debug-damage") == 0) {
            debugDamageRects = true;
        } else if (std::strcmp(argv[argIndex], "--help") == 0 || std::strcmp(argv[argIndex], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        argIndex++;
    }

    if (argIndex < argc) fontPath = argv[argIndex++];
    if (argIndex < argc) width = static_cast<uint32_t>(std::atoi(argv[argIndex++]));
    if (argIndex < argc) height = static_cast<uint32_t>(std::atoi(argv[argIndex++]));

    if (width == 0) width = 1024;
    if (height == 0) height = 768;

#if !YETTY_USE_PREBUILT_ATLAS
    // Generate atlas only mode (no window needed)
    if (generateAtlasOnly) {
        std::cout << "Generating font atlas from: " << fontPath << std::endl;

        Font font;
        float fontSize = 32.0f;
        if (!font.generate(fontPath, fontSize)) {
            std::cerr << "Failed to generate font atlas" << std::endl;
            return 1;
        }

        // Save to assets directory (relative to source for embedding in web build)
        std::string atlasDir = std::string(CMAKE_SOURCE_DIR) + "/assets";
        std::string atlasPath = atlasDir + "/atlas.png";
        std::string metricsPath = atlasDir + "/atlas.json";

        if (!font.saveAtlas(atlasPath, metricsPath)) {
            std::cerr << "Failed to save atlas" << std::endl;
            return 1;
        }

        std::cout << "Atlas saved to:" << std::endl;
        std::cout << "  " << atlasPath << std::endl;
        std::cout << "  " << metricsPath << std::endl;
        return 0;
    }
#endif

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // Don't create OpenGL context - we're using WebGPU
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(width, height, "yetty - WebGPU Terminal", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Initialize WebGPU
    WebGPUContext ctx;
    if (auto result = ctx.init(window, width, height); !result) {
        std::cerr << "Failed to initialize WebGPU: " << error_msg(result) << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Load or generate font atlas
    Font font;
    float fontSize = 32.0f;

#if YETTY_USE_PREBUILT_ATLAS
    // Web build: always use pre-built atlas
    std::cout << "Loading pre-built atlas..." << std::endl;
    if (!font.loadAtlas(DEFAULT_ATLAS, DEFAULT_METRICS)) {
        std::cerr << "Failed to load pre-built atlas from: " << DEFAULT_ATLAS << std::endl;
        std::cerr << "Make sure to generate the atlas first (native build with --generate-atlas)" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    fontSize = font.getFontSize();
#else
    // Native build: try cache first, then generate
    // Cache in ~/.cache/yetty/
    std::string cacheDir;
    const char* homeDir = getenv("HOME");
    if (homeDir) {
        cacheDir = std::string(homeDir) + "/.cache/yetty";
    } else {
        cacheDir = "/tmp/yetty-cache";
    }
    std::string cachedAtlas = cacheDir + "/atlas.png";
    std::string cachedMetrics = cacheDir + "/atlas.json";

    bool loadedFromCache = false;

    if (usePrebuiltAtlas) {
        // Try explicit pre-built atlas path
        std::cout << "Loading pre-built atlas..." << std::endl;
        if (font.loadAtlas(DEFAULT_ATLAS, DEFAULT_METRICS)) {
            fontSize = font.getFontSize();
            loadedFromCache = true;
        } else {
            std::cerr << "Failed to load atlas, falling back to cache/generation" << std::endl;
        }
    }

    if (!loadedFromCache) {
        // Try loading from cache
        std::ifstream cacheTest(cachedAtlas);
        if (cacheTest.good()) {
            cacheTest.close();
            std::cout << "Loading cached atlas from: " << cachedAtlas << std::endl;
            if (font.loadAtlas(cachedAtlas, cachedMetrics)) {
                fontSize = font.getFontSize();
                loadedFromCache = true;
            } else {
                std::cout << "Cache invalid, regenerating..." << std::endl;
            }
        }
    }

    if (!loadedFromCache) {
        // Generate atlas
        std::cout << "Generating font atlas from: " << fontPath << std::endl;
        if (!font.generate(fontPath, fontSize)) {
            std::cerr << "Failed to generate font atlas from: " << fontPath << std::endl;
            std::cerr << "Usage: " << argv[0] << " [path-to-ttf-font]" << std::endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        // Save to cache
        // Create cache directory
        std::string mkdirCmd = "mkdir -p \"" + cacheDir + "\"";
        system(mkdirCmd.c_str());

        std::cout << "Caching atlas to: " << cachedAtlas << std::endl;
        if (!font.saveAtlas(cachedAtlas, cachedMetrics)) {
            std::cerr << "Warning: Failed to cache atlas" << std::endl;
        }
    }
#endif

    if (!font.createTexture(ctx.getDevice(), ctx.getQueue())) {
        std::cerr << "Failed to create font texture" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Create config from command line args
    static Config config;
    config.useDamageTracking = useDamageTracking;
    config.showFps = true;
    config.debugDamageRects = debugDamageRects;

    // Initialize text renderer
    TextRenderer renderer;
    float cellWidth = fontSize * 0.6f;   // Approximate monospace width
    float cellHeight = fontSize * 1.2f;  // Line height
    renderer.setCellSize(cellWidth, cellHeight);
    renderer.resize(width, height);
    renderer.setConfig(&config);

    if (auto result = renderer.init(ctx, font); !result) {
        std::cerr << "Failed to initialize text renderer: " << error_msg(result) << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Calculate grid size based on window
    uint32_t cols = static_cast<uint32_t>(width / cellWidth);
    uint32_t rows = static_cast<uint32_t>(height / cellHeight);

    // Set up application state
    static AppState appState;
    appState.window = window;
    appState.ctx = &ctx;
    appState.renderer = &renderer;
    appState.font = &font;
    appState.config = &config;
    appState.baseCellWidth = cellWidth;
    appState.baseCellHeight = cellHeight;
    appState.zoomLevel = 1.0f;
    appState.demoMode = demoMode;
    appState.cols = cols;
    appState.rows = rows;
    appState.lastFpsTime = glfwGetTime();
    appState.frameCount = 0;

#if !YETTY_WEB
    Terminal* terminal = nullptr;
    Grid* demoGrid = nullptr;
#else
    Grid* demoGrid = nullptr;
#endif

    if (demoMode) {
        // Demo mode: scrolling text
        demoGrid = new Grid(cols, rows);
        appState.demoGrid = demoGrid;
        appState.scrollMs = scrollMs;
        appState.lastScrollTime = glfwGetTime();

        // Load dictionary
        static std::vector<std::string> dictionary;
#if !YETTY_WEB
        std::ifstream dictFile("/usr/share/dict/words");
        if (dictFile.is_open()) {
            std::string word;
            while (std::getline(dictFile, word)) {
                if (!word.empty() && word[0] >= 'a' && word[0] <= 'z') {
                    dictionary.push_back(word);
                }
            }
            std::cout << "Loaded " << dictionary.size() << " words from dictionary" << std::endl;
        } else
#endif
        {
            dictionary = {"hello", "world", "terminal", "webgpu", "render", "scroll", "test",
                         "browser", "wasm", "gpu", "shader", "pixel", "font", "text", "grid",
                         "cell", "color", "alpha", "buffer", "vertex", "fragment", "compute"};
            std::cout << "Using fallback dictionary with " << dictionary.size() << " words" << std::endl;
        }
        appState.dictionary = &dictionary;
        std::srand(static_cast<unsigned>(std::time(nullptr)));

        // Fill initial content
        for (uint32_t row = 0; row < rows; ++row) {
            std::string line = generateLine(dictionary, cols);
            RGB color = g_colors[std::rand() % 4];
            demoGrid->writeString(0, row, line.c_str(), color.r, color.g, color.b, &font);
        }

        std::cout << "Demo mode: Grid " << cols << "x" << rows << ", scroll: " << scrollMs << "ms" << std::endl;
    }
#if !YETTY_WEB
    else {
        // Terminal mode
        terminal = new Terminal(cols, rows, &font);
        terminal->setConfig(&config);
        appState.terminal = terminal;

        // Create and configure PluginManager
        PluginManager* pluginMgr = new PluginManager();
        appState.pluginManager = pluginMgr;

        // Register built-in plugins
        pluginMgr->registerPlugin("shader", ShaderToy::create);
        pluginMgr->registerPlugin("image", Image::create);
#ifdef YETTY_YMERY_ENABLED
        pluginMgr->registerPlugin("ymery", Ymery::create);
#endif

        // Load external plugins from directory (if exists)
        const char* pluginDir = getenv("YETTY_PLUGINS_DIR");
        if (pluginDir) {
            pluginMgr->loadPluginsFromDirectory(pluginDir);
        }

        // Wire up plugin manager to terminal
        terminal->setPluginManager(pluginMgr);
        terminal->setCellSize(static_cast<uint32_t>(cellWidth), static_cast<uint32_t>(cellHeight));

        if (auto result = terminal->start(); !result) {
            std::cerr << "Failed to start terminal: " << error_msg(result) << std::endl;
            delete pluginMgr;
            delete terminal;
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        std::cout << "Terminal mode: Grid " << cols << "x" << rows
                  << " (damage tracking: " << (config.useDamageTracking ? "on" : "off") << ")" << std::endl;

        // Set up keyboard and mouse callbacks
        glfwSetKeyCallback(window, keyCallback);
        glfwSetCharCallback(window, charCallback);
        glfwSetCursorPosCallback(window, cursorPosCallback);
        glfwSetMouseButtonCallback(window, mouseButtonCallback);
    }
#endif

    g_appState = &appState;
    glfwSetWindowUserPointer(window, &appState);

    // Window resize callback
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int newWidth, int newHeight) {
        if (newWidth == 0 || newHeight == 0) return;
        auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(w));
        if (!state) return;

        // Resize WebGPU context
        if (state->ctx) {
            state->ctx->resize(static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));
        }

        // Recalculate grid size based on current cell size
        float cellWidth = state->baseCellWidth * state->zoomLevel;
        float cellHeight = state->baseCellHeight * state->zoomLevel;
        uint32_t newCols = static_cast<uint32_t>(newWidth / cellWidth);
        uint32_t newRows = static_cast<uint32_t>(newHeight / cellHeight);

        if (newCols != state->cols || newRows != state->rows) {
            state->cols = newCols;
            state->rows = newRows;

#if !YETTY_WEB
            if (state->terminal) {
                state->terminal->resize(newCols, newRows);
            }
#endif
            if (state->demoGrid) {
                state->demoGrid->resize(newCols, newRows);
            }

            std::cout << "Window resize -> Grid " << newCols << "x" << newRows << std::endl;
        }
    });

    // Scroll callback for zooming
    glfwSetScrollCallback(window, scrollCallback);

    std::cout << "Starting render loop... (use mouse scroll to zoom, ESC to exit)" << std::endl;

#if YETTY_WEB
    // Emscripten main loop - runs forever, returns immediately
    emscripten_set_main_loop(mainLoopIteration, 0, false);
    return 0;
#else
    // Native main loop
    while (!glfwWindowShouldClose(window)) {
        mainLoopIteration();
    }

    std::cout << "Shutting down..." << std::endl;

    delete appState.pluginManager;
    delete terminal;
    delete demoGrid;

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
#endif
}
