#include "WebGPUContext.h"
#include <glfw3webgpu.h>
#include <iostream>
#include <cassert>

namespace yetty {

WebGPUContext::WebGPUContext() = default;

WebGPUContext::~WebGPUContext() {
    if (device_) wgpuDeviceRelease(device_);
    if (adapter_) wgpuAdapterRelease(adapter_);
    if (surface_) wgpuSurfaceRelease(surface_);
    if (instance_) wgpuInstanceRelease(instance_);
}

bool WebGPUContext::init(GLFWwindow* window, uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    // Create WebGPU instance
    WGPUInstanceDescriptor instanceDesc = {};
    instance_ = wgpuCreateInstance(&instanceDesc);
    if (!instance_) {
        std::cerr << "Failed to create WebGPU instance" << std::endl;
        return false;
    }

    // Create surface from GLFW window
    surface_ = glfwGetWGPUSurface(instance_, window);
    if (!surface_) {
        std::cerr << "Failed to create WebGPU surface" << std::endl;
        return false;
    }

    // Request adapter
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface_;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    bool adapterReceived = false;
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
        std::cerr << "Failed to get WebGPU adapter" << std::endl;
        return false;
    }

    // Request device
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = "yetty device";
    deviceDesc.requiredFeatureCount = 0;
    deviceDesc.requiredLimits = nullptr;
    deviceDesc.defaultQueue.label = "default queue";

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
        std::cerr << "Failed to get WebGPU device" << std::endl;
        return false;
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

    // Get preferred surface format
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    if (caps.formatCount > 0) {
        surfaceFormat_ = caps.formats[0];
    }
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    // Configure surface
    configureSurface(width, height);

    std::cout << "WebGPU initialized successfully" << std::endl;
    return true;
}

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

void WebGPUContext::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    width_ = width;
    height_ = height;
    configureSurface(width, height);
}

WGPUTextureView WebGPUContext::getCurrentTextureView() {
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);

    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success) {
        return nullptr;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = surfaceFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    return wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);
}

void WebGPUContext::present() {
    wgpuSurfacePresent(surface_);
}

} // namespace yetty
