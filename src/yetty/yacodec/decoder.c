/*
 * decoder.c - audio decoder dispatch.
 *
 * v1 implements the Opus backend (libopus); the codec-dispatch slot
 * for WAV/FLAC/MP3/Vorbis is reserved in the public enum but returns
 * "codec not supported" until wired up. The per-codec backend lives in
 * a sibling TU (opus-decoder.c) so each new codec lands in isolation.
 */

#include <yetty/yacodec/decoder.h>
#include <yetty/ytrace/ytrace.h>

#include <opus.h>

#include <stdlib.h>
#include <string.h>

/* Maximum Opus frame size at 48 kHz: 120 ms = 5760 samples per channel.
 * We size the scratch buffer for the worst case so a single decode call
 * always fits in one pull_pcm round trip. */
#define YACODEC_OPUS_MAX_FRAME_SIZE 5760

struct yetty_yacodec_decoder {
    enum yetty_yacodec_codec codec;
    uint32_t sample_rate;
    uint32_t channels;

    /* Opus backend state. NULL for non-Opus codecs (v1 only supports
     * Opus — the alloc is unconditional but the future codec branch in
     * create() picks which field is populated). */
    OpusDecoder *opus;

    /* Decoded-PCM ring. Opus produces blocks up to 120 ms × 48 kHz ×
     * channels — we round up to a power-of-two-frame buffer and rotate
     * read/write indices. Single producer (feed via decode), single
     * consumer (pull_pcm). */
    float *pcm_ring;
    size_t pcm_ring_cap_frames;
    size_t pcm_ring_read_frame;
    size_t pcm_ring_write_frame;
};

static struct yetty_ycore_void_result yacodec_opus_init(struct yetty_yacodec_decoder *dec)
{
    int err = 0;
    dec->opus = opus_decoder_create((opus_int32)dec->sample_rate, (int)dec->channels, &err);
    if (!dec->opus || err != OPUS_OK) {
        return YETTY_ERR(yetty_ycore_void, "yacodec: opus_decoder_create failed");
    }
    /* Buffer 1.0 s of PCM — plenty for the worst-case 120 ms Opus
     * frame to land before the consumer drains it. */
    dec->pcm_ring_cap_frames = dec->sample_rate;
    dec->pcm_ring = calloc(dec->pcm_ring_cap_frames * dec->channels, sizeof(float));
    if (!dec->pcm_ring) {
        opus_decoder_destroy(dec->opus);
        dec->opus = NULL;
        return YETTY_ERR(yetty_ycore_void, "yacodec: pcm_ring alloc failed");
    }
    return YETTY_OK_VOID();
}

struct yetty_yacodec_decoder_ptr_result yetty_yacodec_decoder_create(enum yetty_yacodec_codec codec,
                                                                     uint32_t sample_rate,
                                                                     uint32_t channels)
{
    if (channels == 0u || channels > 2u) {
        /* libopus supports up to 255 channels via multistream API, but
         * the v1 wire envelope only carries mono/stereo; reject early. */
        return YETTY_ERR(yetty_yacodec_decoder_ptr, "yacodec: channels must be 1 or 2");
    }
    if (sample_rate != 8000u && sample_rate != 12000u && sample_rate != 16000u &&
        sample_rate != 24000u && sample_rate != 48000u) {
        return YETTY_ERR(yetty_yacodec_decoder_ptr,
                         "yacodec: opus sample_rate must be 8/12/16/24/48 kHz");
    }
    struct yetty_yacodec_decoder *dec = calloc(1u, sizeof(*dec));
    if (!dec) {
        return YETTY_ERR(yetty_yacodec_decoder_ptr, "yacodec: alloc failed");
    }
    dec->codec = codec;
    dec->sample_rate = sample_rate;
    dec->channels = channels;

    switch (codec) {
    case YETTY_YACODEC_CODEC_OPUS: {
        struct yetty_ycore_void_result ir = yacodec_opus_init(dec);
        if (YETTY_IS_ERR(ir)) {
            free(dec);
            return YETTY_ERR(yetty_yacodec_decoder_ptr, "yacodec: opus init failed", ir);
        }
        break;
    }
    case YETTY_YACODEC_CODEC_NONE:
        free(dec);
        return YETTY_ERR(yetty_yacodec_decoder_ptr, "yacodec: codec=none — nothing to decode");
    case YETTY_YACODEC_CODEC_VORBIS:
    case YETTY_YACODEC_CODEC_FLAC:
    case YETTY_YACODEC_CODEC_MP3:
    case YETTY_YACODEC_CODEC_WAV:
        free(dec);
        return YETTY_ERR(yetty_yacodec_decoder_ptr,
                         "yacodec: codec reserved but not implemented in v1");
    }
    yinfo("yacodec: decoder created codec=%d %u Hz × %u ch", (int)codec, sample_rate, channels);
    return YETTY_OK(yetty_yacodec_decoder_ptr, dec);
}

void yetty_yacodec_decoder_destroy(struct yetty_yacodec_decoder *dec)
{
    if (!dec) {
        return;
    }
    if (dec->opus) {
        opus_decoder_destroy(dec->opus);
    }
    free(dec->pcm_ring);
    free(dec);
}

/* Compute how much room is left in the ring (in frames). */
static size_t pcm_ring_free_frames(const struct yetty_yacodec_decoder *dec)
{
    /* Use a sentinel layout: the buffer is full when write == read - 1
     * (modulo cap). That means free = cap - (write - read) - 1. */
    size_t used;
    if (dec->pcm_ring_write_frame >= dec->pcm_ring_read_frame) {
        used = dec->pcm_ring_write_frame - dec->pcm_ring_read_frame;
    } else {
        used = dec->pcm_ring_cap_frames - (dec->pcm_ring_read_frame - dec->pcm_ring_write_frame);
    }
    if (used >= dec->pcm_ring_cap_frames - 1u) {
        return 0u;
    }
    return dec->pcm_ring_cap_frames - 1u - used;
}

static void pcm_ring_push(struct yetty_yacodec_decoder *dec, const float *src, size_t frames)
{
    /* Two-segment copy across the ring wrap. */
    size_t first = dec->pcm_ring_cap_frames - dec->pcm_ring_write_frame;
    if (first > frames) {
        first = frames;
    }
    memcpy(dec->pcm_ring + dec->pcm_ring_write_frame * dec->channels, src,
           first * dec->channels * sizeof(float));
    size_t rest = frames - first;
    if (rest > 0u) {
        memcpy(dec->pcm_ring, src + first * dec->channels, rest * dec->channels * sizeof(float));
    }
    dec->pcm_ring_write_frame = (dec->pcm_ring_write_frame + frames) % dec->pcm_ring_cap_frames;
}

static size_t pcm_ring_pop(struct yetty_yacodec_decoder *dec, float *dst, size_t max_frames)
{
    size_t used;
    if (dec->pcm_ring_write_frame >= dec->pcm_ring_read_frame) {
        used = dec->pcm_ring_write_frame - dec->pcm_ring_read_frame;
    } else {
        used = dec->pcm_ring_cap_frames - (dec->pcm_ring_read_frame - dec->pcm_ring_write_frame);
    }
    size_t take = (used < max_frames) ? used : max_frames;
    size_t first = dec->pcm_ring_cap_frames - dec->pcm_ring_read_frame;
    if (first > take) {
        first = take;
    }
    memcpy(dst, dec->pcm_ring + dec->pcm_ring_read_frame * dec->channels,
           first * dec->channels * sizeof(float));
    size_t rest = take - first;
    if (rest > 0u) {
        memcpy(dst + first * dec->channels, dec->pcm_ring, rest * dec->channels * sizeof(float));
    }
    dec->pcm_ring_read_frame = (dec->pcm_ring_read_frame + take) % dec->pcm_ring_cap_frames;
    return take;
}

struct yetty_ycore_void_result yetty_yacodec_decoder_feed(struct yetty_yacodec_decoder *dec,
                                                          const uint8_t *data, size_t size)
{
    if (!dec || !dec->opus) {
        return YETTY_ERR(yetty_ycore_void, "yacodec: decoder not initialised");
    }
    if (size == 0u) {
        return YETTY_OK_VOID();
    }
    if (!data) {
        return YETTY_ERR(yetty_ycore_void, "yacodec: null packet, size > 0");
    }

    /* opus_decode_float writes into a stack scratch sized for one max
     * frame; copying into the ring afterwards keeps the producer/
     * consumer indices the only inter-thread state. */
    float scratch[YACODEC_OPUS_MAX_FRAME_SIZE * 2 /* worst case channels */];
    int frames =
        opus_decode_float(dec->opus, data, (opus_int32)size, scratch, YACODEC_OPUS_MAX_FRAME_SIZE,
                          /*decode_fec=*/0);
    if (frames < 0) {
        return YETTY_ERR(yetty_ycore_void, "yacodec: opus_decode_float failed");
    }
    if (frames == 0) {
        return YETTY_OK_VOID();
    }
    if (pcm_ring_free_frames(dec) < (size_t)frames) {
        /* Decode raced ahead of the consumer — drop this packet. The
         * platform-audio consumer will catch up on its own clock; we'd
         * rather lose 20 ms of audio than back-pressure the wire. */
        ywarn("yacodec: pcm_ring full, dropping %d frames", frames);
        return YETTY_OK_VOID();
    }
    pcm_ring_push(dec, scratch, (size_t)frames);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yacodec_decoder_pull_pcm(struct yetty_yacodec_decoder *dec,
                                                              float *out, size_t max_frames,
                                                              size_t *out_frames_written)
{
    if (!dec || !out || !out_frames_written) {
        return YETTY_ERR(yetty_ycore_void, "yacodec: pull_pcm null arg");
    }
    *out_frames_written = pcm_ring_pop(dec, out, max_frames);
    return YETTY_OK_VOID();
}

void yetty_yacodec_decoder_reset(struct yetty_yacodec_decoder *dec)
{
    if (!dec) {
        return;
    }
    if (dec->opus) {
        opus_decoder_ctl(dec->opus, OPUS_RESET_STATE);
    }
    dec->pcm_ring_read_frame = 0u;
    dec->pcm_ring_write_frame = 0u;
}

uint32_t yetty_yacodec_decoder_sample_rate(const struct yetty_yacodec_decoder *dec)
{
    return dec ? dec->sample_rate : 0u;
}

uint32_t yetty_yacodec_decoder_channels(const struct yetty_yacodec_decoder *dec)
{
    return dec ? dec->channels : 0u;
}
