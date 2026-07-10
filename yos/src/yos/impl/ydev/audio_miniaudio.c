/*
 * Cross-platform audio backend on top of miniaudio.
 *
 * Replaces what was previously three per-platform files (Apple's
 * AVAudioEngine code, Android's AAudio code, the empty Linux stub).
 * miniaudio handles platform dispatch — CoreAudio on iOS/tvOS/macOS,
 * AAudio + OpenSL on Android, ALSA/PulseAudio/JACK/PipeWire on Linux
 * (runtime dlopen, no LGPL link-time deps).
 *
 * Wiring shape:
 *   Input  : miniaudio data callback delivers PCM frames; we push
 *            fixed-size chunks into a vfd ring, the client reads them
 *            via ydev_audio_in_read. The vfd's self-pipe drives poll().
 *   Output : miniaudio's callback pulls PCM frames from us. The client
 *            writes into a host-side ring via ydev_audio_out_write; the
 *            callback drains it. The fd becomes readable when ring
 *            occupancy drops below the low watermark (= room to write).
 */

#include "internal.h"
#include <yos/ydev/audio.h>

#include "miniaudio.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ma_format to_ma_format(ydev_sample_format_t f)
{
    return (f == YDEV_SAMPLE_F32) ? ma_format_f32 : ma_format_s16;
}

static size_t bytes_per_frame(const ydev_audio_config_t *c)
{
    size_t bps = (c->format == YDEV_SAMPLE_F32) ? 4 : 2;
    return bps * (c->channels ? c->channels : 1);
}

/* ── input ───────────────────────────────────────────────────────────── */

struct ydev_audio_in {
    ma_device           dev;
    int                 dev_alive;
    struct ydev_vfd     vfd;
    ydev_audio_config_t cfg;
    size_t              frame_bytes;
    size_t              chunk_frames;
};

static void in_cb(ma_device *pDev, void *pOut, const void *pIn, ma_uint32 nFrames)
{
    (void)pOut;
    ydev_audio_in_t *h = pDev->pUserData;
    if (!h || !pIn) return;
    size_t bytes = (size_t)nFrames * h->frame_bytes;
    size_t chunk = h->chunk_frames * h->frame_bytes;
    size_t off   = 0;
    /* Push whole chunks; any tail < 1 chunk is dropped (typically zero,
     * because miniaudio honours periodSizeInFrames = chunk_frames). */
    while (off + chunk <= bytes) {
        ydev_vfd_push(&h->vfd, (const uint8_t *)pIn + off);
        off += chunk;
    }
}

ydev_audio_in_t *ydev_audio_in_open(const ydev_audio_config_t *cfg)
{
    if (!cfg || !cfg->rate_hz || !cfg->channels) {
        ydev_set_error("audio_in_open: invalid config");
        return NULL;
    }
    ydev_audio_in_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->cfg          = *cfg;
    h->frame_bytes  = bytes_per_frame(cfg);
    h->chunk_frames = cfg->frames_per_chunk ? cfg->frames_per_chunk
                                            : (cfg->rate_hz / 100);   /* 10 ms */
    if (ydev_vfd_init(&h->vfd, h->chunk_frames * h->frame_bytes, 8, NULL) != 0) {
        free(h);
        return NULL;
    }

    ma_device_config dcfg = ma_device_config_init(ma_device_type_capture);
    dcfg.capture.format    = to_ma_format(cfg->format);
    dcfg.capture.channels  = cfg->channels;
    dcfg.sampleRate        = cfg->rate_hz;
    dcfg.periodSizeInFrames= (ma_uint32)h->chunk_frames;
    dcfg.dataCallback      = in_cb;
    dcfg.pUserData         = h;

    if (ma_device_init(NULL, &dcfg, &h->dev) != MA_SUCCESS) {
        ydev_set_error("audio_in_open: ma_device_init failed");
        ydev_vfd_destroy(&h->vfd);
        free(h);
        return NULL;
    }
    h->dev_alive = 1;
    return h;
}

ydev_result_t ydev_audio_in_start(ydev_audio_in_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    ydev_perm_status_t pst = ydev_perm_query_platform(YDEV_CAP_MIC);
    if (pst == YDEV_PERM_DENIED || pst == YDEV_PERM_RESTRICTED) return YDEV_DENIED;
    return ma_device_start(&h->dev) == MA_SUCCESS ? YDEV_OK : YDEV_IO;
}

ydev_result_t ydev_audio_in_stop(ydev_audio_in_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    ma_device_stop(&h->dev);
    ydev_vfd_close(&h->vfd);
    return YDEV_OK;
}

void ydev_audio_in_close(ydev_audio_in_t *h)
{
    if (!h) return;
    if (h->dev_alive) { ma_device_uninit(&h->dev); h->dev_alive = 0; }
    ydev_vfd_destroy(&h->vfd);
    free(h);
}

int ydev_audio_in_fd(ydev_audio_in_t *h) { return h ? ydev_vfd_fd(&h->vfd) : -1; }

ssize_t ydev_audio_in_read(ydev_audio_in_t *h, void *buf, size_t bytes,
                           uint64_t *ts_ns, int timeout_ms)
{
    if (!h || !buf) { errno = EINVAL; return -1; }
    size_t chunk  = h->chunk_frames * h->frame_bytes;
    size_t copied = 0;
    while (copied + chunk <= bytes) {
        ydev_result_t r = ydev_vfd_pop(&h->vfd, (uint8_t *)buf + copied,
                                       copied == 0 ? timeout_ms : 0);
        if (r == YDEV_AGAIN) break;
        if (r != YDEV_OK)    { errno = EIO; return copied ? (ssize_t)copied : -1; }
        copied += chunk;
    }
    if (ts_ns) *ts_ns = ydev_now_ns();
    return (ssize_t)copied;
}

/* ── output ──────────────────────────────────────────────────────────── */

struct ydev_audio_out {
    ma_device           dev;
    int                 dev_alive;
    pthread_mutex_t     lock;
    pthread_cond_t      cond_room;
    uint8_t            *ring;
    size_t              ring_cap;
    size_t              ring_used;
    size_t              ring_head;
    size_t              ring_tail;
    size_t              low_watermark;
    int                 pipe_r;
    int                 pipe_w;
    int                 closed;
    ydev_audio_config_t cfg;
    size_t              frame_bytes;
};

static void out_cb(ma_device *pDev, void *pOut, const void *pIn, ma_uint32 nFrames)
{
    (void)pIn;
    ydev_audio_out_t *h = pDev->pUserData;
    if (!h) return;

    size_t need  = (size_t)nFrames * h->frame_bytes;
    size_t given = 0;
    uint8_t *dst = pOut;

    pthread_mutex_lock(&h->lock);
    bool was_above = (h->ring_used > h->low_watermark);
    while (given < need && h->ring_used > 0) {
        size_t chunk  = need - given;
        size_t to_end = h->ring_cap - h->ring_tail;
        if (chunk > h->ring_used) chunk = h->ring_used;
        if (chunk > to_end)       chunk = to_end;
        memcpy(dst + given, h->ring + h->ring_tail, chunk);
        h->ring_tail = (h->ring_tail + chunk) % h->ring_cap;
        h->ring_used -= chunk;
        given += chunk;
    }
    if (given < need) memset(dst + given, 0, need - given);
    bool below = (h->ring_used <= h->low_watermark);
    pthread_cond_broadcast(&h->cond_room);
    pthread_mutex_unlock(&h->lock);

    /* Edge-trigger: only kick the pipe when occupancy crosses the
     * low-watermark from above. Avoids a wakeup per callback. */
    if (was_above && below) {
        char x = 1;
        ssize_t w = write(h->pipe_w, &x, 1);
        (void)w;
    }
}

ydev_audio_out_t *ydev_audio_out_open(const ydev_audio_config_t *cfg)
{
    if (!cfg || !cfg->rate_hz || !cfg->channels) {
        ydev_set_error("audio_out_open: invalid config");
        return NULL;
    }
    ydev_audio_out_t *h = calloc(1, sizeof *h);
    if (!h) return NULL;
    h->cfg         = *cfg;
    h->frame_bytes = bytes_per_frame(cfg);
    /* 500 ms of buffer: cushions jitter without piling up latency. */
    h->ring_cap       = h->frame_bytes * cfg->rate_hz / 2;
    h->low_watermark  = h->ring_cap / 4;
    h->ring           = calloc(1, h->ring_cap);
    pthread_mutex_init(&h->lock, NULL);
    pthread_cond_init(&h->cond_room, NULL);

    int p[2];
    if (pipe(p) != 0) { free(h->ring); free(h); return NULL; }
    int fl;
    fl = fcntl(p[0], F_GETFL, 0); fcntl(p[0], F_SETFL, fl | O_NONBLOCK);
    fl = fcntl(p[1], F_GETFL, 0); fcntl(p[1], F_SETFL, fl | O_NONBLOCK);
    h->pipe_r = p[0]; h->pipe_w = p[1];
    /* Arm the pipe so the very first poll wakes immediately — the ring
     * is empty, the client has all the room in the world. */
    { char x = 1; ssize_t w = write(h->pipe_w, &x, 1); (void)w; }

    ma_device_config dcfg = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format   = to_ma_format(cfg->format);
    dcfg.playback.channels = cfg->channels;
    dcfg.sampleRate        = cfg->rate_hz;
    dcfg.periodSizeInFrames= cfg->frames_per_chunk ? cfg->frames_per_chunk
                                                   : (cfg->rate_hz / 100);
    dcfg.dataCallback      = out_cb;
    dcfg.pUserData         = h;

    if (ma_device_init(NULL, &dcfg, &h->dev) != MA_SUCCESS) {
        ydev_set_error("audio_out_open: ma_device_init failed");
        close(h->pipe_r); close(h->pipe_w);
        free(h->ring);
        pthread_cond_destroy(&h->cond_room);
        pthread_mutex_destroy(&h->lock);
        free(h);
        return NULL;
    }
    h->dev_alive = 1;
    return h;
}

ydev_result_t ydev_audio_out_start(ydev_audio_out_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    return ma_device_start(&h->dev) == MA_SUCCESS ? YDEV_OK : YDEV_IO;
}

ydev_result_t ydev_audio_out_stop(ydev_audio_out_t *h)
{
    if (!h) return YDEV_INVALID_ARG;
    ma_device_stop(&h->dev);
    pthread_mutex_lock(&h->lock);
    h->closed = 1;
    pthread_cond_broadcast(&h->cond_room);
    pthread_mutex_unlock(&h->lock);
    return YDEV_OK;
}

void ydev_audio_out_close(ydev_audio_out_t *h)
{
    if (!h) return;
    if (h->dev_alive) { ma_device_uninit(&h->dev); h->dev_alive = 0; }
    if (h->pipe_r >= 0) close(h->pipe_r);
    if (h->pipe_w >= 0) close(h->pipe_w);
    pthread_cond_destroy(&h->cond_room);
    pthread_mutex_destroy(&h->lock);
    free(h->ring);
    free(h);
}

int ydev_audio_out_fd(ydev_audio_out_t *h) { return h ? h->pipe_r : -1; }

ssize_t ydev_audio_out_write(ydev_audio_out_t *h, const void *buf,
                             size_t bytes, int timeout_ms)
{
    if (!h || !buf) { errno = EINVAL; return -1; }

    struct timespec deadline; bool have_deadline = false;
    if (timeout_ms > 0) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec  += timeout_ms / 1000;
        deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec++;
            deadline.tv_nsec -= 1000000000L;
        }
        have_deadline = true;
    }

    pthread_mutex_lock(&h->lock);
    size_t written = 0;
    while (written < bytes && !h->closed) {
        size_t free_bytes = h->ring_cap - h->ring_used;
        if (free_bytes == 0) {
            if (timeout_ms == 0) break;
            int r = have_deadline
                ? pthread_cond_timedwait(&h->cond_room, &h->lock, &deadline)
                : pthread_cond_wait    (&h->cond_room, &h->lock);
            if (r == ETIMEDOUT) break;
            continue;
        }
        size_t chunk  = bytes - written;
        if (chunk > free_bytes) chunk = free_bytes;
        size_t to_end = h->ring_cap - h->ring_head;
        if (chunk > to_end) chunk = to_end;
        memcpy(h->ring + h->ring_head, (const uint8_t *)buf + written, chunk);
        h->ring_head = (h->ring_head + chunk) % h->ring_cap;
        h->ring_used += chunk;
        written += chunk;
    }
    bool above_now = (h->ring_used > h->low_watermark);
    pthread_mutex_unlock(&h->lock);

    /* Above the low watermark again: drain the pipe so the next poll
     * blocks until more room shows up. */
    if (above_now) {
        char b[8];
        while (read(h->pipe_r, b, sizeof b) > 0) {}
    }
    return (ssize_t)written;
}
