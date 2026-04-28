/*
 * yvideo/types.h - shared video frame types used by encoder + decoder.
 */

#ifndef YETTY_YVIDEO_TYPES_H
#define YETTY_YVIDEO_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Color matrix hint for YUV→RGB conversion. BT.709 is the right default for
 * HD content, BT.601 for SD, BT.2020 for UHD/HDR. */
enum yetty_yvideo_color_matrix {
    YETTY_YVIDEO_COLOR_BT601 = 0,
    YETTY_YVIDEO_COLOR_BT709 = 1,
    YETTY_YVIDEO_COLOR_BT2020 = 2,
};

/* YUV420 frame handed out by the decoder. Pointers reference internal decoder
 * buffers — valid until the next decoder call on the same instance. Copy or
 * convert before handing off. */
struct yetty_yvideo_yuv_frame {
    const uint8_t *y_plane;
    uint32_t y_stride;
    const uint8_t *u_plane;
    uint32_t u_stride;
    const uint8_t *v_plane;
    uint32_t v_stride;

    uint32_t width;
    uint32_t height;

    enum yetty_yvideo_color_matrix color_matrix;
};

/* H.264 / encoded-frame payload produced by the encoder. `data` is owned by
 * the encoder's internal buffer; it stays valid until the next encode() call
 * on the same encoder. */
struct yetty_yvideo_encoded_frame {
    const uint8_t *data;
    size_t size;
    bool is_idr;
    uint64_t timestamp_us;
    uint32_t frame_index;
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVIDEO_TYPES_H */
