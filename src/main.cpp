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
#include <cmath>

using namespace yetty;

// Application state for callbacks
struct AppState {
    WebGPUContext* ctx = nullptr;
    TextRenderer* renderer = nullptr;
    float zoomLevel = 1.0f;
    float baseCellWidth = 0.0f;
    float baseCellHeight = 0.0f;
};

// Scroll callback for zooming
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
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

// Default font path - adjust as needed for your system
#if defined(_WIN32)
const char* DEFAULT_FONT = "C:/Windows/Fonts/consola.ttf";
#elif defined(__APPLE__)
const char* DEFAULT_FONT = "/System/Library/Fonts/Monaco.ttf";
#else
const char* DEFAULT_FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf";
#endif

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [font.ttf] [input.txt] [width] [height]" << std::endl;
    std::cerr << "  font.ttf  - Path to TTF font (default: system monospace)" << std::endl;
    std::cerr << "  input.txt - Text file to display (default: demo text)" << std::endl;
    std::cerr << "  width     - Window width in pixels (default: 1024)" << std::endl;
    std::cerr << "  height    - Window height in pixels (default: 768)" << std::endl;
}

int main(int argc, char* argv[]) {
    const char* fontPath = (argc > 1) ? argv[1] : DEFAULT_FONT;
    uint32_t width = (argc > 3) ? static_cast<uint32_t>(std::atoi(argv[3])) : 1024;
    uint32_t height = (argc > 4) ? static_cast<uint32_t>(std::atoi(argv[4])) : 768;

    if (width == 0) width = 1024;
    if (height == 0) height = 768;

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

    // Generate MSDF font atlas
    Font font;
    float fontSize = 32.0f;
    if (!font.generate(fontPath, fontSize)) {
        std::cerr << "Failed to generate font atlas from: " << fontPath << std::endl;
        std::cerr << "Usage: " << argv[0] << " [path-to-ttf-font]" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

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

    // Load content from file or use demo
    std::vector<std::string> lines;
    const char* inputFile = (argc > 2) ? argv[2] : nullptr;

    if (inputFile) {
        std::ifstream file(inputFile);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                lines.push_back(line);
            }
            std::cout << "Loaded " << lines.size() << " lines from " << inputFile << std::endl;
        } else {
            std::cerr << "Failed to open input file: " << inputFile << std::endl;
        }
    }

    if (lines.empty()) {
        // Demo content
        lines = {
            "Welcome to yetty - WebGPU Terminal Emulator",
            "============================================",
            "",
            "This is a proof-of-concept terminal renderer using:",
            "  * WebGPU (via wgpu-native)",
            "  * MSDF (Multi-channel Signed Distance Field) fonts",
            "  * GPU-accelerated text rendering",
            "",
            "Features:",
            "  - Crisp text at any scale",
            "  - Colored text and backgrounds",
            "  - Fast GPU-based rendering",
            "",
            "Usage: yetty [font.ttf] [input.txt]",
            "Press ESC to exit.",
        };
    }

    // Colors
    glm::vec4 white = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 green = {0.0f, 1.0f, 0.0f, 1.0f};
    glm::vec4 cyan = {0.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 yellow = {1.0f, 1.0f, 0.0f, 1.0f};

    std::cout << "Writing content to grid (" << cols << "x" << rows << ")" << std::endl;

    // Write content
    for (size_t i = 0; i < lines.size() && i < rows; ++i) {
        glm::vec4 color = white;
        if (i == 0) color = green;
        else if (i == 1) color = green;
        else if (lines[i].length() > 2 && lines[i][0] == ' ' && lines[i][1] == ' ' && lines[i][2] == '*') color = cyan;
        else if (lines[i].length() > 2 && lines[i][0] == ' ' && lines[i][1] == ' ' && lines[i][2] == '-') color = yellow;

        grid.writeString(0, static_cast<uint32_t>(i), lines[i].c_str(), color);
    }

    // Set up application state for callbacks
    AppState appState;
    appState.ctx = &ctx;
    appState.renderer = &renderer;
    appState.baseCellWidth = cellWidth;
    appState.baseCellHeight = cellHeight;
    appState.zoomLevel = 1.0f;

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

    // Main loop
    double lastTime = glfwGetTime();
    uint32_t frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Check for ESC key
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        // Get current window size
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            renderer.resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }

        // Render
        renderer.render(ctx, grid);

        // FPS counter
        frameCount++;
        double currentTime = glfwGetTime();
        if (currentTime - lastTime >= 1.0) {
            std::cout << "FPS: " << frameCount << std::endl;
            frameCount = 0;
            lastTime = currentTime;
        }
    }

    std::cout << "Shutting down..." << std::endl;

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
