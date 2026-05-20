/*
 * yacodec/types.h - shared audio types for the audio codec subsystem.
 *
 * yacodec mirrors yvcodec (video codec wrapper around openh264). v1
 * supports Opus (xiph/libopus, BSD 3-Clause); the codec_id enum
 * reserves slots for the additional codecs miniaudio could decode
 * natively (WAV, FLAC, MP3, Vorbis) so future expansion is wire-stable.
 */

#ifndef YETTY_YACODEC_TYPES_H
#define YETTY_YACODEC_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio codec wire identifier. Matches the `audio_codec` uniform value
 * in the v2 yvideo wire envelope (and is referenced by `audio_codec`
 * elsewhere in the wire format). Values are stable — do NOT renumber. */
enum yetty_yacodec_codec {
    YETTY_YACODEC_CODEC_NONE   = 0, /* no audio in this stream */
    YETTY_YACODEC_CODEC_OPUS   = 1, /* v1 — implemented */
    YETTY_YACODEC_CODEC_VORBIS = 2, /* reserved — stb_vorbis is in miniaudio */
    YETTY_YACODEC_CODEC_FLAC   = 3, /* reserved — dr_flac is in miniaudio */
    YETTY_YACODEC_CODEC_MP3    = 4, /* reserved — dr_mp3 is in miniaudio */
    YETTY_YACODEC_CODEC_WAV    = 5, /* reserved — dr_wav is in miniaudio */
};

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YACODEC_TYPES_H */
