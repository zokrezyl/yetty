#include "yetty/renderer/webgpu-context.h"
#include "yetty/renderer/text-renderer.h"
#include "yetty/terminal/grid.h"
#include "yetty/terminal/font.h"
#include "yetty/config.h"

#if !YETTY_WEB
#include "yetty/terminal/terminal.h"
#include "yetty/plugin-manager.h"
#include "yetty/plugins/shader-toy/shader-toy.h"
#include "yetty/plugins/shader-glyph/shader-glyph.h"
#include "yetty/plugins/image/image.h"
#ifdef YETTY_YMERY_ENABLED
#include "yetty/plugins/ymery/ymery.h"
#endif
#include <termios.h>
#include <unistd.h>
#endif

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
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
#include <filesystem>

#if YETTY_WEB
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace yetty;

// Helper macro to check Result in callbacks where we can't return errors
#define CHECK_RESULT(expr) \
    do { \
        if (auto _res = (expr); !_res) { \
            spdlog::error("{}: {}", #expr, _res.error().message()); \
        } \
    } while(0)

// Application state for callbacks and main loop
struct AppState {
    GLFWwindow* _window = nullptr;
    WebGPUContext* _ctx = nullptr;
    TextRenderer* _renderer = nullptr;
    Font* _font = nullptr;
    Config* _config = nullptr;
    float _zoomLevel = 1.0f;
    float _baseCellWidth = 0.0f;
    float _baseCellHeight = 0.0f;

    // Mouse tracking for plugin input
    double _mouseX = 0.0;
    double _mouseY = 0.0;

    // Selection state
    bool _selecting = false;
    double _lastClickTime = 0.0;
    int _clickCount = 0;  // For double/triple click detection

#if !YETTY_WEB
    Terminal* _terminal = nullptr;
    PluginManager* _pluginManager = nullptr;
    struct termios _originalTermios;
    bool _termiosSet = false;
#endif

    // Demo mode (scrolling text)
    bool _demoMode = false;
    Grid* _demoGrid = nullptr;
    int _scrollMs = 50;
    double _lastScrollTime = 0.0;
    std::vector<std::string>* _dictionary = nullptr;
    uint32_t _cols = 80;
    uint32_t _rows = 24;

    // FPS tracking
    double _lastFpsTime = 0.0;
    uint32_t _frameCount = 0;
};

// Global state for Emscripten main loop
static AppState* g_appState = nullptr;

// Colors for random text (RGB uint8)
struct RGB { uint8_t _r, _g, _b; };
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

    state->_mouseX = xpos;
    state->_mouseY = ypos;

    // Handle selection extension while dragging
    if (state->_selecting && state->_terminal) {
        float cellWidth = state->_baseCellWidth * state->_zoomLevel;
        float cellHeight = state->_baseCellHeight * state->_zoomLevel;

        int col = static_cast<int>(xpos / cellWidth);
        int row = static_cast<int>(ypos / cellHeight);

        state->_terminal->extendSelection(row, col);
        return;  // Don't route to plugins while selecting
    }

    // Route to plugins
    if (state->_pluginManager && state->_terminal) {
        float cellWidth = state->_baseCellWidth * state->_zoomLevel;
        float cellHeight = state->_baseCellHeight * state->_zoomLevel;
        int scrollOffset = state->_terminal->getScrollOffset();

        state->_pluginManager->onMouseMove(
            static_cast<float>(xpos), static_cast<float>(ypos),
            &state->_terminal->getGrid(), cellWidth, cellHeight, scrollOffset);
    }
}

// Mouse button callback
void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->_terminal) return;

    float cellWidth = state->_baseCellWidth * state->_zoomLevel;
    float cellHeight = state->_baseCellHeight * state->_zoomLevel;
    int scrollOffset = state->_terminal->getScrollOffset();

    // Check if Shift is held - force selection mode even if app wants mouse
    bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;

    // If app wants mouse events and Shift is not held, pass to app
    if (!shiftHeld && state->_terminal->wantsMouseEvents()) {
        // Convert pixel coords to cell coords for vterm
        int col = static_cast<int>(state->_mouseX / cellWidth);
        int row = static_cast<int>(state->_mouseY / cellHeight);

        // Send mouse event to vterm (which sends to PTY)
        VTermModifier vtermMod = VTERM_MOD_NONE;
        if (mods & GLFW_MOD_CONTROL) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_CTRL);
        if (mods & GLFW_MOD_ALT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_ALT);
        if (mods & GLFW_MOD_SHIFT) vtermMod = (VTermModifier)(vtermMod | VTERM_MOD_SHIFT);

        // Get the vterm and send mouse event
        // Note: vterm buttons are 1-indexed (1=left, 2=middle, 3=right)
        int vtermButton = button + 1;
        VTerm* vterm = nullptr;  // TODO: expose vterm from Terminal if needed
        // For now, skip direct vterm mouse - apps will work via plugin system
        (void)vtermButton;
        (void)vtermMod;
        (void)vterm;
        (void)row;
        (void)col;
    }

    // Route to plugins first (unless selecting)
    if (!state->_selecting && state->_pluginManager) {
        bool consumed = state->_pluginManager->onMouseButton(
            button, action == GLFW_PRESS,
            static_cast<float>(state->_mouseX), static_cast<float>(state->_mouseY),
            &state->_terminal->getGrid(), cellWidth, cellHeight, scrollOffset);

        if (consumed) return;
    }

    // Handle middle mouse button for paste
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        const char* clipboard = glfwGetClipboardString(window);
        if (clipboard && *clipboard) {
            state->_terminal->clearSelection();
            CHECK_RESULT(state->_terminal->sendRaw(clipboard, strlen(clipboard)));
            spdlog::debug("Pasted {} bytes from clipboard (middle-click)", strlen(clipboard));
        }
        return;
    }

    // Handle left mouse button for selection
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        int col = static_cast<int>(state->_mouseX / cellWidth);
        int row = static_cast<int>(state->_mouseY / cellHeight);

        if (action == GLFW_PRESS) {
            // Detect double/triple click
            double currentTime = glfwGetTime();
            double timeSinceLastClick = currentTime - state->_lastClickTime;

            if (timeSinceLastClick < 0.3) {  // 300ms threshold
                state->_clickCount++;
                if (state->_clickCount > 3) state->_clickCount = 1;
            } else {
                state->_clickCount = 1;
            }
            state->_lastClickTime = currentTime;

            // Clear any existing selection on new click
            state->_terminal->clearSelection();

            // Start selection based on click count
            SelectionMode mode = SelectionMode::Character;
            if (state->_clickCount == 2) {
                mode = SelectionMode::Word;
            } else if (state->_clickCount == 3) {
                mode = SelectionMode::Line;
            }

            state->_terminal->startSelection(row, col, mode);
            state->_selecting = true;

        } else if (action == GLFW_RELEASE) {
            state->_selecting = false;

            // Copy selection to clipboard if any
            if (state->_terminal->hasSelection()) {
                std::string selectedText = state->_terminal->getSelectedText();
                if (!selectedText.empty()) {
                    glfwSetClipboardString(window, selectedText.c_str());
                    spdlog::debug("Copied {} bytes to clipboard", selectedText.size());
                }
            }
        }
    }
}

// Key callback for terminal mode
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->_terminal) return;

    // Handle clipboard shortcuts (Ctrl+Shift+C/V or Shift+Insert)
    if (action == GLFW_PRESS) {
        bool ctrlShift = (mods & GLFW_MOD_CONTROL) && (mods & GLFW_MOD_SHIFT);

        // Ctrl+Shift+C: Copy selection
        if (ctrlShift && key == GLFW_KEY_C) {
            if (state->_terminal->hasSelection()) {
                std::string selectedText = state->_terminal->getSelectedText();
                if (!selectedText.empty()) {
                    glfwSetClipboardString(window, selectedText.c_str());
                    spdlog::debug("Copied {} bytes to clipboard (Ctrl+Shift+C)", selectedText.size());
                }
            }
            return;
        }

        // Ctrl+Shift+V or Shift+Insert: Paste
        if ((ctrlShift && key == GLFW_KEY_V) ||
            ((mods & GLFW_MOD_SHIFT) && key == GLFW_KEY_INSERT)) {
            const char* clipboard = glfwGetClipboardString(window);
            if (clipboard && *clipboard) {
                // Clear selection before pasting
                state->_terminal->clearSelection();
                // Send clipboard content to terminal
                CHECK_RESULT(state->_terminal->sendRaw(clipboard, strlen(clipboard)));
                spdlog::debug("Pasted {} bytes from clipboard", strlen(clipboard));
            }
            return;
        }

        // Escape clears selection
        if (key == GLFW_KEY_ESCAPE && state->_terminal->hasSelection()) {
            state->_terminal->clearSelection();
            // Don't return - still send ESC to terminal
        }
    }

    // Route to focused plugin first
    if (state->_pluginManager) {
        if (state->_pluginManager->onKey(key, scancode, action, mods)) {
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
                spdlog::debug("Sending Ctrl/Alt+'{}'", (char)ch);
                CHECK_RESULT(state->_terminal->sendKey(ch, vtermMod));
                return;
            }
        }
    }

    // Map GLFW keys to VTerm keys
    switch (key) {
        case GLFW_KEY_ENTER:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_ENTER, vtermMod));
            break;
        case GLFW_KEY_BACKSPACE:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_BACKSPACE, vtermMod));
            break;
        case GLFW_KEY_TAB:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_TAB, vtermMod));
            break;
        case GLFW_KEY_ESCAPE:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_ESCAPE, vtermMod));
            break;
        case GLFW_KEY_UP:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_UP, vtermMod));
            break;
        case GLFW_KEY_DOWN:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_DOWN, vtermMod));
            break;
        case GLFW_KEY_LEFT:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_LEFT, vtermMod));
            break;
        case GLFW_KEY_RIGHT:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_RIGHT, vtermMod));
            break;
        case GLFW_KEY_HOME:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_HOME, vtermMod));
            break;
        case GLFW_KEY_END:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_END, vtermMod));
            break;
        case GLFW_KEY_PAGE_UP:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_PAGEUP, vtermMod));
            break;
        case GLFW_KEY_PAGE_DOWN:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_PAGEDOWN, vtermMod));
            break;
        case GLFW_KEY_INSERT:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_INS, vtermMod));
            break;
        case GLFW_KEY_DELETE:
            CHECK_RESULT(state->_terminal->sendSpecialKey(VTERM_KEY_DEL, vtermMod));
            break;
        default:
            break;
    }
}

// Character callback for regular text input
void charCallback(GLFWwindow* window, unsigned int codepoint) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->_terminal) return;

    // Route to focused plugin first
    if (state->_pluginManager) {
        if (state->_pluginManager->onChar(codepoint)) {
            return;  // Plugin consumed the event
        }
    }

    CHECK_RESULT(state->_terminal->sendKey(codepoint));
}
#endif

// Main loop iteration (called by Emscripten or native loop)
static void mainLoopIteration() {
    if (!g_appState) return;

    auto& state = *g_appState;

    glfwPollEvents();

#if !YETTY_WEB
    // Terminal mode: update terminal and render its grid
    if (!state._demoMode && state._terminal) {
        CHECK_RESULT(state._terminal->update());

        // Update decorators
        static double lastTime = glfwGetTime();
        double currentTime = glfwGetTime();
        double deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        if (state._pluginManager) {
            if (auto res = state._pluginManager->update(deltaTime); !res) {
                spdlog::error("Plugin update failed: {}", error_msg(res));
            }
            // Update custom glyph plugins (for animation)
            state._pluginManager->updateCustomGlyphs(deltaTime);
        }

        if (!state._terminal->isRunning()) {
            glfwSetWindowShouldClose(state._window, GLFW_TRUE);
            return;
        }

        // Check if we need to render this frame
        auto cursorBlinkResult = state._terminal->updateCursorBlink(currentTime);
        bool cursorChanged = cursorBlinkResult && *cursorBlinkResult;
        bool hasDamage = state._terminal->hasDamage();
        bool hasPlugins = state._pluginManager && !state._pluginManager->getAllLayers().empty();

        // Skip rendering if nothing needs update
        if (!cursorChanged && !hasDamage && !hasPlugins) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60fps idle
            return;
        }

        // Upload any pending fallback glyphs to GPU
        if (state._font && state._font->hasPendingGlyphs()) {
            state._font->uploadPendingGlyphs(state._ctx->getDevice(), state._ctx->getQueue());
            // Need to recreate the bind group with the new glyph metadata buffer
            state._renderer->updateFontBindings(*state._font);
        }

        // Get current window size
        int w, h;
        glfwGetFramebufferSize(state._window, &w, &h);
        if (w > 0 && h > 0) {
            state._renderer->resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }

        // Render terminal grid with cursor and damage tracking
        if (state._config && state._config->_useDamageTracking) {
            state._renderer->render(*state._ctx, state._terminal->getGrid(),
                                   state._terminal->getDamageRects(),
                                   state._terminal->hasFullDamage(),
                                   state._terminal->getCursorCol(),
                                   state._terminal->getCursorRow(),
                                   state._terminal->isCursorVisible());
            // Clear damage after rendering
            state._terminal->clearDamageRects();
            state._terminal->clearFullDamage();
        } else {
            state._renderer->render(*state._ctx, state._terminal->getGrid(),
                                   state._terminal->getCursorCol(),
                                   state._terminal->getCursorRow(),
                                   state._terminal->isCursorVisible());
        }

        // Render custom glyph layers (animated emoji, etc.) - after grid, before decorator plugins
        if (state._pluginManager) {
            auto targetViewResult = state._ctx->getCurrentTextureView();
            if (targetViewResult) {
                float cellWidth = state._baseCellWidth * state._zoomLevel;
                float cellHeight = state._baseCellHeight * state._zoomLevel;
                int scrollOffset = state._terminal ? state._terminal->getScrollOffset() : 0;
                if (auto res = state._pluginManager->renderCustomGlyphs(
                        *state._ctx, *targetViewResult,
                        static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                        cellWidth, cellHeight, scrollOffset); !res) {
                    spdlog::error("Custom glyph render failed: {}", error_msg(res));
                }
            }
        }

        // Render plugin overlays (after terminal, before present)
        if (hasPlugins) {
            auto targetViewResult = state._ctx->getCurrentTextureView();
            if (targetViewResult) {
                float cellWidth = state._baseCellWidth * state._zoomLevel;
                float cellHeight = state._baseCellHeight * state._zoomLevel;
                int scrollOffset = state._terminal ? state._terminal->getScrollOffset() : 0;
                uint32_t termRows = state._terminal ? state._terminal->getGrid().getRows() : 0;
                if (auto res = state._pluginManager->render(*state._ctx, *targetViewResult,
                    static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                    cellWidth, cellHeight, scrollOffset, termRows); !res) {
                    spdlog::error("Plugin render failed: {}", error_msg(res));
                }
            }
        }

        // Present the frame (releases cached texture view)
        state._ctx->present();
    } else
#endif
    {
        // Demo mode: scrolling text
        if (state._demoMode && state._demoGrid) {
            // Check for ESC key
            if (glfwGetKey(state._window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(state._window, GLFW_TRUE);
#if YETTY_WEB
                emscripten_cancel_main_loop();
#endif
                return;
            }

            // Scrolling logic
            double currentTime = glfwGetTime();
            if (state._scrollMs > 0 && state._dictionary &&
                (currentTime - state._lastScrollTime) * 1000.0 >= state._scrollMs) {
                state._demoGrid->scrollUp();
                std::string newLine = generateLine(*state._dictionary, state._cols);
                RGB color = g_colors[std::rand() % 4];
                state._demoGrid->writeString(0, state._rows - 1, newLine.c_str(),
                                            color._r, color._g, color._b, state._font);
                state._lastScrollTime = currentTime;
            }

            // Get current window size
            int w, h;
            glfwGetFramebufferSize(state._window, &w, &h);
            if (w > 0 && h > 0) {
                state._renderer->resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
            }

            // Render demo grid
            state._renderer->render(*state._ctx, *state._demoGrid);
        }
    }

    // FPS counter
    double currentTime = glfwGetTime();
    state._frameCount++;
    if (currentTime - state._lastFpsTime >= 1.0) {
        std::cout << "FPS: " << state._frameCount << std::endl;
        state._frameCount = 0;
        state._lastFpsTime = currentTime;
    }
}

// Scroll callback - wheel=scroll, Ctrl+wheel=zoom
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->_renderer) return;

    bool ctrlPressed = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                       glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    int mods = ctrlPressed ? 0x0002 : 0;  // GLFW_MOD_CONTROL = 0x0002

#if !YETTY_WEB
    // Check if scroll is over a plugin first (with or without Ctrl)
    if (state->_pluginManager && state->_terminal) {
        float cellWidth = state->_baseCellWidth * state->_zoomLevel;
        float cellHeight = state->_baseCellHeight * state->_zoomLevel;
        int scrollOffset = state->_terminal->getScrollOffset();

        bool consumed = state->_pluginManager->onMouseScroll(
            static_cast<float>(xoffset), static_cast<float>(yoffset), mods,
            static_cast<float>(state->_mouseX), static_cast<float>(state->_mouseY),
            &state->_terminal->getGrid(), cellWidth, cellHeight, scrollOffset);

        if (consumed) return;
    }

    // Scroll through scrollback (no Ctrl, not over plugin)
    if (!ctrlPressed && state->_terminal) {
        int lines = static_cast<int>(yoffset * 3);  // 3 lines per scroll notch
        if (lines > 0) {
            state->_terminal->scrollUp(lines);
        } else if (lines < 0) {
            state->_terminal->scrollDown(-lines);
        }
        return;
    }
#endif

    // Zoom grid (Ctrl + wheel, not over plugin)
    state->_zoomLevel += static_cast<float>(yoffset) * 0.1f;
    state->_zoomLevel = glm::clamp(state->_zoomLevel, 0.2f, 5.0f);

    // Update cell size and scale
    float newCellWidth = state->_baseCellWidth * state->_zoomLevel;
    float newCellHeight = state->_baseCellHeight * state->_zoomLevel;
    state->_renderer->setCellSize(newCellWidth, newCellHeight);
    state->_renderer->setScale(state->_zoomLevel);

    // Get current window size
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    // Recalculate grid size based on new cell size
    uint32_t newCols = static_cast<uint32_t>(w / newCellWidth);
    uint32_t newRows = static_cast<uint32_t>(h / newCellHeight);

    if (newCols != state->_cols || newRows != state->_rows) {
        state->_cols = newCols;
        state->_rows = newRows;

#if !YETTY_WEB
        if (state->_terminal) {
            state->_terminal->resize(newCols, newRows);
        }
#endif
        if (state->_demoGrid) {
            state->_demoGrid->resize(newCols, newRows);
        }

        std::cout << "Zoom: " << (state->_zoomLevel * 100.0f) << "% -> Grid "
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
    // Set up logging
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("yetty starting...");

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
        std::filesystem::create_directories(cacheDir);

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
    config._useDamageTracking = useDamageTracking;
    config._showFps = true;
    config._debugDamageRects = debugDamageRects;

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
    appState._window = window;
    appState._ctx = &ctx;
    appState._renderer = &renderer;
    appState._font = &font;
    appState._config = &config;
    appState._baseCellWidth = cellWidth;
    appState._baseCellHeight = cellHeight;
    appState._zoomLevel = 1.0f;
    appState._demoMode = demoMode;
    appState._cols = cols;
    appState._rows = rows;
    appState._lastFpsTime = glfwGetTime();
    appState._frameCount = 0;

#if !YETTY_WEB
    Terminal* terminal = nullptr;
    Grid* demoGrid = nullptr;
#else
    Grid* demoGrid = nullptr;
#endif

    if (demoMode) {
        // Demo mode: scrolling text
        demoGrid = new Grid(cols, rows);
        appState._demoGrid = demoGrid;
        appState._scrollMs = scrollMs;
        appState._lastScrollTime = glfwGetTime();

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
        appState._dictionary = &dictionary;
        std::srand(static_cast<unsigned>(std::time(nullptr)));

        // Fill initial content
        for (uint32_t row = 0; row < rows; ++row) {
            std::string line = generateLine(dictionary, cols);
            RGB color = g_colors[std::rand() % 4];
            demoGrid->writeString(0, row, line.c_str(), color._r, color._g, color._b, &font);
        }

        std::cout << "Demo mode: Grid " << cols << "x" << rows << ", scroll: " << scrollMs << "ms" << std::endl;
    }
#if !YETTY_WEB
    else {
        // Terminal mode
        terminal = new Terminal(cols, rows, &font);
        terminal->setConfig(&config);
        appState._terminal = terminal;

        // Create and configure PluginManager
        PluginManager* pluginMgr = new PluginManager();
        appState._pluginManager = pluginMgr;

        // Register built-in plugins
        pluginMgr->registerPlugin("shader", ShaderToy::create);
        pluginMgr->registerPlugin("image", Image::create);
#ifdef YETTY_YMERY_ENABLED
        pluginMgr->registerPlugin("ymery", Ymery::create);
#endif

        // Register custom glyph plugins (for animated emoji, etc.)
        if (auto shaderGlyphResult = ShaderGlyphPlugin::create()) {
            pluginMgr->registerCustomGlyphPlugin(*shaderGlyphResult);
        }

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
                  << " (damage tracking: " << (config._useDamageTracking ? "on" : "off") << ")" << std::endl;

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
        if (state->_ctx) {
            state->_ctx->resize(static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));
        }

        // Recalculate grid size based on current cell size
        float cellWidth = state->_baseCellWidth * state->_zoomLevel;
        float cellHeight = state->_baseCellHeight * state->_zoomLevel;
        uint32_t newCols = static_cast<uint32_t>(newWidth / cellWidth);
        uint32_t newRows = static_cast<uint32_t>(newHeight / cellHeight);

        if (newCols != state->_cols || newRows != state->_rows) {
            state->_cols = newCols;
            state->_rows = newRows;

#if !YETTY_WEB
            if (state->_terminal) {
                state->_terminal->resize(newCols, newRows);
            }
#endif
            if (state->_demoGrid) {
                state->_demoGrid->resize(newCols, newRows);
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

    delete appState._pluginManager;
    delete terminal;
    delete demoGrid;

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
#endif
}
