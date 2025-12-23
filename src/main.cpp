#include "renderer/WebGPUContext.h"
#include "renderer/TextRenderer.h"
#include "terminal/Grid.h"
#include "terminal/Font.h"

#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
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
    Grid* grid = nullptr;
    float zoomLevel = 1.0f;
    float baseCellWidth = 0.0f;
    float baseCellHeight = 0.0f;

    // Scrolling state
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

// Colors for random text
static glm::vec4 g_colors[] = {
    {1.0f, 1.0f, 1.0f, 1.0f},  // white
    {0.0f, 1.0f, 0.0f, 1.0f},  // green
    {0.0f, 1.0f, 1.0f, 1.0f},  // cyan
    {1.0f, 1.0f, 0.0f, 1.0f}   // yellow
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

// Main loop iteration (called by Emscripten or native loop)
static void mainLoopIteration() {
    if (!g_appState) return;

    auto& state = *g_appState;

    glfwPollEvents();

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
        state.grid->scrollUp();
        std::string newLine = generateLine(*state.dictionary, state.cols);
        glm::vec4 color = g_colors[std::rand() % 4];
        state.grid->writeString(0, state.rows - 1, newLine.c_str(), color);
        state.lastScrollTime = currentTime;
    }

    // Get current window size
    int w, h;
    glfwGetFramebufferSize(state.window, &w, &h);
    if (w > 0 && h > 0) {
        state.renderer->resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    }

    // Render
    state.renderer->render(*state.ctx, *state.grid);

    // FPS counter
    state.frameCount++;
    if (currentTime - state.lastFpsTime >= 1.0) {
        std::cout << "FPS: " << state.frameCount << std::endl;
        state.frameCount = 0;
        state.lastFpsTime = currentTime;
    }
}

// Scroll callback for zooming
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    (void)xoffset;
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!state || !state->renderer) return;

    // Adjust zoom level
    state->zoomLevel += static_cast<float>(yoffset) * 0.1f;
    state->zoomLevel = glm::clamp(state->zoomLevel, 0.2f, 5.0f);

    // Update cell size and scale
    float newCellWidth = state->baseCellWidth * state->zoomLevel;
    float newCellHeight = state->baseCellHeight * state->zoomLevel;
    state->renderer->setCellSize(newCellWidth, newCellHeight);
    state->renderer->setScale(state->zoomLevel);

    std::cout << "Zoom: " << (state->zoomLevel * 100.0f) << "% (cell: "
              << newCellWidth << "x" << newCellHeight << ")" << std::endl;
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
    std::cerr << "Usage: " << prog << " [options] [font.ttf] [width] [height] [scroll_ms]" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Options:" << std::endl;
#if !YETTY_USE_PREBUILT_ATLAS
    std::cerr << "  --generate-atlas   Generate atlas.png and atlas.json in assets/" << std::endl;
#endif
    std::cerr << "  --load-atlas       Use pre-built atlas instead of generating" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Arguments:" << std::endl;
    std::cerr << "  font.ttf   - Path to TTF font (default: system monospace)" << std::endl;
    std::cerr << "  width      - Window width in pixels (default: 1024)" << std::endl;
    std::cerr << "  height     - Window height in pixels (default: 768)" << std::endl;
    std::cerr << "  scroll_ms  - Scroll speed in ms (default: 50, 0=static demo)" << std::endl;
}

int main(int argc, char* argv[]) {
    // Parse command line
    bool generateAtlasOnly = false;
    bool usePrebuiltAtlas = YETTY_USE_PREBUILT_ATLAS;
    const char* fontPath = DEFAULT_FONT;
    uint32_t width = 1024;
    uint32_t height = 768;
    int scrollMs = 50;

    int argIndex = 1;
    while (argIndex < argc && argv[argIndex][0] == '-') {
        if (std::strcmp(argv[argIndex], "--generate-atlas") == 0) {
            generateAtlasOnly = true;
        } else if (std::strcmp(argv[argIndex], "--load-atlas") == 0) {
            usePrebuiltAtlas = true;
        } else if (std::strcmp(argv[argIndex], "--help") == 0 || std::strcmp(argv[argIndex], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        argIndex++;
    }

    if (argIndex < argc) fontPath = argv[argIndex++];
    if (argIndex < argc) width = static_cast<uint32_t>(std::atoi(argv[argIndex++]));
    if (argIndex < argc) height = static_cast<uint32_t>(std::atoi(argv[argIndex++]));
    if (argIndex < argc) scrollMs = std::atoi(argv[argIndex++]);

    if (width == 0) width = 1024;
    if (height == 0) height = 768;
    if (scrollMs < 0) scrollMs = 50;

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
    if (!ctx.init(window, width, height)) {
        std::cerr << "Failed to initialize WebGPU" << std::endl;
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
    // Native build: generate or load
    if (usePrebuiltAtlas) {
        std::cout << "Loading pre-built atlas..." << std::endl;
        if (!font.loadAtlas(DEFAULT_ATLAS, DEFAULT_METRICS)) {
            std::cerr << "Failed to load atlas, falling back to generation" << std::endl;
            usePrebuiltAtlas = false;
        } else {
            fontSize = font.getFontSize();
        }
    }

    if (!usePrebuiltAtlas) {
        std::cout << "Generating font atlas from: " << fontPath << std::endl;
        if (!font.generate(fontPath, fontSize)) {
            std::cerr << "Failed to generate font atlas from: " << fontPath << std::endl;
            std::cerr << "Usage: " << argv[0] << " [path-to-ttf-font]" << std::endl;
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
    }
#endif

    if (!font.createTexture(ctx.getDevice(), ctx.getQueue())) {
        std::cerr << "Failed to create font texture" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Initialize text renderer
    TextRenderer renderer;
    float cellWidth = fontSize * 0.6f;   // Approximate monospace width
    float cellHeight = fontSize * 1.2f;  // Line height
    renderer.setCellSize(cellWidth, cellHeight);
    renderer.resize(width, height);

    if (!renderer.init(ctx, font)) {
        std::cerr << "Failed to initialize text renderer" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Calculate grid size based on window
    uint32_t cols = static_cast<uint32_t>(width / cellWidth);
    uint32_t rows = static_cast<uint32_t>(height / cellHeight);
    Grid grid(cols, rows);

    // Load dictionary for scrolling demo
    static std::vector<std::string> dictionary;
    {
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
            // Fallback words (used for web and when dict not available)
            dictionary = {"hello", "world", "terminal", "webgpu", "render", "scroll", "test",
                         "browser", "wasm", "gpu", "shader", "pixel", "font", "text", "grid",
                         "cell", "color", "alpha", "buffer", "vertex", "fragment", "compute",
                         "async", "await", "promise", "module", "export", "import", "class"};
            std::cout << "Using fallback dictionary with " << dictionary.size() << " words" << std::endl;
        }
    }
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    std::cout << "Grid: " << cols << "x" << rows << ", scroll: " << scrollMs << "ms" << std::endl;

    // Fill initial content
    for (uint32_t row = 0; row < rows; ++row) {
        std::string line = generateLine(dictionary, cols);
        glm::vec4 color = g_colors[std::rand() % 4];
        grid.writeString(0, row, line.c_str(), color);
    }

    // Set up application state for callbacks and main loop
    static AppState appState;
    appState.window = window;
    appState.ctx = &ctx;
    appState.renderer = &renderer;
    appState.grid = &grid;
    appState.baseCellWidth = cellWidth;
    appState.baseCellHeight = cellHeight;
    appState.zoomLevel = 1.0f;
    appState.scrollMs = scrollMs;
    appState.dictionary = &dictionary;
    appState.cols = cols;
    appState.rows = rows;
    appState.lastScrollTime = glfwGetTime();
    appState.lastFpsTime = glfwGetTime();
    appState.frameCount = 0;

    g_appState = &appState;

    glfwSetWindowUserPointer(window, &appState);

    // Window resize callback
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int newWidth, int newHeight) {
        if (newWidth == 0 || newHeight == 0) return;
        auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(w));
        if (state && state->ctx) {
            state->ctx->resize(static_cast<uint32_t>(newWidth), static_cast<uint32_t>(newHeight));
        }
    });

    // Scroll callback for zooming
    glfwSetScrollCallback(window, scrollCallback);

    std::cout << "Starting render loop... (use mouse scroll to zoom)" << std::endl;
    if (scrollMs > 0) {
        std::cout << "Scrolling mode: new line every " << scrollMs << "ms" << std::endl;
    } else {
        std::cout << "Static mode: no scrolling" << std::endl;
    }

#if YETTY_WEB
    // Emscripten main loop - runs forever, returns immediately
    emscripten_set_main_loop(mainLoopIteration, 0, false);
    // Don't cleanup - Emscripten keeps running
    return 0;
#else
    // Native main loop
    while (!glfwWindowShouldClose(window)) {
        mainLoopIteration();
    }

    std::cout << "Shutting down..." << std::endl;

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
#endif
}
