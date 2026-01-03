#include <yetty/yetty-command.h>
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include "grid-renderer.h"
#include <spdlog/spdlog.h>

namespace yetty {

//=============================================================================
// Resource Upload Commands
//=============================================================================

bool UploadShaderCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();

    // Check if already exists
    if (res.shaders.find(name_) != res.shaders.end()) {
        spdlog::debug("Shader '{}' already exists, skipping upload", name_);
        return true;
    }

    WGPUDevice device = ctx.getDevice();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    WGPU_SHADER_CODE(wgslDesc, wgslSource_);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    moduleDesc.label = WGPU_STR(name_.c_str());

    ShaderResource shader;
    shader.module = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    shader.vertexEntry = vertexEntry_;
    shader.fragmentEntry = fragmentEntry_;

    if (!shader.module) {
        spdlog::error("Failed to create shader module '{}'", name_);
        return false;
    }

    res.shaders[name_] = shader;
    spdlog::debug("Uploaded shader '{}'", name_);
    return true;
}

bool UploadTextureCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();
    WGPUDevice device = ctx.getDevice();
    WGPUQueue queue = ctx.getQueue();

    // Check if texture exists - if so, update data
    auto it = res.textures.find(name_);
    if (it != res.textures.end()) {
        auto& tex = it->second;

        // If size changed, need to recreate
        if (tex.width != width_ || tex.height != height_) {
            if (tex.view) wgpuTextureViewRelease(tex.view);
            if (tex.texture) wgpuTextureRelease(tex.texture);
            res.textures.erase(it);
        } else {
            // Same size - just update data
            WGPUTexelCopyTextureInfo dest = {};
            dest.texture = tex.texture;
            dest.mipLevel = 0;
            dest.origin = {0, 0, 0};
            dest.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferLayout layout = {};
            layout.offset = 0;
            layout.bytesPerRow = width_ * 4;  // Assume RGBA8
            layout.rowsPerImage = height_;

            WGPUExtent3D size = {width_, height_, 1};
            wgpuQueueWriteTexture(queue, &dest, data_.data(), data_.size(), &layout, &size);
            return true;
        }
    }

    // Create new texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = WGPU_STR(name_.c_str());
    texDesc.size = {width_, height_, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = format_;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    TextureResource tex;
    tex.texture = wgpuDeviceCreateTexture(device, &texDesc);
    tex.width = width_;
    tex.height = height_;
    tex.format = format_;

    if (!tex.texture) {
        spdlog::error("Failed to create texture '{}'", name_);
        return false;
    }

    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = format_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    tex.view = wgpuTextureCreateView(tex.texture, &viewDesc);

    // Upload data
    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = tex.texture;
    dest.mipLevel = 0;
    dest.origin = {0, 0, 0};
    dest.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = width_ * 4;  // Assume RGBA8
    layout.rowsPerImage = height_;

    WGPUExtent3D size = {width_, height_, 1};
    wgpuQueueWriteTexture(queue, &dest, data_.data(), data_.size(), &layout, &size);

    res.textures[name_] = tex;
    spdlog::debug("Uploaded texture '{}' ({}x{})", name_, width_, height_);
    return true;
}

bool UploadBufferCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();
    WGPUDevice device = ctx.getDevice();
    WGPUQueue queue = ctx.getQueue();

    // Check if buffer exists
    auto it = res.buffers.find(name_);
    if (it != res.buffers.end()) {
        auto& buf = it->second;

        // If size changed, recreate
        if (buf.size != data_.size()) {
            wgpuBufferRelease(buf.buffer);
            res.buffers.erase(it);
        } else {
            // Same size - just update data
            wgpuQueueWriteBuffer(queue, buf.buffer, 0, data_.data(), data_.size());
            return true;
        }
    }

    // Create new buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.label = WGPU_STR(name_.c_str());
    bufDesc.size = data_.size();
    bufDesc.usage = usage_ | WGPUBufferUsage_CopyDst;

    BufferResource buf;
    buf.buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    buf.size = data_.size();
    buf.usage = usage_;

    if (!buf.buffer) {
        spdlog::error("Failed to create buffer '{}'", name_);
        return false;
    }

    // Upload data
    wgpuQueueWriteBuffer(queue, buf.buffer, 0, data_.data(), data_.size());

    res.buffers[name_] = buf;
    spdlog::debug("Uploaded buffer '{}' ({} bytes)", name_, data_.size());
    return true;
}

//=============================================================================
// Bind Commands
//=============================================================================

bool BindShaderCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();

    auto it = res.shaders.find(name_);
    if (it == res.shaders.end()) {
        spdlog::error("Shader '{}' not found", name_);
        return false;
    }

    auto pass = engine.currentRenderPass();
    if (!pass) {
        spdlog::error("No active render pass for BindShaderCmd");
        return false;
    }

    if (it->second.pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, it->second.pipeline);
    }

    (void)ctx;
    return true;
}

bool BindTextureCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    // Textures are bound via bind groups, not directly
    // This command would update the bind group
    // For now, stub
    (void)ctx;
    (void)engine;
    (void)name_;
    (void)bindingSlot_;
    return true;
}

bool BindBufferCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();

    auto it = res.buffers.find(name_);
    if (it == res.buffers.end()) {
        spdlog::error("Buffer '{}' not found", name_);
        return false;
    }

    auto pass = engine.currentRenderPass();
    if (!pass) {
        spdlog::error("No active render pass for BindBufferCmd");
        return false;
    }

    // Bind as vertex buffer (slot = bindingSlot_)
    wgpuRenderPassEncoderSetVertexBuffer(pass, bindingSlot_, it->second.buffer, 0, WGPU_WHOLE_SIZE);

    (void)ctx;
    return true;
}

bool BindFontCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    // Get font from FontManager using descriptor
    // Font has texture view and metadata buffer
    // Bind them to specified slots
    // TODO: implement when FontManager integration is ready
    (void)ctx;
    (void)engine;
    return true;
}

//=============================================================================
// Render Pass Commands
//=============================================================================

bool BeginRenderPassCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    WGPUDevice device = ctx.getDevice();

    // Get current texture view
    auto textureViewResult = ctx.getCurrentTextureView();
    if (!textureViewResult) {
        spdlog::error("BeginRenderPassCmd: failed to get texture view");
        return false;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
    engine.setCurrentEncoder(encoder);

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = *textureViewResult;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
#if YETTY_WEB
    WGPU_COLOR_ATTACHMENT_CLEAR(colorAttachment, clearR_, clearG_, clearB_, clearA_);
#else
    colorAttachment.clearValue = {clearR_, clearG_, clearB_, clearA_};
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
#endif

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    engine.setCurrentRenderPass(pass);

    return true;
}

bool DrawCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto pass = engine.currentRenderPass();
    if (!pass) {
        spdlog::error("No active render pass for DrawCmd");
        return false;
    }

    wgpuRenderPassEncoderDraw(pass, vertexCount_, instanceCount_, firstVertex_, firstInstance_);

    (void)ctx;
    return true;
}

bool EndRenderPassCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto pass = engine.currentRenderPass();
    auto encoder = engine.currentEncoder();

    if (!pass || !encoder) {
        spdlog::error("No active render pass/encoder for EndRenderPassCmd");
        return false;
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    engine.setCurrentRenderPass(nullptr);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuCommandEncoderRelease(encoder);
    engine.setCurrentEncoder(nullptr);

    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);

    return true;
}

//=============================================================================
// Resource Deletion Commands
//=============================================================================

bool DeleteShaderCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();

    auto it = res.shaders.find(name_);
    if (it == res.shaders.end()) {
        return true;  // Already deleted
    }

    auto& shader = it->second;
    if (shader.pipeline) wgpuRenderPipelineRelease(shader.pipeline);
    if (shader.pipelineLayout) wgpuPipelineLayoutRelease(shader.pipelineLayout);
    if (shader.bindGroupLayout) wgpuBindGroupLayoutRelease(shader.bindGroupLayout);
    if (shader.module) wgpuShaderModuleRelease(shader.module);

    res.shaders.erase(it);
    spdlog::debug("Deleted shader '{}'", name_);

    (void)ctx;
    return true;
}

bool DeleteTextureCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();

    auto it = res.textures.find(name_);
    if (it == res.textures.end()) {
        return true;
    }

    auto& tex = it->second;
    if (tex.sampler) wgpuSamplerRelease(tex.sampler);
    if (tex.view) wgpuTextureViewRelease(tex.view);
    if (tex.texture) wgpuTextureRelease(tex.texture);

    res.textures.erase(it);
    spdlog::debug("Deleted texture '{}'", name_);

    (void)ctx;
    return true;
}

bool DeleteBufferCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto& res = engine.currentResources();

    auto it = res.buffers.find(name_);
    if (it == res.buffers.end()) {
        return true;
    }

    wgpuBufferRelease(it->second.buffer);
    res.buffers.erase(it);
    spdlog::debug("Deleted buffer '{}'", name_);

    (void)ctx;
    return true;
}

//=============================================================================
// Grid Render Command
//=============================================================================

bool RenderGridCmd::execute(WebGPUContext& ctx, Yetty& engine) {
    auto renderer = engine.renderer();
    if (!renderer) {
        spdlog::error("RenderGridCmd: no renderer available");
        return false;
    }

    const auto& buf = buffers_;
    renderer->renderFromBuffers(
        buf.cols, buf.rows,
        buf.glyphs.data(),
        buf.fgColors.data(),
        buf.bgColors.data(),
        buf.attrs.data(),
        cursorCol_, cursorRow_, cursorVisible_
    );

    (void)ctx;
    return true;
}

} // namespace yetty
