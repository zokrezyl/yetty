#include "ydraw.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>

namespace yetty {

//-----------------------------------------------------------------------------
// YDrawPlugin
//-----------------------------------------------------------------------------

YDrawPlugin::~YDrawPlugin() {
    (void)dispose();
}

Result<PluginPtr> YDrawPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new YDrawPlugin(std::move(engine)));
    if (auto res = static_cast<YDrawPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init YDrawPlugin", res);
    }
    return Ok(p);
}

Result<void> YDrawPlugin::pluginInit() noexcept {
    initialized_ = true;
    return Ok();
}

Result<void> YDrawPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose YDrawPlugin", res);
    }
    initialized_ = false;
    return Ok();
}

Result<WidgetPtr> YDrawPlugin::createWidget(const std::string& payload) {
    return YDrawW::create(payload);
}

//-----------------------------------------------------------------------------
// YDrawW
//-----------------------------------------------------------------------------

YDrawW::~YDrawW() {
    (void)dispose();
}

Result<void> YDrawW::init() {
    renderer_ = std::make_unique<YDrawRenderer>();

    if (!payload_.empty()) {
        auto result = renderer_->parse(payload_);
        if (!result) {
            return Err<void>("Failed to parse ydraw content", result);
        }
    }

    spdlog::info("YDrawW: initialized with {} primitives", renderer_->primitiveCount());
    return Ok();
}

Result<void> YDrawW::dispose() {
    if (renderer_) {
        renderer_->dispose();
        renderer_.reset();
    }
    return Ok();
}

bool YDrawW::onMouseMove(float localX, float localY) {
    (void)localX; (void)localY;
    return true;
}

bool YDrawW::onMouseButton(int button, bool pressed) {
    (void)button; (void)pressed;
    return true;
}

bool YDrawW::onKey(int key, int scancode, int action, int mods) {
    (void)key; (void)scancode; (void)action; (void)mods;
    return true;
}

bool YDrawW::onChar(unsigned int codepoint) {
    (void)codepoint;
    return true;
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

Result<void> YDrawW::render(WebGPUContext& ctx) {
    if (failed_) return Err<void>("YDrawW already failed");
    if (!visible_) return Ok();
    if (!renderer_ || renderer_->primitiveCount() == 0) return Ok();

    const auto& rc = renderCtx_;

    // Calculate pixel position from cell position
    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    // For Relative layers, adjust position when viewing scrollback
    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return Ok();
        }
    }

    // Create render pass
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        return Err<void>("YDrawW: Failed to create command encoder");
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
        return Err<void>("YDrawW: Failed to begin render pass");
    }

    // Render using core YDrawRenderer
    auto result = renderer_->render(ctx, pass, pixelX, pixelY, pixelW, pixelH,
                                     rc.screenWidth, rc.screenHeight, rc.targetFormat);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("YDrawW: Failed to finish command encoder");
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    if (!result) {
        failed_ = true;
        return Err<void>("YDrawW: render failed", result);
    }

    return Ok();
}

bool YDrawW::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (failed_ || !visible_ || !renderer_) return false;

    const auto& rc = renderCtx_;

    float pixelX = x_ * rc.cellWidth;
    float pixelY = y_ * rc.cellHeight;
    float pixelW = widthCells_ * rc.cellWidth;
    float pixelH = heightCells_ * rc.cellHeight;

    if (positionMode_ == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    auto result = renderer_->render(ctx, pass, pixelX, pixelY, pixelW, pixelH,
                                     rc.screenWidth, rc.screenHeight, rc.targetFormat);
    return result.has_value();
}

} // namespace yetty

extern "C" {
    const char* name() { return "ydraw"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) {
        return yetty::YDrawPlugin::create(std::move(engine));
    }
}
