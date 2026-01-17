#pragma once

#include <yetty/result.hpp>
#include <yetty/widget.h>
#include <webgpu/webgpu.h>
#include <memory>
#include <vector>

namespace yetty {

class WebGPUContext;

// Toolbox button types
enum class ToolboxButton {
    None,
    Close,
    StopStart
};

// Widget frame renderer - draws thin borders around widgets and toolbox buttons
class WidgetFrameRenderer {
public:
    static Result<std::unique_ptr<WidgetFrameRenderer>> create(
        WGPUDevice device,
        WGPUTextureFormat format
    );

    ~WidgetFrameRenderer();

    // Render frame around a widget
    void renderFrame(
        WGPURenderPassEncoder pass,
        WGPUQueue queue,
        uint32_t screenWidth,
        uint32_t screenHeight,
        float pixelX, float pixelY,
        float pixelW, float pixelH,
        float r, float g, float b, float a,
        float thickness = 1.0f
    );

    // Render toolbox for focused widget (close and stop/start buttons)
    void renderToolbox(
        WGPURenderPassEncoder pass,
        WGPUQueue queue,
        uint32_t screenWidth,
        uint32_t screenHeight,
        float widgetPixelX, float widgetPixelY,
        float widgetPixelW, float widgetPixelH,
        bool isRunning
    );

    // Check if a click hit a toolbox button
    // Returns the button type, or None if not hit
    // Static method - can be called without renderer instance
    static ToolboxButton hitTestToolbox(
        float clickX, float clickY,
        float widgetPixelX, float widgetPixelY,
        float widgetPixelW, float widgetPixelH
    );

    // Get toolbox dimensions
    static constexpr float TOOLBOX_BUTTON_SIZE = 20.0f;
    static constexpr float TOOLBOX_PADDING = 5.0f;
    static constexpr float TOOLBOX_MARGIN = 3.0f;

    // Reset draw index (call at start of each frame)
    void resetDrawIndex() { drawIndex_ = 0; }

private:
    WidgetFrameRenderer() = default;
    Result<void> init(WGPUDevice device, WGPUTextureFormat format);

    // Get next uniform buffer/bind group for drawing
    std::pair<WGPUBuffer, WGPUBindGroup> getNextUniformBuffer();

    WGPUDevice device_ = nullptr;
    WGPURenderPipeline framePipeline_ = nullptr;
    WGPURenderPipeline iconPipeline_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;

    // Pool of uniform buffers and bind groups for multiple draws per frame
    static constexpr size_t MAX_DRAWS_PER_FRAME = 32;
    std::vector<WGPUBuffer> uniformBuffers_;
    std::vector<WGPUBindGroup> bindGroups_;
    size_t drawIndex_ = 0;

    bool initialized_ = false;
};

} // namespace yetty
