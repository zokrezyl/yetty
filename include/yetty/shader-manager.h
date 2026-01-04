#pragma once

#include <yetty/result.hpp>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>

namespace yetty {

class WebGPUContext;

//-----------------------------------------------------------------------------
// Global uniforms - shared by all shaders, updated once per frame
//-----------------------------------------------------------------------------
struct GlobalUniforms {
    float iTime;              // Time since app start (ShaderToy compatible)
    float iTimeRelative;      // Same as iTime (explicit name)
    float iTimeDelta;         // Time since last frame
    uint32_t iFrame;          // Frame counter
    float iMouse[4];          // xy = current pos (normalized), zw = click pos
    float iScreenResolution[2]; // Screen size in pixels
    float _pad[2];            // Padding to 48 bytes (16-byte aligned)
};

//-----------------------------------------------------------------------------
// ShaderManager - caches compiled shader modules (owned by Yetty)
//
// Used by ShaderGlyphRenderer for animated glyphs and by ShaderToy plugin.
// Also manages global uniforms (time, mouse) shared by all shaders.
//-----------------------------------------------------------------------------
class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager();

    // Initialize with device and shader search path
    Result<void> init(WGPUDevice device, const std::string& shaderPath);
    void dispose();

    // Get compiled fragment shader by name (loads and caches)
    Result<WGPUShaderModule> getFragmentShader(const std::string& name);

    // Shared vertex shader for rendering quads
    WGPUShaderModule getQuadVertexShader();

    // Check if shader exists (without loading)
    bool hasShader(const std::string& name) const;

    // Get shader path
    const std::string& shaderPath() const { return _shaderPath; }

    //-------------------------------------------------------------------------
    // Global uniforms - updated once per frame, shared by all shaders
    //-------------------------------------------------------------------------

    // Update global uniforms (call once per frame from main render loop)
    void updateGlobalUniforms(WGPUQueue queue,
                              float deltaTime,
                              float mouseX, float mouseY,
                              float mouseClickX, float mouseClickY,
                              uint32_t screenWidth, uint32_t screenHeight);

    // Get global bind group layout (for creating pipelines)
    WGPUBindGroupLayout getGlobalBindGroupLayout() const { return _globalBindGroupLayout; }

    // Get global bind group (for rendering)
    WGPUBindGroup getGlobalBindGroup() const { return _globalBindGroup; }

    // Get current global uniforms (for plugins that need CPU-side values)
    const GlobalUniforms& getGlobalUniforms() const { return _globalUniforms; }

private:
    Result<WGPUShaderModule> loadShader(const std::string& filename);
    Result<void> createQuadVertexShader();
    Result<void> createGlobalUniformBuffer();

    WGPUDevice _device = nullptr;
    std::string _shaderPath;

    // Shared vertex shader for all quad-based rendering
    WGPUShaderModule _quadVertexShader = nullptr;

    // Cached fragment shaders by name
    std::unordered_map<std::string, WGPUShaderModule> _fragmentShaders;

    // Global uniforms (time, mouse, screen)
    GlobalUniforms _globalUniforms = {};
    WGPUBuffer _globalUniformBuffer = nullptr;
    WGPUBindGroupLayout _globalBindGroupLayout = nullptr;
    WGPUBindGroup _globalBindGroup = nullptr;
    uint32_t _frameCount = 0;
    double _appStartTime = 0.0;
    double _lastFrameTime = 0.0;
};

} // namespace yetty
