//=============================================================================
// Yetty Android Native Activity
//=============================================================================
// Entry point for Android using native_app_glue
// Uses WebGPU (wgpu-native with Vulkan backend) for rendering
// Uses BusyBox ash as the shell

#include <jni.h>
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

// Helper macro to check Result in callbacks where we can't return errors
#define CHECK_RESULT(expr) \
    do { \
        if (auto _res = (expr); !_res) { \
            LOGE("%s: %s", #expr, _res.error().message().c_str()); \
        } \
    } while(0)

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

static std::string getNativeLibraryDir(struct android_app* app) {
    JNIEnv* env;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    // Get the NativeActivity class
    jclass activityClass = env->GetObjectClass(app->activity->clazz);

    // Get getApplicationInfo() method
    jmethodID getAppInfo = env->GetMethodID(activityClass, "getApplicationInfo",
                                             "()Landroid/content/pm/ApplicationInfo;");
    jobject appInfo = env->CallObjectMethod(app->activity->clazz, getAppInfo);

    // Get nativeLibraryDir field
    jclass appInfoClass = env->GetObjectClass(appInfo);
    jfieldID nativeLibDirField = env->GetFieldID(appInfoClass, "nativeLibraryDir",
                                                  "Ljava/lang/String;");
    jstring nativeLibDir = (jstring)env->GetObjectField(appInfo, nativeLibDirField);

    // Convert to C++ string
    const char* nativeLibDirCStr = env->GetStringUTFChars(nativeLibDir, nullptr);
    std::string result(nativeLibDirCStr);
    env->ReleaseStringUTFChars(nativeLibDir, nativeLibDirCStr);

    // Clean up local refs
    env->DeleteLocalRef(nativeLibDir);
    env->DeleteLocalRef(appInfoClass);
    env->DeleteLocalRef(appInfo);
    env->DeleteLocalRef(activityClass);

    app->activity->vm->DetachCurrentThread();

    return result;
}

static bool setupBusybox(struct android_app* app) {
    g_state.dataDir = getInternalDataPath(app);

    // BusyBox is now in the native library directory as libbusybox.so
    // This directory has execute permissions (unlike the files directory)
    std::string nativeLibDir = getNativeLibraryDir(app);
    g_state.busyboxPath = nativeLibDir + "/libbusybox.so";

    LOGI("Looking for BusyBox at %s", g_state.busyboxPath.c_str());

    // Check if busybox exists and is executable
    if (access(g_state.busyboxPath.c_str(), X_OK) == 0) {
        LOGI("BusyBox found at %s", g_state.busyboxPath.c_str());
        return true;
    }

    LOGE("BusyBox not found at %s", g_state.busyboxPath.c_str());
    return false;
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

    // Set up environment
    setenv("TERM", "xterm-256color", 1);
    setenv("HOME", g_state.dataDir.c_str(), 1);
    setenv("PATH", "/system/bin:/system/xbin", 1);
    setenv("SHELL", "/system/bin/sh", 1);
    // Make busybox available as BUSYBOX env var
    setenv("BUSYBOX", g_state.busyboxPath.c_str(), 1);

    // Create terminal
    g_state.terminal = std::make_unique<yetty::Terminal>(
        g_state.cols, g_state.rows, g_state.font.get());

    // Start shell - use system shell (busybox crashes on some devices)
    // BusyBox is available via $BUSYBOX command (e.g., $BUSYBOX ls)
    std::string shell = "/system/bin/sh";
    auto result = g_state.terminal->start(shell);
    if (!result) {
        LOGE("Failed to start shell: %s", result.error().message().c_str());
        return false;
    }
    LOGI("Terminal started with shell: %s", shell.c_str());
    LOGI("BusyBox available at: %s", g_state.busyboxPath.c_str());

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

// Show soft keyboard using JNI (more reliable than ANativeActivity_showSoftInput)
static void showSoftKeyboard(struct android_app* app) {
    JNIEnv* env;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    // Get the NativeActivity class
    jclass activityClass = env->GetObjectClass(app->activity->clazz);

    // Get getSystemService method
    jmethodID getSystemService = env->GetMethodID(activityClass, "getSystemService",
                                                   "(Ljava/lang/String;)Ljava/lang/Object;");

    // Get INPUT_METHOD_SERVICE constant
    jstring serviceName = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(app->activity->clazz, getSystemService, serviceName);
    env->DeleteLocalRef(serviceName);

    if (imm) {
        // Get InputMethodManager class
        jclass immClass = env->GetObjectClass(imm);

        // Get getWindow().getDecorView() to get the view
        jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
        jobject window = env->CallObjectMethod(app->activity->clazz, getWindow);

        if (window) {
            jclass windowClass = env->GetObjectClass(window);
            jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
            jobject decorView = env->CallObjectMethod(window, getDecorView);

            if (decorView) {
                // Call showSoftInput(view, flags)
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
    app->activity->vm->DetachCurrentThread();
}

// Hide soft keyboard
static void hideSoftKeyboard(struct android_app* app) {
    JNIEnv* env;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    jclass activityClass = env->GetObjectClass(app->activity->clazz);
    jmethodID getSystemService = env->GetMethodID(activityClass, "getSystemService",
                                                   "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring serviceName = env->NewStringUTF("input_method");
    jobject imm = env->CallObjectMethod(app->activity->clazz, getSystemService, serviceName);
    env->DeleteLocalRef(serviceName);

    if (imm) {
        jclass immClass = env->GetObjectClass(imm);
        jmethodID getWindow = env->GetMethodID(activityClass, "getWindow", "()Landroid/view/Window;");
        jobject window = env->CallObjectMethod(app->activity->clazz, getWindow);

        if (window) {
            jclass windowClass = env->GetObjectClass(window);
            jmethodID getDecorView = env->GetMethodID(windowClass, "getDecorView", "()Landroid/view/View;");
            jobject decorView = env->CallObjectMethod(window, getDecorView);

            if (decorView) {
                // Get window token
                jclass viewClass = env->GetObjectClass(decorView);
                jmethodID getWindowToken = env->GetMethodID(viewClass, "getWindowToken",
                                                             "()Landroid/os/IBinder;");
                jobject token = env->CallObjectMethod(decorView, getWindowToken);

                if (token) {
                    jmethodID hideSoftInputFromWindow = env->GetMethodID(immClass,
                        "hideSoftInputFromWindow", "(Landroid/os/IBinder;I)Z");
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
    app->activity->vm->DetachCurrentThread();
}

// Get VTerm modifier flags from Android meta state
static VTermModifier getModifiers(int32_t metaState) {
    int mod = VTERM_MOD_NONE;
    if (metaState & AMETA_SHIFT_ON) mod |= VTERM_MOD_SHIFT;
    if (metaState & AMETA_CTRL_ON) mod |= VTERM_MOD_CTRL;
    if (metaState & AMETA_ALT_ON) mod |= VTERM_MOD_ALT;
    return static_cast<VTermModifier>(mod);
}

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

                // Show soft keyboard on tap
                showSoftKeyboard(app);

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
        int32_t metaState = AKeyEvent_getMetaState(event);
        VTermModifier mod = getModifiers(metaState);

        LOGI("Key event: keyCode=%d action=%d meta=0x%x", keyCode, action, metaState);

        if (action == AKEY_EVENT_ACTION_DOWN && g_state.terminal) {
            // Handle special keys first
            switch (keyCode) {
                case AKEYCODE_ENTER:
                    CHECK_RESULT(g_state.terminal->sendKey('\r', mod));
                    return 1;
                case AKEYCODE_DEL:  // Backspace
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_BACKSPACE, mod));
                    return 1;
                case AKEYCODE_FORWARD_DEL:  // Delete
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_DEL, mod));
                    return 1;
                case AKEYCODE_TAB:
                    CHECK_RESULT(g_state.terminal->sendKey('\t', mod));
                    return 1;
                case AKEYCODE_ESCAPE:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_ESCAPE, mod));
                    return 1;
                case AKEYCODE_DPAD_UP:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_UP, mod));
                    return 1;
                case AKEYCODE_DPAD_DOWN:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_DOWN, mod));
                    return 1;
                case AKEYCODE_DPAD_LEFT:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_LEFT, mod));
                    return 1;
                case AKEYCODE_DPAD_RIGHT:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_RIGHT, mod));
                    return 1;
                case AKEYCODE_MOVE_HOME:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_HOME, mod));
                    return 1;
                case AKEYCODE_MOVE_END:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_END, mod));
                    return 1;
                case AKEYCODE_PAGE_UP:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_PAGEUP, mod));
                    return 1;
                case AKEYCODE_PAGE_DOWN:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_PAGEDOWN, mod));
                    return 1;
                case AKEYCODE_INSERT:
                    CHECK_RESULT(g_state.terminal->sendSpecialKey(VTERM_KEY_INS, mod));
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

            // For regular characters, get the Unicode character
            // We need to use JNI to get the actual character from KeyEvent
            JNIEnv* env;
            app->activity->vm->AttachCurrentThread(&env, nullptr);

            // Get KeyEvent class and create instance
            jclass keyEventClass = env->FindClass("android/view/KeyEvent");
            jmethodID constructor = env->GetMethodID(keyEventClass, "<init>", "(II)V");
            jobject keyEvent = env->NewObject(keyEventClass, constructor, action, keyCode);

            // Get getUnicodeChar method
            jmethodID getUnicodeChar = env->GetMethodID(keyEventClass, "getUnicodeChar", "(I)I");
            jint unicodeChar = env->CallIntMethod(keyEvent, getUnicodeChar, metaState);

            env->DeleteLocalRef(keyEvent);
            env->DeleteLocalRef(keyEventClass);
            app->activity->vm->DetachCurrentThread();

            if (unicodeChar > 0) {
                LOGI("Unicode char: %d ('%c')", unicodeChar, (char)unicodeChar);
                // Handle Ctrl+key combinations
                if (mod & VTERM_MOD_CTRL) {
                    // Ctrl+A through Ctrl+Z become 1-26
                    if (unicodeChar >= 'a' && unicodeChar <= 'z') {
                        CHECK_RESULT(g_state.terminal->sendKey(unicodeChar - 'a' + 1, VTERM_MOD_NONE));
                    } else if (unicodeChar >= 'A' && unicodeChar <= 'Z') {
                        CHECK_RESULT(g_state.terminal->sendKey(unicodeChar - 'A' + 1, VTERM_MOD_NONE));
                    } else {
                        CHECK_RESULT(g_state.terminal->sendKey(unicodeChar, mod));
                    }
                } else {
                    CHECK_RESULT(g_state.terminal->sendKey(unicodeChar, VTERM_MOD_NONE));
                }
                return 1;
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

        case APP_CMD_CONTENT_RECT_CHANGED:
            // Content rect changed - this happens when soft keyboard appears/disappears
            LOGI("APP_CMD_CONTENT_RECT_CHANGED: rect=[%d,%d,%d,%d]",
                 app->contentRect.left, app->contentRect.top,
                 app->contentRect.right, app->contentRect.bottom);
            if (g_state.initialized && g_state.ctx) {
                int32_t newWidth = app->contentRect.right - app->contentRect.left;
                int32_t newHeight = app->contentRect.bottom - app->contentRect.top;

                if (newWidth > 0 && newHeight > 0 &&
                    (newWidth != g_state.width || newHeight != g_state.height)) {
                    g_state.width = newWidth;
                    g_state.height = newHeight;
                    LOGI("Resizing to content rect: %dx%d", newWidth, newHeight);

                    // Reconfigure surface
                    g_state.ctx->resize(g_state.width, g_state.height);

                    // Resize renderer
                    if (g_state.renderer) {
                        g_state.renderer->resize(g_state.width, g_state.height);
                    }

                    // Resize terminal
                    uint32_t newCols = static_cast<uint32_t>(g_state.width / g_state.cellWidth);
                    uint32_t newRows = static_cast<uint32_t>(g_state.height / g_state.cellHeight);
                    if (newCols < 20) newCols = 20;
                    if (newRows < 5) newRows = 5;
                    if (g_state.terminal && (newCols != g_state.cols || newRows != g_state.rows)) {
                        g_state.cols = newCols;
                        g_state.rows = newRows;
                        g_state.terminal->resize(newCols, newRows);
                        LOGI("Terminal resized to %ux%u", newCols, newRows);
                    }
                }
            }
            break;
    }
}

//-----------------------------------------------------------------------------
// Render Frame
//-----------------------------------------------------------------------------
static void renderFrame() {
    static int frameCount = 0;
    static int skipCount = 0;

    if (!g_state.initialized || !g_state.running) {
        if (skipCount++ % 1000 == 0) {
            LOGI("renderFrame skipped: initialized=%d running=%d", g_state.initialized, g_state.running);
        }
        return;
    }
    if (!g_state.ctx || !g_state.renderer || !g_state.terminal) {
        if (skipCount++ % 1000 == 0) {
            LOGI("renderFrame skipped: ctx=%p renderer=%p terminal=%p",
                 g_state.ctx.get(), g_state.renderer.get(), g_state.terminal.get());
        }
        return;
    }

    // Update terminal (read PTY output)
    CHECK_RESULT(g_state.terminal->update());

    // Log grid state for first few frames
    if (frameCount < 3) {
        const auto& grid = g_state.terminal->getGrid();
        LOGI("Frame %d: grid=%ux%u, cursor=(%d,%d)",
             frameCount, grid.getCols(), grid.getRows(),
             g_state.terminal->getCursorCol(), g_state.terminal->getCursorRow());

        // Check if grid has any non-zero glyphs
        const uint16_t* glyphData = grid.getGlyphData();
        int nonZeroGlyphs = 0;
        for (uint32_t i = 0; i < grid.getCols() * grid.getRows() && i < 100; i++) {
            if (glyphData[i] != 0) nonZeroGlyphs++;
        }
        LOGI("Frame %d: first 100 cells have %d non-zero glyphs", frameCount, nonZeroGlyphs);
    }

    // Get surface texture
    auto textureViewResult = g_state.ctx->getCurrentTextureView();
    if (!textureViewResult) {
        if (frameCount % 60 == 0) {
            LOGE("Failed to get texture view: %s", textureViewResult.error().message().c_str());
        }
        frameCount++;
        return;
    }

    if (frameCount == 0) {
        LOGI("First frame rendering, textureView=%p", *textureViewResult);
        // Log font state
        LOGI("Font: glyphCount=%zu, textureView=%p, sampler=%p, metadataBuffer=%p",
             g_state.font->getGlyphCount(),
             g_state.font->getTextureView(),
             g_state.font->getSampler(),
             g_state.font->getGlyphMetadataBuffer());
        LOGI("Surface format: %d, screen size: %dx%d",
             g_state.ctx->getSurfaceFormat(), g_state.width, g_state.height);
    }

    // Render terminal using TextRenderer
    g_state.renderer->render(*g_state.ctx, g_state.terminal->getGrid(),
                             g_state.terminal->getCursorCol(),
                             g_state.terminal->getCursorRow(),
                             g_state.terminal->isCursorVisible());

    // Present
    g_state.ctx->present();

    if (frameCount == 0) {
        LOGI("First frame presented");
    }
    if (frameCount < 5 || frameCount % 60 == 0) {
        LOGI("Frame %d rendered", frameCount);
    }
    frameCount++;
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
    static int loopCount = 0;
    while (true) {
        int events;
        struct android_poll_source* source;

        // Poll for events
        // Use timeout of 0 when running (non-blocking), -1 when paused (blocking)
        int timeout = g_state.running ? 0 : -1;

        if (loopCount == 0) {
            LOGI("Main loop starting, running=%d, timeout=%d", g_state.running, timeout);
        }

        int pollResult;
        while ((pollResult = ALooper_pollAll(timeout, nullptr, &events, reinterpret_cast<void**>(&source))) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }

            if (app->destroyRequested) {
                LOGI("Destroy requested, cleaning up...");
                cleanup();
                return;
            }

            // Recalculate timeout in case running state changed
            timeout = g_state.running ? 0 : -1;
        }

        // Render frame
        if (g_state.running) {
            renderFrame();
        }

        loopCount++;
        if (loopCount == 1) {
            LOGI("Main loop iteration 1 complete");
        }
    }
}
