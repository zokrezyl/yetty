#include "webgpu-context.h"
#include <iostream>
#include <cassert>

#if YETTY_WEB
#include <emscripten/html5_webgpu.h>
#elif YETTY_ANDROID
#include <android/native_window.h>
#include <webgpu/wgpu.h>  // For wgpu-native Android surface
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
#else
Result<void> WebGPUContext::init(GLFWwindow* window, uint32_t width, uint32_t height) {
#endif
    width_ = width;
    height_ = height;

    // Create WebGPU instance
#if YETTY_WEB
    instance_ = wgpuCreateInstance(nullptr);
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
    // Android: create surface from ANativeWindow
    WGPUSurfaceDescriptorFromAndroidNativeWindow androidDesc = {};
    androidDesc.chain.sType = WGPUSType_SurfaceDescriptorFromAndroidNativeWindow;
    androidDesc.window = window;

    WGPUSurfaceDescriptor surfaceDesc = {};
    surfaceDesc.nextInChain = &androidDesc.chain;
    surface_ = wgpuInstanceCreateSurface(instance_, &surfaceDesc);
#else
    surface_ = glfwGetWGPUSurface(instance_, window);
#endif
    if (!surface_) {
        return Err<void>("Failed to create WebGPU surface");
    }

    // Request adapter
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface_;
#if !YETTY_WEB
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;
#endif

    wgpuInstanceRequestAdapter(
        instance_,
        &adapterOpts,
        [](WGPURequestAdapterStatus status, WGPUAdapter adapter, char const* message, void* userdata) {
            if (status == WGPURequestAdapterStatus_Success) {
                *static_cast<WGPUAdapter*>(userdata) = adapter;
            } else {
                std::cerr << "Failed to get WebGPU adapter: " << (message ? message : "unknown error") << std::endl;
            }
        },
        &adapter_
    );

    if (!adapter_) {
        return Err<void>("Failed to get WebGPU adapter");
    }

    // Request device
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = "yetty device";
#if !YETTY_WEB
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits = nullptr;
    deviceDesc.defaultQueue.label = "default queue";
#endif

    wgpuAdapterRequestDevice(
        adapter_,
        &deviceDesc,
        [](WGPURequestDeviceStatus status, WGPUDevice device, char const* message, void* userdata) {
            if (status == WGPURequestDeviceStatus_Success) {
                *static_cast<WGPUDevice*>(userdata) = device;
            } else {
                std::cerr << "Failed to get WebGPU device: " << (message ? message : "unknown error") << std::endl;
            }
        },
        &device_
    );

    if (!device_) {
        return Err<void>("Failed to get WebGPU device");
    }

    // Set error callback
    wgpuDeviceSetUncapturedErrorCallback(
        device_,
        [](WGPUErrorType type, char const* message, void* userdata) {
            std::cerr << "WebGPU error (" << type << "): " << (message ? message : "unknown") << std::endl;
        },
        nullptr
    );

    // Get queue
    queue_ = wgpuDeviceGetQueue(device_);

#if YETTY_WEB
    // Emscripten: use swapchain API
    surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;  // Standard for web
    createSwapChain(width, height);
#else
    // wgpu-native: get preferred surface format
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
    config.device = device_;
    config.format = surfaceFormat_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Immediate;  // No VSync, unlimited FPS

    wgpuSurfaceConfigure(surface_, &config);
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

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
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
