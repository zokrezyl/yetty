/*
 * encoder.c - H.264 encoder wrapping openh264's C vtable API.
 *
 * openh264 exposes both a C++ virtual-class interface and a C COM-style
 * vtable interface (`ISVCEncoder = const ISVCEncoderVtbl*`). We use the C one
 * so this translation unit stays pure C and fits the rest of yetty-perf.
 * Calls are of the form:
 *     enc->Method(&enc, args);
 * where `enc` is the vtable pointer and `&enc` is the self handle passed as
 * the first argument to each vtable method.
 */

#include <yetty/yvcodec/encoder.h>
#include <yetty/yplatform/time.h>
#include <yetty/ytrace/ytrace.h>

#include <wels/codec_api.h>

#include <stdlib.h>
#include <string.h>

/* openh264 log → ytrace. Level thresholds mirror openh264 internal values
 * (error < warn < info < debug). We only keep warnings and above — info from
 * openh264 is too noisy for the steady-state frame loop. */
static void openh264_trace_cb(void *ctx, int level, const char *msg)
{
    (void)ctx;
    /* Strip trailing newlines without allocating. */
    size_t len = strlen(msg);
    while (len > 0 && (msg[len - 1] == '\n' || msg[len - 1] == '\r')) {
        len--;
    }
    char buf[256];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, msg, len);
    buf[len] = '\0';

    if (level <= WELS_LOG_ERROR) {
        yerror("[openh264] %s", buf);
    } else if (level <= WELS_LOG_WARNING) {
        ywarn("[openh264] %s", buf);
    } else {
        ydebug("[openh264] %s", buf);
    }
}

struct yetty_yvcodec_encoder {
    /* Handle = pointer to vtable pointer (COM-style). `ISVCEncoder` is
     * already a `const ISVCEncoderVtbl*`, so the actual encoder handle is
     * one more indirection up. Call methods as `(*h264)->Method(h264, ...)`. */
    ISVCEncoder *h264;
    struct yetty_yvcodec_encoder_config cfg;
    uint32_t frame_index;
    int force_idr;
    double start_time_sec;

    /* Bitstream scratch. openh264 returns NAL pointers into its own buffers
     * that only stay stable until the next call — stitch them here so the
     * caller gets a simple (data, size) pair with predictable lifetime. */
    uint8_t *out_buf;
    size_t out_size;
    size_t out_cap;
};

void yetty_yvcodec_encoder_config_defaults(struct yetty_yvcodec_encoder_config *cfg, uint32_t width,
                                           uint32_t height)
{
    if (!cfg) {
        return;
    }
    cfg->width = width;
    cfg->height = height;
    /* Auto-scale bitrate with pixel count: ~0.1 bits/pixel/frame at 30 fps,
     * clamped to [4, 40] Mbps. At 2696x1576 this lands around 12.7 Mbps —
     * enough to keep text edges crisp. Callers can still override via
     * --vnc-h264-bitrate / vnc/h264/bitrate before handing the cfg in. */
    double px_per_frame = (double)width * (double)height;
    uint32_t bps = (uint32_t)(px_per_frame * 0.1 * 30.0);
    if (bps < 4000000u) {
        bps = 4000000u;
    }
    if (bps > 40000000u) {
        bps = 40000000u;
    }
    cfg->bitrate = bps;
    cfg->frame_rate = 30.0f;
    cfg->idr_interval = 60; /* keyframe every 2 s at 30 fps */
    cfg->screen_content = true;
}

struct yetty_yvcodec_encoder_ptr_result yetty_yvcodec_encoder_config_encoder_create(
    const struct yetty_yvcodec_encoder_config *cfg)
{
    if (!cfg || cfg->width == 0 || cfg->height == 0 || (cfg->width & 1) || (cfg->height & 1)) {
        return YETTY_ERR(yetty_yvcodec_encoder_ptr,
                         "invalid encoder config (dims must be >0 and even)");
    }

    struct yetty_yvcodec_encoder *enc = calloc(1, sizeof(*enc));
    if (!enc) {
        return YETTY_ERR(yetty_yvcodec_encoder_ptr, "encoder alloc failed");
    }
    enc->cfg = *cfg;

    if (WelsCreateSVCEncoder(&enc->h264) != 0 || !enc->h264) {
        free(enc);
        return YETTY_ERR(yetty_yvcodec_encoder_ptr, "WelsCreateSVCEncoder failed");
    }

    WelsTraceCallback cb = openh264_trace_cb;
    (*enc->h264)->SetOption(enc->h264, ENCODER_OPTION_TRACE_CALLBACK, &cb);
    int trace_level = WELS_LOG_WARNING;
    (*enc->h264)->SetOption(enc->h264, ENCODER_OPTION_TRACE_LEVEL, &trace_level);

    SEncParamExt param;
    (*enc->h264)->GetDefaultParams(enc->h264, &param);
    param.iPicWidth = (int)cfg->width;
    param.iPicHeight = (int)cfg->height;
    param.iTargetBitrate = (int)cfg->bitrate;
    param.iMaxBitrate = (int)cfg->bitrate * 2;
    param.fMaxFrameRate = cfg->frame_rate;
    param.iUsageType = cfg->screen_content ? SCREEN_CONTENT_REAL_TIME : CAMERA_VIDEO_REAL_TIME;
    param.iSpatialLayerNum = 1;
    param.iTemporalLayerNum = 1;
    param.sSpatialLayers[0].iVideoWidth = (int)cfg->width;
    param.sSpatialLayers[0].iVideoHeight = (int)cfg->height;
    param.sSpatialLayers[0].fFrameRate = cfg->frame_rate;
    param.sSpatialLayers[0].iSpatialBitrate = (int)cfg->bitrate;
    param.sSpatialLayers[0].iMaxSpatialBitrate = (int)cfg->bitrate * 2;
    param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;
    param.sSpatialLayers[0].uiLevelIdc = LEVEL_3_1;
    param.sSpatialLayers[0].sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    param.iRCMode = RC_BITRATE_MODE;
    param.iPaddingFlag = 0;
    param.uiIntraPeriod = (unsigned int)cfg->idr_interval;
    param.bEnableFrameSkip = false;
    param.iEntropyCodingModeFlag = 0; /* CAVLC — baseline profile */
    param.iMultipleThreadIdc = 1;     /* single-thread: lowest latency */
    param.bEnableLongTermReference = false;

    if ((*enc->h264)->InitializeExt(enc->h264, &param) != 0) {
        WelsDestroySVCEncoder(enc->h264);
        free(enc);
        return YETTY_ERR(yetty_yvcodec_encoder_ptr, "InitializeExt failed");
    }

    int video_format = videoFormatI420;
    (*enc->h264)->SetOption(enc->h264, ENCODER_OPTION_DATAFORMAT, &video_format);
    int prepend = 1;
    (*enc->h264)->SetOption(enc->h264, ENCODER_OPTION_SPS_PPS_ID_STRATEGY, &prepend);

    enc->start_time_sec = yetty_yplatform_ytime_monotonic_sec();

    yinfo("yvcodec: encoder %ux%u @ %.1ffps, %u kbps, IDR every %u, screen=%d", cfg->width,
          cfg->height, cfg->frame_rate, cfg->bitrate / 1000, cfg->idr_interval,
          (int)cfg->screen_content);

    return YETTY_OK(yetty_yvcodec_encoder_ptr, enc);
}

void yetty_yvcodec_encoder_destroy(struct yetty_yvcodec_encoder *enc)
{
    if (!enc) {
        return;
    }
    if (enc->h264) {
        (*enc->h264)->Uninitialize(enc->h264);
        WelsDestroySVCEncoder(enc->h264);
    }
    free(enc->out_buf);
    free(enc);
}

/* Grow the bitstream scratch buffer to at least `need` bytes. */
static int ensure_out_cap(struct yetty_yvcodec_encoder *enc, size_t need)
{
    if (enc->out_cap >= need) {
        return 1;
    }
    size_t new_cap = enc->out_cap ? enc->out_cap : 64 * 1024;
    while (new_cap < need) {
        new_cap *= 2;
    }
    uint8_t *nb = realloc(enc->out_buf, new_cap);
    if (!nb) {
        return 0;
    }
    enc->out_buf = nb;
    enc->out_cap = new_cap;
    return 1;
}

struct yetty_ycore_void_result yetty_yvcodec_encoder_encode(struct yetty_yvcodec_encoder *enc,
                                                            const uint8_t *y_plane,
                                                            const uint8_t *u_plane,
                                                            const uint8_t *v_plane,
                                                            uint32_t y_stride, uint32_t uv_stride,
                                                            struct yetty_yvcodec_encoded_frame *out)
{
    if (!enc || !enc->h264 || !y_plane || !u_plane || !v_plane || !out) {
        return YETTY_ERR(yetty_ycore_void, "null args");
    }

    SSourcePicture src = {0};
    src.iColorFormat = videoFormatI420;
    src.iPicWidth = (int)enc->cfg.width;
    src.iPicHeight = (int)enc->cfg.height;
    src.iStride[0] = (int)y_stride;
    src.iStride[1] = (int)uv_stride;
    src.iStride[2] = (int)uv_stride;
    /* openh264 declares pData as uint8_t* (non-const) but doesn't write to
     * the source frames during encode; the cast is safe. */
    src.pData[0] = (uint8_t *)y_plane;
    src.pData[1] = (uint8_t *)u_plane;
    src.pData[2] = (uint8_t *)v_plane;

    double now = yetty_yplatform_ytime_monotonic_sec();
    uint64_t ts_us = (uint64_t)((now - enc->start_time_sec) * 1e6);
    src.uiTimeStamp = (long long)ts_us;

    if (enc->force_idr) {
        (*enc->h264)->ForceIntraFrame(enc->h264, true);
        enc->force_idr = 0;
    }

    SFrameBSInfo info = {0};
    if ((*enc->h264)->EncodeFrame(enc->h264, &src, &info) != 0) {
        return YETTY_ERR(yetty_ycore_void, "EncodeFrame failed");
    }

    /* Rate-control skip: no bytes produced — signal via size=0. */
    if (info.eFrameType == videoFrameTypeSkip) {
        out->data = NULL;
        out->size = 0;
        out->is_idr = false;
        out->timestamp_us = ts_us;
        out->frame_index = enc->frame_index++;
        return YETTY_OK_VOID();
    }

    /* Stitch all NALs across all layers into our owned scratch buffer. */
    enc->out_size = 0;
    for (int l = 0; l < info.iLayerNum; l++) {
        const SLayerBSInfo *li = &info.sLayerInfo[l];
        size_t layer_size = 0;
        for (int n = 0; n < li->iNalCount; n++) {
            layer_size += (size_t)li->pNalLengthInByte[n];
        }
        if (!ensure_out_cap(enc, enc->out_size + layer_size)) {
            return YETTY_ERR(yetty_ycore_void, "out buf alloc failed");
        }
        memcpy(enc->out_buf + enc->out_size, li->pBsBuf, layer_size);
        enc->out_size += layer_size;
    }

    out->data = enc->out_buf;
    out->size = enc->out_size;
    out->is_idr = (info.eFrameType == videoFrameTypeIDR);
    out->timestamp_us = ts_us;
    out->frame_index = enc->frame_index++;
    return YETTY_OK_VOID();
}

void yetty_yvcodec_encoder_force_idr(struct yetty_yvcodec_encoder *enc)
{
    if (enc) {
        enc->force_idr = 1;
    }
}

struct yetty_ycore_void_result yetty_yvcodec_encoder_set_bitrate(struct yetty_yvcodec_encoder *enc,
                                                                 uint32_t bitrate)
{
    if (!enc || !enc->h264) {
        return YETTY_ERR(yetty_ycore_void, "null encoder");
    }
    SBitrateInfo bi = {0};
    bi.iLayer = SPATIAL_LAYER_ALL;
    bi.iBitrate = (int)bitrate;
    if ((*enc->h264)->SetOption(enc->h264, ENCODER_OPTION_BITRATE, &bi) != 0) {
        return YETTY_ERR(yetty_ycore_void, "set bitrate failed");
    }
    enc->cfg.bitrate = bitrate;
    return YETTY_OK_VOID();
}

uint32_t yetty_yvcodec_encoder_width(const struct yetty_yvcodec_encoder *enc)
{
    return enc ? enc->cfg.width : 0;
}

uint32_t yetty_yvcodec_encoder_height(const struct yetty_yvcodec_encoder *enc)
{
    return enc ? enc->cfg.height : 0;
}

/*---------------------------------------------------------------------------
 * BGRA → YUV420 (BT.709, video range). Scalar C path — sub-ms at typical
 * terminal resolutions. Ported from yetty-poc's convertBgraToYuv420Cpu.
 *-------------------------------------------------------------------------*/
void yetty_yvcodec_bgra_to_yuv420(const uint8_t *bgra, uint32_t width, uint32_t height,
                                  uint32_t bgra_stride, uint8_t *y_plane, uint8_t *u_plane,
                                  uint8_t *v_plane, uint32_t y_stride, uint32_t uv_stride)
{
    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = bgra + (size_t)y * bgra_stride;
        uint8_t *y_row = y_plane + (size_t)y * y_stride;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            int yv = 16 + ((66 * r + 129 * g + 25 * b + 128) >> 8);
            if (yv < 16) {
                yv = 16;
            }
            if (yv > 235) {
                yv = 235;
            }
            y_row[x] = (uint8_t)yv;
        }
    }

    uint32_t uv_height = height / 2;
    uint32_t uv_width = width / 2;
    for (uint32_t uv_y = 0; uv_y < uv_height; uv_y++) {
        uint8_t *u_row = u_plane + (size_t)uv_y * uv_stride;
        uint8_t *v_row = v_plane + (size_t)uv_y * uv_stride;
        for (uint32_t uv_x = 0; uv_x < uv_width; uv_x++) {
            uint32_t sx = uv_x * 2;
            uint32_t sy = uv_y * 2;
            int sum_r = 0, sum_g = 0, sum_b = 0;
            for (int dy = 0; dy < 2 && sy + (uint32_t)dy < height; dy++) {
                const uint8_t *row = bgra + (size_t)(sy + dy) * bgra_stride;
                for (int dx = 0; dx < 2 && sx + (uint32_t)dx < width; dx++) {
                    sum_b += row[(sx + dx) * 4 + 0];
                    sum_g += row[(sx + dx) * 4 + 1];
                    sum_r += row[(sx + dx) * 4 + 2];
                }
            }
            int avg_r = sum_r / 4;
            int avg_g = sum_g / 4;
            int avg_b = sum_b / 4;
            int cb = 128 + ((-38 * avg_r - 74 * avg_g + 112 * avg_b + 128) >> 8);
            int cr = 128 + ((112 * avg_r - 94 * avg_g - 18 * avg_b + 128) >> 8);
            if (cb < 16) {
                cb = 16;
            }
            if (cb > 240) {
                cb = 240;
            }
            if (cr < 16) {
                cr = 16;
            }
            if (cr > 240) {
                cr = 240;
            }
            u_row[uv_x] = (uint8_t)cb;
            v_row[uv_x] = (uint8_t)cr;
        }
    }
}
