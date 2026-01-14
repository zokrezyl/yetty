#include "ydraw.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>

namespace yetty {

//-----------------------------------------------------------------------------
// YDrawPlugin
//-----------------------------------------------------------------------------

YDrawPlugin::~YDrawPlugin() {
    (void)dispose();
}

Result<PluginPtr> YDrawPlugin::create() noexcept {
    auto p = PluginPtr(new YDrawPlugin());
    if (auto res = static_cast<YDrawPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init YDrawPlugin", res);
    }
    return Ok(p);
}

Result<void> YDrawPlugin::pluginInit() noexcept {
    _initialized = true;
    return Ok();
}

Result<void> YDrawPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose YDrawPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<WidgetPtr> YDrawPlugin::createWidget(
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
    yinfo("payload size={} x={} y={} w={} h={}", payload.size(), x, y, widthCells, heightCells);
    return YDrawW::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload);
}

//-----------------------------------------------------------------------------
// YDrawW
//-----------------------------------------------------------------------------

YDrawW::~YDrawW() {
    (void)dispose();
}

Result<void> YDrawW::init() {
    renderer_ = std::make_unique<YDrawRenderer>();

    if (!_payload.empty()) {
        auto result = renderer_->parse(_payload);
        if (!result) {
            return Err<void>("Failed to parse ydraw content", result);
        }
    }

    yinfo("YDrawW: initialized with {} primitives", renderer_->primitiveCount());
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

void YDrawW::prepareFrame(WebGPUContext& ctx) {
    if (failed_ || !_visible) return;
    if (!renderer_ || renderer_->primitiveCount() == 0) return;

    const auto& rc = _renderCtx;

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    // For Relative layers, adjust position when viewing scrollback
    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    // Skip if off-screen
    if (rc.termRows > 0) {
        float screenPixelHeight = rc.termRows * rc.cellHeight;
        if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
            return;
        }
    }

    // Create render pass
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        yerror("YDrawW: Failed to create command encoder");
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
        yerror("YDrawW: Failed to begin render pass");
        return;
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
        yerror("YDrawW: Failed to finish command encoder");
        return;
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    if (!result) {
        failed_ = true;
        yerror("YDrawW: render failed: {}", result.error().message());
    }
}

Result<void> YDrawW::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (failed_ || !_visible || !renderer_) return Ok();

    const auto& rc = _renderCtx;

    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _widthCells * rc.cellWidth;
    float pixelH = _heightCells * rc.cellHeight;

    if (_positionMode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    return renderer_->render(ctx, pass, pixelX, pixelY, pixelW, pixelH,
                             rc.screenWidth, rc.screenHeight, rc.targetFormat);
}

} // namespace yetty

extern "C" {
    const char* name() { return "ydraw"; }
    yetty::Result<yetty::PluginPtr> create() {
        return yetty::YDrawPlugin::create();
    }
}
