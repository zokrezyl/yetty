#pragma once

#include "../result.hpp"
#include <webgpu/webgpu.h>
#if !YETTY_ANDROID
#include <GLFW/glfw3.h>
#endif
#include <functional>
#include <string>

#if YETTY_ANDROID
struct ANativeWindow;
#endif

namespace yetty {

class WebGPUContext {
public:
    WebGPUContext();
    ~WebGPUContext();

#if YETTY_ANDROID
    Result<void> init(ANativeWindow* window, uint32_t width, uint32_t height);
#else
    Result<void> init(GLFWwindow* window, uint32_t width, uint32_t height);
#endif
    void resize(uint32_t width, uint32_t height);

    WGPUDevice getDevice() const { return device_; }
    WGPUQueue getQueue() const { return queue_; }
    WGPUSurface getSurface() const { return surface_; }
    WGPUTextureFormat getSurfaceFormat() const { return surfaceFormat_; }

    Result<WGPUTextureView> getCurrentTextureView();
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

    // Cached texture view for current frame (to avoid double-acquire)
    WGPUTextureView currentTextureView_ = nullptr;
    WGPUTexture currentTexture_ = nullptr;
};

} // namespace yetty
