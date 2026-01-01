#include "video.h"
#include "../../renderer/webgpu-context.h"
#include "../../renderer/wgpu-compat.h"
#include <iostream>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace yetty {

//-----------------------------------------------------------------------------
// Custom AVIOContext for reading from memory
//-----------------------------------------------------------------------------
struct MemoryBuffer {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

static int read_packet(void* opaque, uint8_t* buf, int buf_size) {
    auto* mb = static_cast<MemoryBuffer*>(opaque);
    size_t remaining = mb->size - mb->pos;
    if (remaining == 0) return AVERROR_EOF;

    size_t to_read = std::min(static_cast<size_t>(buf_size), remaining);
    memcpy(buf, mb->data + mb->pos, to_read);
    mb->pos += to_read;
    return static_cast<int>(to_read);
}

static int64_t seek_packet(void* opaque, int64_t offset, int whence) {
    auto* mb = static_cast<MemoryBuffer*>(opaque);

    if (whence == AVSEEK_SIZE) {
        return static_cast<int64_t>(mb->size);
    }

    int64_t new_pos;
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = static_cast<int64_t>(mb->pos) + offset;
            break;
        case SEEK_END:
            new_pos = static_cast<int64_t>(mb->size) + offset;
            break;
        default:
            return AVERROR(EINVAL);
    }

    if (new_pos < 0 || static_cast<size_t>(new_pos) > mb->size) {
        return AVERROR(EINVAL);
    }

    mb->pos = static_cast<size_t>(new_pos);
    return new_pos;
}

//-----------------------------------------------------------------------------
// Video format detection via magic bytes
//-----------------------------------------------------------------------------
bool VideoPlugin::isVideoFormat(const std::string& data) {
    if (data.size() < 12) return false;

    const auto* d = reinterpret_cast<const uint8_t*>(data.data());

    // MP4/MOV/M4V: "ftyp" at offset 4
    if (data.size() >= 8 && memcmp(d + 4, "ftyp", 4) == 0) {
        return true;
    }

    // WebM/MKV: EBML header (0x1A 0x45 0xDF 0xA3)
    if (d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3) {
        return true;
    }

    // AVI: "RIFF" + size + "AVI "
    if (data.size() >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "AVI ", 4) == 0) {
        return true;
    }

    // MPEG-TS: sync byte 0x47 repeated
    if (d[0] == 0x47 && (data.size() < 188 || d[188] == 0x47)) {
        return true;
    }

    // MPEG-PS: pack start code 0x000001BA
    if (d[0] == 0x00 && d[1] == 0x00 && d[2] == 0x01 && d[3] == 0xBA) {
        return true;
    }

    // FLV: "FLV"
    if (memcmp(d, "FLV", 3) == 0) {
        return true;
    }

    // Ogg (may contain Theora video): "OggS"
    if (memcmp(d, "OggS", 4) == 0) {
        return true;
    }

    // GIF (animated): "GIF87a" or "GIF89a"
    if (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0) {
        return true;
    }

    // APNG/PNG with animation (check for acTL chunk would need more parsing)
    // For now, we don't treat PNG as video

    return false;
}

//-----------------------------------------------------------------------------
// VideoPlugin
//-----------------------------------------------------------------------------

VideoPlugin::VideoPlugin() = default;
VideoPlugin::~VideoPlugin() { (void)dispose(); }

Result<PluginPtr> VideoPlugin::create() {
    return Ok<PluginPtr>(std::make_shared<VideoPlugin>());
}

Result<void> VideoPlugin::init(WebGPUContext* ctx) {
    (void)ctx;
    _initialized = true;
    return Ok();
}

Result<void> VideoPlugin::dispose() {
    if (auto res = Plugin::dispose(); !res) {
        return Err<void>("Failed to dispose VideoPlugin", res);
    }
    _initialized = false;
    return Ok();
}

Result<PluginLayerPtr> VideoPlugin::createLayer(const std::string& payload) {
    auto layer = std::make_shared<VideoLayer>();
    auto result = layer->init(payload);
    if (!result) {
        return Err<PluginLayerPtr>("Failed to init VideoLayer", result);
    }
    return Ok<PluginLayerPtr>(layer);
}

Result<void> VideoPlugin::renderAll(WebGPUContext& ctx,
                                     WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                     uint32_t screenWidth, uint32_t screenHeight,
                                     float cellWidth, float cellHeight,
                                     int scrollOffset, uint32_t termRows,
                                     bool isAltScreen) {
    ScreenType currentScreen = isAltScreen ? ScreenType::Alternate : ScreenType::Main;
    for (auto& layerBase : _layers) {
        if (!layerBase->isVisible()) continue;
        if (layerBase->getScreenType() != currentScreen) continue;

        auto layer = std::static_pointer_cast<VideoLayer>(layerBase);

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

        if (auto res = layer->render(ctx, targetView, targetFormat,
                                      screenWidth, screenHeight,
                                      pixelX, pixelY, pixelW, pixelH); !res) {
            return Err<void>("Failed to render VideoLayer", res);
        }
    }
    return Ok();
}

//-----------------------------------------------------------------------------
// VideoLayer
//-----------------------------------------------------------------------------

VideoLayer::VideoLayer() = default;

VideoLayer::~VideoLayer() { (void)dispose(); }

Result<void> VideoLayer::init(const std::string& payload) {
    if (payload.empty()) {
        return Err<void>("VideoLayer: empty payload");
    }

    _payload = payload;
    (void)dispose();

    auto result = initFFmpeg(payload);
    if (!result) {
        return result;
    }

    std::cout << "VideoLayer: loaded " << _video_width << "x" << _video_height
              << " @ " << _frame_rate << " fps, duration=" << _duration << "s" << std::endl;
    return Ok();
}

Result<void> VideoLayer::initFFmpeg(const std::string& data) {
    // Store input data for custom I/O
    _input_data.assign(data.begin(), data.end());
    _input_pos = 0;

    // Allocate format context
    _format_ctx = avformat_alloc_context();
    if (!_format_ctx) {
        return Err<void>("Failed to allocate AVFormatContext");
    }

    // Create custom I/O context for reading from memory
    constexpr int AVIO_BUFFER_SIZE = 32768;
    auto* avio_buffer = static_cast<uint8_t*>(av_malloc(AVIO_BUFFER_SIZE));
    if (!avio_buffer) {
        return Err<void>("Failed to allocate AVIO buffer");
    }

    auto* mem_buffer = new MemoryBuffer{_input_data.data(), _input_data.size(), 0};
    AVIOContext* avio_ctx = avio_alloc_context(
        avio_buffer, AVIO_BUFFER_SIZE,
        0,  // write_flag = 0 (read-only)
        mem_buffer,
        read_packet,
        nullptr,  // write_packet
        seek_packet
    );

    if (!avio_ctx) {
        av_free(avio_buffer);
        delete mem_buffer;
        return Err<void>("Failed to allocate AVIOContext");
    }

    _format_ctx->pb = avio_ctx;
    _format_ctx->flags |= AVFMT_FLAG_CUSTOM_IO;

    // Open input (will use our custom I/O)
    int ret = avformat_open_input(&_format_ctx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        return Err<void>(std::string("Failed to open video: ") + errbuf);
    }

    // Find stream info
    ret = avformat_find_stream_info(_format_ctx, nullptr);
    if (ret < 0) {
        return Err<void>("Failed to find stream info");
    }

    // Find video stream
    _video_stream_idx = -1;
    const AVCodec* codec = nullptr;
    for (unsigned i = 0; i < _format_ctx->nb_streams; i++) {
        if (_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            _video_stream_idx = static_cast<int>(i);
            codec = avcodec_find_decoder(_format_ctx->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (_video_stream_idx < 0 || !codec) {
        return Err<void>("No video stream found");
    }

    // Allocate codec context
    _codec_ctx = avcodec_alloc_context3(codec);
    if (!_codec_ctx) {
        return Err<void>("Failed to allocate codec context");
    }

    AVStream* stream = _format_ctx->streams[_video_stream_idx];
    ret = avcodec_parameters_to_context(_codec_ctx, stream->codecpar);
    if (ret < 0) {
        return Err<void>("Failed to copy codec parameters");
    }

    // Open codec
    ret = avcodec_open2(_codec_ctx, codec, nullptr);
    if (ret < 0) {
        return Err<void>("Failed to open codec");
    }

    // Get video properties
    _video_width = _codec_ctx->width;
    _video_height = _codec_ctx->height;

    // Calculate frame rate
    AVRational fr = stream->avg_frame_rate;
    if (fr.num > 0 && fr.den > 0) {
        _frame_rate = av_q2d(fr);
    } else {
        fr = stream->r_frame_rate;
        if (fr.num > 0 && fr.den > 0) {
            _frame_rate = av_q2d(fr);
        }
    }
    _frame_time = 1.0 / _frame_rate;

    // Time base for seeking
    _time_base = av_q2d(stream->time_base);

    // Duration
    if (stream->duration != AV_NOPTS_VALUE) {
        _duration = stream->duration * _time_base;
    } else if (_format_ctx->duration != AV_NOPTS_VALUE) {
        _duration = _format_ctx->duration / static_cast<double>(AV_TIME_BASE);
    }

    // Allocate frames
    _frame = av_frame_alloc();
    _frame_rgba = av_frame_alloc();
    _packet = av_packet_alloc();

    if (!_frame || !_frame_rgba || !_packet) {
        return Err<void>("Failed to allocate frame/packet");
    }

    // Allocate RGBA frame buffer
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, _video_width, _video_height, 1);
    _frame_buffer.resize(num_bytes);

    av_image_fill_arrays(_frame_rgba->data, _frame_rgba->linesize,
                         _frame_buffer.data(), AV_PIX_FMT_RGBA,
                         _video_width, _video_height, 1);

    // Create swscale context for format conversion
    _sws_ctx = sws_getContext(
        _video_width, _video_height, _codec_ctx->pix_fmt,
        _video_width, _video_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!_sws_ctx) {
        return Err<void>("Failed to create swscale context");
    }

    // Decode first frame
    auto decRes = decodeNextFrame();
    if (!decRes) {
        std::cerr << "Warning: Failed to decode first frame: " << error_msg(decRes) << std::endl;
    }

    return Ok();
}

Result<void> VideoLayer::decodeNextFrame() {
    if (!_format_ctx || !_codec_ctx || !_frame || !_packet) {
        return Err<void>("FFmpeg not initialized");
    }

    while (true) {
        int ret = av_read_frame(_format_ctx, _packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (_loop) {
                    // Seek to beginning
                    av_seek_frame(_format_ctx, _video_stream_idx, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(_codec_ctx);
                    _current_time = 0.0;
                    continue;
                }
                return Err<void>("End of stream");
            }
            return Err<void>("Error reading frame");
        }

        if (_packet->stream_index != _video_stream_idx) {
            av_packet_unref(_packet);
            continue;
        }

        ret = avcodec_send_packet(_codec_ctx, _packet);
        av_packet_unref(_packet);

        if (ret < 0) {
            continue;  // Try next packet
        }

        ret = avcodec_receive_frame(_codec_ctx, _frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;  // Need more packets
        }
        if (ret < 0) {
            return Err<void>("Error decoding frame");
        }

        // Convert to RGBA
        sws_scale(_sws_ctx,
                  _frame->data, _frame->linesize,
                  0, _video_height,
                  _frame_rgba->data, _frame_rgba->linesize);

        // Update current time from PTS
        if (_frame->pts != AV_NOPTS_VALUE) {
            _current_time = _frame->pts * _time_base;
        }

        _frame_updated = true;
        return Ok();
    }
}

Result<void> VideoLayer::update(double deltaTime) {
    if (!_playing || _failed) return Ok();

    _accumulated_time += deltaTime;

    // Check if it's time for next frame
    if (_accumulated_time >= _frame_time) {
        _accumulated_time -= _frame_time;

        auto res = decodeNextFrame();
        if (!res && !_loop) {
            _playing = false;
        }
    }

    return Ok();
}

void VideoLayer::play() { _playing = true; }
void VideoLayer::pause() { _playing = false; }
void VideoLayer::stop() {
    _playing = false;
    seek(0.0);
}

void VideoLayer::seek(double seconds) {
    if (!_format_ctx || _video_stream_idx < 0) return;

    int64_t timestamp = static_cast<int64_t>(seconds / _time_base);
    av_seek_frame(_format_ctx, _video_stream_idx, timestamp, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(_codec_ctx);
    _current_time = seconds;
    _accumulated_time = 0.0;

    // Decode frame at new position
    (void)decodeNextFrame();
}

bool VideoLayer::onMouseButton(int button, bool pressed) {
    if (button == 0 && pressed) {
        // Left click toggles play/pause
        if (_playing) pause();
        else play();
        return true;
    }
    return false;
}

Result<void> VideoLayer::dispose() {
    // Release WebGPU resources
    if (_bind_group) { wgpuBindGroupRelease(_bind_group); _bind_group = nullptr; }
    if (_pipeline) { wgpuRenderPipelineRelease(_pipeline); _pipeline = nullptr; }
    if (_uniform_buffer) { wgpuBufferRelease(_uniform_buffer); _uniform_buffer = nullptr; }
    if (_sampler) { wgpuSamplerRelease(_sampler); _sampler = nullptr; }
    if (_texture_view) { wgpuTextureViewRelease(_texture_view); _texture_view = nullptr; }
    if (_texture) { wgpuTextureRelease(_texture); _texture = nullptr; }

    // Release FFmpeg resources
    if (_sws_ctx) { sws_freeContext(_sws_ctx); _sws_ctx = nullptr; }
    if (_frame) { av_frame_free(&_frame); }
    if (_frame_rgba) { av_frame_free(&_frame_rgba); }
    if (_packet) { av_packet_free(&_packet); }
    if (_codec_ctx) { avcodec_free_context(&_codec_ctx); }

    // Clean up custom I/O
    if (_format_ctx) {
        if (_format_ctx->pb) {
            // Free the opaque MemoryBuffer
            delete static_cast<MemoryBuffer*>(_format_ctx->pb->opaque);
            // av_freep will free the buffer
            av_freep(&_format_ctx->pb->buffer);
            avio_context_free(&_format_ctx->pb);
        }
        avformat_close_input(&_format_ctx);
    }

    _frame_buffer.clear();
    _input_data.clear();
    _gpu_initialized = false;

    return Ok();
}

void VideoLayer::updateTexture(WebGPUContext& ctx) {
    if (!_texture || !_frame_updated || _frame_buffer.empty()) return;

    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = _texture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = _video_width * 4;
    layout.rowsPerImage = _video_height;
    WGPUExtent3D extent = {static_cast<uint32_t>(_video_width),
                           static_cast<uint32_t>(_video_height), 1};

    wgpuQueueWriteTexture(ctx.getQueue(), &dst, _frame_buffer.data(),
                          _frame_buffer.size(), &layout, &extent);

    _frame_updated = false;
}

Result<void> VideoLayer::render(WebGPUContext& ctx,
                                 WGPUTextureView targetView, WGPUTextureFormat targetFormat,
                                 uint32_t screenWidth, uint32_t screenHeight,
                                 float pixelX, float pixelY, float pixelW, float pixelH) {
    if (_failed) return Err<void>("VideoLayer already failed");
    if (_frame_buffer.empty()) return Err<void>("VideoLayer has no frame data");

    if (!_gpu_initialized) {
        auto result = createPipeline(ctx, targetFormat);
        if (!result) {
            _failed = true;
            return Err<void>("Failed to create pipeline", result);
        }
        _gpu_initialized = true;
    }

    if (!_pipeline || !_uniform_buffer || !_bind_group) {
        _failed = true;
        return Err<void>("VideoLayer pipeline not initialized");
    }

    // Update texture with latest frame
    updateTexture(ctx);

    // Update uniforms
    float ndcX = (pixelX / screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / screenHeight) * 2.0f;
    float ndcW = (pixelW / screenWidth) * 2.0f;
    float ndcH = (pixelH / screenHeight) * 2.0f;

    struct Uniforms { float rect[4]; } uniforms;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;

    wgpuQueueWriteBuffer(ctx.getQueue(), _uniform_buffer, 0, &uniforms, sizeof(uniforms));

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

Result<void> VideoLayer::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = _video_width;
    texDesc.size.height = _video_height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    _texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!_texture) return Err<void>("Failed to create texture");

    // Upload initial frame
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = _texture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = _video_width * 4;
    layout.rowsPerImage = _video_height;
    WGPUExtent3D extent = {static_cast<uint32_t>(_video_width),
                           static_cast<uint32_t>(_video_height), 1};
    wgpuQueueWriteTexture(ctx.getQueue(), &dst, _frame_buffer.data(),
                          _frame_buffer.size(), &layout, &extent);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    _texture_view = wgpuTextureCreateView(_texture, &viewDesc);
    if (!_texture_view) return Err<void>("Failed to create texture view");

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    _sampler = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!_sampler) return Err<void>("Failed to create sampler");

    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    _uniform_buffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!_uniform_buffer) return Err<void>("Failed to create uniform buffer");

    // Shader (same as image plugin)
    const char* shaderCode = R"(
struct Uniforms { rect: vec4<f32>, }
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
struct VertexOutput { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32>, }
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> VertexOutput {
    var p = array<vec2<f32>,6>(vec2(0.,0.),vec2(1.,0.),vec2(1.,1.),vec2(0.,0.),vec2(1.,1.),vec2(0.,1.));
    let pos = p[vi];
    var o: VertexOutput;
    o.position = vec4(u.rect.x + pos.x * u.rect.z, u.rect.y - pos.y * u.rect.w, 0., 1.);
    o.uv = pos;
    return o;
}
@fragment fn fs_main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
    return textureSample(tex, texSampler, uv);
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
    entries[0].binding = 0; entries[0].visibility = WGPUShaderStage_Vertex;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[1].binding = 1; entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[2].binding = 2; entries[2].visibility = WGPUShaderStage_Fragment;
    entries[2].texture.sampleType = WGPUTextureSampleType_Float;
    entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 3; bglDesc.entries = entries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(device, &bglDesc);
    if (!bgl) { wgpuShaderModuleRelease(shaderModule); return Err<void>("Failed to create bgl"); }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1; plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &plDesc);

    // Bind group
    WGPUBindGroupEntry bgE[3] = {};
    bgE[0].binding = 0; bgE[0].buffer = _uniform_buffer; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = _sampler;
    bgE[2].binding = 2; bgE[2].textureView = _texture_view;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl; bgDesc.entryCount = 3; bgDesc.entries = bgE;
    _bind_group = wgpuDeviceCreateBindGroup(device, &bgDesc);

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = WGPU_STR("vs_main");
    WGPUFragmentState fragState = {};
    fragState.module = shaderModule; fragState.entryPoint = WGPU_STR("fs_main");
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = targetFormat; colorTarget.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState blend = {};
    blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_One;
    blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blend.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blend;
    fragState.targetCount = 1; fragState.targets = &colorTarget;
    pipelineDesc.fragment = &fragState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1; pipelineDesc.multisample.mask = ~0u;

    _pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!_pipeline) return Err<void>("Failed to create render pipeline");

    std::cout << "VideoLayer: pipeline created" << std::endl;
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* video_plugin_name() { return "video"; }
    yetty::Result<yetty::PluginPtr> video_plugin_create() { return yetty::VideoPlugin::create(); }
}
