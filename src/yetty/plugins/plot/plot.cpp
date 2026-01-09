#include "plot.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace yetty {

//-----------------------------------------------------------------------------
// Default plot colors (distinguishable palette)
//-----------------------------------------------------------------------------
static const float DEFAULT_COLORS[16 * 4] = {
    0.2f, 0.6f, 1.0f, 1.0f,  // Blue
    1.0f, 0.4f, 0.4f, 1.0f,  // Red
    0.4f, 0.9f, 0.4f, 1.0f,  // Green
    1.0f, 0.8f, 0.2f, 1.0f,  // Yellow
    0.8f, 0.4f, 1.0f, 1.0f,  // Purple
    0.2f, 0.9f, 0.9f, 1.0f,  // Cyan
    1.0f, 0.6f, 0.2f, 1.0f,  // Orange
    0.9f, 0.5f, 0.7f, 1.0f,  // Pink
    0.6f, 0.8f, 0.2f, 1.0f,  // Lime
    0.4f, 0.4f, 0.8f, 1.0f,  // Indigo
    0.8f, 0.6f, 0.4f, 1.0f,  // Brown
    0.5f, 0.9f, 0.7f, 1.0f,  // Mint
    0.9f, 0.3f, 0.6f, 1.0f,  // Magenta
    0.3f, 0.7f, 0.5f, 1.0f,  // Teal
    0.7f, 0.7f, 0.3f, 1.0f,  // Olive
    0.6f, 0.6f, 0.6f, 1.0f,  // Gray
};

//-----------------------------------------------------------------------------
// PlotPlugin
//-----------------------------------------------------------------------------

PlotPlugin::~PlotPlugin() {
    (void)dispose();
}

Result<PluginPtr> PlotPlugin::create(YettyPtr engine) noexcept {
    auto p = PluginPtr(new PlotPlugin(std::move(engine)));
    if (auto res = static_cast<PlotPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init PlotPlugin", res);
    }
    return Ok(p);
}

Result<void> PlotPlugin::pluginInit() noexcept {
    initialized_ = true;
    return Ok();
}

Result<void> PlotPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PlotPlugin", res);
    }
    initialized_ = false;
    return Ok();
}

Result<WidgetPtr> PlotPlugin::createWidget(const std::string& payload) {
    return PlotW::create(payload);
}

Result<void> PlotPlugin::renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                    uint32_t screenWidth, uint32_t screenHeight,
                                    float cellWidth, float cellHeight,
                                    int scrollOffset, uint32_t termRows,
                                    bool isAltScreen) {
    if (!engine_) return Err<void>("PlotPlugin::renderAll: no engine");

    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : widgets_) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<PlotW>(layerBase);

        float pixelX = layer->getX() * cellWidth;
        float pixelY = layer->getY() * cellHeight;
        float pixelW = layer->getWidthCells() * cellWidth;
        float pixelH = layer->getHeightCells() * cellHeight;

        if (layer->getPositionMode() == PositionMode::Relative && scrollOffset > 0) {
            pixelY += scrollOffset * cellHeight;
        }

        if (termRows > 0) {
            float screenPixelHeight = termRows * cellHeight;
            if (pixelY + pixelH <= 0 || pixelY >= screenPixelHeight) {
                continue;
            }
        }

        if (auto res = layer->render(*engine_->context(), targetView, targetFormat,
                                      screenWidth, screenHeight,
                                      pixelX, pixelY, pixelW, pixelH); !res) {
            return Err<void>("Failed to render PlotW layer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// PlotW
//-----------------------------------------------------------------------------

PlotW::~PlotW() {
    (void)dispose();
}

Result<void> PlotW::init() {
    std::memcpy(colors_, DEFAULT_COLORS, sizeof(colors_));

    if (payload_.empty()) {
        return Ok();
    }

    // Binary format: [uint32 N][uint32 M][float xmin][float xmax][float ymin][float ymax][N*M floats]
    // Header size: 8 + 16 = 24 bytes
    constexpr size_t HEADER_SIZE = 24;

    if (payload_.size() >= HEADER_SIZE) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload_.data());

        uint32_t n, m;
        float xmin, xmax, ymin, ymax;
        std::memcpy(&n, data + 0, sizeof(uint32_t));
        std::memcpy(&m, data + 4, sizeof(uint32_t));
        std::memcpy(&xmin, data + 8, sizeof(float));
        std::memcpy(&xmax, data + 12, sizeof(float));
        std::memcpy(&ymin, data + 16, sizeof(float));
        std::memcpy(&ymax, data + 20, sizeof(float));

        size_t expected_size = HEADER_SIZE + n * m * sizeof(float);

        // Check if this looks like valid binary data
        if (n > 0 && n <= MAX_PLOTS && m > 0 && m <= 65536 &&
            payload_.size() == expected_size &&
            std::isfinite(xmin) && std::isfinite(xmax) &&
            std::isfinite(ymin) && std::isfinite(ymax)) {

            numPlots_ = n;
            numPoints_ = m;
            data_.resize(numPlots_ * numPoints_);
            std::memcpy(data_.data(), data + HEADER_SIZE, numPlots_ * numPoints_ * sizeof(float));
            setViewport(xmin, xmax, ymin, ymax);
            dataDirty_ = true;

            spdlog::info("PlotW: initialized from binary (N={}, M={}, viewport=[{},{},{},{}])",
                         numPlots_, numPoints_, xmin, xmax, ymin, ymax);
            return Ok();
        }
    }

    // Fallback: text format "N,M" or "N,M,xmin,xmax,ymin,ymax"
    uint32_t n = 0, m = 0;
    float xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    int parsed = sscanf(payload_.c_str(), "%u,%u,%f,%f,%f,%f",
                        &n, &m, &xmin, &xmax, &ymin, &ymax);
    if (parsed >= 2 && n > 0 && m > 0) {
        numPlots_ = std::min(n, MAX_PLOTS);
        numPoints_ = m;
        data_.resize(numPlots_ * numPoints_, 0.0f);
        dataDirty_ = true;
        if (parsed >= 6) {
            setViewport(xmin, xmax, ymin, ymax);
        }
    }

    spdlog::info("PlotW: initialized (N={}, M={})", numPlots_, numPoints_);
    return Ok();
}

Result<void> PlotW::dispose() {
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }
    if (dataTextureView_) { wgpuTextureViewRelease(dataTextureView_); dataTextureView_ = nullptr; }
    if (dataTexture_) { wgpuTextureRelease(dataTexture_); dataTexture_ = nullptr; }
    gpuInitialized_ = false;
    data_.clear();
    return Ok();
}

Result<void> PlotW::update(double deltaTime) {
    (void)deltaTime;
    return Ok();
}

Result<void> PlotW::setData(const float* data, uint32_t numPlots, uint32_t numPoints) {
    if (!data || numPlots == 0 || numPoints == 0) {
        return Err<void>("Invalid plot data");
    }

    numPlots_ = std::min(numPlots, MAX_PLOTS);
    numPoints_ = numPoints;
    data_.resize(numPlots_ * numPoints_);
    std::memcpy(data_.data(), data, numPlots_ * numPoints_ * sizeof(float));
    dataDirty_ = true;

    spdlog::debug("PlotW: data updated (N={}, M={})", numPlots_, numPoints_);
    return Ok();
}

void PlotW::setViewport(float xMin, float xMax, float yMin, float yMax) {
    xMin_ = xMin;
    xMax_ = xMax;
    yMin_ = yMin;
    yMax_ = yMax;
}

void PlotW::setPlotColor(uint32_t plotIndex, float r, float g, float b, float a) {
    if (plotIndex >= MAX_PLOTS) return;
    colors_[plotIndex * 4 + 0] = r;
    colors_[plotIndex * 4 + 1] = g;
    colors_[plotIndex * 4 + 2] = b;
    colors_[plotIndex * 4 + 3] = a;
}

void PlotW::setLineWidth(float width) {
    lineWidth_ = std::max(0.5f, std::min(10.0f, width));
}

void PlotW::setGridEnabled(bool enabled) {
    gridEnabled_ = enabled;
}

bool PlotW::onMouseMove(float localX, float localY) {
    float normX = localX / static_cast<float>(pixelWidth_);
    float normY = localY / static_cast<float>(pixelHeight_);

    if (panning_) {
        float dx = normX - panStartX_;
        float dy = normY - panStartY_;

        float rangeX = viewportStartXMax_ - viewportStartXMin_;
        float rangeY = viewportStartYMax_ - viewportStartYMin_;

        xMin_ = viewportStartXMin_ - dx * rangeX;
        xMax_ = viewportStartXMax_ - dx * rangeX;
        yMin_ = viewportStartYMin_ + dy * rangeY;
        yMax_ = viewportStartYMax_ + dy * rangeY;
    }

    mouseX_ = normX;
    mouseY_ = normY;
    return true;
}

bool PlotW::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        panning_ = pressed;
        if (pressed) {
            panStartX_ = mouseX_;
            panStartY_ = mouseY_;
            viewportStartXMin_ = xMin_;
            viewportStartXMax_ = xMax_;
            viewportStartYMin_ = yMin_;
            viewportStartYMax_ = yMax_;
        }
        return true;
    }
    if (button == -1) {
        panning_ = false;
        return false;
    }
    return false;
}

bool PlotW::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;

    float zoomFactor = 1.0f - yoffset * 0.1f;
    zoomFactor = std::max(0.5f, std::min(2.0f, zoomFactor));

    // Zoom centered on mouse position
    float pivotX = xMin_ + mouseX_ * (xMax_ - xMin_);
    float pivotY = yMin_ + (1.0f - mouseY_) * (yMax_ - yMin_);

    bool ctrlPressed = (mods & 0x0002) != 0;

    if (ctrlPressed) {
        // Zoom only Y axis
        yMin_ = pivotY + (yMin_ - pivotY) * zoomFactor;
        yMax_ = pivotY + (yMax_ - pivotY) * zoomFactor;
    } else {
        // Zoom both axes
        xMin_ = pivotX + (xMin_ - pivotX) * zoomFactor;
        xMax_ = pivotX + (xMax_ - pivotX) * zoomFactor;
        yMin_ = pivotY + (yMin_ - pivotY) * zoomFactor;
        yMax_ = pivotY + (yMax_ - pivotY) * zoomFactor;
    }

    return true;
}

Result<void> PlotW::updateDataTexture(WebGPUContext& ctx) {
    if (data_.empty() || numPlots_ == 0 || numPoints_ == 0) {
        return Ok();
    }

    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = dataTexture_;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = numPoints_ * sizeof(float);
    layout.rowsPerImage = numPlots_;
    WGPUExtent3D extent = {numPoints_, numPlots_, 1};

    wgpuQueueWriteTexture(ctx.getQueue(), &dst, data_.data(),
                          data_.size() * sizeof(float), &layout, &extent);
    dataDirty_ = false;
    return Ok();
}

Result<void> PlotW::render(WebGPUContext& ctx,
                                WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                uint32_t screenWidth, uint32_t screenHeight,
                                float pixelX, float pixelY, float pixelW, float pixelH) {
    if (failed_) return Err<void>("PlotW already failed");
    if (data_.empty()) return Ok();  // Nothing to render

    if (!gpuInitialized_) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            failed_ = true;
            return Err<void>("Failed to create pipeline", result);
        }
        gpuInitialized_ = true;
        dataDirty_ = true;
    }

    if (dataDirty_) {
        auto result = updateDataTexture(ctx);
        if (!result) {
            return Err<void>("Failed to update data texture", result);
        }
    }

    if (!pipeline_ || !uniformBuffer_ || !bindGroup_) {
        failed_ = true;
        return Err<void>("PlotW pipeline not initialized");
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    // Uniform layout (must match shader):
    // rect: vec4<f32>        - 16 bytes
    // viewport: vec4<f32>    - 16 bytes
    // resolution: vec2<f32>  - 8 bytes
    // lineWidth: f32         - 4 bytes
    // gridEnabled: f32       - 4 bytes
    // numPlots: u32          - 4 bytes
    // numPoints: u32         - 4 bytes
    // _pad: vec2<u32>        - 8 bytes
    // colors: array<vec4<f32>, 16> - 256 bytes
    // Total: 320 bytes

    struct Uniforms {
        float rect[4];
        float viewport[4];
        float resolution[2];
        float lineWidth;
        float gridEnabled;
        uint32_t numPlots;
        uint32_t numPoints;
        uint32_t _pad[2];
        float colors[16 * 4];
    } uniforms;

    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;
    uniforms.viewport[0] = xMin_;
    uniforms.viewport[1] = xMax_;
    uniforms.viewport[2] = yMin_;
    uniforms.viewport[3] = yMax_;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.lineWidth = lineWidth_;
    uniforms.gridEnabled = gridEnabled_ ? 1.0f : 0.0f;
    uniforms.numPlots = numPlots_;
    uniforms.numPoints = numPoints_;
    uniforms._pad[0] = 0;
    uniforms._pad[1] = 0;
    std::memcpy(uniforms.colors, colors_, sizeof(colors_));

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    // Create command encoder and render
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) return Err<void>("Failed to create command encoder");

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = targetView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        wgpuCommandEncoderRelease(encoder);
        return Err<void>("Failed to begin render pass");
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    if (cmdBuffer) {
        wgpuQueueSubmit(ctx.getQueue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
    }
    wgpuCommandEncoderRelease(encoder);
    return Ok();
}

Result<void> PlotW::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create data texture (NxM, R32Float)
    uint32_t texWidth = std::max(numPoints_, 1u);
    uint32_t texHeight = std::max(numPlots_, 1u);

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = texWidth;
    texDesc.size.height = texHeight;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_R32Float;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    dataTexture_ = wgpuDeviceCreateTexture(device, &texDesc);
    if (!dataTexture_) return Err<void>("Failed to create data texture");

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_R32Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    dataTextureView_ = wgpuTextureCreateView(dataTexture_, &viewDesc);
    if (!dataTextureView_) return Err<void>("Failed to create texture view");

    // Sampler for data texture (nearest - R32Float is non-filterable)
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler_) return Err<void>("Failed to create sampler");

    // Uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 320;  // sizeof(Uniforms)
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

    // Shader code
    const char* shaderCode = R"(
struct Uniforms {
    rect: vec4<f32>,
    viewport: vec4<f32>,
    resolution: vec2<f32>,
    lineWidth: f32,
    gridEnabled: f32,
    numPlots: u32,
    numPoints: u32,
    _pad: vec2<u32>,
    colors: array<vec4<f32>, 16>,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var dataSampler: sampler;
@group(0) @binding(2) var dataTexture: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var p = array<vec2<f32>, 6>(
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
        vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    let pos = p[vi];
    var o: VertexOutput;
    o.position = vec4(u.rect.x + pos.x * u.rect.z, u.rect.y - pos.y * u.rect.w, 0.0, 1.0);
    o.uv = pos;
    return o;
}

// Distance from point to line segment
fn distToSegment(p: vec2<f32>, a: vec2<f32>, b: vec2<f32>) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = uv * u.resolution;

    // Background color (dark)
    var color = vec4<f32>(0.1, 0.1, 0.12, 1.0);

    // Grid
    if (u.gridEnabled > 0.5) {
        let viewX = u.viewport.x + uv.x * (u.viewport.y - u.viewport.x);
        let viewY = u.viewport.z + (1.0 - uv.y) * (u.viewport.w - u.viewport.z);

        let rangeX = u.viewport.y - u.viewport.x;
        let rangeY = u.viewport.w - u.viewport.z;

        // Adaptive grid spacing
        let gridStepX = pow(10.0, floor(log(rangeX) / log(10.0)));
        let gridStepY = pow(10.0, floor(log(rangeY) / log(10.0)));

        let gridX = abs(fract(viewX / gridStepX + 0.5) - 0.5) * gridStepX;
        let gridY = abs(fract(viewY / gridStepY + 0.5) - 0.5) * gridStepY;

        let pixelSizeX = rangeX / u.resolution.x;
        let pixelSizeY = rangeY / u.resolution.y;

        if (gridX < pixelSizeX * 1.5 || gridY < pixelSizeY * 1.5) {
            color = vec4<f32>(0.2, 0.2, 0.25, 1.0);
        }

        // Axes
        if (abs(viewX) < pixelSizeX * 2.0 || abs(viewY) < pixelSizeY * 2.0) {
            color = vec4<f32>(0.4, 0.4, 0.45, 1.0);
        }
    }

    // Render each plot
    let numPts = f32(u.numPoints);
    let halfWidth = u.lineWidth * 0.5;

    for (var plotIdx: u32 = 0u; plotIdx < u.numPlots; plotIdx = plotIdx + 1u) {
        let plotColor = u.colors[plotIdx];
        let plotV = (f32(plotIdx) + 0.5) / f32(u.numPlots);

        var minDist = 1e10;

        // Check distance to each line segment
        // We sample at fixed intervals and connect consecutive points
        let step = 1.0 / numPts;

        for (var i: u32 = 0u; i < u.numPoints - 1u; i = i + 1u) {
            let t0 = (f32(i) + 0.5) / numPts;
            let t1 = (f32(i + 1u) + 0.5) / numPts;

            // Sample Y values from texture
            let y0 = textureSampleLevel(dataTexture, dataSampler, vec2(t0, plotV), 0.0).r;
            let y1 = textureSampleLevel(dataTexture, dataSampler, vec2(t1, plotV), 0.0).r;

            // Convert data coordinates to screen coordinates
            let x0_data = u.viewport.x + t0 * (u.viewport.y - u.viewport.x);
            let x1_data = u.viewport.x + t1 * (u.viewport.y - u.viewport.x);

            // Normalize to [0,1] in current viewport
            let x0_norm = (x0_data - u.viewport.x) / (u.viewport.y - u.viewport.x);
            let x1_norm = (x1_data - u.viewport.x) / (u.viewport.y - u.viewport.x);
            let y0_norm = (y0 - u.viewport.z) / (u.viewport.w - u.viewport.z);
            let y1_norm = (y1 - u.viewport.z) / (u.viewport.w - u.viewport.z);

            // Convert to pixel coordinates
            let p0 = vec2<f32>(x0_norm * u.resolution.x, (1.0 - y0_norm) * u.resolution.y);
            let p1 = vec2<f32>(x1_norm * u.resolution.x, (1.0 - y1_norm) * u.resolution.y);

            let d = distToSegment(fragCoord, p0, p1);
            minDist = min(minDist, d);
        }

        // Anti-aliased line
        let alpha = 1.0 - smoothstep(halfWidth - 1.0, halfWidth + 1.0, minDist);
        if (alpha > 0.0) {
            color = mix(color, plotColor, alpha);
        }
    }

    // Border
    let border = 2.0;
    let onBorder = fragCoord.x < border || fragCoord.x > u.resolution.x - border ||
                   fragCoord.y < border || fragCoord.y > u.resolution.y - border;
    if (onBorder) {
        color = vec4<f32>(0.3, 0.3, 0.35, 1.0);
    }

    return color;
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPU_STR(shaderCode);
    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);
    if (!shaderModule) return Err<void>("Failed to create shader module");

    // Bind group layout
    WGPUBindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_NonFiltering;
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 3;
    bglDesc.entries = entries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bgl) {
        wgpuShaderModuleRelease(shaderModule);
        return Err<void>("Failed to create bind group layout");
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Bind group
    WGPUBindGroupEntry bgE[3] = {};
    bgE[0].binding = 0;
    bgE[0].buffer = uniformBuffer_;
    bgE[0].size = 320;
    bgE[1].binding = 1;
    bgE[1].sampler = sampler_;
    bgE[2].binding = 2;
    bgE[2].textureView = dataTextureView_;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgE;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
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
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) return Err<void>("Failed to create render pipeline");

    spdlog::info("PlotW: pipeline created ({}x{} texture)", texWidth, texHeight);
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "plot"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) { return yetty::PlotPlugin::create(std::move(engine)); }
}
