/*
 * ydev/audio.h — microphone (input) and speaker (output).
 *
 * Input: the framework callback (or kernel ring) pushes samples; the
 * library copies them into its internal ring; the client reads from
 * the ring. The fd becomes readable when samples are available.
 *
 * Output: inverted. The audio realtime callback pulls from the
 * library's ring; the client writes into it. The fd becomes readable
 * when the ring level drops below the low watermark — i.e. when there
 * is room for more data. Polling for POLLIN on an output handle is
 * therefore "wake me when I can write more", not "wake me on data".
 */

#ifndef YOS_YDEV_AUDIO_H
#define YOS_YDEV_AUDIO_H

#include <yos/ydev/ydev.h>
#include <sys/types.h>   /* ssize_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YDEV_SAMPLE_S16 = 1,
    YDEV_SAMPLE_F32 = 2,
} ydev_sample_format_t;

typedef struct {
    uint32_t             rate_hz;            /* 16000, 44100, 48000 ...  */
    uint32_t             channels;           /* 1 or 2 in practice       */
    ydev_sample_format_t format;
    uint32_t             frames_per_chunk;   /* ring granularity hint    */
} ydev_audio_config_t;

/* ── microphone ─────────────────────────────────────────────────────── */

typedef struct ydev_audio_in ydev_audio_in_t;

ydev_audio_in_t *ydev_audio_in_open(const ydev_audio_config_t *cfg);
ydev_result_t    ydev_audio_in_start(ydev_audio_in_t *);
ydev_result_t    ydev_audio_in_stop(ydev_audio_in_t *);
void             ydev_audio_in_close(ydev_audio_in_t *);

int              ydev_audio_in_fd(ydev_audio_in_t *);
ssize_t          ydev_audio_in_read(ydev_audio_in_t *, void *buf, size_t bytes,
                                    uint64_t *ts_ns, int timeout_ms);

/* ── speaker ────────────────────────────────────────────────────────── */

typedef struct ydev_audio_out ydev_audio_out_t;

ydev_audio_out_t *ydev_audio_out_open(const ydev_audio_config_t *cfg);
ydev_result_t     ydev_audio_out_start(ydev_audio_out_t *);
ydev_result_t     ydev_audio_out_stop(ydev_audio_out_t *);
void              ydev_audio_out_close(ydev_audio_out_t *);

int               ydev_audio_out_fd(ydev_audio_out_t *);   /* POLLIN = room  */
ssize_t           ydev_audio_out_write(ydev_audio_out_t *, const void *buf,
                                       size_t bytes, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
