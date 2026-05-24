/*
 * yacodec/decoder.h - audio decoder (Opus backend; codec-dispatch for
 * the reserved formats in yetty_yacodec_codec).
 *
 * Wire-shape mirror of yvcodec_decoder: packet in → PCM frames out.
 *
 *   create(codec, sample_rate, channels)
 *   feed(packet, size)        — for Opus, one packet per call
 *   pull_pcm(out, max_frames, *written) — drains decoded PCM
 *   destroy()
 *
 * For Opus the natural unit is one Opus packet (a CMD_UPDATE may carry
 * several, framed by a length prefix — the caller is responsible for
 * unpacking). For block codecs (WAV/FLAC/MP3/Vorbis) a packet equals a
 * decoder-defined unit; not implemented in v1.
 */

#ifndef YETTY_YACODEC_DECODER_H
#define YETTY_YACODEC_DECODER_H

#include <yetty/ycore/result.h>
#include <yetty/yacodec/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yacodec_decoder;

YETTY_YRESULT_DECLARE(yetty_yacodec_decoder_ptr, struct yetty_yacodec_decoder *);

/* Create a decoder for a given codec at `sample_rate` × `channels`.
 * Opus accepts 8/12/16/24/48 kHz; 48 kHz is the recommended default —
 * the decoder transparently upsamples lower-rate streams. */
struct yetty_yacodec_decoder_ptr_result yetty_yacodec_decoder_create(enum yetty_yacodec_codec codec,
                                                                     uint32_t sample_rate,
                                                                     uint32_t channels);

void yetty_yacodec_decoder_destroy(struct yetty_yacodec_decoder *dec);

/* Feed one compressed packet. For Opus this MUST be exactly one packet
 * (the codec rejects multi-packet feeds). The caller owns `data`. */
struct yetty_ycore_void_result yetty_yacodec_decoder_feed(struct yetty_yacodec_decoder *dec,
                                                          const uint8_t *data, size_t size);

/* Pull decoded interleaved float32 PCM. Returns the number of frames
 * (multi-channel samples) actually written to `out`, up to `max_frames`.
 * `out` capacity must be at least `max_frames * channels * sizeof(float)`.
 *
 * Returns *out_frames_written == 0 when no decoded audio is currently
 * buffered — call feed() again. */
struct yetty_ycore_void_result yetty_yacodec_decoder_pull_pcm(struct yetty_yacodec_decoder *dec,
                                                              float *out, size_t max_frames,
                                                              size_t *out_frames_written);

/* Reset decoder state for a new stream (seek). */
void yetty_yacodec_decoder_reset(struct yetty_yacodec_decoder *dec);

uint32_t yetty_yacodec_decoder_sample_rate(const struct yetty_yacodec_decoder *dec);
uint32_t yetty_yacodec_decoder_channels(const struct yetty_yacodec_decoder *dec);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YACODEC_DECODER_H */
