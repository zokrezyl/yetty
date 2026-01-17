#include "shader.h"
#include "../../widget-factory.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/shader-manager.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>
#include <iostream>
#include <sstream>
#include <regex>
#include <chrono>
#include <stb_image.h>

namespace yetty {

//-----------------------------------------------------------------------------
// ShaderPlugin
//-----------------------------------------------------------------------------

ShaderPlugin::~ShaderPlugin() {
    (void)dispose();
}

Result<PluginPtr> ShaderPlugin::create() noexcept {
    auto p = PluginPtr(new ShaderPlugin());
    if (auto res = static_cast<ShaderPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init ShaderPlugin", res);
    }
    return Ok(p);
}

Result<void> ShaderPlugin::pluginInit() noexcept {
    _initialized = true;
    return Ok();
}

Result<void> ShaderPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose ShaderPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<WidgetPtr> ShaderPlugin::createWidget(
    const std::string& widgetName,
    WidgetFactory* factory,
    FontManager* fontManager,
    uv_loop_t* loop,
    int32_t x,
    int32_t y,
    uint32_t widthCells,
    uint32_t heightCells,
    const std::string& pluginArgs,
    const std::string& payload
) {
    (void)widgetName;
    yfunc();
    yinfo("payload size={} x={} y={} w={} h={} pluginArgs='{}'", payload.size(), x, y, widthCells, heightCells, pluginArgs);
    return Shader::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload);
}

//-----------------------------------------------------------------------------
// Shader
//-----------------------------------------------------------------------------

Shader::~Shader() {
    (void)dispose();
}

Result<void> Shader::init() {
    if (_payload.empty()) {
        return Err<void>("Shader: empty payload");
    }
    _compiled = false;
    _failed = false;
    
    // Parse plugin args for channel textures
    parsePluginArgs();
    
    yinfo("Shader: initialized with {} bytes of shader code, pluginArgs='{}'", _payload.size(), _pluginArgs);
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!_channels[i].path.empty()) {
            yinfo("Shader: channel[{}] = '{}'", i, _channels[i].path);
        }
    }
    return Ok();
}

Result<void> Shader::dispose() {
    // Release main pass resources
    if (_bindGroup) {
        wgpuBindGroupRelease(_bindGroup);
        _bindGroup = nullptr;
    }
    if (_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(_bindGroupLayout);
        _bindGroupLayout = nullptr;
    }
    if (_pipeline) {
        wgpuRenderPipelineRelease(_pipeline);
        _pipeline = nullptr;
    }
    if (_uniformBuffer) {
        wgpuBufferRelease(_uniformBuffer);
        _uniformBuffer = nullptr;
    }

    // Release buffer pass resources
    for (auto& pass : _bufferPasses) {
        if (pass.bindGroup) {
            wgpuBindGroupRelease(pass.bindGroup);
            pass.bindGroup = nullptr;
        }
        if (pass.pipeline) {
            wgpuRenderPipelineRelease(pass.pipeline);
            pass.pipeline = nullptr;
        }
        if (pass.textureView) {
            wgpuTextureViewRelease(pass.textureView);
            pass.textureView = nullptr;
        }
        if (pass.texturePrevView) {
            wgpuTextureViewRelease(pass.texturePrevView);
            pass.texturePrevView = nullptr;
        }
        if (pass.texture) {
            wgpuTextureRelease(pass.texture);
            pass.texture = nullptr;
        }
        if (pass.texturePrev) {
            wgpuTextureRelease(pass.texturePrev);
            pass.texturePrev = nullptr;
        }
        pass.enabled = false;
    }
    if (_bufferSampler) {
        wgpuSamplerRelease(_bufferSampler);
        _bufferSampler = nullptr;
    }
    if (_bufferBindGroupLayout) {
        wgpuBindGroupLayoutRelease(_bufferBindGroupLayout);
        _bufferBindGroupLayout = nullptr;
    }

    // Release channel texture resources
    for (auto& channel : _channels) {
        if (channel.textureView) {
            wgpuTextureViewRelease(channel.textureView);
            channel.textureView = nullptr;
        }
        if (channel.texture) {
            wgpuTextureRelease(channel.texture);
            channel.texture = nullptr;
        }
        if (channel.imageData) {
            stbi_image_free(channel.imageData);
            channel.imageData = nullptr;
        }
        channel.loaded = false;
    }
    if (_channelSampler) {
        wgpuSamplerRelease(_channelSampler);
        _channelSampler = nullptr;
    }
    if (_placeholderTextureView) {
        wgpuTextureViewRelease(_placeholderTextureView);
        _placeholderTextureView = nullptr;
    }
    if (_placeholderTexture) {
        wgpuTextureRelease(_placeholderTexture);
        _placeholderTexture = nullptr;
    }
    
    // Release standalone global resources
    if (_globalBindGroup) {
        wgpuBindGroupRelease(_globalBindGroup);
        _globalBindGroup = nullptr;
    }
    if (_globalBindGroupLayout) {
        wgpuBindGroupLayoutRelease(_globalBindGroupLayout);
        _globalBindGroupLayout = nullptr;
    }
    if (_globalUniformBuffer) {
        wgpuBufferRelease(_globalUniformBuffer);
        _globalUniformBuffer = nullptr;
    }

    _compiled = false;
    return Ok();
}

void Shader::releaseGPUResources() {
    // Release GPU resources but keep shader code and channel paths for recompilation

    // Release main pass resources
    if (_bindGroup) { wgpuBindGroupRelease(_bindGroup); _bindGroup = nullptr; }
    if (_bindGroupLayout) { wgpuBindGroupLayoutRelease(_bindGroupLayout); _bindGroupLayout = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniformBuffer) { wgpuBufferRelease(_uniformBuffer); _uniformBuffer = nullptr; }

    // Release buffer pass resources
    for (auto& pass : _bufferPasses) {
        if (pass.bindGroup) { wgpuBindGroupRelease(pass.bindGroup); pass.bindGroup = nullptr; }
        if (pass.pipeline) { wgpuRenderPipelineRelease(pass.pipeline); pass.pipeline = nullptr; }
        if (pass.textureView) { wgpuTextureViewRelease(pass.textureView); pass.textureView = nullptr; }
        if (pass.texturePrevView) { wgpuTextureViewRelease(pass.texturePrevView); pass.texturePrevView = nullptr; }
        if (pass.texture) { wgpuTextureRelease(pass.texture); pass.texture = nullptr; }
        if (pass.texturePrev) { wgpuTextureRelease(pass.texturePrev); pass.texturePrev = nullptr; }
        // Keep pass.enabled and pass.shaderCode
    }
    if (_bufferSampler) { wgpuSamplerRelease(_bufferSampler); _bufferSampler = nullptr; }
    if (_bufferBindGroupLayout) { wgpuBindGroupLayoutRelease(_bufferBindGroupLayout); _bufferBindGroupLayout = nullptr; }
    _bufferWidth = 0;
    _bufferHeight = 0;

    // Release channel texture GPU resources but keep paths and loaded image data
    for (auto& channel : _channels) {
        if (channel.textureView) { wgpuTextureViewRelease(channel.textureView); channel.textureView = nullptr; }
        if (channel.texture) { wgpuTextureRelease(channel.texture); channel.texture = nullptr; }
        // Keep channel.path, channel.imageData, channel.width, channel.height
        // Just mark as not loaded so texture gets recreated
        if (channel.loaded) channel.loaded = false;
    }
    if (_channelSampler) { wgpuSamplerRelease(_channelSampler); _channelSampler = nullptr; }
    if (_placeholderTextureView) { wgpuTextureViewRelease(_placeholderTextureView); _placeholderTextureView = nullptr; }
    if (_placeholderTexture) { wgpuTextureRelease(_placeholderTexture); _placeholderTexture = nullptr; }

    // Release standalone global resources
    if (_globalBindGroup) { wgpuBindGroupRelease(_globalBindGroup); _globalBindGroup = nullptr; }
    if (_globalBindGroupLayout) { wgpuBindGroupLayoutRelease(_globalBindGroupLayout); _globalBindGroupLayout = nullptr; }
    if (_globalUniformBuffer) { wgpuBufferRelease(_globalUniformBuffer); _globalUniformBuffer = nullptr; }

    _compiled = false;
    yinfo("Shader: GPU resources released");
}

void Shader::prepareFrame(WebGPUContext& ctx, bool on) {
    // Handle on/off transitions for GPU resource management
    if (!on && wasOn_) {
        // Transitioning to off - release GPU resources
        yinfo("Shader: Transitioning to off - releasing GPU resources");
        releaseGPUResources();
        wasOn_ = false;
        return;
    }

    if (on && !wasOn_) {
        // Transitioning to on - will reinitialize
        yinfo("Shader: Transitioning to on - will reinitialize");
        wasOn_ = true;
        _compiled = false;  // Force recompilation
    }

    if (!on || _failed || !_visible) return;

    const auto& rc = _renderCtx;

    if (!_compiled) {
        ydebug("Shader prepareFrame: compiling, pluginArgs='{}', channels[0].path='{}'", _pluginArgs, _channels[0].path);

        // Load channel textures if specified
        for (int i = 0; i < kMaxChannels; ++i) {
            ydebug("Shader prepareFrame: channel[{}] path='{}' loaded={}", i, _channels[i].path, _channels[i].loaded);
            if (!_channels[i].path.empty() && !_channels[i].loaded) {
                if (auto res = createChannelSampler(ctx); !res) {
                    ywarn("Shader prepareFrame: Failed to create channel sampler: {}", res.error().message());
                }
                if (auto res = loadChannelTexture(ctx, i, _channels[i].path); !res) {
                    ywarn("Shader prepareFrame: Failed to load iChannel{}: {}", i, res.error().message());
                }
            }
        }

        ydebug("Shader prepareFrame: after loading, _hasChannelTextures={}", _hasChannelTextures);

        // Parse multipass sections first
        _isMultipass = parseMultipassShader(_payload);

        // Use parsed _mainImageCode or original payload
        std::string shaderToCompile = _mainImageCode.empty() ? _payload : _mainImageCode;
        auto result = compileShader(ctx, rc.targetFormat, shaderToCompile);
        if (!result) {
            _failed = true;
            yerror("Shader prepareFrame: Failed to compile shader: {}", result.error().message());
            return;
        }
        
        // Setup multipass if detected
        if (_isMultipass) {
            uint32_t w = static_cast<uint32_t>(_widthCells * rc.cellWidth);
            uint32_t h = static_cast<uint32_t>(_heightCells * rc.cellHeight);
            if (w > 0 && h > 0) {
                if (auto res = createBufferTextures(ctx, w, h); !res) {
                    ywarn("Shader: Failed to create buffer textures: {}", res.error().message());
                    _isMultipass = false;
                } else {
                    for (int i = 0; i < kMaxBufferPasses; ++i) {
                        if (_bufferPasses[i].enabled) {
                            if (auto res2 = compileBufferPass(ctx, i); !res2) {
                                ywarn("Shader: Failed to compile buffer pass {}: {}", i, res2.error().message());
                            }
                        }
                    }
                    // Create bind group for Image pass with texture bindings
                    if (_bufferBindGroupLayout && _bufferSampler) {
                        std::array<WGPUBindGroupEntry, 6> bgEntries = {};
                        bgEntries[0].binding = 0;
                        bgEntries[0].buffer = _uniformBuffer;
                        bgEntries[0].size = 48;
                        
                        bgEntries[1].binding = 1;
                        bgEntries[1].sampler = _bufferSampler;
                        
                        // Bind buffer textures as iChannel inputs
                        for (int i = 0; i < 4; ++i) {
                            bgEntries[2 + i].binding = 2 + i;
                            if (i < kMaxBufferPasses && _bufferPasses[i].texturePrevView) {
                                bgEntries[2 + i].textureView = _bufferPasses[i].texturePrevView;
                            } else if (_bufferPasses[0].texturePrevView) {
                                bgEntries[2 + i].textureView = _bufferPasses[0].texturePrevView;
                            }
                        }
                        
                        WGPUBindGroupDescriptor bgDesc = {};
                        bgDesc.layout = _bufferBindGroupLayout;
                        bgDesc.entryCount = bgEntries.size();
                        bgDesc.entries = bgEntries.data();
                        _bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
                    }
                }
            }
        }
        _compiled = true;
    }

    if (!_pipeline || !_uniformBuffer || !_bindGroup) {
        _failed = true;
        yerror("Shader prepareFrame: pipeline not initialized");
        return;
    }

    // Get global bind group - standalone mode (plugin tester or standalone)
    WGPUBindGroup globalBindGroup = nullptr;
    if (!_globalBindGroup) {
        if (auto res = createStandaloneGlobalBindGroup(ctx); !res) {
            yerror("Shader prepareFrame: failed to create standalone global bind group: {}", res.error().message());
            return;
        }
    }
    globalBindGroup = _globalBindGroup;
    
    // Update global uniforms
    struct GlobalUniforms {
        float iTime;
        float iTimeRelative;
        float iTimeDelta;
        uint32_t iFrame;
        float iMouse[4];
        float iScreenResolution[2];
        float _pad[2];
    } globals;
    
    static auto startTime = std::chrono::steady_clock::now();
    static uint32_t frameCount = 0;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - startTime).count();
    
    globals.iTime = elapsed;
    globals.iTimeRelative = elapsed;
    globals.iTimeDelta = rc.deltaTime;
    globals.iFrame = frameCount++;
    globals.iMouse[0] = _mouseX * rc.screenWidth;
    globals.iMouse[1] = _mouseY * rc.screenHeight;
    globals.iMouse[2] = _mouseDown ? 1.0f : 0.0f;
    globals.iMouse[3] = _mouseGrabbed ? 1.0f : 0.0f;
    globals.iScreenResolution[0] = static_cast<float>(rc.screenWidth);
    globals.iScreenResolution[1] = static_cast<float>(rc.screenHeight);
    
    wgpuQueueWriteBuffer(ctx.getQueue(), _globalUniformBuffer, 0, &globals, sizeof(globals));

    // Render buffer passes first (if any)
    if (_isMultipass) {
        renderBufferPasses(ctx);
    }

    // Compute pixel position from cell position and scroll
    float pixelX = static_cast<float>(_x) * rc.cellWidth;
    float pixelY = static_cast<float>(_y) * rc.cellHeight;
    float pixelW = static_cast<float>(_widthCells) * rc.cellWidth;
    float pixelH = static_cast<float>(_heightCells) * rc.cellHeight;

    // Relative widgets scroll with content
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Visibility check
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return;  // Off-screen, skip
        }
    }

    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    struct PluginUniforms {
        float resolution[2];
        float param;
        float zoom;
        float rect[4];
        float mouse[4];
    } uniforms;

    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.param = _param;
    uniforms.zoom = _zoom;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = _mouseX;
    uniforms.mouse[1] = _mouseY;
    uniforms.mouse[2] = _mouseGrabbed ? 1.0f : 0.0f;
    uniforms.mouse[3] = _mouseDown ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        yerror("Shader prepareFrame: Failed to create command encoder");
        return;
    }

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = rc.targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        yerror("Shader prepareFrame: Failed to begin render pass");
        return;
    }

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, _bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        yerror("Shader prepareFrame: Failed to finish command encoder");
        return;
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

Result<void> Shader::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    if (!on) return Ok();  // Skip when off
    if (_failed) return Err<void>("Shader already failed");
    if (!_visible) return Ok();

    const auto& rc = _renderCtx;

    if (!_compiled) {
        ydebug("Shader: compiling, pluginArgs='{}', channels[0].path='{}'", _pluginArgs, _channels[0].path);

        // Load channel textures if specified
        for (int i = 0; i < kMaxChannels; ++i) {
            ydebug("Shader: channel[{}] path='{}' loaded={}", i, _channels[i].path, _channels[i].loaded);
            if (!_channels[i].path.empty() && !_channels[i].loaded) {
                if (auto res = createChannelSampler(ctx); !res) {
                    ywarn("Shader: Failed to create channel sampler: {}", res.error().message());
                }
                if (auto res = loadChannelTexture(ctx, i, _channels[i].path); !res) {
                    ywarn("Shader: Failed to load iChannel{}: {}", i, res.error().message());
                }
            }
        }

        ydebug("Shader: after loading, _hasChannelTextures={}", _hasChannelTextures);

        // Parse multipass sections first
        _isMultipass = parseMultipassShader(_payload);
        
        // Use parsed _mainImageCode or original payload
        std::string shaderToCompile = _mainImageCode.empty() ? _payload : _mainImageCode;
        auto result = compileShader(ctx, rc.targetFormat, shaderToCompile);
        if (!result) {
            _failed = true;
            return Err<void>("Shader: Failed to compile shader", result);
        }
        
        // Setup multipass if detected
        if (_isMultipass) {
            uint32_t w = static_cast<uint32_t>(_widthCells * rc.cellWidth);
            uint32_t h = static_cast<uint32_t>(_heightCells * rc.cellHeight);
            if (w > 0 && h > 0) {
                if (auto res = createBufferTextures(ctx, w, h); res) {
                    for (int i = 0; i < kMaxBufferPasses; ++i) {
                        if (_bufferPasses[i].enabled) {
                            (void)compileBufferPass(ctx, i);
                        }
                    }
                    // Create bind group for Image pass with texture bindings
                    if (_bufferBindGroupLayout && _bufferSampler) {
                        std::array<WGPUBindGroupEntry, 6> bgEntries = {};
                        bgEntries[0].binding = 0;
                        bgEntries[0].buffer = _uniformBuffer;
                        bgEntries[0].size = 48;
                        
                        bgEntries[1].binding = 1;
                        bgEntries[1].sampler = _bufferSampler;
                        
                        for (int i = 0; i < 4; ++i) {
                            bgEntries[2 + i].binding = 2 + i;
                            if (i < kMaxBufferPasses && _bufferPasses[i].texturePrevView) {
                                bgEntries[2 + i].textureView = _bufferPasses[i].texturePrevView;
                            } else if (_bufferPasses[0].texturePrevView) {
                                bgEntries[2 + i].textureView = _bufferPasses[0].texturePrevView;
                            }
                        }
                        
                        WGPUBindGroupDescriptor bgDesc = {};
                        bgDesc.layout = _bufferBindGroupLayout;
                        bgDesc.entryCount = bgEntries.size();
                        bgDesc.entries = bgEntries.data();
                        _bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
                    }
                } else {
                    _isMultipass = false;
                }
            }
        }
        _compiled = true;
    }

    if (!_pipeline || !_uniformBuffer || !_bindGroup) {
        _failed = true;
        return Err<void>("Shader: pipeline not initialized");
    }

    // Get global bind group - standalone mode
    WGPUBindGroup globalBindGroup = nullptr;
    if (!_globalBindGroup) {
        // Create standalone bind group
        if (auto res = createStandaloneGlobalBindGroup(ctx); !res) {
            return Err<void>("Shader: failed to create standalone global bind group", res);
        }
    }
    globalBindGroup = _globalBindGroup;
    
    // Update global uniforms
    struct GlobalUniforms {
        float iTime;
        float iTimeRelative;
        float iTimeDelta;
        uint32_t iFrame;
        float iMouse[4];
        float iScreenResolution[2];
        float _pad[2];
    } globals;
    
    static auto startTime = std::chrono::steady_clock::now();
    static uint32_t frameCount = 0;
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - startTime).count();
    
    globals.iTime = elapsed;
    globals.iTimeRelative = elapsed;
    globals.iTimeDelta = rc.deltaTime;
    globals.iFrame = frameCount++;
    globals.iMouse[0] = _mouseX * rc.screenWidth;
    globals.iMouse[1] = _mouseY * rc.screenHeight;
    globals.iMouse[2] = _mouseDown ? 1.0f : 0.0f;
    globals.iMouse[3] = _mouseGrabbed ? 1.0f : 0.0f;
    globals.iScreenResolution[0] = static_cast<float>(rc.screenWidth);
    globals.iScreenResolution[1] = static_cast<float>(rc.screenHeight);
    
    wgpuQueueWriteBuffer(ctx.getQueue(), _globalUniformBuffer, 0, &globals, sizeof(globals));

    if (!globalBindGroup) return Err<void>("Shader: no global bind group");

    // Render buffer passes first (if any) - these create their own command buffers
    if (_isMultipass) {
        renderBufferPasses(ctx);
    }

    // Compute pixel position from cell position and scroll
    float pixelX = static_cast<float>(_x) * rc.cellWidth;
    float pixelY = static_cast<float>(_y) * rc.cellHeight;
    float pixelW = static_cast<float>(_widthCells) * rc.cellWidth;
    float pixelH = static_cast<float>(_heightCells) * rc.cellHeight;

    // Relative widgets scroll with content
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Visibility check
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();  // Off-screen, not an error
        }
    }

    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    struct PluginUniforms {
        float resolution[2];
        float param;
        float zoom;
        float rect[4];
        float mouse[4];
    } uniforms;

    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.param = _param;
    uniforms.zoom = _zoom;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.mouse[0] = _mouseX;
    uniforms.mouse[1] = _mouseY;
    uniforms.mouse[2] = _mouseGrabbed ? 1.0f : 0.0f;
    uniforms.mouse[3] = _mouseDown ? 1.0f : 0.0f;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniformBuffer, 0, &uniforms, sizeof(uniforms));

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, globalBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, _bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    return Ok();
}

bool Shader::onMouseMove(float localX, float localY) {
    _mouseX = localX / static_cast<float>(_pixelWidth);
    // Invert Y to match Shadertoy/GLSL convention (Y=0 at bottom)
    _mouseY = 1.0f - (localY / static_cast<float>(_pixelHeight));
    ydebug("Shader::onMouseMove: local=({},{}) normalized=({},{})",
                  localX, localY, _mouseX, _mouseY);
    return true;
}

bool Shader::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        _mouseDown = pressed;
        _mouseGrabbed = pressed;
        ydebug("Shader::onMouseButton: button={} pressed={} grabbed={}",
                      button, pressed, _mouseGrabbed);
        return true;
    }
    if (button == -1) {
        _mouseGrabbed = false;
        ydebug("Shader::onMouseButton: focus lost");
        return false;
    }
    return false;
}

bool Shader::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;
    bool ctrlPressed = (mods & 0x0002) != 0;

    if (ctrlPressed) {
        _zoom += yoffset * 0.1f;
        _zoom = std::max(0.1f, std::min(5.0f, _zoom));
        ydebug("Shader::onMouseScroll: CTRL+scroll _zoom={}", _zoom);
    } else {
        _param += yoffset * 0.1f;
        _param = std::max(0.0f, std::min(1.0f, _param));
        ydebug("Shader::onMouseScroll: scroll _param={}", _param);
    }
    return true;
}

const char* Shader::getVertexShader() {
    return R"(
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 1.0)
    );

    let pos = positions[vertexIndex];
    let ndcX = plugin.rect.x + pos.x * plugin.rect.z;
    let ndcY = plugin.rect.y - pos.y * plugin.rect.w;

    var output: VertexOutput;
    output.position = vec4<f32>(ndcX, ndcY, 0.0, 1.0);
    output.uv = pos;
    return output;
}
)";
}

std::string Shader::wrapFragmentShader(const std::string& userCode) {
    std::ostringstream ss;
    ss << R"(
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;

fn iTime() -> f32 { return global.iTime; }
fn iTimeRelative() -> f32 { return global.iTimeRelative; }
fn iTimeDelta() -> f32 { return global.iTimeDelta; }
fn iFrame() -> u32 { return global.iFrame; }
fn iResolution() -> vec2<f32> { return plugin.resolution; }
fn iScreenResolution() -> vec2<f32> { return global.iScreenResolution; }
fn iMouse() -> vec4<f32> { return plugin.mouse; }
fn iMouseGlobal() -> vec4<f32> { return global.iMouse; }
fn iParam() -> f32 { return plugin.param; }
fn iZoom() -> f32 { return plugin.zoom; }
fn iGrabbed() -> bool { return plugin.mouse.z > 0.5; }
fn iMouseDown() -> bool { return plugin.mouse.w > 0.5; }

)" << userCode << R"(

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = vec2<f32>(uv.x, 1.0 - uv.y) * plugin.resolution;
    var col = mainImage(fragCoord);

    let border = 3.0;
    let res = plugin.resolution;
    let onBorder = fragCoord.x < border || fragCoord.x > res.x - border ||
                   fragCoord.y < border || fragCoord.y > res.y - border;

    if (onBorder) {
        if (iGrabbed()) {
            col = vec4<f32>(0.2, 0.9, 0.3, 1.0);
        } else {
            col = vec4<f32>(0.4, 0.4, 0.4, 1.0);
        }
    }

    return col;
}
)";
    return ss.str();
}

Result<void> Shader::compileShader(WebGPUContext& ctx,
                                        WGPUTextureFormat targetFormat,
                                        const std::string& fragmentCode) {
    // Get global bind group layout - standalone mode
    WGPUBindGroupLayout globalBGL = _globalBindGroupLayout;
    
    // Create standalone if needed
    if (!globalBGL) {
        if (auto res = createStandaloneGlobalBindGroup(ctx); !res) {
            return Err<void>("Failed to create standalone global bind group", res);
        }
        globalBGL = _globalBindGroupLayout;
    }
    
    if (!globalBGL) {
        return Err<void>("Shader: no global bind group layout");
    }

    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniformBuffer = wgpuDeviceCreateBuffer(ctx.getDevice(), &bufDesc);
    if (!_uniformBuffer) {
        return Err<void>("Failed to create uniform buffer");
    }

    std::string vertCode = getVertexShader();
    WGPUShaderSourceWGSL wgslDescVert = {};
    wgslDescVert.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDescVert.code = { .data = vertCode.c_str(), .length = vertCode.size() };

    WGPUShaderModuleDescriptor shaderDescVert = {};
    shaderDescVert.nextInChain = &wgslDescVert.chain;
    WGPUShaderModule vertModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDescVert);

    // Use different wrapper based on multipass mode or channel textures
    std::string fragCode;
    if (_isMultipass || _hasChannelTextures) {
        fragCode = wrapImagePassShader(fragmentCode);
    } else {
        fragCode = wrapFragmentShader(fragmentCode);
    }
    ydebug("Shader: compiling fragment shader (multipass={}, hasChannels={})", _isMultipass, _hasChannelTextures);

    WGPUShaderSourceWGSL wgslDescFrag = {};
    wgslDescFrag.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDescFrag.code = { .data = fragCode.c_str(), .length = fragCode.size() };

    WGPUShaderModuleDescriptor shaderDescFrag = {};
    shaderDescFrag.nextInChain = &wgslDescFrag.chain;
    WGPUShaderModule fragModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDescFrag);

    if (!vertModule || !fragModule) {
        if (vertModule) wgpuShaderModuleRelease(vertModule);
        if (fragModule) wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create shader modules");
    }

    // Create bind group layout - different for multipass/channel textures (needs textures)
    if (_isMultipass || _hasChannelTextures) {
        // Reuse _bufferBindGroupLayout which has uniform + sampler + textures
        if (!_bufferBindGroupLayout) {
            std::array<WGPUBindGroupLayoutEntry, 6> entries = {};
            
            // Uniform buffer
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            entries[0].buffer.type = WGPUBufferBindingType_Uniform;
            
            // Sampler
            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            
            // iChannel0-3 textures
            for (int i = 0; i < 4; ++i) {
                entries[2 + i].binding = 2 + i;
                entries[2 + i].visibility = WGPUShaderStage_Fragment;
                entries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
                entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
            }
            
            WGPUBindGroupLayoutDescriptor bglDesc = {};
            bglDesc.entryCount = entries.size();
            bglDesc.entries = entries.data();
            _bufferBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
        }
        _bindGroupLayout = _bufferBindGroupLayout;
        // Add reference since we'll use it
        if (_bindGroupLayout) {
            wgpuBindGroupLayoutAddRef(_bindGroupLayout);
        }
    } else {
        WGPUBindGroupLayoutEntry bindingEntry = {};
        bindingEntry.binding = 0;
        bindingEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bindingEntry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor bglDesc = {};
        bglDesc.entryCount = 1;
        bglDesc.entries = &bindingEntry;
        _bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
    }
    
    if (!_bindGroupLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create per-plugin bind group layout");
    }

    WGPUBindGroupLayout layouts[2] = { globalBGL, _bindGroupLayout };
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.getDevice(), &plDesc);
    if (!pipelineLayout) {
        wgpuShaderModuleRelease(vertModule);
        wgpuShaderModuleRelease(fragModule);
        return Err<void>("Failed to create pipeline layout");
    }

    // Create bind group - different for multipass/channel textures
    if (_isMultipass) {
        // Will be created later when textures are ready
        _bindGroup = nullptr;
    } else if (_hasChannelTextures) {
        // Create bind group with channel textures
        // Use _channelSampler or create one if needed
        if (!_channelSampler) {
            WGPUSamplerDescriptor samplerDesc = {};
            samplerDesc.addressModeU = WGPUAddressMode_Repeat;
            samplerDesc.addressModeV = WGPUAddressMode_Repeat;
            samplerDesc.addressModeW = WGPUAddressMode_Repeat;
            samplerDesc.magFilter = WGPUFilterMode_Linear;
            samplerDesc.minFilter = WGPUFilterMode_Linear;
            samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
            samplerDesc.maxAnisotropy = 1;
            _channelSampler = wgpuDeviceCreateSampler(ctx.getDevice(), &samplerDesc);
        }
        
        // Create placeholder texture for unloaded channels
        if (auto res = createPlaceholderTexture(ctx); !res) {
            wgpuShaderModuleRelease(vertModule);
            wgpuShaderModuleRelease(fragModule);
            wgpuPipelineLayoutRelease(pipelineLayout);
            return Err<void>("Failed to create placeholder texture", res);
        }
        
        std::array<WGPUBindGroupEntry, 6> bgEntries = {};
        bgEntries[0].binding = 0;
        bgEntries[0].buffer = _uniformBuffer;
        bgEntries[0].size = 48;
        
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = _channelSampler;
        
        // Bind channel textures - use loaded texture or placeholder
        for (int i = 0; i < 4; ++i) {
            bgEntries[2 + i].binding = 2 + i;
            if (_channels[i].loaded && _channels[i].textureView) {
                bgEntries[2 + i].textureView = _channels[i].textureView;
            } else {
                bgEntries[2 + i].textureView = _placeholderTextureView;
            }
        }
        
        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = _bindGroupLayout;
        bgDesc.entryCount = bgEntries.size();
        bgDesc.entries = bgEntries.data();
        _bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
        if (!_bindGroup) {
            wgpuShaderModuleRelease(vertModule);
            wgpuShaderModuleRelease(fragModule);
            wgpuPipelineLayoutRelease(pipelineLayout);
            return Err<void>("Failed to create per-plugin bind group with textures");
        }
    } else {
        WGPUBindGroupEntry bgEntry = {};
        bgEntry.binding = 0;
        bgEntry.buffer = _uniformBuffer;
        bgEntry.size = 48;

        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = _bindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        _bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
        if (!_bindGroup) {
            wgpuShaderModuleRelease(vertModule);
            wgpuShaderModuleRelease(fragModule);
            wgpuPipelineLayoutRelease(pipelineLayout);
            return Err<void>("Failed to create per-plugin bind group");
        }
    }

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = vertModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUFragmentState fragState = {};
    fragState.module = fragModule;
    fragState.entryPoint = WGPU_STR("fs_main");

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;

    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(ctx.getDevice(), &pipelineDesc);

    wgpuShaderModuleRelease(vertModule);
    wgpuShaderModuleRelease(fragModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) {
        return Err<void>("Failed to create render pipeline");
    }

    ydebug("Shader: pipeline created with global + per-plugin uniforms");
    return Ok();
}

//-----------------------------------------------------------------------------
// Multipass support
//-----------------------------------------------------------------------------

bool Shader::parseMultipassShader(const std::string& code) {
    // Look for section markers: //--- BufferA, //--- BufferB, //--- BufferC, //--- BufferD, //--- Image
    // If no markers found, treat entire code as Image pass
    
    std::regex sectionRegex(R"(//---\s*(BufferA|BufferB|BufferC|BufferD|Image)\s*\n)");
    std::sregex_iterator it(code.begin(), code.end(), sectionRegex);
    std::sregex_iterator end;
    
    if (it == end) {
        // No sections found - single pass shader
        _mainImageCode = code;
        return false;  // Not multipass
    }
    
    // Parse sections
    std::vector<std::pair<std::string, size_t>> sections;
    while (it != end) {
        sections.push_back({(*it)[1].str(), it->position() + it->length()});
        ++it;
    }
    
    for (size_t i = 0; i < sections.size(); ++i) {
        size_t start = sections[i].second;
        size_t end_pos = (i + 1 < sections.size()) ? 
            code.rfind("//---", sections[i + 1].second - 10) : code.size();
        
        std::string sectionCode = code.substr(start, end_pos - start);
        const std::string& name = sections[i].first;
        
        if (name == "BufferA") {
            _bufferPasses[0].shaderCode = sectionCode;
            _bufferPasses[0].enabled = true;
        } else if (name == "BufferB") {
            _bufferPasses[1].shaderCode = sectionCode;
            _bufferPasses[1].enabled = true;
        } else if (name == "BufferC") {
            _bufferPasses[2].shaderCode = sectionCode;
            _bufferPasses[2].enabled = true;
        } else if (name == "BufferD") {
            _bufferPasses[3].shaderCode = sectionCode;
            _bufferPasses[3].enabled = true;
        } else if (name == "Image") {
            _mainImageCode = sectionCode;
        }
    }
    
    if (_mainImageCode.empty()) {
        _mainImageCode = code;  // Fallback
    }
    
    bool hasBufferPass = false;
    for (const auto& pass : _bufferPasses) {
        if (pass.enabled) hasBufferPass = true;
    }
    
    ydebug("Shader: parsed multipass shader, hasBufferPass={}", hasBufferPass);
    return hasBufferPass;
}

Result<void> Shader::createBufferTextures(WebGPUContext& ctx, uint32_t width, uint32_t height) {
    if (_bufferWidth == width && _bufferHeight == height && _bufferSampler) {
        return Ok();  // Already created with correct size
    }
    
    _bufferWidth = width;
    _bufferHeight = height;
    
    // Create sampler if needed
    if (!_bufferSampler) {
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.maxAnisotropy = 1;
        _bufferSampler = wgpuDeviceCreateSampler(ctx.getDevice(), &samplerDesc);
        if (!_bufferSampler) {
            return Err<void>("Failed to create buffer sampler");
        }
    }
    
    // Create textures for each enabled buffer pass
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        if (!_bufferPasses[i].enabled) continue;
        
        // Release old textures
        if (_bufferPasses[i].textureView) {
            wgpuTextureViewRelease(_bufferPasses[i].textureView);
        }
        if (_bufferPasses[i].texturePrevView) {
            wgpuTextureViewRelease(_bufferPasses[i].texturePrevView);
        }
        if (_bufferPasses[i].texture) {
            wgpuTextureRelease(_bufferPasses[i].texture);
        }
        if (_bufferPasses[i].texturePrev) {
            wgpuTextureRelease(_bufferPasses[i].texturePrev);
        }
        
        WGPUTextureDescriptor texDesc = {};
        texDesc.size = {width, height, 1};
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        
        _bufferPasses[i].texture = wgpuDeviceCreateTexture(ctx.getDevice(), &texDesc);
        _bufferPasses[i].texturePrev = wgpuDeviceCreateTexture(ctx.getDevice(), &texDesc);
        
        if (!_bufferPasses[i].texture || !_bufferPasses[i].texturePrev) {
            return Err<void>("Failed to create buffer textures");
        }
        
        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        
        _bufferPasses[i].textureView = wgpuTextureCreateView(_bufferPasses[i].texture, &viewDesc);
        _bufferPasses[i].texturePrevView = wgpuTextureCreateView(_bufferPasses[i].texturePrev, &viewDesc);
        
        ydebug("Shader: created buffer texture {} ({}x{})", i, width, height);
    }
    
    return Ok();
}

std::string Shader::wrapBufferPassShader(const std::string& userCode, int passIndex) {
    std::ostringstream ss;
    // Buffer passes render to fullscreen quad (entire texture)
    ss << R"(
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;
@group(1) @binding(1) var bufferSampler: sampler;
@group(1) @binding(2) var iChannel0: texture_2d<f32>;
@group(1) @binding(3) var iChannel1: texture_2d<f32>;
@group(1) @binding(4) var iChannel2: texture_2d<f32>;
@group(1) @binding(5) var iChannel3: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

// Fullscreen quad vertex shader for buffer passes
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 0.0),
        vec2<f32>(1.0, 1.0),
        vec2<f32>(0.0, 1.0)
    );

    let pos = positions[vertexIndex];
    // Map to NDC: -1 to 1
    let ndcX = pos.x * 2.0 - 1.0;
    let ndcY = 1.0 - pos.y * 2.0;

    var output: VertexOutput;
    output.position = vec4<f32>(ndcX, ndcY, 0.0, 1.0);
    output.uv = pos;
    return output;
}

fn iTime() -> f32 { return global.iTime; }
fn iTimeRelative() -> f32 { return global.iTimeRelative; }
fn iTimeDelta() -> f32 { return global.iTimeDelta; }
fn iFrame() -> u32 { return global.iFrame; }
fn iResolution() -> vec2<f32> { return plugin.resolution; }
fn iScreenResolution() -> vec2<f32> { return global.iScreenResolution; }
fn iMouse() -> vec4<f32> { return plugin.mouse; }
fn iParam() -> f32 { return plugin.param; }
fn iZoom() -> f32 { return plugin.zoom; }

fn texture(tex: texture_2d<f32>, uv: vec2<f32>) -> vec4<f32> {
    return textureSample(tex, bufferSampler, uv);
}

)" << userCode << R"(

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = vec2<f32>(uv.x, 1.0 - uv.y) * plugin.resolution;
    return mainImage(fragCoord);
}
)";
    (void)passIndex;
    return ss.str();
}

std::string Shader::wrapImagePassShader(const std::string& userCode) {
    std::ostringstream ss;
    ss << R"(
struct GlobalUniforms {
    iTime: f32,
    iTimeRelative: f32,
    iTimeDelta: f32,
    iFrame: u32,
    iMouse: vec4<f32>,
    iScreenResolution: vec2<f32>,
    _pad: vec2<f32>,
}

struct PluginUniforms {
    resolution: vec2<f32>,
    param: f32,
    zoom: f32,
    rect: vec4<f32>,
    mouse: vec4<f32>,
}

@group(0) @binding(0) var<uniform> global: GlobalUniforms;
@group(1) @binding(0) var<uniform> plugin: PluginUniforms;
@group(1) @binding(1) var bufferSampler: sampler;
@group(1) @binding(2) var iChannel0: texture_2d<f32>;
@group(1) @binding(3) var iChannel1: texture_2d<f32>;
@group(1) @binding(4) var iChannel2: texture_2d<f32>;
@group(1) @binding(5) var iChannel3: texture_2d<f32>;

fn iTime() -> f32 { return global.iTime; }
fn iTimeRelative() -> f32 { return global.iTimeRelative; }
fn iTimeDelta() -> f32 { return global.iTimeDelta; }
fn iFrame() -> u32 { return global.iFrame; }
fn iResolution() -> vec2<f32> { return plugin.resolution; }
fn iScreenResolution() -> vec2<f32> { return global.iScreenResolution; }
fn iMouse() -> vec4<f32> { return plugin.mouse; }
fn iMouseGlobal() -> vec4<f32> { return global.iMouse; }
fn iParam() -> f32 { return plugin.param; }
fn iZoom() -> f32 { return plugin.zoom; }
fn iGrabbed() -> bool { return plugin.mouse.z > 0.5; }
fn iMouseDown() -> bool { return plugin.mouse.w > 0.5; }

fn texture(tex: texture_2d<f32>, uv: vec2<f32>) -> vec4<f32> {
    return textureSample(tex, bufferSampler, uv);
}

)" << userCode << R"(

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = vec2<f32>(uv.x, 1.0 - uv.y) * plugin.resolution;
    var col = mainImage(fragCoord);

    let border = 3.0;
    let res = plugin.resolution;
    let onBorder = fragCoord.x < border || fragCoord.x > res.x - border ||
                   fragCoord.y < border || fragCoord.y > res.y - border;

    if (onBorder) {
        if (iGrabbed()) {
            col = vec4<f32>(0.2, 0.9, 0.3, 1.0);
        } else {
            col = vec4<f32>(0.4, 0.4, 0.4, 1.0);
        }
    }

    return col;
}
)";
    return ss.str();
}

Result<void> Shader::compileBufferPass(WebGPUContext& ctx, int passIndex) {
    auto& pass = _bufferPasses[passIndex];
    if (!pass.enabled || pass.shaderCode.empty()) {
        return Ok();
    }
    
    // Get global bind group layout - standalone mode
    WGPUBindGroupLayout globalBGL = _globalBindGroupLayout;
    if (!globalBGL) {
        return Err<void>("Shader: no global bind group layout for buffer pass");
    }
    
    // Create bind group layout for buffer pass (uniforms + sampler + 4 textures)
    if (!_bufferBindGroupLayout) {
        std::array<WGPUBindGroupLayoutEntry, 6> entries = {};
        
        // Uniform buffer
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        
        // Sampler
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
        
        // iChannel0-3 textures
        for (int i = 0; i < 4; ++i) {
            entries[2 + i].binding = 2 + i;
            entries[2 + i].visibility = WGPUShaderStage_Fragment;
            entries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
            entries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
        }
        
        WGPUBindGroupLayoutDescriptor bglDesc = {};
        bglDesc.entryCount = entries.size();
        bglDesc.entries = entries.data();
        _bufferBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
        if (!_bufferBindGroupLayout) {
            return Err<void>("Failed to create buffer bind group layout");
        }
    }
    
    // Compile shaders - wrapBufferPassShader includes both vertex and fragment
    std::string shaderCode = wrapBufferPassShader(pass.shaderCode, passIndex);
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = { .data = shaderCode.c_str(), .length = shaderCode.size() };
    
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.getDevice(), &shaderDesc);
    
    if (!shaderModule) {
        return Err<void>("Failed to create buffer pass shader module");
    }
    
    // Pipeline layout
    WGPUBindGroupLayout layouts[2] = { globalBGL, _bufferBindGroupLayout };
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 2;
    plDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.getDevice(), &plDesc);
    
    // Create bind group
    std::array<WGPUBindGroupEntry, 6> bgEntries = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = _uniformBuffer;
    bgEntries[0].size = 48;
    
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = _bufferSampler;
    
    // Bind previous frame textures as iChannel inputs
    // iChannel0 = self (previous frame), others are other buffers
    for (int i = 0; i < 4; ++i) {
        bgEntries[2 + i].binding = 2 + i;
        // For buffer A, iChannel0 is its own previous frame
        // In a real implementation, this would be configurable
        int srcBuffer = (i == 0) ? passIndex : i - 1;
        if (srcBuffer >= 0 && srcBuffer < kMaxBufferPasses && 
            _bufferPasses[srcBuffer].texturePrevView) {
            bgEntries[2 + i].textureView = _bufferPasses[srcBuffer].texturePrevView;
        } else if (_bufferPasses[passIndex].texturePrevView) {
            // Fallback to own previous frame
            bgEntries[2 + i].textureView = _bufferPasses[passIndex].texturePrevView;
        }
    }
    
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _bufferBindGroupLayout;
    bgDesc.entryCount = bgEntries.size();
    bgDesc.entries = bgEntries.data();
    pass.bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    
    // Create pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");
    
    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = WGPU_STR("fs_main");
    
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;  // Buffer format
    colorTarget.writeMask = WGPUColorWriteMask_All;
    
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;
    
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    
    pass.pipeline = wgpuDeviceCreateRenderPipeline(ctx.getDevice(), &pipelineDesc);
    
    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);
    
    if (!pass.pipeline) {
        return Err<void>("Failed to create buffer pass pipeline");
    }
    
    ydebug("Shader: compiled buffer pass {}", passIndex);
    return Ok();
}

void Shader::swapBufferTextures() {
    for (auto& pass : _bufferPasses) {
        if (!pass.enabled) continue;
        std::swap(pass.texture, pass.texturePrev);
        std::swap(pass.textureView, pass.texturePrevView);
    }
}

void Shader::renderBufferPasses(WebGPUContext& ctx) {
    // Get global bind group - standalone mode
    WGPUBindGroup globalBindGroup = _globalBindGroup;
    if (!globalBindGroup) return;
    if (!_bufferBindGroupLayout || !_bufferSampler) return;
    
    // First swap so texturePrev has last frame's result, texture is render target
    swapBufferTextures();
    
    for (int i = 0; i < kMaxBufferPasses; ++i) {
        auto& pass = _bufferPasses[i];
        if (!pass.enabled || !pass.pipeline || !pass.textureView || !pass.texturePrevView) continue;
        
        // Recreate bind group each frame with correct texture references
        // (after swap: texturePrevView = what to sample, textureView = render target)
        if (pass.bindGroup) {
            wgpuBindGroupRelease(pass.bindGroup);
            pass.bindGroup = nullptr;
        }
        
        std::array<WGPUBindGroupEntry, 6> bgEntries = {};
        bgEntries[0].binding = 0;
        bgEntries[0].buffer = _uniformBuffer;
        bgEntries[0].size = 48;
        
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = _bufferSampler;
        
        // iChannel0 = own previous frame, iChannel1-3 = other buffers' previous frames
        for (int j = 0; j < 4; ++j) {
            bgEntries[2 + j].binding = 2 + j;
            if (j == 0) {
                // Self - sample from previous frame
                bgEntries[2 + j].textureView = pass.texturePrevView;
            } else {
                // Other buffers
                int srcBuffer = j - 1;
                if (srcBuffer < kMaxBufferPasses && _bufferPasses[srcBuffer].enabled && 
                    _bufferPasses[srcBuffer].texturePrevView) {
                    bgEntries[2 + j].textureView = _bufferPasses[srcBuffer].texturePrevView;
                } else {
                    // Fallback to own previous frame
                    bgEntries[2 + j].textureView = pass.texturePrevView;
                }
            }
        }
        
        WGPUBindGroupDescriptor bgDesc = {};
        bgDesc.layout = _bufferBindGroupLayout;
        bgDesc.entryCount = bgEntries.size();
        bgDesc.entries = bgEntries.data();
        pass.bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
        if (!pass.bindGroup) continue;
        
        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
        if (!encoder) continue;
        
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = pass.textureView;  // Render to current frame texture
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        
        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        
        WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        if (!renderPass) {
            wgpuCommandEncoderRelease(encoder);
            continue;
        }
        
        wgpuRenderPassEncoderSetPipeline(renderPass, pass.pipeline);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 0, globalBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 1, pass.bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(renderPass);
        wgpuRenderPassEncoderRelease(renderPass);
        
        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        if (cmdBuffer) {
            wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
            wgpuCommandBufferRelease(cmdBuffer);
        }
        wgpuCommandEncoderRelease(encoder);
    }
    
    // Update main Image pass bind group to sample from buffer results
    if (_bindGroup) {
        wgpuBindGroupRelease(_bindGroup);
        _bindGroup = nullptr;
    }
    
    std::array<WGPUBindGroupEntry, 6> bgEntries = {};
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = _uniformBuffer;
    bgEntries[0].size = 48;
    
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = _bufferSampler;
    
    // For Image pass: iChannel0-3 = buffer results (current frame, just rendered)
    for (int i = 0; i < 4; ++i) {
        bgEntries[2 + i].binding = 2 + i;
        if (i < kMaxBufferPasses && _bufferPasses[i].enabled && _bufferPasses[i].textureView) {
            bgEntries[2 + i].textureView = _bufferPasses[i].textureView;  // Current frame result
        } else if (_bufferPasses[0].textureView) {
            bgEntries[2 + i].textureView = _bufferPasses[0].textureView;
        }
    }
    
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _bufferBindGroupLayout;
    bgDesc.entryCount = bgEntries.size();
    bgDesc.entries = bgEntries.data();
    _bindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
}

//-----------------------------------------------------------------------------
// Channel texture support (iChannel0-3)
//-----------------------------------------------------------------------------

void Shader::parsePluginArgs() {
    // Parse args like: --channel0=/path/to/image.png --channel1=/path/to/other.jpg
    // Format: --channel<N>=<path> or -c<N>=<path>
    
    yinfo("Shader: parsePluginArgs input: '{}'", _pluginArgs);
    
    if (_pluginArgs.empty()) {
        yinfo("Shader: no plugin args to parse");
        return;
    }
    
    std::regex channelRegex(R"(--channel(\d)=(\S+))");
    std::sregex_iterator it(_pluginArgs.begin(), _pluginArgs.end(), channelRegex);
    std::sregex_iterator end;
    
    while (it != end) {
        int channelNum = std::stoi((*it)[1].str());
        std::string path = (*it)[2].str();
        
        if (channelNum >= 0 && channelNum < kMaxChannels) {
            _channels[channelNum].path = path;
            _hasChannelTextures = true;  // Set early so correct wrapper is used
            yinfo("Shader: parsed channel {} = {}", channelNum, path);
        }
        ++it;
    }
}

Result<void> Shader::createChannelSampler(WebGPUContext& ctx) {
    if (_channelSampler) return Ok();
    
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    
    _channelSampler = wgpuDeviceCreateSampler(ctx.getDevice(), &samplerDesc);
    if (!_channelSampler) {
        return Err<void>("Failed to create channel sampler");
    }
    return Ok();
}

Result<void> Shader::loadChannelTexture(WebGPUContext& ctx, int channel, const std::string& path) {
    if (channel < 0 || channel >= kMaxChannels) {
        return Err<void>("Invalid channel number");
    }
    
    auto& ch = _channels[channel];
    
    // Load image with stb_image
    int width, height, _channelsin_file;
    ch.imageData = stbi_load(path.c_str(), &width, &height, &_channelsin_file, 4);
    
    if (!ch.imageData) {
        return Err<void>(std::string("Failed to load texture: ") + stbi_failure_reason());
    }
    
    ch.width = width;
    ch.height = height;
    
    // Create WebGPU texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    
    ch.texture = wgpuDeviceCreateTexture(ctx.getDevice(), &texDesc);
    if (!ch.texture) {
        stbi_image_free(ch.imageData);
        ch.imageData = nullptr;
        return Err<void>("Failed to create channel texture");
    }
    
    // Upload texture data
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = ch.texture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = width * 4;
    layout.rowsPerImage = height;
    WGPUExtent3D extent = {(uint32_t)width, (uint32_t)height, 1};
    wgpuQueueWriteTexture(ctx.getQueue(), &dst, ch.imageData, width * height * 4, &layout, &extent);
    
    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    
    ch.textureView = wgpuTextureCreateView(ch.texture, &viewDesc);
    if (!ch.textureView) {
        wgpuTextureRelease(ch.texture);
        ch.texture = nullptr;
        stbi_image_free(ch.imageData);
        ch.imageData = nullptr;
        return Err<void>("Failed to create channel texture view");
    }
    
    ch.loaded = true;
    _hasChannelTextures = true;
    
    yinfo("Shader: loaded iChannel{} from {} ({}x{})", channel, path, width, height);
    return Ok();
}

Result<void> Shader::createPlaceholderTexture(WebGPUContext& ctx) {
    if (_placeholderTexture) return Ok();
    
    // Create 1x1 magenta placeholder texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size = { 1, 1, 1 };
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    
    _placeholderTexture = wgpuDeviceCreateTexture(ctx.getDevice(), &texDesc);
    if (!_placeholderTexture) {
        return Err<void>("Failed to create placeholder texture");
    }
    
    // Upload magenta pixel
    uint8_t pixel[4] = { 255, 0, 255, 255 };
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = _placeholderTexture;
    dst.mipLevel = 0;
    dst.origin = { 0, 0, 0 };
    dst.aspect = WGPUTextureAspect_All;
    
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = 4;
    layout.rowsPerImage = 1;
    
    WGPUExtent3D size = { 1, 1, 1 };
    wgpuQueueWriteTexture(ctx.getQueue(), &dst, pixel, 4, &layout, &size);
    
    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    
    _placeholderTextureView = wgpuTextureCreateView(_placeholderTexture, &viewDesc);
    if (!_placeholderTextureView) {
        wgpuTextureRelease(_placeholderTexture);
        _placeholderTexture = nullptr;
        return Err<void>("Failed to create placeholder texture view");
    }
    
    return Ok();
}

Result<void> Shader::createStandaloneGlobalBindGroup(WebGPUContext& ctx) {
    if (_globalBindGroup) return Ok();
    
    // Create global uniform buffer (matches ShaderManager::GlobalUniforms)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 48;  // iTime, iTimeRelative, iTimeDelta, iFrame, iMouse, iScreenResolution, pad
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    bufDesc.mappedAtCreation = false;
    
    _globalUniformBuffer = wgpuDeviceCreateBuffer(ctx.getDevice(), &bufDesc);
    if (!_globalUniformBuffer) {
        return Err<void>("Failed to create global uniform buffer");
    }
    
    // Create bind group layout
    WGPUBindGroupLayoutEntry entry = {};
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    
    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 1;
    bglDesc.entries = &entry;
    
    _globalBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.getDevice(), &bglDesc);
    if (!_globalBindGroupLayout) {
        wgpuBufferRelease(_globalUniformBuffer);
        _globalUniformBuffer = nullptr;
        return Err<void>("Failed to create global bind group layout");
    }
    
    // Create bind group
    WGPUBindGroupEntry bgEntry = {};
    bgEntry.binding = 0;
    bgEntry.buffer = _globalUniformBuffer;
    bgEntry.size = 48;
    
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = _globalBindGroupLayout;
    bgDesc.entryCount = 1;
    bgDesc.entries = &bgEntry;
    
    _globalBindGroup = wgpuDeviceCreateBindGroup(ctx.getDevice(), &bgDesc);
    if (!_globalBindGroup) {
        wgpuBindGroupLayoutRelease(_globalBindGroupLayout);
        _globalBindGroupLayout = nullptr;
        wgpuBufferRelease(_globalUniformBuffer);
        _globalUniformBuffer = nullptr;
        return Err<void>("Failed to create global bind group");
    }
    
    return Ok();
}

WGPUBindGroup Shader::getGlobalBindGroup() {
    if (_globalBindGroup) return _globalBindGroup;
    // Factory mode - defer to ShaderManager (but don't call getShaderManager to avoid symbol dep)
    // In standalone mode, _globalBindGroup should already be created
    return nullptr;
}

WGPUBindGroupLayout Shader::getGlobalBindGroupLayout() {
    if (_globalBindGroupLayout) return _globalBindGroupLayout;
    return nullptr;
}

} // namespace yetty

extern "C" {
    const char* name() { return "shader"; }
    yetty::Result<yetty::PluginPtr> create() {
        return yetty::ShaderPlugin::create();
    }
}
