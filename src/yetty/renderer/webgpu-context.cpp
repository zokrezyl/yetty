#include "webgpu-context.h"
#include "wgpu-compat.h"
#include <iostream>
#include <cassert>

#if YETTY_WEB
#include <emscripten/html5_webgpu.h>
#elif YETTY_ANDROID
#include <android/native_window.h>
#include <android/log.h>
#include <webgpu/wgpu.h>  // For wgpu-native Android surface
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "yetty-webgpu", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "yetty-webgpu", __VA_ARGS__)

// wgpu-native log callback for debugging (v27 signature)
static void wgpuLogCallback(WGPULogLevel level, WGPUStringView message, void* userdata) {
    const char* levelStr = "UNKNOWN";
    switch (level) {
        case WGPULogLevel_Error: levelStr = "ERROR"; break;
        case WGPULogLevel_Warn: levelStr = "WARN"; break;
        case WGPULogLevel_Info: levelStr = "INFO"; break;
        case WGPULogLevel_Debug: levelStr = "DEBUG"; break;
        case WGPULogLevel_Trace: levelStr = "TRACE"; break;
        default: break;
    }
    __android_log_print(ANDROID_LOG_INFO, "wgpu-native", "[%s] %.*s", levelStr,
                        (int)message.length, message.data ? message.data : "");
}
#else
#include <glfw3webgpu.h>
#endif

namespace yetty {

WebGPUContext::WebGPUContext() = default;

WebGPUContext::~WebGPUContext() {
    if (device_) wgpuDeviceRelease(device_);
    if (adapter_) wgpuAdapterRelease(adapter_);
#if YETTY_WEB
    if (swapChain_) wgpuSwapChainRelease(swapChain_);
#endif
    if (surface_) wgpuSurfaceRelease(surface_);
    if (instance_) wgpuInstanceRelease(instance_);
}

#if YETTY_ANDROID
Result<void> WebGPUContext::init(ANativeWindow* window, uint32_t width, uint32_t height) {
    LOGI("WebGPUContext::init start, window=%p, size=%dx%d", window, width, height);
#else
Result<void> WebGPUContext::init(GLFWwindow* window, uint32_t width, uint32_t height) {
#endif
    width_ = width;
    height_ = height;

    // Create WebGPU instance
#if YETTY_WEB
    instance_ = wgpuCreateInstance(nullptr);
#elif YETTY_ANDROID
    // Enable wgpu-native logging for debugging
    wgpuSetLogCallback(wgpuLogCallback, nullptr);
    wgpuSetLogLevel(WGPULogLevel_Warn);
    LOGI("Creating WebGPU instance...");
    WGPUInstanceDescriptor instanceDesc = {};
    instance_ = wgpuCreateInstance(&instanceDesc);
    LOGI("Instance created: %p", instance_);
#else
    WGPUInstanceDescriptor instanceDesc = {};
    instance_ = wgpuCreateInstance(&instanceDesc);
#endif
    if (!instance_) {
        return Err<void>("Failed to create WebGPU instance");
    }

    // Create surface from window
#if YETTY_WEB
    // Emscripten: create surface from canvas
    WGPUSurfaceDescriptorFromCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;
    canvasDesc.selector = "#canvas";

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = &canvasDesc.chain;
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
#elif YETTY_ANDROID
    // Android: create surface from ANativeWindow (v27 API)
    LOGI("Creating surface from ANativeWindow...");
    WGPUSurfaceSourceAndroidNativeWindow androidDesc = {};
    androidDesc.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
    androidDesc.window = window;

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = &androidDesc.chain;
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
    LOGI("Surface created: %p", surface_);
#else
    surface_ = glfwCreateWindowWGPUSurface(instance_, window);
#endif
    if (!surface_) {
        return Err<void>("Failed to create WebGPU surface");
    }

    // Request adapter
#if YETTY_ANDROID
    LOGI("Requesting adapter...");
#endif
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface_;
#if !YETTY_WEB
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;
#endif

    WGPURequestAdapterCallbackInfo adapterCallbackInfo = {};
    adapterCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallbackInfo.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* userdata1, void* userdata2) {
        if (status == WGPURequestAdapterStatus_Success) {
            *static_cast<WGPUAdapter*>(userdata1) = adapter;
        } else {
            std::cerr << "Failed to get WebGPU adapter: " << (message.data ? std::string(message.data, message.length) : "unknown error") << std::endl;
        }
    };
    adapterCallbackInfo.userdata1 = &adapter_;
    wgpuInstanceRequestAdapter(instance_, &adapterOpts, adapterCallbackInfo);
#if YETTY_ANDROID
    LOGI("Adapter obtained: %p", adapter_);
#endif

    if (!adapter_) {
        return Err<void>("Failed to get WebGPU adapter");
    }

    // Request device
#if YETTY_ANDROID
    LOGI("Requesting device...");
#endif
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = WGPU_STR("yetty device");
#if !YETTY_WEB
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits = nullptr;
    deviceDesc.defaultQueue.label = WGPU_STR("default queue");
    // v27 API: error callback with new signature
    deviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const* device, WGPUErrorType type, WGPUStringView message, void* userdata1, void* userdata2) {
        std::cerr << "WebGPU error (" << type << "): " << (message.data ? std::string(message.data, message.length) : "unknown") << std::endl;
#if YETTY_ANDROID
        __android_log_print(ANDROID_LOG_ERROR, "yetty-webgpu", "WebGPU error (%d): %.*s", type,
                            (int)message.length, message.data ? message.data : "unknown");
#endif
    };
#endif

    WGPURequestDeviceCallbackInfo deviceCallbackInfo = {};
    deviceCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallbackInfo.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* userdata1, void* userdata2) {
        if (status == WGPURequestDeviceStatus_Success) {
            *static_cast<WGPUDevice*>(userdata1) = device;
        } else {
            std::cerr << "Failed to get WebGPU device: " << (message.data ? std::string(message.data, message.length) : "unknown error") << std::endl;
        }
    };
    deviceCallbackInfo.userdata1 = &device_;
    wgpuAdapterRequestDevice(adapter_, &deviceDesc, deviceCallbackInfo);
#if YETTY_ANDROID
    LOGI("Device obtained: %p", device_);
#endif

    if (!device_) {
        return Err<void>("Failed to get WebGPU device");
    }

    // Get queue
#if YETTY_ANDROID
    LOGI("Getting queue...");
#endif
    queue_ = wgpuDeviceGetQueue(device_);
#if YETTY_ANDROID
    LOGI("Queue obtained: %p", queue_);
#endif

#if YETTY_WEB
    // Emscripten: use swapchain API
    surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;  // Standard for web
    createSwapChain(width, height);
#elif YETTY_ANDROID
    // Android/Vulkan: query surface capabilities to get supported formats
    LOGI("Querying surface capabilities...");
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    LOGI("Surface capabilities: formatCount=%zu, presentModeCount=%zu, alphaModeCount=%zu",
         caps.formatCount, caps.presentModeCount, caps.alphaModeCount);

    // Log all supported formats
    for (size_t i = 0; i < caps.formatCount; i++) {
        LOGI("  Supported format[%zu]: %d", i, caps.formats[i]);
    }
    for (size_t i = 0; i < caps.presentModeCount; i++) {
        LOGI("  Supported presentMode[%zu]: %d", i, caps.presentModes[i]);
    }
    for (size_t i = 0; i < caps.alphaModeCount; i++) {
        LOGI("  Supported alphaMode[%zu]: %d", i, caps.alphaModes[i]);
    }

    if (caps.formatCount > 0) {
        surfaceFormat_ = caps.formats[0];  // Use first (preferred) format
        LOGI("Using preferred format: %d", surfaceFormat_);
    } else {
        surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;  // Fallback
        LOGI("No formats reported, using fallback BGRA8Unorm");
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    LOGI("Configuring surface with format %d, size %dx%d...", surfaceFormat_, width, height);
    configureSurface(width, height);
    LOGI("Surface configured successfully");
#else
    // Desktop wgpu-native: get preferred surface format
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    if (caps.formatCount > 0) {
        surfaceFormat_ = caps.formats[0];
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    // Configure surface
    configureSurface(width, height);
#endif

    std::cout << "WebGPU initialized successfully" << std::endl;
    return Ok();
}

#if YETTY_WEB
void WebGPUContext::createSwapChain(uint32_t width, uint32_t height) {
    if (swapChain_) {
        wgpuSwapChainRelease(swapChain_);
    }

    WGPUSwapChainDescriptor swapChainDesc = {};
    swapChainDesc.usage = WGPUTextureUsage_RenderAttachment;
    swapChainDesc.format = surfaceFormat_;
    swapChainDesc.width = width;
    swapChainDesc.height = height;
    swapChainDesc.presentMode = WGPUPresentMode_Fifo;

    swapChain_ = wgpuDeviceCreateSwapChain(device_, surface_, &swapChainDesc);
}
#else
void WebGPUContext::configureSurface(uint32_t width, uint32_t height) {
    WGPUSurfaceConfiguration config = {};
    config.nextInChain = nullptr;
    config.device = device_;
    config.format = surfaceFormat_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
#if YETTY_ANDROID
    // Android/Vulkan: use Auto alpha mode and Fifo present mode for compatibility
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    LOGI("configureSurface: device=%p surface=%p format=%d alpha=%d present=%d size=%dx%d",
         device_, surface_, config.format, config.alphaMode, config.presentMode, width, height);
#else
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Immediate;  // No VSync, unlimited FPS
#endif
    config.width = width;
    config.height = height;

#if YETTY_ANDROID
    LOGI("Calling wgpuSurfaceConfigure...");
#endif
    wgpuSurfaceConfigure(surface_, &config);
#if YETTY_ANDROID
    LOGI("wgpuSurfaceConfigure completed");
#endif
}
#endif

void WebGPUContext::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    width_ = width;
    height_ = height;
#if YETTY_WEB
    createSwapChain(width, height);
#else
    configureSurface(width, height);
#endif
}

Result<WGPUTextureView> WebGPUContext::getCurrentTextureView() {
    // Return cached view if already acquired this frame
    if (currentTextureView_) {
        return Ok(currentTextureView_);
    }

#if YETTY_WEB
    // Emscripten: get texture view from swapchain
    currentTextureView_ = wgpuSwapChainGetCurrentTextureView(swapChain_);
    if (!currentTextureView_) {
        return Err<WGPUTextureView>("Failed to get swapchain texture view");
    }
    return Ok(currentTextureView_);
#else
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);

    // v27: Success was split into SuccessOptimal and SuccessSuboptimal
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return Err<WGPUTextureView>("Failed to get surface texture");
    }

    currentTexture_ = surfaceTexture.texture;

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = surfaceFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    currentTextureView_ = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);
    if (!currentTextureView_) {
        return Err<WGPUTextureView>("Failed to create texture view");
    }
    return Ok(currentTextureView_);
#endif
}

void WebGPUContext::present() {
    // Release cached texture view
    if (currentTextureView_) {
        wgpuTextureViewRelease(currentTextureView_);
        currentTextureView_ = nullptr;
    }
    currentTexture_ = nullptr;

#if YETTY_WEB
    // Emscripten: swapchain presents automatically
#else
    wgpuSurfacePresent(surface_);
#endif
}

} // namespace yetty
