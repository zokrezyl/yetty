#pragma once

#include <yetty/renderable.h>
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <atomic>

namespace yetty {

class WebGPUContext;
class ShaderManager;

//-----------------------------------------------------------------------------
// Cursor style
//-----------------------------------------------------------------------------
enum class CursorStyle : uint32_t {
    Block = 0,
    Underline = 1,
    Bar = 2
};

//-----------------------------------------------------------------------------
// CursorRenderer - GPU-driven blinking cursor
//
// Renders a single cell cursor with GPU-driven blink animation.
// Registered with ShaderManager, uses shared quad vertex shader.
// Runs every frame but is very lightweight (single quad draw).
//-----------------------------------------------------------------------------
class CursorRenderer : public Renderable {
public:
    CursorRenderer(uint32_t id);
    ~CursorRenderer() override;

    // Initialize with context and shader manager
    Result<void> init(WebGPUContext* ctx, ShaderManager* shaderMgr,
                      WGPUTextureFormat targetFormat);
    Result<void> dispose();

    // Renderable interface
    uint32_t id() const override { return _id; }
    uint32_t zOrder() const override { return _zOrder; }
    const std::string& name() const override { return _name; }
    void start() override { _running.store(true); }
    void stop() override { _running.store(false); }
    bool isRunning() const override { return _running.load(); }
    Result<void> render(WebGPUContext& ctx) override;

    // Set cursor position (in cells)
    void setCursorPos(int col, int row);

    // Set cursor visibility (controlled by terminal)
    void setVisible(bool visible) { _visible = visible; }
    bool isVisible() const { return _visible; }

    // Set cursor style
    void setCursorStyle(CursorStyle style) { _cursorStyle = style; }
    CursorStyle getCursorStyle() const { return _cursorStyle; }

    // Set cell size (in pixels)
    void setCellSize(float width, float height);

    // Set screen size (for NDC calculation)
    void setScreenSize(uint32_t width, uint32_t height);

    // Set target texture view (for render pass)
    void setTargetView(WGPUTextureView view) { _targetView = view; }

    // Update time (called each frame)
    void updateTime(float deltaTime) { _time += deltaTime; }

    // Set z-order (default 50 = after terminal grid, before shader glyphs)
    void setZOrder(uint32_t z) { _zOrder = z; }

private:
    Result<void> createPipeline(WGPUTextureFormat targetFormat);

    uint32_t _id;
    uint32_t _zOrder = 50;  // After terminal grid (0), before shader glyphs (100)
    std::string _name;
    std::atomic<bool> _running{false};

    WebGPUContext* _ctx = nullptr;
    ShaderManager* _shaderManager = nullptr;

    // Cursor state
    int _cursorCol = 0;
    int _cursorRow = 0;
    bool _visible = true;
    CursorStyle _cursorStyle = CursorStyle::Block;
    float _cellWidth = 10.0f;
    float _cellHeight = 20.0f;
    uint32_t _screenWidth = 800;
    uint32_t _screenHeight = 600;
    float _time = 0.0f;
    WGPUTextureView _targetView = nullptr;

    // GPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _bindGroupLayout = nullptr;
    WGPUPipelineLayout _pipelineLayout = nullptr;
    WGPUBuffer _uniformBuffer = nullptr;
    WGPUBindGroup _bindGroup = nullptr;

    bool _initialized = false;
    bool _failed = false;
};

} // namespace yetty
