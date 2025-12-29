//=============================================================================
// Yetty Android Native Activity
//=============================================================================
// Entry point for Android using native_app_glue
// Uses WebGPU (wgpu-native with Vulkan backend) for rendering
// Uses BusyBox ash as the shell

#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>

#include "yetty/renderer/webgpu-context.h"
#include "yetty/renderer/text-renderer.h"
#include "yetty/terminal/grid.h"
#include "yetty/terminal/font.h"
#include "yetty/terminal/terminal.h"

#include <webgpu/webgpu.h>

#include <cmath>
#include <ctime>
#include <cstring>
#include <string>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "yetty"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

//-----------------------------------------------------------------------------
// Application State
//-----------------------------------------------------------------------------
struct AndroidAppState {
    struct android_app* app = nullptr;

    // Rendering (using WebGPUContext for proper abstraction)
    std::unique_ptr<yetty::WebGPUContext> ctx;
    std::unique_ptr<yetty::TextRenderer> renderer;
    std::unique_ptr<yetty::Font> font;
    std::unique_ptr<yetty::Terminal> terminal;

    // Window dimensions
    int32_t width = 0;
    int32_t height = 0;

    // Cell dimensions
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
    uint32_t cols = 80;
    uint32_t rows = 24;

    // Touch state
    float touchX = 0.0f;
    float touchY = 0.0f;
    bool touching = false;
    double touchDownTime = 0.0;

    // Selection
    bool selecting = false;

    // State flags
    bool initialized = false;
    bool running = false;

    // BusyBox paths
    std::string dataDir;
    std::string busyboxPath;
};

static AndroidAppState g_state;

//-----------------------------------------------------------------------------
// Utility Functions
//-----------------------------------------------------------------------------
static double getTime() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1000000000.0;
}

static std::string getInternalDataPath(struct android_app* app) {
    return std::string(app->activity->internalDataPath);
}

//-----------------------------------------------------------------------------
// BusyBox Setup
//-----------------------------------------------------------------------------
static bool extractAsset(struct android_app* app, const char* assetName, const char* destPath) {
    AAssetManager* assetManager = app->activity->assetManager;
    AAsset* asset = AAssetManager_open(assetManager, assetName, AASSET_MODE_BUFFER);

    if (!asset) {
        LOGE("Failed to open asset: %s", assetName);
        return false;
    }

    off_t size = AAsset_getLength(asset);
    const void* buffer = AAsset_getBuffer(asset);

    FILE* file = fopen(destPath, "wb");
    if (!file) {
        LOGE("Failed to create file: %s", destPath);
        AAsset_close(asset);
        return false;
    }

    fwrite(buffer, 1, size, file);
    fclose(file);
    AAsset_close(asset);

    // Make executable
    chmod(destPath, 0755);

    LOGI("Extracted asset %s to %s", assetName, destPath);
    return true;
}

static bool setupBusybox(struct android_app* app) {
    g_state.dataDir = getInternalDataPath(app);
    g_state.busyboxPath = g_state.dataDir + "/busybox";

    // Check if busybox already exists
    if (access(g_state.busyboxPath.c_str(), X_OK) == 0) {
        LOGI("BusyBox already installed at %s", g_state.busyboxPath.c_str());
        return true;
    }

    // Extract from assets
    if (!extractAsset(app, "busybox", g_state.busyboxPath.c_str())) {
        LOGE("Failed to extract BusyBox");
        return false;
    }

    LOGI("BusyBox installed at %s", g_state.busyboxPath.c_str());
    return true;
}

//-----------------------------------------------------------------------------
// WebGPU Initialization (using WebGPUContext)
//-----------------------------------------------------------------------------
static bool initWebGPU(ANativeWindow* window) {
    LOGI("Initializing WebGPU...");

    g_state.width = ANativeWindow_getWidth(window);
    g_state.height = ANativeWindow_getHeight(window);
    LOGI("Window size: %dx%d", g_state.width, g_state.height);

    g_state.ctx = std::make_unique<yetty::WebGPUContext>();
    auto result = g_state.ctx->init(window, g_state.width, g_state.height);
    if (!result) {
        LOGE("Failed to initialize WebGPU: %s", result.error().message().c_str());
        return false;
    }

    LOGI("WebGPU initialized successfully");
    return true;
}

//-----------------------------------------------------------------------------
// Terminal Initialization
//-----------------------------------------------------------------------------
static bool initTerminal() {
    LOGI("Initializing terminal...");

    // Extract font assets and shaders
    std::string atlasPath = g_state.dataDir + "/atlas.png";
    std::string metricsPath = g_state.dataDir + "/atlas.json";
    std::string shaderPath = g_state.dataDir + "/shaders.wgsl";

    if (access(atlasPath.c_str(), R_OK) != 0) {
        if (!extractAsset(g_state.app, "atlas.png", atlasPath.c_str())) {
            LOGE("Failed to extract atlas.png");
            return false;
        }
    }
    if (access(metricsPath.c_str(), R_OK) != 0) {
        if (!extractAsset(g_state.app, "atlas.json", metricsPath.c_str())) {
            LOGE("Failed to extract atlas.json");
            return false;
        }
    }
    // Always extract shader (may have been updated)
    if (!extractAsset(g_state.app, "shaders.wgsl", shaderPath.c_str())) {
        LOGE("Failed to extract shaders.wgsl");
        return false;
    }
    LOGI("Shader extracted to %s", shaderPath.c_str());

    // Set environment variable for TextRenderer to find the shader
    setenv("YETTY_SHADER_PATH", shaderPath.c_str(), 1);

    // Load font
    g_state.font = std::make_unique<yetty::Font>();
    if (!g_state.font->loadAtlas(atlasPath, metricsPath)) {
        LOGE("Failed to load font atlas");
        return false;
    }
    LOGI("Font atlas loaded");

    // Create font texture using WebGPUContext
    if (!g_state.font->createTexture(g_state.ctx->getDevice(), g_state.ctx->getQueue())) {
        LOGE("Failed to create font texture");
        return false;
    }
    LOGI("Font texture created");

    // Initialize TextRenderer
    g_state.renderer = std::make_unique<yetty::TextRenderer>();
    auto rendererResult = g_state.renderer->init(*g_state.ctx, *g_state.font);
    if (!rendererResult) {
        LOGE("Failed to init TextRenderer: %s", rendererResult.error().message().c_str());
        return false;
    }
    // Set screen size for rendering
    g_state.renderer->resize(g_state.width, g_state.height);
    LOGI("TextRenderer initialized with screen size %dx%d", g_state.width, g_state.height);

    // Get cell dimensions from font
    float fontSize = g_state.font->getFontSize();
    g_state.cellWidth = fontSize * 0.6f;  // Approximate monospace width
    g_state.cellHeight = g_state.font->getLineHeight();
    LOGI("Cell size: %.1fx%.1f (fontSize=%.1f)", g_state.cellWidth, g_state.cellHeight, fontSize);

    // Set cell size for text renderer
    g_state.renderer->setCellSize(g_state.cellWidth, g_state.cellHeight);

    // Calculate terminal dimensions based on screen size
    g_state.cols = static_cast<uint32_t>(g_state.width / g_state.cellWidth);
    g_state.rows = static_cast<uint32_t>(g_state.height / g_state.cellHeight);

    // Ensure minimum dimensions
    if (g_state.cols < 40) g_state.cols = 40;
    if (g_state.rows < 10) g_state.rows = 10;

    LOGI("Terminal dimensions: %ux%u", g_state.cols, g_state.rows);

    // Set up environment for BusyBox shell
    setenv("TERM", "xterm-256color", 1);
    setenv("HOME", g_state.dataDir.c_str(), 1);
    setenv("PATH", g_state.dataDir.c_str(), 1);
    setenv("SHELL", g_state.busyboxPath.c_str(), 1);

    // Create terminal
    g_state.terminal = std::make_unique<yetty::Terminal>(
        g_state.cols, g_state.rows, g_state.font.get());

    // Start shell
    std::string shell = g_state.busyboxPath + " ash";
    auto result = g_state.terminal->start(shell);
    if (!result) {
        LOGE("Failed to start shell: %s", result.error().message().c_str());
        return false;
    }
    LOGI("Terminal started with shell: %s", shell.c_str());

    return true;
}

//-----------------------------------------------------------------------------
// Cleanup
//-----------------------------------------------------------------------------
static void cleanup() {
    LOGI("Cleaning up...");

    // Reset in reverse order of creation
    g_state.terminal.reset();
    g_state.renderer.reset();
    g_state.font.reset();
    g_state.ctx.reset();  // WebGPUContext handles WebGPU cleanup

    g_state.initialized = false;
}

//-----------------------------------------------------------------------------
// Input Handling
//-----------------------------------------------------------------------------
static int32_t handleInput(struct android_app* app, AInputEvent* event) {
    int32_t eventType = AInputEvent_getType(event);

    if (eventType == AINPUT_EVENT_TYPE_MOTION) {
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);

        // Convert touch position to terminal cell
        int col = static_cast<int>(x / g_state.cellWidth);
        int row = static_cast<int>(y / g_state.cellHeight);

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                g_state.touchX = x;
                g_state.touchY = y;
                g_state.touching = true;
                g_state.touchDownTime = getTime();

                // Start selection
                if (g_state.terminal) {
                    g_state.terminal->startSelection(row, col);
                    g_state.selecting = true;
                }
                break;

            case AMOTION_EVENT_ACTION_MOVE:
                if (g_state.selecting && g_state.terminal) {
                    g_state.terminal->extendSelection(row, col);
                }
                g_state.touchX = x;
                g_state.touchY = y;
                break;

            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                g_state.touching = false;
                g_state.selecting = false;
                break;
        }

        return 1;
    }

    if (eventType == AINPUT_EVENT_TYPE_KEY) {
        int32_t keyCode = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);

        if (action == AKEY_EVENT_ACTION_DOWN && g_state.terminal) {
            // Handle Android key events
            // TODO: Map Android keycodes to VTerm keys
            switch (keyCode) {
                case AKEYCODE_ENTER:
                    g_state.terminal->sendKey('\r', VTERM_MOD_NONE);
                    break;
                case AKEYCODE_DEL:  // Backspace
                    g_state.terminal->sendKey(VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
                    break;
                case AKEYCODE_DPAD_UP:
                    g_state.terminal->sendKey(VTERM_KEY_UP, VTERM_MOD_NONE);
                    break;
                case AKEYCODE_DPAD_DOWN:
                    g_state.terminal->sendKey(VTERM_KEY_DOWN, VTERM_MOD_NONE);
                    break;
                case AKEYCODE_DPAD_LEFT:
                    g_state.terminal->sendKey(VTERM_KEY_LEFT, VTERM_MOD_NONE);
                    break;
                case AKEYCODE_DPAD_RIGHT:
                    g_state.terminal->sendKey(VTERM_KEY_RIGHT, VTERM_MOD_NONE);
                    break;
                default:
                    // Try to get Unicode character
                    // Note: Full keyboard support needs soft keyboard integration
                    break;
            }
        }

        return 1;
    }

    return 0;
}

//-----------------------------------------------------------------------------
// App Command Handler
//-----------------------------------------------------------------------------
static void handleCmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window != nullptr && !g_state.initialized) {
                // Set up BusyBox
                if (!setupBusybox(app)) {
                    LOGE("Failed to setup BusyBox");
                    return;
                }

                // Initialize WebGPU (includes surface configuration)
                if (!initWebGPU(app->window)) {
                    LOGE("Failed to initialize WebGPU");
                    return;
                }

                // Initialize terminal and renderer
                if (!initTerminal()) {
                    LOGE("Failed to initialize terminal");
                    return;
                }

                g_state.initialized = true;
                g_state.running = true;
                LOGI("Yetty initialized successfully");
            }
            break;

        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            g_state.running = false;
            cleanup();
            break;

        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            // TODO: Show soft keyboard
            break;

        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            // TODO: Hide soft keyboard
            break;

        case APP_CMD_CONFIG_CHANGED:
            LOGI("APP_CMD_CONFIG_CHANGED");
            // Handle orientation change, etc.
            if (g_state.initialized && g_state.ctx && app->window) {
                // Get new dimensions
                g_state.width = ANativeWindow_getWidth(app->window);
                g_state.height = ANativeWindow_getHeight(app->window);
                LOGI("New window size: %dx%d", g_state.width, g_state.height);

                // Reconfigure surface
                g_state.ctx->resize(g_state.width, g_state.height);

                // Resize renderer
                if (g_state.renderer) {
                    g_state.renderer->resize(g_state.width, g_state.height);
                }

                // Resize terminal
                uint32_t newCols = static_cast<uint32_t>(g_state.width / g_state.cellWidth);
                uint32_t newRows = static_cast<uint32_t>(g_state.height / g_state.cellHeight);
                if (g_state.terminal && (newCols != g_state.cols || newRows != g_state.rows)) {
                    g_state.cols = newCols;
                    g_state.rows = newRows;
                    g_state.terminal->resize(newCols, newRows);
                }
            }
            break;
    }
}

//-----------------------------------------------------------------------------
// Render Frame
//-----------------------------------------------------------------------------
static void renderFrame() {
    if (!g_state.initialized || !g_state.running) return;
    if (!g_state.ctx || !g_state.renderer || !g_state.terminal) return;

    // Update terminal (read PTY output)
    g_state.terminal->update();

    // Render terminal using TextRenderer
    g_state.renderer->render(*g_state.ctx, g_state.terminal->getGrid(),
                             g_state.terminal->getCursorCol(),
                             g_state.terminal->getCursorRow(),
                             g_state.terminal->isCursorVisible());

    // Present
    g_state.ctx->present();
}

} // anonymous namespace

//-----------------------------------------------------------------------------
// Android Main Entry Point
//-----------------------------------------------------------------------------
void android_main(struct android_app* app) {
    LOGI("Yetty Android starting...");

    g_state.app = app;
    app->onAppCmd = handleCmd;
    app->onInputEvent = handleInput;

    // Main loop
    while (true) {
        int events;
        struct android_poll_source* source;

        // Poll for events
        // Use timeout of 0 when running (non-blocking), -1 when paused (blocking)
        int timeout = g_state.running ? 0 : -1;

        while (ALooper_pollAll(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }

            if (app->destroyRequested) {
                LOGI("Destroy requested, cleaning up...");
                cleanup();
                return;
            }
        }

        // Render frame
        if (g_state.running) {
            renderFrame();
        }
    }
}
