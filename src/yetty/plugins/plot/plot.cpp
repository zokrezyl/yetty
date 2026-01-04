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
    if (auto res = static_cast<PlotPlugin*>(p.get())->init(); !res) {
        return Err<PluginPtr>("Failed to init PlotPlugin", res);
    }
    return Ok(p);
}

Result<void> PlotPlugin::init() noexcept {
    _initialized = true;
    return Ok();
}

Result<void> PlotPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose PlotPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> PlotPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<PlotLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to initialize Plot layer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> PlotPlugin::renderAll(WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                    uint32_t screenWidth, uint32_t screenHeight,
                                    float cellWidth, float cellHeight,
                                    int scrollOffset, uint32_t termRows,
                                    bool isAltScreen) {
    if (!engine_) return Err<void>("PlotPlugin::renderAll: no engine");

    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<PlotLayer>(layerBase);

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
            return Err<void>("Failed to render Plot layer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// PlotLayer
//-----------------------------------------------------------------------------

PlotLayer::PlotLayer() {
    std::memcpy(_colors, DEFAULT_COLORS, sizeof(_colors));
}

PlotLayer::~PlotLayer() {
    (void)dispose();
}

Result<void> PlotLayer::init(const std::string& payload) {
    _payload = payload;

    if (payload.empty()) {
        return Ok();
    }

    // Binary format: [uint32 N][uint32 M][float xmin][float xmax][float ymin][float ymax][N*M floats]
    // Header size: 8 + 16 = 24 bytes
    constexpr size_t HEADER_SIZE = 24;

    if (payload.size() >= HEADER_SIZE) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload.data());

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
            payload.size() == expected_size &&
            std::isfinite(xmin) && std::isfinite(xmax) &&
            std::isfinite(ymin) && std::isfinite(ymax)) {

            _num_plots = n;
            _num_points = m;
            _data.resize(_num_plots * _num_points);
            std::memcpy(_data.data(), data + HEADER_SIZE, _num_plots * _num_points * sizeof(float));
            setViewport(xmin, xmax, ymin, ymax);
            _data_dirty = true;

            spdlog::info("PlotLayer: initialized from binary (N={}, M={}, viewport=[{},{},{},{}])",
                         _num_plots, _num_points, xmin, xmax, ymin, ymax);
            return Ok();
        }
    }

    // Fallback: text format "N,M" or "N,M,xmin,xmax,ymin,ymax"
    uint32_t n = 0, m = 0;
    float xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    int parsed = sscanf(payload.c_str(), "%u,%u,%f,%f,%f,%f",
                        &n, &m, &xmin, &xmax, &ymin, &ymax);
    if (parsed >= 2 && n > 0 && m > 0) {
        _num_plots = std::min(n, MAX_PLOTS);
        _num_points = m;
        _data.resize(_num_plots * _num_points, 0.0f);
        _data_dirty = true;
        if (parsed >= 6) {
            setViewport(xmin, xmax, ymin, ymax);
        }
    }

    spdlog::info("PlotLayer: initialized (N={}, M={})", _num_plots, _num_points);
    return Ok();
}

Result<void> PlotLayer::dispose() {
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_sampler) { wgpuSamplerRelease(_sampler); _sampler = nullptr; }
    if (_data_texture_view) { wgpuTextureViewRelease(_data_texture_view); _data_texture_view = nullptr; }
    if (_data_texture) { wgpuTextureRelease(_data_texture); _data_texture = nullptr; }
    _gpu_initialized = false;
    _data.clear();
    return Ok();
}

Result<void> PlotLayer::update(double deltaTime) {
    (void)deltaTime;
    return Ok();
}

Result<void> PlotLayer::setData(const float* data, uint32_t numPlots, uint32_t numPoints) {
    if (!data || numPlots == 0 || numPoints == 0) {
        return Err<void>("Invalid plot data");
    }

    _num_plots = std::min(numPlots, MAX_PLOTS);
    _num_points = numPoints;
    _data.resize(_num_plots * _num_points);
    std::memcpy(_data.data(), data, _num_plots * _num_points * sizeof(float));
    _data_dirty = true;

    spdlog::debug("PlotLayer: data updated (N={}, M={})", _num_plots, _num_points);
    return Ok();
}

void PlotLayer::setViewport(float xMin, float xMax, float yMin, float yMax) {
    _x_min = xMin;
    _x_max = xMax;
    _y_min = yMin;
    _y_max = yMax;
}

void PlotLayer::setPlotColor(uint32_t plotIndex, float r, float g, float b, float a) {
    if (plotIndex >= MAX_PLOTS) return;
    _colors[plotIndex * 4 + 0] = r;
    _colors[plotIndex * 4 + 1] = g;
    _colors[plotIndex * 4 + 2] = b;
    _colors[plotIndex * 4 + 3] = a;
}

void PlotLayer::setLineWidth(float width) {
    _line_width = std::max(0.5f, std::min(10.0f, width));
}

void PlotLayer::setGridEnabled(bool enabled) {
    _grid_enabled = enabled;
}

bool PlotLayer::onMouseMove(float localX, float localY) {
    float normX = localX / static_cast<float>(_pixel_width);
    float normY = localY / static_cast<float>(_pixel_height);

    if (_panning) {
        float dx = normX - _pan_start_x;
        float dy = normY - _pan_start_y;

        float rangeX = _viewport_start_x_max - _viewport_start_x_min;
        float rangeY = _viewport_start_y_max - _viewport_start_y_min;

        _x_min = _viewport_start_x_min - dx * rangeX;
        _x_max = _viewport_start_x_max - dx * rangeX;
        _y_min = _viewport_start_y_min + dy * rangeY;
        _y_max = _viewport_start_y_max + dy * rangeY;
    }

    _mouse_x = normX;
    _mouse_y = normY;
    return true;
}

bool PlotLayer::onMouseButton(int button, bool pressed) {
    if (button == 0) {
        _panning = pressed;
        if (pressed) {
            _pan_start_x = _mouse_x;
            _pan_start_y = _mouse_y;
            _viewport_start_x_min = _x_min;
            _viewport_start_x_max = _x_max;
            _viewport_start_y_min = _y_min;
            _viewport_start_y_max = _y_max;
        }
        return true;
    }
    if (button == -1) {
        _panning = false;
        return false;
    }
    return false;
}

bool PlotLayer::onMouseScroll(float xoffset, float yoffset, int mods) {
    (void)xoffset;

    float zoomFactor = 1.0f - yoffset * 0.1f;
    zoomFactor = std::max(0.5f, std::min(2.0f, zoomFactor));

    // Zoom centered on mouse position
    float pivotX = _x_min + _mouse_x * (_x_max - _x_min);
    float pivotY = _y_min + (1.0f - _mouse_y) * (_y_max - _y_min);

    bool ctrlPressed = (mods & 0x0002) != 0;

    if (ctrlPressed) {
        // Zoom only Y axis
        _y_min = pivotY + (_y_min - pivotY) * zoomFactor;
        _y_max = pivotY + (_y_max - pivotY) * zoomFactor;
    } else {
        // Zoom both axes
        _x_min = pivotX + (_x_min - pivotX) * zoomFactor;
        _x_max = pivotX + (_x_max - pivotX) * zoomFactor;
        _y_min = pivotY + (_y_min - pivotY) * zoomFactor;
        _y_max = pivotY + (_y_max - pivotY) * zoomFactor;
    }

    return true;
}

Result<void> PlotLayer::updateDataTexture(WebGPUContext& ctx) {
    if (_data.empty() || _num_plots == 0 || _num_points == 0) {
        return Ok();
    }

    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = _data_texture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = _num_points * sizeof(float);
    layout.rowsPerImage = _num_plots;
    WGPUExtent3D extent = {_num_points, _num_plots, 1};

    wgpuQueueWriteTexture(ctx.getQueue(), &dst, _data.data(),
                          _data.size() * sizeof(float), &layout, &extent);
    _data_dirty = false;
    return Ok();
}

Result<void> PlotLayer::render(WebGPUContext& ctx,
                                WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                uint32_t screenWidth, uint32_t screenHeight,
                                float pixelX, float pixelY, float pixelW, float pixelH) {
    if (_failed) return Err<void>("PlotLayer already failed");
    if (_data.empty()) return Ok();  // Nothing to render

    if (!_gpu_initialized) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to create pipeline", result);
        }
        _gpu_initialized = true;
        _data_dirty = true;
    }

    if (_data_dirty) {
        auto result = updateDataTexture(ctx);
        if (!result) {
            return Err<void>("Failed to update data texture", result);
        }
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("PlotLayer pipeline not initialized");
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
    uniforms.viewport[0] = _x_min;
    uniforms.viewport[1] = _x_max;
    uniforms.viewport[2] = _y_min;
    uniforms.viewport[3] = _y_max;
    uniforms.resolution[0] = pixelW;
    uniforms.resolution[1] = pixelH;
    uniforms.lineWidth = _line_width;
    uniforms.gridEnabled = _grid_enabled ? 1.0f : 0.0f;
    uniforms.numPlots = _num_plots;
    uniforms.numPoints = _num_points;
    uniforms._pad[0] = 0;
    uniforms._pad[1] = 0;
    std::memcpy(uniforms.colors, _colors, sizeof(_colors));

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

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

    wgpuRenderPassEncoderSetPipeline(pass, _pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, _bind_group, 0, nullptr);
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

Result<void> PlotLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create data texture (NxM, R32Float)
    uint32_t texWidth = std::max(_num_points, 1u);
    uint32_t texHeight = std::max(_num_plots, 1u);

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = texWidth;
    texDesc.size.height = texHeight;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_R32Float;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    _data_texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!_data_texture) return Err<void>("Failed to create data texture");

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_R32Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    _data_texture_view = wgpuTextureCreateView(_data_texture, &viewDesc);
    if (!_data_texture_view) return Err<void>("Failed to create texture view");

    // Sampler for data texture (nearest - R32Float is non-filterable)
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Nearest;
    samplerDesc.magFilter = WGPUFilterMode_Nearest;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    _sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!_sampler) return Err<void>("Failed to create sampler");

    // Uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 320;  // sizeof(Uniforms)
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

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
    bgE[0].buffer = _uniform_buffer;
    bgE[0].size = 320;
    bgE[1].binding = 1;
    bgE[1].sampler = _sampler;
    bgE[2].binding = 2;
    bgE[2].textureView = _data_texture_view;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

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

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) return Err<void>("Failed to create render pipeline");

    spdlog::info("PlotLayer: pipeline created ({}x{} texture)", texWidth, texHeight);
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "plot"; }
    yetty::Result<yetty::PluginPtr> create(yetty::YettyPtr engine) { return yetty::PlotPlugin::create(std::move(engine)); }
}
