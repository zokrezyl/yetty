#pragma once

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <functional>
#include <string>

namespace yetty {

class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

    bool init(GLFWwindow* window, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);

    WGPUDevice getDevice() const { return device_; }
    WGPUQueue getQueue() const { return queue_; }
    WGPUSurface getSurface() const { return surface_; }
    WGPUTextureFormat getSurfaceFormat() const { return surfaceFormat_; }

    WGPUTextureView getCurrentTextureView();
    void present();

private:
#if YETTY_WEB
    void createSwapChain(uint32_t width, uint32_t height);
#else
    void configureSurface(uint32_t width, uint32_t height);
#endif

    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_BGRA8Unorm;

#if YETTY_WEB
    WGPUSwapChain swapChain_ = nullptr;
#endif

    uint32_t width_ = 0;
    uint32_t height_ = 0;
};

} // namespace yetty
