#pragma once

#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <memory>
#if !YETTY_ANDROID
#include <GLFW/glfw3.h>
#endif

#if YETTY_ANDROID
struct ANativeWindow;
#endif

namespace yetty {

class WebGPUContext {
public:
    using Ptr = std::shared_ptr<WebGPUContext>;

#if YETTY_ANDROID
    static Result<Ptr> create(ANativeWindow* window, uint32_t width, uint32_t height) noexcept;
#else
    static Result<Ptr> create(GLFWwindow* window, uint32_t width, uint32_t height) noexcept;
#endif

    ~WebGPUContext();

    // Non-copyable
    WebGPUContext(const WebGPUContext&) = delete;
    WebGPUContext& operator=(const WebGPUContext&) = delete;

    void resize(uint32_t width, uint32_t height) noexcept;

    WGPUDevice getDevice() const noexcept { return device_; }
    WGPUQueue getQueue() const noexcept { return queue_; }
    WGPUSurface getSurface() const noexcept { return surface_; }
    WGPUTextureFormat getSurfaceFormat() const noexcept { return surfaceFormat_; }
    WGPUInstance getInstance() const noexcept { return instance_; }
    uint32_t getSurfaceWidth() const noexcept { return width_; }
    uint32_t getSurfaceHeight() const noexcept { return height_; }

    Result<WGPUTextureView> getCurrentTextureView() noexcept;
    void present() noexcept;
    
    // Check if a texture was acquired this frame (to know if present() should be called)
    bool hasCurrentTexture() const noexcept { return currentTextureView_ != nullptr; }

private:
#if YETTY_ANDROID
    WebGPUContext(ANativeWindow* window, uint32_t width, uint32_t height) noexcept;
#else
    WebGPUContext(GLFWwindow* window, uint32_t width, uint32_t height) noexcept;
#endif

    Result<void> init() noexcept;

#if YETTY_WEB
    void createSwapChain(uint32_t width, uint32_t height) noexcept;
#else
    void configureSurface(uint32_t width, uint32_t height) noexcept;
#endif

#if YETTY_ANDROID
    ANativeWindow* window_ = nullptr;
#else
    GLFWwindow* window_ = nullptr;
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
