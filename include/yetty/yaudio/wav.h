/*
 * yaudio/wav.h - mmap-backed PCM WAV reader for offline analysis.
 *
 * Designed for very large files (sensor recordings, multi-hour chunks):
 * open() mmaps the whole file read-only; the sample data is a pointer
 * into the mmap, not a heap copy. Iterators decode on the fly.
 *
 * Supported formats (PCM only — no codec dispatch here, that's yacodec):
 *   int16, int24, int32, float32
 * Multi-channel interleaved. RIFF/WAVE container; non-fmt/data chunks
 * (LIST, JUNK, bext, ...) are skipped.
 */

#ifndef YETTY_YAUDIO_WAV_H
#define YETTY_YAUDIO_WAV_H

#include <yetty/ycore/result.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum yetty_yaudio_wav_sample_format {
    YETTY_YAUDIO_WAV_SAMPLE_FMT_S16 = 1,
    YETTY_YAUDIO_WAV_SAMPLE_FMT_S24 = 2,
    YETTY_YAUDIO_WAV_SAMPLE_FMT_S32 = 3,
    YETTY_YAUDIO_WAV_SAMPLE_FMT_F32 = 4,
};

struct yetty_yaudio_wav {
    /* mmap state */
    int            fd;
    const uint8_t *map_base;
    size_t         map_size;

    /* format */
    enum yetty_yaudio_wav_sample_format fmt;
    uint32_t       sample_rate;
    uint16_t       channels;
    uint16_t       bits_per_sample;   /* container width: 16/24/32 */
    uint16_t       bytes_per_sample;  /* 2/3/4 */
    uint16_t       frame_stride;      /* channels * bytes_per_sample */

    /* data chunk view (into mmap) */
    const uint8_t *data;
    size_t         data_size;         /* bytes */
    size_t         frames;            /* full multi-channel frames */
};

YETTY_YRESULT_DECLARE(yetty_yaudio_wav_ptr, struct yetty_yaudio_wav *);

/* Open + parse header. Returns a heap-allocated wav object on success;
 * destroy with wav_close(). */
struct yetty_yaudio_wav_ptr_result
yetty_yaudio_wav_open(const char *path);

void yetty_yaudio_wav_close(struct yetty_yaudio_wav *w);

/* Decode `n_frames` worth of one channel into `out`, normalized to
 * [-1, 1] float32. `frame_off` is the starting frame (0 = beginning of
 * data). Returns the number of frames actually written (clipped to
 * w->frames - frame_off). */
struct yetty_ycore_size_result
yetty_yaudio_wav_read_channel_f32(const struct yetty_yaudio_wav *w,
                                  uint32_t channel,
                                  size_t   frame_off,
                                  float   *out,
                                  size_t   n_frames);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YAUDIO_WAV_H */
