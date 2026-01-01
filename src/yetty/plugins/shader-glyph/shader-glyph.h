#pragma once

#include <yetty/custom-glyph-plugin.h>
#include <webgpu/webgpu.h>
#include <unordered_map>
#include <string>

namespace yetty {

// Cached pipeline for a specific shader file
struct ShaderPipeline {
    WGPURenderPipeline pipeline = nullptr;
    WGPUShaderModule fragModule = nullptr;
    std::string shaderFile;
};

//-----------------------------------------------------------------------------
// ShaderGlyphLayer - renders a single animated glyph
//-----------------------------------------------------------------------------
class ShaderGlyphLayer : public CustomGlyphLayer {
public:
    ShaderGlyphLayer() = default;
    ~ShaderGlyphLayer() override;

    Result<void> init(uint32_t codepoint) override;
    void update(double deltaTime) override;
    Result<void> render(WebGPUContext& ctx,
                        WGPUTextureView targetView,
                        WGPUTextureFormat targetFormat,
                        uint32_t screenWidth, uint32_t screenHeight,
                        float pixelX, float pixelY,
                        float pixelW, float pixelH) override;
    void dispose() override;

    void setParent(class ShaderGlyphPlugin* parent) { _parent = parent; }

private:
    class ShaderGlyphPlugin* _parent = nullptr;
};

//-----------------------------------------------------------------------------
// ShaderGlyphPlugin - manages shader-based glyph rendering with external files
//-----------------------------------------------------------------------------
class ShaderGlyphPlugin : public CustomGlyphPlugin {
public:
    ShaderGlyphPlugin() = default;
    ~ShaderGlyphPlugin() override;

    static Result<CustomGlyphPluginPtr> create();

    const char* pluginName() const override { return "shader-glyph"; }

    std::vector<CodepointRange> getCodepointRanges() const override;

    Result<void> init(WebGPUContext* ctx) override;
    Result<CustomGlyphLayerPtr> createLayer(uint32_t codepoint) override;
    Result<void> renderAll(WebGPUContext& ctx,
                           WGPUTextureView targetView,
                           WGPUTextureFormat targetFormat,
                           uint32_t screenWidth, uint32_t screenHeight,
                           float cellWidth, float cellHeight,
                           int scrollOffset = 0) override;
    void update(double deltaTime) override;
    void dispose() override;

    // Get pipeline for a codepoint (loads/caches as needed)
    WGPURenderPipeline getPipelineForCodepoint(uint32_t codepoint);

    // Render a single layer
    Result<void> renderLayer(WebGPUContext& ctx,
                              WGPUTextureView targetView,
                              WGPUTextureFormat targetFormat,
                              uint32_t screenWidth, uint32_t screenHeight,
                              float pixelX, float pixelY,
                              float pixelW, float pixelH,
                              float time, uint32_t codepoint);

private:
    Result<void> loadMapping(const std::string& mappingPath);
    Result<void> initSharedResources(WGPUDevice device, WGPUTextureFormat targetFormat);
    Result<WGPURenderPipeline> loadShaderPipeline(const std::string& shaderFile);
    std::string getShaderForCodepoint(uint32_t codepoint) const;
    static const char* getVertexShader();
    std::string loadShaderFile(const std::string& filename) const;

    // Shader base path
    std::string _shaderPath;

    // Codepoint -> shader file mapping
    std::unordered_map<uint32_t, std::string> _codepointToShader;
    std::string _defaultShader = "default.wgsl";

    // Cached pipelines per shader file
    std::unordered_map<std::string, WGPURenderPipeline> _pipelineCache;

    // Shared resources
    WGPUDevice _device = nullptr;
    WGPUBindGroupLayout _bindGroupLayout = nullptr;
    WGPUPipelineLayout _pipelineLayout = nullptr;
    WGPUBuffer _uniformBuffer = nullptr;
    WGPUBindGroup _bindGroup = nullptr;
    WGPUShaderModule _vertModule = nullptr;
    WGPUTextureFormat _targetFormat = WGPUTextureFormat_Undefined;
};

} // namespace yetty
