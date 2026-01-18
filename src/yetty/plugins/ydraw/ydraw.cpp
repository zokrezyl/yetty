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
    return YDraw::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload);
}

//-----------------------------------------------------------------------------
// YDraw
//-----------------------------------------------------------------------------

YDraw::~YDraw() {
    (void)dispose();
}

Result<void> YDraw::init() {
    renderer_ = std::make_unique<YDrawRenderer>();

    if (!_payload.empty()) {
        auto result = renderer_->parse(_payload);
        if (!result) {
            return Err<void>("Failed to parse ydraw content", result);
        }
    }

    yinfo("YDraw: initialized with {} primitives", renderer_->primitiveCount());
    return Ok();
}

Result<void> YDraw::dispose() {
    if (renderer_) {
        renderer_->dispose();
        renderer_.reset();
    }
    return Ok();
}

bool YDraw::onMouseMove(float localX, float localY) {
    (void)localX; (void)localY;
    return true;
}

bool YDraw::onMouseButton(int button, bool pressed) {
    (void)button; (void)pressed;
    return true;
}

bool YDraw::onKey(int key, int scancode, int action, int mods) {
    (void)key; (void)scancode; (void)action; (void)mods;
    return true;
}

bool YDraw::onChar(unsigned int codepoint) {
    (void)codepoint;
    return true;
}

//-----------------------------------------------------------------------------
// Rendering
//-----------------------------------------------------------------------------

void YDraw::prepareFrame(WebGPUContext& ctx, bool on) {
    if (!on || failed_ || !_visible) return;
    if (!renderer_ || renderer_->primitiveCount() == 0) return;

    // Create render pass
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        yerror("YDraw: Failed to create command encoder");
        return;
    }

    auto viewResult = ctx.getCurrentTextureView();
    if (!viewResult) {
        wgpuCommandEncoderRelease(encoder);
        return;
    }
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = *viewResult;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        yerror("YDraw: Failed to begin render pass");
        return;
    }

    // Render using core YDrawRenderer
    yinfo("YDraw::prepareFrame rendering at pixel=({},{}) size={}x{} screen={}x{}",
          _pixelX, _pixelY, _pixelWidth, _pixelHeight, ctx.getSurfaceWidth(), ctx.getSurfaceHeight());
    auto result = renderer_->render(ctx, pass,
                                    static_cast<float>(_pixelX), static_cast<float>(_pixelY),
                                    static_cast<float>(_pixelWidth), static_cast<float>(_pixelHeight),
                                    ctx.getSurfaceWidth(), ctx.getSurfaceHeight(), ctx.getSurfaceFormat());

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (!cmdBuffer) {
        wgpuCommandEncoderRelease(encoder);
        yerror("YDraw: Failed to finish command encoder");
        return;
    }
    wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    if (!result) {
        failed_ = true;
        yerror("YDraw: render failed: {}", result.error().message());
    }
}

Result<void> YDraw::render(WGPURenderPassEncoder pass, WebGPUContext& ctx, bool on) {
    if (!on || failed_ || !_visible || !renderer_) return Ok();

    return renderer_->render(ctx, pass,
                             static_cast<float>(_pixelX), static_cast<float>(_pixelY),
                             static_cast<float>(_pixelWidth), static_cast<float>(_pixelHeight),
                             ctx.getSurfaceWidth(), ctx.getSurfaceHeight(), ctx.getSurfaceFormat());
}

} // namespace yetty

extern "C" {
    const char* name() { return "ydraw"; }
    yetty::Result<yetty::PluginPtr> create() {
        return yetty::YDrawPlugin::create();
    }
}
