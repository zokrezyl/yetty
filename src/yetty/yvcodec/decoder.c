/*
 * decoder.c - H.264 decoder wrapping openh264's C vtable API.
 *
 * Same C COM-style pattern as encoder.c: `dec` is a vtable pointer,
 * methods are called as `dec->Method(&dec, args)`.
 */

#include <yetty/yvcodec/decoder.h>
#include <yetty/ytrace/ytrace.h>

#include <wels/codec_api.h>

#include <stdlib.h>
#include <string.h>

static void openh264_trace_cb(void *ctx, int level, const char *msg)
{
    (void)ctx;
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

struct yetty_yvcodec_decoder {
    /* Handle = pointer to vtable pointer. See note in encoder.c. */
    ISVCDecoder *h264;

    /* Incoming NALs buffered by feed(), consumed by get_frame(). openh264
     * processes them in a single call. */
    uint8_t *pending;
    size_t pending_size;
    size_t pending_cap;

    /* Per-frame YUV copies. openh264's DecodeFrameNoDelay returns pointers
     * into its internal buffers that get clobbered on the next decode, so
     * we copy out — callers then get stable pointers until their next
     * feed/get_frame call. */
    uint8_t *y_buf;
    size_t y_buf_cap;
    uint8_t *u_buf;
    size_t u_buf_cap;
    uint8_t *v_buf;
    size_t v_buf_cap;

    struct yetty_yvcodec_yuv_frame current_frame;
};

static int ensure_cap(uint8_t **buf, size_t *cap, size_t need)
{
    if (*cap >= need) {
        return 1;
    }
    uint8_t *nb = realloc(*buf, need);
    if (!nb) {
        return 0;
    }
    *buf = nb;
    *cap = need;
    return 1;
}

struct yetty_yvcodec_decoder_ptr_result yetty_yvcodec_decoder_create_h264(void)
{
    struct yetty_yvcodec_decoder *dec = calloc(1, sizeof(*dec));
    if (!dec) {
        return YETTY_ERR(yetty_yvcodec_decoder_ptr, "decoder alloc failed");
    }

    if (WelsCreateDecoder(&dec->h264) != 0 || !dec->h264) {
        free(dec);
        return YETTY_ERR(yetty_yvcodec_decoder_ptr, "WelsCreateDecoder failed");
    }

    WelsTraceCallback cb = openh264_trace_cb;
    (*dec->h264)->SetOption(dec->h264, DECODER_OPTION_TRACE_CALLBACK, &cb);
    int trace_level = WELS_LOG_WARNING;
    (*dec->h264)->SetOption(dec->h264, DECODER_OPTION_TRACE_LEVEL, &trace_level);

    SDecodingParam param = {0};
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    param.bParseOnly = false;

    if ((*dec->h264)->Initialize(dec->h264, &param) != 0) {
        WelsDestroyDecoder(dec->h264);
        free(dec);
        return YETTY_ERR(yetty_yvcodec_decoder_ptr, "decoder Initialize failed");
    }

    ydebug("yvcodec: H.264 decoder initialized");
    return YETTY_OK(yetty_yvcodec_decoder_ptr, dec);
}

void yetty_yvcodec_decoder_destroy(struct yetty_yvcodec_decoder *dec)
{
    if (!dec) {
        return;
    }
    if (dec->h264) {
        (*dec->h264)->Uninitialize(dec->h264);
        WelsDestroyDecoder(dec->h264);
    }
    free(dec->pending);
    free(dec->y_buf);
    free(dec->u_buf);
    free(dec->v_buf);
    free(dec);
}

struct yetty_ycore_void_result yetty_yvcodec_decoder_feed(struct yetty_yvcodec_decoder *dec,
                                                         const uint8_t *data, size_t size)
{
    if (!dec || !dec->h264) {
        return YETTY_ERR(yetty_ycore_void, "decoder not initialized");
    }
    if (!data || size == 0) {
        return YETTY_OK_VOID();
    }

    if (!ensure_cap(&dec->pending, &dec->pending_cap, size)) {
        return YETTY_ERR(yetty_ycore_void, "pending buf alloc failed");
    }
    memcpy(dec->pending, data, size);
    dec->pending_size = size;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvcodec_decoder_get_frame(struct yetty_yvcodec_decoder *dec,
                                                              struct yetty_yvcodec_yuv_frame *out,
                                                              bool *out_has_frame)
{
    if (!dec || !dec->h264 || !out || !out_has_frame) {
        return YETTY_ERR(yetty_ycore_void, "null args");
    }

    *out_has_frame = false;

    if (dec->pending_size == 0) {
        return YETTY_OK_VOID();
    }

    uint8_t *yuv_ptrs[3] = {NULL, NULL, NULL};
    SBufferInfo buf_info = {0};

    DECODING_STATE state = (*dec->h264)
                               ->DecodeFrameNoDelay(dec->h264, dec->pending, (int)dec->pending_size,
                                                    yuv_ptrs, &buf_info);

    dec->pending_size = 0;

    if (state != dsErrorFree) {
        return YETTY_OK_VOID(); /* need more data — not an error */
    }
    if (buf_info.iBufferStatus != 1) {
        return YETTY_OK_VOID();
    }
    if (!yuv_ptrs[0] || !yuv_ptrs[1] || !yuv_ptrs[2]) {
        return YETTY_OK_VOID();
    }

    int width = buf_info.UsrData.sSystemBuffer.iWidth;
    int height = buf_info.UsrData.sSystemBuffer.iHeight;
    int y_stride = buf_info.UsrData.sSystemBuffer.iStride[0];
    int uv_stride = buf_info.UsrData.sSystemBuffer.iStride[1];

    size_t y_size = (size_t)y_stride * height;
    size_t uv_size = (size_t)uv_stride * (height / 2);

    if (!ensure_cap(&dec->y_buf, &dec->y_buf_cap, y_size) ||
        !ensure_cap(&dec->u_buf, &dec->u_buf_cap, uv_size) ||
        !ensure_cap(&dec->v_buf, &dec->v_buf_cap, uv_size)) {
        return YETTY_ERR(yetty_ycore_void, "yuv buf alloc failed");
    }

    memcpy(dec->y_buf, yuv_ptrs[0], y_size);
    memcpy(dec->u_buf, yuv_ptrs[1], uv_size);
    memcpy(dec->v_buf, yuv_ptrs[2], uv_size);

    dec->current_frame.y_plane = dec->y_buf;
    dec->current_frame.y_stride = (uint32_t)y_stride;
    dec->current_frame.u_plane = dec->u_buf;
    dec->current_frame.u_stride = (uint32_t)uv_stride;
    dec->current_frame.v_plane = dec->v_buf;
    dec->current_frame.v_stride = (uint32_t)uv_stride;
    dec->current_frame.width = (uint32_t)width;
    dec->current_frame.height = (uint32_t)height;
    dec->current_frame.color_matrix = YETTY_YVCODEC_COLOR_BT709;

    *out = dec->current_frame;
    *out_has_frame = true;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvcodec_decoder_flush(struct yetty_yvcodec_decoder *dec)
{
    (void)dec;
    /* openh264 DecodeFrameNoDelay doesn't hold reordered frames; no flush
     * queue to drain. */
    return YETTY_OK_VOID();
}

void yetty_yvcodec_decoder_reset(struct yetty_yvcodec_decoder *dec)
{
    if (!dec) {
        return;
    }
    dec->pending_size = 0;
    memset(&dec->current_frame, 0, sizeof(dec->current_frame));
}

/*---------------------------------------------------------------------------
 * YUV420 → BGRA8. Inverse of the encoder's BGRA→YUV helper. Fixed-point,
 * full-range output. Supports BT.601 and BT.709 coefficients.
 *-------------------------------------------------------------------------*/
void yetty_yvcodec_yuv_frame_yuv420_to_bgra(const struct yetty_yvcodec_yuv_frame *frame,
                                           uint8_t *bgra_out)
{
    if (!frame || !bgra_out || !frame->y_plane || !frame->u_plane || !frame->v_plane) {
        return;
    }

    int Kr_num, Kb_num, Kgr_num, Kgb_num;
    if (frame->color_matrix == YETTY_YVCODEC_COLOR_BT601) {
        Kr_num = 359;
        Kb_num = 454;
        Kgr_num = 183;
        Kgb_num = 88;
    } else {
        /* BT.709 (and BT.2020 at this precision) */
        Kr_num = 403;
        Kb_num = 475;
        Kgr_num = 120;
        Kgb_num = 48;
    }

    uint32_t w = frame->width;
    uint32_t h = frame->height;

    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *y_row = frame->y_plane + (size_t)y * frame->y_stride;
        const uint8_t *u_row = frame->u_plane + (size_t)(y >> 1) * frame->u_stride;
        const uint8_t *v_row = frame->v_plane + (size_t)(y >> 1) * frame->v_stride;
        uint8_t *dst_row = bgra_out + (size_t)y * w * 4;

        for (uint32_t x = 0; x < w; x++) {
            int Y = (int)y_row[x] - 16;
            int U = (int)u_row[x >> 1] - 128;
            int V = (int)v_row[x >> 1] - 128;

            int c = 298 * Y;
            int r = (c + Kr_num * V + 128) >> 8;
            int g = (c - Kgr_num * V - Kgb_num * U + 128) >> 8;
            int b = (c + Kb_num * U + 128) >> 8;

            if (r < 0) {
                r = 0;
            } else if (r > 255) {
                r = 255;
            }
            if (g < 0) {
                g = 0;
            } else if (g > 255) {
                g = 255;
            }
            if (b < 0) {
                b = 0;
            } else if (b > 255) {
                b = 255;
            }

            dst_row[x * 4 + 0] = (uint8_t)b;
            dst_row[x * 4 + 1] = (uint8_t)g;
            dst_row[x * 4 + 2] = (uint8_t)r;
            dst_row[x * 4 + 3] = 255;
        }
    }
}
