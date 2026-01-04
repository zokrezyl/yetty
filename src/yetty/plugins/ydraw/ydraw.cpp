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
    if (auto res = static_cast<YDrawPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init YDrawPlugin", res);
    }
    return Ok(p);
}

Result<void> YDrawPlugin::init() noexcept {
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

Result<PluginLayerPtr> YDrawPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<YDrawLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize YDraw layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

//-----------------------------------------------------------------------------
// YDrawLayer
//-----------------------------------------------------------------------------

YDrawLayer::YDrawLayer() = default;

YDrawLayer::~YDrawLayer() {
    (void)dispose();
}

Result<void> YDrawLayer::init(const std::string& payload) {
    _payload = payload;

    _renderer = std::make_unique<YDrawRenderer>();

    if (!payload.empty()) {
        auto result = _renderer->parse(payload);
        if (!result) {
            return Err<void>("Failed to parse ydraw content", result);
        }
    }

    spdlog::info("YDrawLayer: initialized with {} primitives", _renderer->primitiveCount());
    return Ok();
}

Result<void> YDrawLayer::dispose() {
    if (_renderer) {
        _renderer->dispose();
        _renderer.reset();
    }
    return Ok();
}

bool YDrawLayer::onMouseMove(float localX, float localY) {
    (void)localX; (void)localY;
    return true;
}

bool YDrawLayer::onMouseButton(int button, bool pressed) {
    (void)button; (void)pressed;
    return true;
}

bool YDrawLayer::onKey(int key, int scancode, int action, int mods) {
    (void)key; (void)scancode; (void)action; (void)mods;
    return true;
}

bool YDrawLayer::onChar(unsigned int codepoint) {
    (void)codepoint;
    return true;
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

Result<void> YDrawLayer::render(WebGPUContext& ctx) {
    if (_failed) return Err<void>("YDrawLayer already failed");
    if (!_visible) return Ok();
    if (!_renderer || _renderer->primitiveCount() == 0) return Ok();

    const auto& rc = _render_context;

    // Calculate pixel position from cell position
    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    // For Relative layers, adjust position when viewing scrollback
    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
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
        return Err<void>("YDrawLayer: Failed to create command encoder");
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
        return Err<void>("YDrawLayer: Failed to begin render pass");
    }

    // Render using core YDrawRenderer
    auto result = _renderer->render(ctx, pass, pixelX, pixelY, pixelW, pixelH,
                                     rc.screenWidth, rc.screenHeight, rc.targetFormat);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("YDrawLayer: Failed to finish command encoder");
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    if (!result) {
        _failed = true;
        return Err<void>("YDrawLayer: render failed", result);
    }

    return Ok();
}

bool YDrawLayer::renderToPass(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    if (_failed || !_visible || !_renderer) return false;

    const auto& rc = _render_context;

    float pixelX = _x * rc.cellWidth;
    float pixelY = _y * rc.cellHeight;
    float pixelW = _width_cells * rc.cellWidth;
    float pixelH = _height_cells * rc.cellHeight;

    if (_position_mode == PositionMode::Relative && rc.scrollOffset > 0) {
        pixelY += rc.scrollOffset * rc.cellHeight;
    }

    auto result = _renderer->render(ctx, pass, pixelX, pixelY, pixelW, pixelH,
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
