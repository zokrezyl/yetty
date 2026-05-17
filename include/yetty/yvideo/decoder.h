/*
 * yvideo/decoder.h - H.264 video decoder (openh264 backend).
 *
 * Used by:
 *   - VNC client — decode incoming H.264 frames from yetty-as-VNC-server
 *   - Terminal video playback — decode MP4/H.264 streams inside a ydraw card
 */

#ifndef YETTY_YVIDEO_DECODER_H
#define YETTY_YVIDEO_DECODER_H

#include <yetty/ycore/result.h>
#include <yetty/yvideo/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvideo_decoder;

YETTY_YRESULT_DECLARE(yetty_yvideo_decoder_ptr, struct yetty_yvideo_decoder *);

/* Create an H.264 decoder. No configuration needed — dimensions are learned
 * from the first SPS NAL. */
struct yetty_yvideo_decoder_ptr_result yetty_yvideo_decoder_create_h264(void);

void yetty_yvideo_decoder_destroy(struct yetty_yvideo_decoder *dec);

/*
 * Feed compressed data. May or may not produce a frame this call (openh264
 * does frame reordering internally). Follow with get_frame() in a loop.
 */
struct yetty_ycore_void_result yetty_yvideo_decoder_feed(struct yetty_yvideo_decoder *dec,
                                                         const uint8_t *data, size_t size);

/*
 * Pull the next decoded frame. `out->y_plane` etc. point into the decoder's
 * internal buffers; valid until the next feed()/get_frame()/release_frame()
 * call. Returns:
 *   ok=1, out_has_frame=1 → frame in `out`
 *   ok=1, out_has_frame=0 → no frame yet (need more feed())
 *   ok=0                  → decode error
 */
struct yetty_ycore_void_result yetty_yvideo_decoder_get_frame(struct yetty_yvideo_decoder *dec,
                                                              struct yetty_yvideo_yuv_frame *out,
                                                              bool *out_has_frame);

/*
 * Flush pending frames (stream end). Call get_frame() in a loop afterwards
 * to drain any reordered output.
 */
struct yetty_ycore_void_result yetty_yvideo_decoder_flush(struct yetty_yvideo_decoder *dec);

/* Reset decoder state for a new stream (e.g. seek). */
void yetty_yvideo_decoder_reset(struct yetty_yvideo_decoder *dec);

/*---------------------------------------------------------------------------
 * Color conversion helpers
 *-------------------------------------------------------------------------*/

/*
 * Convert a YUV420 frame to BGRA8. Output buffer must hold `width*height*4`
 * bytes; its stride equals `width*4`. Picks coefficients from frame->color_matrix.
 */
void yetty_yvideo_yuv_frame_yuv420_to_bgra(const struct yetty_yvideo_yuv_frame *frame,
                                           uint8_t *bgra_out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVIDEO_DECODER_H */
