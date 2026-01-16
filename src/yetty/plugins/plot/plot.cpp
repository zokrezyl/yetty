#include "plot.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/font-manager.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>
#include <ytrace/ytrace.hpp>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <cstdio>

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

Result<PluginPtr> PlotPlugin::create() noexcept {
    auto p = PluginPtr(new PlotPlugin());
    if (auto res = static_cast<PlotPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init PlotPlugin", res);
    }
    return Ok(p);
}

Result<void> PlotPlugin::pluginInit() noexcept {
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

Result<WidgetPtr> PlotPlugin::createWidget(
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
    (void)factory;
    (void)loop;
    (void)pluginArgs;
    yfunc();
    yinfo("payload size={} x={} y={} w={} h={}", payload.size(), x, y, widthCells, heightCells);

    // Get default font for axis labels
    Font* font = fontManager ? fontManager->getDefaultFont() : nullptr;
    auto result = Plot::create(payload, font);
    if (result) {
        auto& widget = *result;
        widget->setPosition(x, y);
        widget->setCellSize(widthCells, heightCells);
    }
    return result;
}

//-----------------------------------------------------------------------------
// Plot
//-----------------------------------------------------------------------------

Plot::~Plot() {
    (void)dispose();
}

Result<void> Plot::init() {
    ydebug("Plot::init() called, payload.size={}", _payload.size());
    std::memcpy(colors_, DEFAULT_COLORS, sizeof(colors_));

    if (_payload.empty()) {
        ydebug("Plot::init() empty payload, returning");
        return Ok();
    }

    // Binary format: [uint32 N][uint32 M][float xmin][float xmax][float ymin][float ymax][N*M floats]
    // Header size: 8 + 16 = 24 bytes
    constexpr size_t HEADER_SIZE = 24;

    if (_payload.size() >= HEADER_SIZE) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(_payload.data());

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
            _payload.size() == expected_size &&
            std::isfinite(xmin) && std::isfinite(xmax) &&
            std::isfinite(ymin) && std::isfinite(ymax)) {

            numPlots_ = n;
            numPoints_ = m;
            data_.resize(numPlots_ * numPoints_);
            std::memcpy(data_.data(), data + HEADER_SIZE, numPlots_ * numPoints_ * sizeof(float));
            setViewport(xmin, xmax, ymin, ymax);
            dataDirty_ = true;

            yinfo("Plot: initialized from binary (N={}, M={}, viewport=[{},{},{},{}])",
                         numPlots_, numPoints_, xmin, xmax, ymin, ymax);
            return Ok();
        }
    }

    // Fallback: text format "N,M" or "N,M,xmin,xmax,ymin,ymax"
    uint32_t n = 0, m = 0;
    float xmin = 0, xmax = 1, ymin = 0, ymax = 1;
    int parsed = sscanf(_payload.c_str(), "%u,%u,%f,%f,%f,%f",
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

    yinfo("Plot: initialized (N={}, M={})", numPlots_, numPoints_);
    return Ok();
}

Result<void> Plot::dispose() {
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

void Plot::update(double deltaTime) {
    (void)deltaTime;
}

Result<void> Plot::setData(const float* data, uint32_t numPlots, uint32_t numPoints) {
    if (!data || numPlots == 0 || numPoints == 0) {
        return Err<void>("Invalid plot data");
    }

    numPlots_ = std::min(numPlots, MAX_PLOTS);
    numPoints_ = numPoints;
    data_.resize(numPlots_ * numPoints_);
    std::memcpy(data_.data(), data, numPlots_ * numPoints_ * sizeof(float));
    dataDirty_ = true;

    ydebug("Plot: data updated (N={}, M={})", numPlots_, numPoints_);
    return Ok();
}

void Plot::setViewport(float xMin, float xMax, float yMin, float yMax) {
    xMin_ = xMin;
    xMax_ = xMax;
    yMin_ = yMin;
    yMax_ = yMax;
    _ticksDirty = true;
}

void Plot::setPlotColor(uint32_t plotIndex, float r, float g, float b, float a) {
    if (plotIndex >= MAX_PLOTS) return;
    colors_[plotIndex * 4 + 0] = r;
    colors_[plotIndex * 4 + 1] = g;
    colors_[plotIndex * 4 + 2] = b;
    colors_[plotIndex * 4 + 3] = a;
}

void Plot::setLineWidth(float width) {
    lineWidth_ = std::max(0.5f, std::min(10.0f, width));
}

void Plot::setGridEnabled(bool enabled) {
    gridEnabled_ = enabled;
}

bool Plot::onMouseMove(float localX, float localY) {
    float normX = localX / static_cast<float>(_pixelWidth);
    float normY = localY / static_cast<float>(_pixelHeight);

    if (panning_) {
        float dx = normX - panStartX_;
        float dy = normY - panStartY_;

        float rangeX = viewportStartXMax_ - viewportStartXMin_;
        float rangeY = viewportStartYMax_ - viewportStartYMin_;

        xMin_ = viewportStartXMin_ - dx * rangeX;
        xMax_ = viewportStartXMax_ - dx * rangeX;
        yMin_ = viewportStartYMin_ + dy * rangeY;
        yMax_ = viewportStartYMax_ + dy * rangeY;
        _ticksDirty = true;
    }

    mouseX_ = normX;
    mouseY_ = normY;
    return true;
}

bool Plot::onMouseButton(int button, bool pressed) {
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

bool Plot::onMouseScroll(float xoffset, float yoffset, int mods) {
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

    _ticksDirty = true;
    return true;
}

//-----------------------------------------------------------------------------
// Axis tick calculation
//-----------------------------------------------------------------------------

// Calculate a "nice" step size for axis ticks
static float niceNumber(float range, bool round) {
    float exponent = std::floor(std::log10(range));
    float fraction = range / std::pow(10.0f, exponent);
    float niceFraction;

    if (round) {
        if (fraction < 1.5f) niceFraction = 1.0f;
        else if (fraction < 3.0f) niceFraction = 2.0f;
        else if (fraction < 7.0f) niceFraction = 5.0f;
        else niceFraction = 10.0f;
    } else {
        if (fraction <= 1.0f) niceFraction = 1.0f;
        else if (fraction <= 2.0f) niceFraction = 2.0f;
        else if (fraction <= 5.0f) niceFraction = 5.0f;
        else niceFraction = 10.0f;
    }

    return niceFraction * std::pow(10.0f, exponent);
}

std::string Plot::formatTickValue(float value) const {
    char buf[32];
    float absVal = std::abs(value);

    if (absVal == 0.0f) {
        return "0";
    } else if (absVal >= 1e6f || absVal < 1e-3f) {
        // Scientific notation for very large/small
        std::snprintf(buf, sizeof(buf), "%.1e", value);
    } else if (absVal >= 1000.0f) {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else if (absVal >= 1.0f) {
        // Remove trailing zeros
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        // Strip trailing zeros
        char* dot = strchr(buf, '.');
        if (dot) {
            char* end = buf + strlen(buf) - 1;
            while (end > dot && *end == '0') *end-- = '\0';
            if (end == dot) *end = '\0';
        }
    } else {
        std::snprintf(buf, sizeof(buf), "%.3f", value);
        // Strip trailing zeros
        char* dot = strchr(buf, '.');
        if (dot) {
            char* end = buf + strlen(buf) - 1;
            while (end > dot && *end == '0') *end-- = '\0';
        }
    }

    return buf;
}

void Plot::calculateAxisTicks() {
    if (!_font) return;

    // Calculate X-axis ticks
    float xRange = xMax_ - xMin_;
    float xStep = niceNumber(xRange / 5.0f, true);
    float xStart = std::ceil(xMin_ / xStep) * xStep;

    _xTicks.count = 0;
    for (float x = xStart; x <= xMax_ && _xTicks.count < MAX_TICKS; x += xStep) {
        // Convert data X to pixel position
        float normX = (x - xMin_) / xRange;
        float pixelX = _marginLeft + normX * (_pixelWidth - _marginLeft);

        if (pixelX >= _marginLeft && pixelX <= _pixelWidth) {
            _xTicks.positions[_xTicks.count] = pixelX;

            // Format and convert to glyph indices
            std::string label = formatTickValue(x);
            _xTicks.charCounts[_xTicks.count] = std::min((uint32_t)label.size(), MAX_LABEL_CHARS);

            for (uint32_t i = 0; i < _xTicks.charCounts[_xTicks.count]; i++) {
                _xTicks.glyphIndices[_xTicks.count][i] = _font->getGlyphIndex((uint32_t)label[i]);
            }
            // Pad with zeros
            for (uint32_t i = _xTicks.charCounts[_xTicks.count]; i < MAX_LABEL_CHARS; i++) {
                _xTicks.glyphIndices[_xTicks.count][i] = 0;
            }

            _xTicks.count++;
        }
    }

    // Calculate Y-axis ticks
    float yRange = yMax_ - yMin_;
    float yStep = niceNumber(yRange / 5.0f, true);
    float yStart = std::ceil(yMin_ / yStep) * yStep;

    _yTicks.count = 0;
    for (float y = yStart; y <= yMax_ && _yTicks.count < MAX_TICKS; y += yStep) {
        // Convert data Y to pixel position (Y is flipped)
        float normY = (y - yMin_) / yRange;
        float pixelY = (_pixelHeight - _marginBottom) * (1.0f - normY);

        if (pixelY >= 0 && pixelY <= _pixelHeight - _marginBottom) {
            _yTicks.positions[_yTicks.count] = pixelY;

            // Format and convert to glyph indices
            std::string label = formatTickValue(y);
            _yTicks.charCounts[_yTicks.count] = std::min((uint32_t)label.size(), MAX_LABEL_CHARS);

            for (uint32_t i = 0; i < _yTicks.charCounts[_yTicks.count]; i++) {
                _yTicks.glyphIndices[_yTicks.count][i] = _font->getGlyphIndex((uint32_t)label[i]);
            }
            // Pad with zeros
            for (uint32_t i = _yTicks.charCounts[_yTicks.count]; i < MAX_LABEL_CHARS; i++) {
                _yTicks.glyphIndices[_yTicks.count][i] = 0;
            }

            _yTicks.count++;
        }
    }

    _ticksDirty = false;
}

Result<void> Plot::updateDataTexture(WebGPUContext& ctx) {
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

// Batched render - uses the shared render pass
Result<void> Plot::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    ydebug("Plot::render called, failed_={}, data_.size()={}", failed_, data_.size());
    if (failed_ || data_.empty()) {
        ydebug("Plot::render returning early - no data or failed");
        return Ok();
    }

    // Get render context parameters
    WGPUTextureFormat targetFormat = _renderCtx.targetFormat;
    uint32_t screenWidth = _renderCtx.screenWidth;
    uint32_t screenHeight = _renderCtx.screenHeight;
    float pixelX = _pixelX;
    float pixelY = _pixelY;
    float pixelW = static_cast<float>(_pixelWidth);
    float pixelH = static_cast<float>(_pixelHeight);

    if (!gpuInitialized_) {
        if (auto res = createPipeline(ctx, targetFormat); !res) {
            failed_ = true;
            return Err<void>("Plot: failed to create pipeline", res);
        }
        gpuInitialized_ = true;
        dataDirty_ = true;
        _ticksDirty = true;
    }

    if (dataDirty_) {
        if (auto res = updateDataTexture(ctx); !res) {
            return Err<void>("Plot: failed to update data texture", res);
        }
    }

    // Calculate axis ticks if viewport changed
    if (_ticksDirty && _font) {
        calculateAxisTicks();
    }

    if (!pipeline_ || !uniformBuffer_ || !bindGroup_) {
        failed_ = true;
        return Err<void>("Plot: pipeline resources not initialized");
    }

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    // Extended uniform structure (must match shader - all arrays use vec4 for 16-byte alignment)
    struct Uniforms {
        float rect[4];              // 16 bytes
        float viewport[4];          // 16 bytes
        float resolution[2];        // 8 bytes
        float lineWidth;            // 4 bytes
        float gridEnabled;          // 4 bytes
        uint32_t numPlots;          // 4 bytes
        uint32_t numPoints;         // 4 bytes
        uint32_t _pad[2];           // 8 bytes
        float colors[16 * 4];       // 256 bytes = 320 bytes total
        // Axis label data (16-byte aligned arrays)
        float marginLeft;           // 4 bytes
        float marginBottom;         // 4 bytes
        uint32_t xTickCount;        // 4 bytes
        uint32_t yTickCount;        // 4 bytes
        float xTickPositions[12];   // 48 bytes (3 vec4s, 10 used)
        float yTickPositions[12];   // 48 bytes
        uint32_t xTickGlyphs[60];   // 240 bytes (15 vec4<u32>, 10*6=60 used)
        uint32_t yTickGlyphs[60];   // 240 bytes
        uint32_t xTickCharCounts[12]; // 48 bytes (3 vec4<u32>, 10 used)
        uint32_t yTickCharCounts[12]; // 48 bytes
        float fontScale;            // 4 bytes
        float pixelRange;           // 4 bytes
        float _pad2[2];             // 8 bytes
    } uniforms;  // Total: 1024 bytes

    std::memset(&uniforms, 0, sizeof(uniforms));

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
    std::memcpy(uniforms.colors, colors_, sizeof(colors_));

    // Axis label data
    uniforms.marginLeft = _font ? _marginLeft : 0.0f;
    uniforms.marginBottom = _font ? _marginBottom : 0.0f;
    uniforms.xTickCount = _xTicks.count;
    uniforms.yTickCount = _yTicks.count;

    for (uint32_t i = 0; i < MAX_TICKS; i++) {
        uniforms.xTickPositions[i] = _xTicks.positions[i];
        uniforms.yTickPositions[i] = _yTicks.positions[i];
        uniforms.xTickCharCounts[i] = _xTicks.charCounts[i];
        uniforms.yTickCharCounts[i] = _yTicks.charCounts[i];

        // Pack glyph indices (2 uint16 per uint32) into flat array
        // Each tick uses 6 u32s at offset i*6
        for (uint32_t j = 0; j < 6; j++) {
            uint32_t idx0 = j * 2;
            uint32_t idx1 = j * 2 + 1;
            uint16_t g0 = (idx0 < MAX_LABEL_CHARS) ? _xTicks.glyphIndices[i][idx0] : 0;
            uint16_t g1 = (idx1 < MAX_LABEL_CHARS) ? _xTicks.glyphIndices[i][idx1] : 0;
            uniforms.xTickGlyphs[i * 6 + j] = (uint32_t)g0 | ((uint32_t)g1 << 16);

            g0 = (idx0 < MAX_LABEL_CHARS) ? _yTicks.glyphIndices[i][idx0] : 0;
            g1 = (idx1 < MAX_LABEL_CHARS) ? _yTicks.glyphIndices[i][idx1] : 0;
            uniforms.yTickGlyphs[i * 6 + j] = (uint32_t)g0 | ((uint32_t)g1 << 16);
        }
    }

    uniforms.fontScale = 0.5f;  // Scale factor for axis labels
    uniforms.pixelRange = 4.0f; // MSDF pixel range (typical value)

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    // Draw using the shared render pass
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup_, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);

    return Ok();
}

Result<void> Plot::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
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

    // Uniform buffer (expanded for axis labels)
    // Base: 320 bytes
    // + margins: 8 bytes (marginLeft, marginBottom)
    // + xTickCount, yTickCount: 8 bytes
    // + xTickPositions: 40 bytes (10 floats)
    // + yTickPositions: 40 bytes (10 floats)
    // + xTickGlyphs: 10 * 12 * 2 = 240 bytes (uint16 indices, packed as u32)
    // + yTickGlyphs: 240 bytes
    // + xTickCharCounts: 40 bytes
    // + yTickCharCounts: 40 bytes
    // + fontScale, pixelRange: 8 bytes
    // Total: ~1024 bytes (round up for alignment)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 1024;  // Extended uniform buffer
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

    // Shader code with MSDF text rendering
    // NOTE: Uniform arrays require 16-byte alignment, so we pack floats into vec4s
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
    // Axis label data (after colors at offset 320)
    marginLeft: f32,
    marginBottom: f32,
    xTickCount: u32,
    yTickCount: u32,
    // Tick positions packed as vec4 (10 floats -> 3 vec4s = 12 floats)
    xTickPositions: array<vec4<f32>, 3>,
    yTickPositions: array<vec4<f32>, 3>,
    // Glyph indices: 10 ticks * 6 u32s = 60 u32s -> 15 vec4<u32>
    xTickGlyphs: array<vec4<u32>, 15>,
    yTickGlyphs: array<vec4<u32>, 15>,
    // Char counts packed as vec4 (10 u32s -> 3 vec4s = 12 u32s)
    xTickCharCounts: array<vec4<u32>, 3>,
    yTickCharCounts: array<vec4<u32>, 3>,
    fontScale: f32,
    pixelRange: f32,
    _pad2: vec2<f32>,
}

struct GlyphMetadata {
    uvMin: vec2<f32>,
    uvMax: vec2<f32>,
    size: vec2<f32>,
    bearing: vec2<f32>,
    advance: f32,
    _pad: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var dataSampler: sampler;
@group(0) @binding(2) var dataTexture: texture_2d<f32>;
@group(0) @binding(3) var fontTexture: texture_2d<f32>;
@group(0) @binding(4) var fontSampler: sampler;
@group(0) @binding(5) var<storage, read> glyphMetadata: array<GlyphMetadata>;

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

// MSDF median for signed distance
fn median(r: f32, g: f32, b: f32) -> f32 {
    return max(min(r, g), min(max(r, g), b));
}

// Get tick position from packed vec4 array
fn getTickPosition(positions: array<vec4<f32>, 3>, idx: u32) -> f32 {
    let vecIdx = idx / 4u;
    let component = idx % 4u;
    let v = positions[vecIdx];
    if (component == 0u) { return v.x; }
    else if (component == 1u) { return v.y; }
    else if (component == 2u) { return v.z; }
    else { return v.w; }
}

// Get char count from packed vec4 array
fn getCharCount(counts: array<vec4<u32>, 3>, idx: u32) -> u32 {
    let vecIdx = idx / 4u;
    let component = idx % 4u;
    let v = counts[vecIdx];
    if (component == 0u) { return v.x; }
    else if (component == 1u) { return v.y; }
    else if (component == 2u) { return v.z; }
    else { return v.w; }
}

// Extract glyph index from packed vec4<u32> array
// Each tick has 6 u32s (12 chars packed as 2 per u32)
// glyphs array has 15 vec4s total (60 u32s = 10 ticks * 6 u32s)
fn getGlyphIndexFromTick(glyphs: array<vec4<u32>, 15>, tickIdx: u32, charIdx: u32) -> u32 {
    // Each tick uses 6 u32s, packed into 1.5 vec4s
    let u32Offset = tickIdx * 6u + charIdx / 2u;
    let vecIdx = u32Offset / 4u;
    let component = u32Offset % 4u;
    let v = glyphs[vecIdx];
    var word: u32;
    if (component == 0u) { word = v.x; }
    else if (component == 1u) { word = v.y; }
    else if (component == 2u) { word = v.z; }
    else { word = v.w; }

    let isLow = (charIdx % 2u) == 0u;
    if (isLow) {
        return word & 0xFFFFu;
    } else {
        return (word >> 16u) & 0xFFFFu;
    }
}

// Render a single glyph, returns alpha if fragment is inside glyph
fn renderGlyph(fragPos: vec2<f32>, glyphPos: vec2<f32>, glyphIdx: u32, scale: f32) -> f32 {
    if (glyphIdx == 0u) {
        return 0.0;
    }

    let glyph = glyphMetadata[glyphIdx];
    let scaledSize = glyph.size * scale;
    let scaledBearing = glyph.bearing * scale;

    // Glyph bounds (top-left origin for Y)
    let glyphMin = vec2<f32>(glyphPos.x + scaledBearing.x, glyphPos.y - scaledBearing.y);
    let glyphMax = glyphMin + scaledSize;

    // Check if fragment is inside glyph bounding box
    if (fragPos.x < glyphMin.x || fragPos.x >= glyphMax.x ||
        fragPos.y < glyphMin.y || fragPos.y >= glyphMax.y) {
        return 0.0;
    }

    // Calculate UV within glyph
    let localPos = (fragPos - glyphMin) / scaledSize;
    let texUV = mix(glyph.uvMin, glyph.uvMax, localPos);

    // Sample MSDF
    let msdf = textureSampleLevel(fontTexture, fontSampler, texUV, 0.0);
    let sd = median(msdf.r, msdf.g, msdf.b);

    // Calculate alpha with pixel range
    let screenPxRange = u.pixelRange * scale;
    let alpha = clamp((sd - 0.5) * screenPxRange + 0.5, 0.0, 1.0);

    return alpha;
}

@fragment
fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    let fragCoord = uv * u.resolution;

    // Background color (dark)
    var color = vec4<f32>(0.1, 0.1, 0.12, 1.0);

    // Check if in margin area for axis labels
    let inLeftMargin = fragCoord.x < u.marginLeft;
    let inBottomMargin = fragCoord.y > u.resolution.y - u.marginBottom;

    // Render Y-axis labels (left margin)
    if (inLeftMargin && u.yTickCount > 0u) {
        let labelColor = vec3<f32>(0.8, 0.8, 0.8);
        for (var i = 0u; i < u.yTickCount; i = i + 1u) {
            let tickY = getTickPosition(u.yTickPositions, i);
            let charCount = getCharCount(u.yTickCharCounts, i);

            // Right-align: calculate total width first
            var totalWidth = 0.0;
            for (var c = 0u; c < charCount; c = c + 1u) {
                let glyphIdx = getGlyphIndexFromTick(u.yTickGlyphs, i, c);
                if (glyphIdx > 0u) {
                    let glyph = glyphMetadata[glyphIdx];
                    totalWidth = totalWidth + glyph.advance * u.fontScale;
                }
            }

            // Start position (right-aligned with 4px padding from plot area)
            var cursorX = u.marginLeft - totalWidth - 4.0;
            let baseY = tickY + u.fontScale * 0.35;  // Approximate vertical centering

            for (var c = 0u; c < charCount; c = c + 1u) {
                let glyphIdx = getGlyphIndexFromTick(u.yTickGlyphs, i, c);
                if (glyphIdx > 0u) {
                    let alpha = renderGlyph(fragCoord, vec2<f32>(cursorX, baseY), glyphIdx, u.fontScale);
                    if (alpha > 0.01) {
                        color = vec4<f32>(mix(color.rgb, labelColor, alpha), 1.0);
                    }
                    let glyph = glyphMetadata[glyphIdx];
                    cursorX = cursorX + glyph.advance * u.fontScale;
                }
            }
        }
    }

    // Render X-axis labels (bottom margin)
    if (inBottomMargin && u.xTickCount > 0u) {
        let labelColor = vec3<f32>(0.8, 0.8, 0.8);
        for (var i = 0u; i < u.xTickCount; i = i + 1u) {
            let tickX = getTickPosition(u.xTickPositions, i);
            let charCount = getCharCount(u.xTickCharCounts, i);

            // Center-align: calculate total width first
            var totalWidth = 0.0;
            for (var c = 0u; c < charCount; c = c + 1u) {
                let glyphIdx = getGlyphIndexFromTick(u.xTickGlyphs, i, c);
                if (glyphIdx > 0u) {
                    let glyph = glyphMetadata[glyphIdx];
                    totalWidth = totalWidth + glyph.advance * u.fontScale;
                }
            }

            // Start position (centered on tick position)
            var cursorX = tickX - totalWidth * 0.5;
            let baseY = u.resolution.y - u.marginBottom + 4.0 + u.fontScale * 0.8;

            for (var c = 0u; c < charCount; c = c + 1u) {
                let glyphIdx = getGlyphIndexFromTick(u.xTickGlyphs, i, c);
                if (glyphIdx > 0u) {
                    let alpha = renderGlyph(fragCoord, vec2<f32>(cursorX, baseY), glyphIdx, u.fontScale);
                    if (alpha > 0.01) {
                        color = vec4<f32>(mix(color.rgb, labelColor, alpha), 1.0);
                    }
                    let glyph = glyphMetadata[glyphIdx];
                    cursorX = cursorX + glyph.advance * u.fontScale;
                }
            }
        }
    }

    // Skip plot rendering in margins
    if (inLeftMargin || inBottomMargin) {
        return color;
    }

    // Calculate plot area (excluding margins)
    let plotAreaX = u.marginLeft;
    let plotAreaY = 0.0;
    let plotAreaW = u.resolution.x - u.marginLeft;
    let plotAreaH = u.resolution.y - u.marginBottom;

    // Normalized position within plot area
    let plotUV = vec2<f32>(
        (fragCoord.x - plotAreaX) / plotAreaW,
        fragCoord.y / plotAreaH
    );

    // Grid
    if (u.gridEnabled > 0.5) {
        let viewX = u.viewport.x + plotUV.x * (u.viewport.y - u.viewport.x);
        let viewY = u.viewport.z + (1.0 - plotUV.y) * (u.viewport.w - u.viewport.z);

        let rangeX = u.viewport.y - u.viewport.x;
        let rangeY = u.viewport.w - u.viewport.z;

        // Adaptive grid spacing
        let gridStepX = pow(10.0, floor(log(rangeX) / log(10.0)));
        let gridStepY = pow(10.0, floor(log(rangeY) / log(10.0)));

        let gridX = abs(fract(viewX / gridStepX + 0.5) - 0.5) * gridStepX;
        let gridY = abs(fract(viewY / gridStepY + 0.5) - 0.5) * gridStepY;

        let pixelSizeX = rangeX / plotAreaW;
        let pixelSizeY = rangeY / plotAreaH;

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

        for (var i: u32 = 0u; i < u.numPoints - 1u; i = i + 1u) {
            let t0 = (f32(i) + 0.5) / numPts;
            let t1 = (f32(i + 1u) + 0.5) / numPts;

            // Sample Y values from texture
            let y0 = textureSampleLevel(dataTexture, dataSampler, vec2(t0, plotV), 0.0).r;
            let y1 = textureSampleLevel(dataTexture, dataSampler, vec2(t1, plotV), 0.0).r;

            // Convert data coordinates to normalized [0,1]
            let x0_data = u.viewport.x + t0 * (u.viewport.y - u.viewport.x);
            let x1_data = u.viewport.x + t1 * (u.viewport.y - u.viewport.x);
            let x0_norm = (x0_data - u.viewport.x) / (u.viewport.y - u.viewport.x);
            let x1_norm = (x1_data - u.viewport.x) / (u.viewport.y - u.viewport.x);
            let y0_norm = (y0 - u.viewport.z) / (u.viewport.w - u.viewport.z);
            let y1_norm = (y1 - u.viewport.z) / (u.viewport.w - u.viewport.z);

            // Convert to pixel coordinates within plot area
            let p0 = vec2<f32>(plotAreaX + x0_norm * plotAreaW, (1.0 - y0_norm) * plotAreaH);
            let p1 = vec2<f32>(plotAreaX + x1_norm * plotAreaW, (1.0 - y1_norm) * plotAreaH);

            let d = distToSegment(fragCoord, p0, p1);
            minDist = min(minDist, d);
        }

        // Anti-aliased line
        let alpha = 1.0 - smoothstep(halfWidth - 1.0, halfWidth + 1.0, minDist);
        if (alpha > 0.0) {
            color = mix(color, plotColor, alpha);
        }
    }

    // Border around plot area
    let border = 2.0;
    let onPlotBorder = fragCoord.x < plotAreaX + border ||
                        fragCoord.y < border ||
                        fragCoord.y > plotAreaH - border;
    if (onPlotBorder) {
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

    // Bind group layout (6 bindings: uniforms, dataSampler, dataTexture, fontTexture, fontSampler, glyphMetadata)
    WGPUBindGroupLayoutEntry entries[6] = {};
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
    // Font texture (MSDF atlas)
    entries[3].binding = 3;
    entries[3].visibility = WGPUShaderStage_Fragment;
    entries[3].texture.sampleType = WGPUTextureSampleType_Float;
    entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
    // Font sampler (linear filtering for MSDF)
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Fragment;
    entries[4].sampler.type = WGPUSamplerBindingType_Filtering;
    // Glyph metadata storage buffer
    entries[5].binding = 5;
    entries[5].visibility = WGPUShaderStage_Fragment;
    entries[5].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = _font ? 6 : 3;  // Only include font bindings if font available
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
    WGPUBindGroupEntry bgE[6] = {};
    bgE[0].binding = 0;
    bgE[0].buffer = uniformBuffer_;
    bgE[0].size = 1024;  // Extended uniform buffer
    bgE[1].binding = 1;
    bgE[1].sampler = sampler_;
    bgE[2].binding = 2;
    bgE[2].textureView = dataTextureView_;

    uint32_t entryCount = 3;
    if (_font) {
        bgE[3].binding = 3;
        bgE[3].textureView = _font->getTextureView();
        bgE[4].binding = 4;
        bgE[4].sampler = _font->getSampler();
        bgE[5].binding = 5;
        bgE[5].buffer = _font->getGlyphMetadataBuffer();
        bgE[5].size = _font->getGlyphCount() * sizeof(GlyphMetadataGPU);
        entryCount = 6;
    }

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = entryCount;
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

    yinfo("Plot: pipeline created ({}x{} texture)", texWidth, texHeight);
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "plot"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::PlotPlugin::create(); }
}
