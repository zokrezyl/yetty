#pragma once

#include <yetty/widget.h>
#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <atomic>

namespace yetty {

class WebGPUContext;
class ShaderManager;

//-----------------------------------------------------------------------------
// ShaderGlyphInstance - position data for one glyph instance
//-----------------------------------------------------------------------------
struct ShaderGlyphInstance {
    float x;        // pixel x
    float y;        // pixel y
    float w;        // pixel width
    float h;        // pixel height
    float time;     // animation time for this instance
};

//-----------------------------------------------------------------------------
// RenderContext for ShaderGlyphRenderer
//-----------------------------------------------------------------------------
struct ShaderGlyphRenderContext {
    WGPUTextureView targetView = nullptr;
    WGPUTextureFormat targetFormat = WGPUTextureFormat_BGRA8Unorm;
    uint32_t screenWidth = 0;
    uint32_t screenHeight = 0;
};

//-----------------------------------------------------------------------------
// ShaderGlyphRenderer - Widget, one per shader
//
// Terminal creates one instance per shader name it uses.
// Positions are updated only when terminal is dirty.
// Renders all instances in one batched draw call.
//-----------------------------------------------------------------------------
class ShaderGlyphRenderer : public Widget {
public:
    ShaderGlyphRenderer(uint32_t id, const std::string& shaderName);
    ~ShaderGlyphRenderer() override;

    // Initialize with context and shader manager
    Result<void> init(WebGPUContext* ctx, ShaderManager* shaderMgr,
                      WGPUTextureFormat targetFormat);
    Result<void> dispose() override;

    // Widget interface
    // id() inherited from Widget
    uint32_t zOrder() const override { return _zOrder; }
    const std::string& name() const override { return _name; }
    void start() override { _running.store(true); }
    void stop() override { _running.store(false); }
    bool isRunning() const override { return _running.load(); }

    void prepareFrame(WebGPUContext& ctx, bool on) override;
    Result<void> render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) override;

    // Update instances (called by Terminal when dirty)
    void setInstances(const std::vector<ShaderGlyphInstance>& instances);
    void clearInstances();

    // Render context (set by Terminal before render)
    void setRenderContext(const ShaderGlyphRenderContext& rc) { _renderContext = rc; }

    // Shader name
    const std::string& shaderName() const { return _shaderName; }

    // Set z-order (default 100 = after terminal, before plugins)
    void setZOrder(uint32_t z) { _zOrder = z; }

private:
    Result<void> createPipeline(WGPUTextureFormat targetFormat);

    uint32_t _id;
    uint32_t _zOrder = 100;
    std::string _name;
    std::string _shaderName;
    std::atomic<bool> _running{false};

    WebGPUContext* _ctx = nullptr;
    ShaderManager* _shaderManager = nullptr;
    ShaderGlyphRenderContext _renderContext;

    // GPU resources
    WGPURenderPipeline _pipeline = nullptr;
    WGPUBindGroupLayout _bindGroupLayout = nullptr;
    WGPUPipelineLayout _pipelineLayout = nullptr;
    WGPUBuffer _uniformBuffer = nullptr;
    WGPUBindGroup _bindGroup = nullptr;

    // Current instances
    std::vector<ShaderGlyphInstance> _instances;

    bool _initialized = false;
    bool _failed = false;
};

} // namespace yetty
