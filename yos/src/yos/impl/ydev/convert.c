/*
 * ydev_convert_to_i420 — NV12 plane shuffle to I420.
 *
 * Almost every mobile camera delivers NV12 (Y plane, then interleaved UV
 * plane); most encoders want I420 (Y, U, V as three planar planes). This
 * is a hot path so we walk the UV pairs straight without any allocations.
 *
 * Anything fancier — scaling, color-space conversion, rotation — stays
 * out of scope; pull in libyuv for that.
 */

#include <yos/ydev/camera.h>
#include "internal.h"

#include <string.h>

static ydev_result_t copy_plane(const uint8_t *src, size_t src_stride,
                                uint8_t *dst, size_t dst_stride,
                                uint32_t width, uint32_t height)
{
    for (uint32_t y = 0; y < height; y++) {
        memcpy(dst + (size_t)y * dst_stride,
               src + (size_t)y * src_stride,
               width);
    }
    return YDEV_OK;
}

ydev_result_t ydev_convert_to_i420(const ydev_frame_t *src,
                                   uint8_t *dst, size_t dst_size,
                                   size_t *bytes_written)
{
    if (!src || !dst) return YDEV_INVALID_ARG;
    if (src->format != YDEV_PIX_NV12) {
        ydev_set_error("convert_to_i420: src is not NV12");
        return YDEV_UNSUPPORTED;
    }
    uint32_t w = src->width;
    uint32_t h = src->height;
    if (w == 0 || h == 0 || (w & 1) || (h & 1)) {
        ydev_set_error("convert_to_i420: odd or zero dimensions %ux%u", w, h);
        return YDEV_INVALID_ARG;
    }
    size_t need = (size_t)w * h * 3 / 2;
    if (dst_size < need) {
        ydev_set_error("convert_to_i420: dst too small (%zu < %zu)", dst_size, need);
        return YDEV_NO_MEM;
    }

    const uint8_t *y_src  = src->data + src->plane_offset[0];
    const uint8_t *uv_src = src->data + src->plane_offset[1];
    size_t y_stride  = src->stride[0] ? src->stride[0] : w;
    size_t uv_stride = src->stride[1] ? src->stride[1] : w;

    uint8_t *y_dst = dst;
    uint8_t *u_dst = dst + (size_t)w * h;
    uint8_t *v_dst = u_dst + (size_t)(w / 2) * (h / 2);

    copy_plane(y_src, y_stride, y_dst, w, w, h);

    uint32_t uv_w = w / 2;
    uint32_t uv_h = h / 2;
    for (uint32_t y = 0; y < uv_h; y++) {
        const uint8_t *row = uv_src + (size_t)y * uv_stride;
        uint8_t *u_row = u_dst + (size_t)y * uv_w;
        uint8_t *v_row = v_dst + (size_t)y * uv_w;
        for (uint32_t x = 0; x < uv_w; x++) {
            u_row[x] = row[x * 2 + 0];
            v_row[x] = row[x * 2 + 1];
        }
    }

    if (bytes_written) *bytes_written = need;
    ydev_clear_error();
    return YDEV_OK;
}
