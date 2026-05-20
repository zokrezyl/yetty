/*
 * yplatform/audio/default.c — single TU that owns miniaudio's
 * implementation symbols, shared across every yetty target.
 *
 * miniaudio is single-header STB-style: exactly one TU in the program
 * must `#define MINIAUDIO_IMPLEMENTATION` before including the header.
 * That TU is this file. Everything else in yetty sees miniaudio only
 * through the abstract API in <yetty/yplatform/audio.h>.
 *
 * Backend selection happens *inside* miniaudio at compile time — no
 * #ifdef ladder here. On Linux miniaudio probes PulseAudio first, falls
 * back to ALSA, then JACK, then sndio; on Apple CoreAudio; on Windows
 * WASAPI → DirectSound → WinMM; on Android AAudio → OpenSL ES; on
 * Emscripten WebAudio.
 *
 * Concurrency model:
 *   - Producer: any thread that calls write_pcm() (decode thread for
 *     yvideo / yacodec).
 *   - Consumer: miniaudio's backend callback (runs on a backend-owned
 *     thread on desktop / Android, on the Web Audio worklet on
 *     Emscripten).
 *   - Ring buffer: ma_pcm_rb (miniaudio's own SPSC lock-free ring).
 *   - played_out_sec: atomic_size_t counter incremented by the consumer
 *     thread; sample-precise wall-clock for A/V sync.
 */

#include <yetty/yplatform/audio.h>
#include <yetty/ytrace/ytrace.h>

/* Tell miniaudio we don't need its built-in decoders or device-enumerate
 * tools for yetty's playback-only use case. Trims binary size by ~80 KB
 * and skips the dr_libs vendored decoders we'd otherwise drag in. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_ENGINE
#define MA_NO_NODE_GRAPH
#define MA_API static
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct yetty_yplatform_audio_device {
    ma_device device;
    ma_pcm_rb ring;
    uint32_t sample_rate;
    uint32_t channels;
    /* Played-out frame counter; bumped by the backend callback as it
     * drains the ring. Stored atomically so the producer can read it
     * for A/V sync without locking. */
    _Atomic uint64_t frames_played_out;
    /* Tracks whether ma_device_start has been called. write_pcm doesn't
     * require start; the ring buffers regardless. */
    _Atomic int started;
};

/* miniaudio drives playback by calling this callback in the backend
 * thread whenever it needs more output. We pull from our ring and let
 * the caller (decode side) keep filling it via write_pcm. */
static void audio_data_callback(ma_device *device, void *out, const void *in,
                                ma_uint32 frame_count)
{
    (void)in;
    struct yetty_yplatform_audio_device *dev =
        (struct yetty_yplatform_audio_device *)device->pUserData;
    if (!dev) {
        memset(out, 0, (size_t)frame_count * device->playback.channels * sizeof(float));
        return;
    }
    ma_uint32 want = frame_count;
    float *dst = (float *)out;
    while (want > 0) {
        ma_uint32 chunk = want;
        void *read_ptr = NULL;
        if (ma_pcm_rb_acquire_read(&dev->ring, &chunk, &read_ptr) != MA_SUCCESS || chunk == 0) {
            /* Underrun — fill with silence so the device doesn't glitch
             * its clock. The played-out counter still advances. */
            size_t silence_frames = (size_t)want;
            memset(dst, 0, silence_frames * dev->channels * sizeof(float));
            atomic_fetch_add_explicit(&dev->frames_played_out,
                                      silence_frames, memory_order_relaxed);
            return;
        }
        memcpy(dst, read_ptr, (size_t)chunk * dev->channels * sizeof(float));
        ma_pcm_rb_commit_read(&dev->ring, chunk);
        dst += (size_t)chunk * dev->channels;
        atomic_fetch_add_explicit(&dev->frames_played_out, chunk,
                                  memory_order_relaxed);
        want -= chunk;
    }
}

struct yetty_yplatform_audio_device_ptr_result
yetty_yplatform_audio_device_create(uint32_t sample_rate, uint32_t channels)
{
    if (sample_rate == 0u || channels == 0u || channels > 8u) {
        return YETTY_ERR(yetty_yplatform_audio_device_ptr,
                         "audio: invalid sample_rate/channels");
    }

    struct yetty_yplatform_audio_device *dev = calloc(1u, sizeof(*dev));
    if (!dev) {
        return YETTY_ERR(yetty_yplatform_audio_device_ptr,
                         "audio: device alloc failed");
    }
    dev->sample_rate = sample_rate;
    dev->channels = channels;
    atomic_store_explicit(&dev->frames_played_out, 0u, memory_order_relaxed);
    atomic_store_explicit(&dev->started, 0, memory_order_relaxed);

    /* Ring sized for ~1 s of audio at the configured rate. Plenty of
     * headroom for the decode side without burning RAM (e.g. 48 kHz × 2
     * channels × 4 bytes ≈ 380 KB). */
    ma_uint32 ring_frames = sample_rate;
    if (ma_pcm_rb_init(ma_format_f32, channels, ring_frames, NULL, NULL,
                       &dev->ring) != MA_SUCCESS) {
        free(dev);
        return YETTY_ERR(yetty_yplatform_audio_device_ptr,
                         "audio: ring buffer init failed");
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate        = sample_rate;
    cfg.dataCallback      = audio_data_callback;
    cfg.pUserData         = dev;

    if (ma_device_init(NULL, &cfg, &dev->device) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&dev->ring);
        free(dev);
        return YETTY_ERR(yetty_yplatform_audio_device_ptr,
                         "audio: ma_device_init failed (no backend?)");
    }
    yinfo("audio: device opened %u Hz × %u ch (%s backend)",
          sample_rate, channels, ma_get_backend_name(dev->device.pContext->backend));
    return YETTY_OK(yetty_yplatform_audio_device_ptr, dev);
}

void yetty_yplatform_audio_device_destroy(struct yetty_yplatform_audio_device *dev)
{
    if (!dev) {
        return;
    }
    if (atomic_load_explicit(&dev->started, memory_order_relaxed)) {
        ma_device_stop(&dev->device);
    }
    ma_device_uninit(&dev->device);
    ma_pcm_rb_uninit(&dev->ring);
    free(dev);
}

struct yetty_ycore_void_result
yetty_yplatform_audio_device_start(struct yetty_yplatform_audio_device *dev)
{
    if (!dev) {
        return YETTY_ERR(yetty_ycore_void, "audio: start on null device");
    }
    if (atomic_load_explicit(&dev->started, memory_order_relaxed)) {
        return YETTY_OK_VOID();
    }
    if (ma_device_start(&dev->device) != MA_SUCCESS) {
        return YETTY_ERR(yetty_ycore_void, "audio: ma_device_start failed");
    }
    atomic_store_explicit(&dev->started, 1, memory_order_release);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result
yetty_yplatform_audio_device_stop(struct yetty_yplatform_audio_device *dev)
{
    if (!dev) {
        return YETTY_ERR(yetty_ycore_void, "audio: stop on null device");
    }
    if (!atomic_load_explicit(&dev->started, memory_order_relaxed)) {
        return YETTY_OK_VOID();
    }
    if (ma_device_stop(&dev->device) != MA_SUCCESS) {
        return YETTY_ERR(yetty_ycore_void, "audio: ma_device_stop failed");
    }
    atomic_store_explicit(&dev->started, 0, memory_order_release);
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result
yetty_yplatform_audio_device_write_pcm(struct yetty_yplatform_audio_device *dev,
                                       const float *pcm, size_t frames)
{
    if (!dev || !pcm) {
        return YETTY_ERR(yetty_ycore_size, "audio: write_pcm null arg");
    }
    size_t written = 0u;
    while (written < frames) {
        ma_uint32 want = (ma_uint32)(frames - written);
        void *dst = NULL;
        if (ma_pcm_rb_acquire_write(&dev->ring, &want, &dst) != MA_SUCCESS ||
            want == 0u) {
            /* Ring full — drop the surplus. The audio clock continues
             * to advance from the consumer; the decode side just sees
             * a return value < frames and can decide what to do. */
            break;
        }
        memcpy(dst, pcm + written * dev->channels,
               (size_t)want * dev->channels * sizeof(float));
        ma_pcm_rb_commit_write(&dev->ring, want);
        written += want;
    }
    return YETTY_OK(yetty_ycore_size, written);
}

double yetty_yplatform_audio_device_played_out_sec(
    const struct yetty_yplatform_audio_device *dev)
{
    if (!dev) {
        return 0.0;
    }
    uint64_t played = atomic_load_explicit(&dev->frames_played_out,
                                           memory_order_relaxed);
    return (double)played / (double)dev->sample_rate;
}

void yetty_yplatform_audio_device_flush(struct yetty_yplatform_audio_device *dev)
{
    if (!dev) {
        return;
    }
    /* ma_pcm_rb_reset moves both pointers to 0 — safe ONLY when the
     * producer is quiescent (the seek-path caller holds the only write
     * handle by convention). */
    ma_pcm_rb_reset(&dev->ring);
}

uint32_t yetty_yplatform_audio_device_sample_rate(
    const struct yetty_yplatform_audio_device *dev)
{
    return dev ? dev->sample_rate : 0u;
}

uint32_t yetty_yplatform_audio_device_channels(
    const struct yetty_yplatform_audio_device *dev)
{
    return dev ? dev->channels : 0u;
}
