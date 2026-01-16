#include "video.h"
#include <yetty/yetty.h>
#include <yetty/webgpu-context.h>
#include <yetty/wgpu-compat.h>
#include <ytrace/ytrace.hpp>
#include <ytrace/ytrace.hpp>
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

VideoPlugin::~VideoPlugin() { (void)dispose(); }

Result<PluginPtr> VideoPlugin::create() noexcept {
    auto p = PluginPtr(new VideoPlugin());
    if (auto res = static_cast<VideoPlugin*>(p.get())->pluginInit(); !res) {
        return Err<PluginPtr>("Failed to init VideoPlugin", res);
    }
    return Ok(p);
}

Result<void> VideoPlugin::pluginInit() noexcept {
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

Result<WidgetPtr> VideoPlugin::createWidget(
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
    yinfo("payload size={} x={} y={} w={} h={}", payload.size(), x, y, widthCells, heightCells);
    return Video::create(factory, fontManager, loop, x, y, widthCells, heightCells, pluginArgs, payload);
}

//-----------------------------------------------------------------------------
// Video
//-----------------------------------------------------------------------------

Video::~Video() { (void)dispose(); }

Result<void> Video::init() {
    if (_payload.empty()) {
        return Err<void>("Video: empty payload");
    }

    (void)dispose();

    auto result = initFFmpeg(_payload);
    if (!result) {
        return result;
    }

    std::cout << "Video: loaded " << videoWidth_ << "x" << videoHeight_
              << " @ " << frameRate_ << " fps, duration=" << duration_ << "s" << std::endl;
    return Ok();
}

Result<void> Video::initFFmpeg(const std::string& filepath) {
    // Store file path
    filePath_ = filepath;

    // Open input file directly
    int ret = avformat_open_input(&formatCtx_, filepath.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        return Err<void>(std::string("Failed to open video file: ") + errbuf);
    }

    // Find stream info
    ret = avformat_find_stream_info(formatCtx_, nullptr);
    if (ret < 0) {
        return Err<void>("Failed to find stream info");
    }

    // Find video stream
    videoStreamIdx_ = -1;
    const AVCodec* codec = nullptr;
    for (unsigned i = 0; i < formatCtx_->nb_streams; i++) {
        if (formatCtx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx_ = static_cast<int>(i);
            codec = avcodec_find_decoder(formatCtx_->streams[i]->codecpar->codec_id);
            break;
        }
    }

    if (videoStreamIdx_ < 0 || !codec) {
        return Err<void>("No video stream found");
    }

    // Allocate codec context
    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) {
        return Err<void>("Failed to allocate codec context");
    }

    AVStream* stream = formatCtx_->streams[videoStreamIdx_];
    ret = avcodec_parameters_to_context(codecCtx_, stream->codecpar);
    if (ret < 0) {
        return Err<void>("Failed to copy codec parameters");
    }

    // Open codec
    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) {
        return Err<void>("Failed to open codec");
    }

    // Get video properties
    videoWidth_ = codecCtx_->width;
    videoHeight_ = codecCtx_->height;

    // Calculate frame rate
    AVRational fr = stream->avg_frame_rate;
    if (fr.num > 0 && fr.den > 0) {
        frameRate_ = av_q2d(fr);
    } else {
        fr = stream->r_frame_rate;
        if (fr.num > 0 && fr.den > 0) {
            frameRate_ = av_q2d(fr);
        }
    }
    frameTime_ = 1.0 / frameRate_;

    // Time base for seeking
    timeBase_ = av_q2d(stream->time_base);

    // Duration
    if (stream->duration != AV_NOPTS_VALUE) {
        duration_ = stream->duration * timeBase_;
    } else if (formatCtx_->duration != AV_NOPTS_VALUE) {
        duration_ = formatCtx_->duration / static_cast<double>(AV_TIME_BASE);
    }

    // Allocate frames
    frame_ = av_frame_alloc();
    frameRgba_ = av_frame_alloc();
    packet_ = av_packet_alloc();

    if (!frame_ || !frameRgba_ || !packet_) {
        return Err<void>("Failed to allocate frame/packet");
    }

    // Allocate RGBA frame buffer
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, videoWidth_, videoHeight_, 1);
    frameBuffer_.resize(num_bytes);

    av_image_fill_arrays(frameRgba_->data, frameRgba_->linesize,
                         frameBuffer_.data(), AV_PIX_FMT_RGBA,
                         videoWidth_, videoHeight_, 1);

    // Create swscale context for format conversion
    swsCtx_ = sws_getContext(
        videoWidth_, videoHeight_, codecCtx_->pix_fmt,
        videoWidth_, videoHeight_, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx_) {
        return Err<void>("Failed to create swscale context");
    }

    // Decode first frame
    auto decRes = decodeNextFrame();
    if (!decRes) {
        std::cerr << "Warning: Failed to decode first frame: " << error_msg(decRes) << std::endl;
    }

    return Ok();
}

Result<void> Video::decodeNextFrame() {
    if (!formatCtx_ || !codecCtx_ || !frame_ || !packet_) {
        return Err<void>("FFmpeg not initialized");
    }

    while (true) {
        int ret = av_read_frame(formatCtx_, packet_);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                if (loop_) {
                    // Seek to beginning
                    av_seek_frame(formatCtx_, videoStreamIdx_, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(codecCtx_);
                    currentTime_ = 0.0;
                    continue;
                }
                return Err<void>("End of stream");
            }
            return Err<void>("Error reading frame");
        }

        if (packet_->stream_index != videoStreamIdx_) {
            av_packet_unref(packet_);
            continue;
        }

        ret = avcodec_send_packet(codecCtx_, packet_);
        av_packet_unref(packet_);

        if (ret < 0) {
            continue;  // Try next packet
        }

        ret = avcodec_receive_frame(codecCtx_, frame_);
        if (ret == AVERROR(EAGAIN)) {
            continue;  // Need more packets
        }
        if (ret < 0) {
            return Err<void>("Error decoding frame");
        }

        // Convert to RGBA
        sws_scale(swsCtx_,
                  frame_->data, frame_->linesize,
                  0, videoHeight_,
                  frameRgba_->data, frameRgba_->linesize);

        // Update current time from PTS
        if (frame_->pts != AV_NOPTS_VALUE) {
            currentTime_ = frame_->pts * timeBase_;
        }

        frameUpdated_ = true;
        return Ok();
    }
}

void Video::play() { playing_ = true; }
void Video::pause() { playing_ = false; }
void Video::stop() {
    playing_ = false;
    seek(0.0);
}

void Video::seek(double seconds) {
    if (!formatCtx_ || videoStreamIdx_ < 0) return;

    int64_t timestamp = static_cast<int64_t>(seconds / timeBase_);
    av_seek_frame(formatCtx_, videoStreamIdx_, timestamp, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx_);
    currentTime_ = seconds;
    accumulatedTime_ = 0.0;

    // Decode frame at new position
    (void)decodeNextFrame();
}

bool Video::onMouseButton(int button, bool pressed) {
    if (button == 0 && pressed) {
        // Left click toggles play/pause
        if (playing_) pause();
        else play();
        return true;
    }
    return false;
}

Result<void> Video::dispose() {
    // Release WebGPU resources
    if (bindGroup_) { wgpuBindGroupRelease(bindGroup_); bindGroup_ = nullptr; }
    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (uniformBuffer_) { wgpuBufferRelease(uniformBuffer_); uniformBuffer_ = nullptr; }
    if (sampler_) { wgpuSamplerRelease(sampler_); sampler_ = nullptr; }
    if (textureView_) { wgpuTextureViewRelease(textureView_); textureView_ = nullptr; }
    if (texture_) { wgpuTextureRelease(texture_); texture_ = nullptr; }

    // Release FFmpeg resources
    if (swsCtx_) { sws_freeContext(swsCtx_); swsCtx_ = nullptr; }
    if (frame_) { av_frame_free(&frame_); }
    if (frameRgba_) { av_frame_free(&frameRgba_); }
    if (packet_) { av_packet_free(&packet_); }
    if (codecCtx_) { avcodec_free_context(&codecCtx_); }

    if (formatCtx_) {
        avformat_close_input(&formatCtx_);
    }

    frameBuffer_.clear();
    filePath_.clear();
    gpuInitialized_ = false;

    return Ok();
}

void Video::updateTexture(WebGPUContext& ctx) {
    if (!texture_ || !frameUpdated_ || frameBuffer_.empty()) return;

    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = texture_;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = videoWidth_ * 4;
    layout.rowsPerImage = videoHeight_;
    WGPUExtent3D extent = {static_cast<uint32_t>(videoWidth_),
                           static_cast<uint32_t>(videoHeight_), 1};

    wgpuQueueWriteTexture(ctx.getQueue(), &dst, frameBuffer_.data(),
                          frameBuffer_.size(), &layout, &extent);

    frameUpdated_ = false;
}

void Video::prepareFrame(WebGPUContext& ctx) {
    // Video uses its own command encoder/pass, so render during prepareFrame
    if (failed_ || !_visible) return;
    if (frameBuffer_.empty()) {
        yerror("Video has no frame data");
        return;
    }

    // Get render context set by owner
    const auto& rc = _renderCtx;

    // Update playback (integrate former update() logic)
    if (playing_) {
        accumulatedTime_ += rc.deltaTime;
        if (accumulatedTime_ >= frameTime_) {
            accumulatedTime_ -= frameTime_;
            auto res = decodeNextFrame();
            if (!res && !loop_) {
                playing_ = false;
            }
        }
    }

    if (!gpuInitialized_) {
        auto result = createPipeline(ctx, rc.targetFormat);
        if (!result) {
            failed_ = true;
            yerror("Video: Failed to create pipeline: {}", result.error().message());
            return;
        }
        gpuInitialized_ = true;
    }

    if (!pipeline_ || !uniformBuffer_ || !bindGroup_) {
        failed_ = true;
        yerror("Video pipeline not initialized");
        return;
    }

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

    // Update texture with latest frame
    updateTexture(ctx);

    // Update uniforms
    float ndcX = (pixelX / rc.screenWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (pixelY / rc.screenHeight) * 2.0f;
    float ndcW = (pixelW / rc.screenWidth) * 2.0f;
    float ndcH = (pixelH / rc.screenHeight) * 2.0f;

    struct Uniforms { float rect[4]; } uniforms;
    uniforms.rect[0] = ndcX;
    uniforms.rect[1] = ndcY;
    uniforms.rect[2] = ndcW;
    uniforms.rect[3] = ndcH;

    wgpuQueueWriteBuffer(ctx.getQueue(), uniformBuffer_, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.getDevice(), &encoderDesc);
    if (!encoder) {
        yerror("Video: Failed to create command encoder");
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
        yerror("Video: Failed to begin render pass");
        return;
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
}

Result<void> Video::render(WGPURenderPassEncoder pass, WebGPUContext& ctx) {
    (void)pass;
    (void)ctx;
    // Video renders in prepareFrame() with its own pass, nothing to do here
    return Ok();
}

Result<void> Video::createPipeline(WebGPUContext& ctx, WGPUTextureFormat targetFormat) {
    WGPUDevice device = ctx.getDevice();

    // Create texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = videoWidth_;
    texDesc.size.height = videoHeight_;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    texture_ = wgpuDeviceCreateTexture(device, &texDesc);
    if (!texture_) return Err<void>("Failed to create texture");

    // Upload initial frame
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = texture_;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = videoWidth_ * 4;
    layout.rowsPerImage = videoHeight_;
    WGPUExtent3D extent = {static_cast<uint32_t>(videoWidth_),
                           static_cast<uint32_t>(videoHeight_), 1};
    wgpuQueueWriteTexture(ctx.getQueue(), &dst, frameBuffer_.data(),
                          frameBuffer_.size(), &layout, &extent);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
    if (!textureView_) return Err<void>("Failed to create texture view");

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    if (!sampler_) return Err<void>("Failed to create sampler");

    // Create uniform buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(device, &bufDesc);
    if (!uniformBuffer_) return Err<void>("Failed to create uniform buffer");

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
    bgE[0].binding = 0; bgE[0].buffer = uniformBuffer_; bgE[0].size = 16;
    bgE[1].binding = 1; bgE[1].sampler = sampler_;
    bgE[2].binding = 2; bgE[2].textureView = textureView_;
    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl; bgDesc.entryCount = 3; bgDesc.entries = bgE;
    bindGroup_ = wgpuDeviceCreateBindGroup(device, &bgDesc);

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

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) return Err<void>("Failed to create render pipeline");

    std::cout << "Video: pipeline created" << std::endl;
    return Ok();
}

} // namespace yetty

extern "C" {
    const char* name() { return "video"; }
    yetty::Result<yetty::PluginPtr> create() { return yetty::VideoPlugin::create(); }
}
