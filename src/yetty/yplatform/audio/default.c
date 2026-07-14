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

#include <ctype.h>
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
static void audio_data_callback(ma_device *device, void *out, const void *in, ma_uint32 frame_count)
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
            atomic_fetch_add_explicit(&dev->frames_played_out, silence_frames,
                                      memory_order_relaxed);
            return;
        }
        memcpy(dst, read_ptr, (size_t)chunk * dev->channels * sizeof(float));
        ma_pcm_rb_commit_read(&dev->ring, chunk);
        dst += (size_t)chunk * dev->channels;
        atomic_fetch_add_explicit(&dev->frames_played_out, chunk, memory_order_relaxed);
        want -= chunk;
    }
}

struct yetty_yplatform_audio_device_ptr_result yetty_yplatform_audio_device_create(
    uint32_t sample_rate, uint32_t channels)
{
    if (sample_rate == 0u || channels == 0u || channels > 8u) {
        return YETTY_ERR(yetty_yplatform_audio_device_ptr, "audio: invalid sample_rate/channels");
    }

    struct yetty_yplatform_audio_device *dev = calloc(1u, sizeof(*dev));
    if (!dev) {
        return YETTY_ERR(yetty_yplatform_audio_device_ptr, "audio: device alloc failed");
    }
    dev->sample_rate = sample_rate;
    dev->channels = channels;
    atomic_store_explicit(&dev->frames_played_out, 0u, memory_order_relaxed);
    atomic_store_explicit(&dev->started, 0, memory_order_relaxed);

    /* Ring sized for ~1 s of audio at the configured rate. Plenty of
     * headroom for the decode side without burning RAM (e.g. 48 kHz × 2
     * channels × 4 bytes ≈ 380 KB). */
    ma_uint32 ring_frames = sample_rate;
    if (ma_pcm_rb_init(ma_format_f32, channels, ring_frames, NULL, NULL, &dev->ring) !=
        MA_SUCCESS) {
        free(dev);
        return YETTY_ERR(yetty_yplatform_audio_device_ptr, "audio: ring buffer init failed");
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = channels;
    cfg.sampleRate = sample_rate;
    cfg.dataCallback = audio_data_callback;
    cfg.pUserData = dev;

    if (ma_device_init(NULL, &cfg, &dev->device) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&dev->ring);
        free(dev);
        return YETTY_ERR(yetty_yplatform_audio_device_ptr,
                         "audio: ma_device_init failed (no backend?)");
    }
    yinfo("audio: device opened %u Hz × %u ch (%s backend)", sample_rate, channels,
          ma_get_backend_name(dev->device.pContext->backend));
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

struct yetty_ycore_void_result yetty_yplatform_audio_device_start(
    struct yetty_yplatform_audio_device *dev)
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

struct yetty_ycore_void_result yetty_yplatform_audio_device_stop(
    struct yetty_yplatform_audio_device *dev)
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

struct yetty_ycore_size_result yetty_yplatform_audio_device_write_pcm(
    struct yetty_yplatform_audio_device *dev, const float *pcm, size_t frames)
{
    if (!dev || !pcm) {
        return YETTY_ERR(yetty_ycore_size, "audio: write_pcm null arg");
    }
    size_t written = 0u;
    while (written < frames) {
        ma_uint32 want = (ma_uint32)(frames - written);
        void *dst = NULL;
        if (ma_pcm_rb_acquire_write(&dev->ring, &want, &dst) != MA_SUCCESS || want == 0u) {
            /* Ring full — drop the surplus. The audio clock continues
             * to advance from the consumer; the decode side just sees
             * a return value < frames and can decide what to do. */
            break;
        }
        memcpy(dst, pcm + written * dev->channels, (size_t)want * dev->channels * sizeof(float));
        ma_pcm_rb_commit_write(&dev->ring, want);
        written += want;
    }
    return YETTY_OK(yetty_ycore_size, written);
}

double yetty_yplatform_audio_device_played_out_sec(const struct yetty_yplatform_audio_device *dev)
{
    if (!dev) {
        return 0.0;
    }
    uint64_t played = atomic_load_explicit(&dev->frames_played_out, memory_order_relaxed);
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

uint32_t yetty_yplatform_audio_device_sample_rate(const struct yetty_yplatform_audio_device *dev)
{
    return dev ? dev->sample_rate : 0u;
}

uint32_t yetty_yplatform_audio_device_channels(const struct yetty_yplatform_audio_device *dev)
{
    return dev ? dev->channels : 0u;
}

/*===========================================================================
 * Capture-device implementation.
 *
 * Mirror image of the playback device above: miniaudio drives the mic on
 * its own backend thread and dumps s16 frames into an SPSC ring; the
 * consumer (mp4 recorder's encoder pump) drains via read_s16.
 *===========================================================================*/

struct yetty_yplatform_audio_capture {
    /* Own the context (rather than passing NULL to ma_device_init) so the
     * device can be opened by an explicit ma_device_id resolved through
     * enumeration on this same context. The context must outlive the
     * device — uninit order in destroy is device then context. */
    ma_context context;
    ma_device device;
    ma_pcm_rb ring;
    uint32_t sample_rate;
    uint32_t channels;
    _Atomic int started;
};

/* Case-insensitive "does `haystack` contain `needle`?" — portable stand-in
 * for the GNU-only strcasestr, used to match a device selector against a
 * backend device name. Empty needle matches. */
static bool audio_ci_contains(const char *haystack, const char *needle)
{
    if (!needle[0]) {
        return true;
    }
    for (const char *base = haystack; *base; base++) {
        const char *hay = base;
        const char *ndl = needle;
        while (*hay && *ndl && tolower((unsigned char)*hay) == tolower((unsigned char)*ndl)) {
            hay++;
            ndl++;
        }
        if (!*ndl) {
            return true;
        }
    }
    return false;
}

/* Resolve a user selector against enumerated capture devices. Returns the
 * matching index (>= 0), or -1 for "use the backend default" (empty /
 * "default" selector). A non-empty selector that matches nothing returns
 * -1 and sets *not_found. */
static int32_t audio_resolve_capture_index(const ma_device_info *infos, ma_uint32 count,
                                           const char *device_sel, bool *not_found)
{
    *not_found = false;
    if (!device_sel || !device_sel[0] || strcmp(device_sel, "default") == 0) {
        return -1;
    }

    bool all_digits = true;
    for (const char *cursor = device_sel; *cursor; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            all_digits = false;
            break;
        }
    }
    if (all_digits) {
        long index = strtol(device_sel, NULL, 10);
        if (index >= 0 && (ma_uint32)index < count) {
            return (int32_t)index;
        }
        *not_found = true;
        return -1;
    }

    for (ma_uint32 i = 0; i < count; i++) {
        if (audio_ci_contains(infos[i].name, device_sel)) {
            return (int32_t)i;
        }
    }
    *not_found = true;
    return -1;
}

static void audio_capture_callback(ma_device *device, void *out, const void *in,
                                   ma_uint32 frame_count)
{
    (void)out;
    struct yetty_yplatform_audio_capture *cap =
        (struct yetty_yplatform_audio_capture *)device->pUserData;
    if (!cap || !in) {
        return;
    }
    ma_uint32 want = frame_count;
    const int16_t *src = (const int16_t *)in;
    while (want > 0u) {
        ma_uint32 chunk = want;
        void *write_ptr = NULL;
        if (ma_pcm_rb_acquire_write(&cap->ring, &chunk, &write_ptr) != MA_SUCCESS || chunk == 0u) {
            /* Ring full — drop the surplus. Recording tolerates the
             * occasional drop better than blocking the mic thread. */
            return;
        }
        memcpy(write_ptr, src, (size_t)chunk * cap->channels * sizeof(int16_t));
        ma_pcm_rb_commit_write(&cap->ring, chunk);
        src += (size_t)chunk * cap->channels;
        want -= chunk;
    }
}

struct yetty_yplatform_audio_capture_ptr_result yetty_yplatform_audio_capture_create(
    uint32_t sample_rate, uint32_t channels, const char *device_sel)
{
    if (sample_rate == 0u || channels == 0u || channels > 8u) {
        return YETTY_ERR(yetty_yplatform_audio_capture_ptr,
                         "audio-capture: invalid sample_rate/channels");
    }

    struct yetty_yplatform_audio_capture *cap = calloc(1u, sizeof(*cap));
    if (!cap) {
        return YETTY_ERR(yetty_yplatform_audio_capture_ptr, "audio-capture: alloc failed");
    }
    cap->sample_rate = sample_rate;
    cap->channels = channels;
    atomic_store_explicit(&cap->started, 0, memory_order_relaxed);

    /* ~2 s of headroom — the encoder pump ticks every video frame, but
     * we shouldn't drop mic data if the pump is briefly delayed. */
    ma_uint32 ring_frames = sample_rate * 2u;
    if (ma_pcm_rb_init(ma_format_s16, channels, ring_frames, NULL, NULL, &cap->ring) !=
        MA_SUCCESS) {
        free(cap);
        return YETTY_ERR(yetty_yplatform_audio_capture_ptr,
                         "audio-capture: ring buffer init failed");
    }

    if (ma_context_init(NULL, 0u, NULL, &cap->context) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&cap->ring);
        free(cap);
        return YETTY_ERR(yetty_yplatform_audio_capture_ptr,
                         "audio-capture: ma_context_init failed (no mic backend?)");
    }

    /* Resolve an explicit device selector to a backend device id. NULL id
     * pointer = the backend default. The id union is copied out by value
     * before ma_device_init, so it stays valid even though the enumerated
     * array is owned by the context. */
    ma_device_id chosen_id;
    ma_device_id *device_id_ptr = NULL;
    if (device_sel && device_sel[0] && strcmp(device_sel, "default") != 0) {
        ma_device_info *playback_infos = NULL;
        ma_uint32 playback_count = 0u;
        ma_device_info *capture_infos = NULL;
        ma_uint32 capture_count = 0u;
        if (ma_context_get_devices(&cap->context, &playback_infos, &playback_count, &capture_infos,
                                   &capture_count) != MA_SUCCESS) {
            ma_context_uninit(&cap->context);
            ma_pcm_rb_uninit(&cap->ring);
            free(cap);
            return YETTY_ERR(yetty_yplatform_audio_capture_ptr,
                             "audio-capture: ma_context_get_devices failed");
        }
        bool not_found = false;
        int32_t index =
            audio_resolve_capture_index(capture_infos, capture_count, device_sel, &not_found);
        if (index < 0) {
            /* device_sel is non-empty and non-"default" here, so index < 0
             * always means "no match". Name the selector in the log (the
             * Result msg is a borrowed literal and can't carry it). */
            yerror("audio-capture: no input device matches '%s' (%u available; see --info)",
                   device_sel, capture_count);
            ma_context_uninit(&cap->context);
            ma_pcm_rb_uninit(&cap->ring);
            free(cap);
            return YETTY_ERR(yetty_yplatform_audio_capture_ptr,
                             "audio-capture: requested input device not found");
        }
        chosen_id = capture_infos[index].id;
        device_id_ptr = &chosen_id;
        yinfo("audio-capture: selected input [%d] %s", index, capture_infos[index].name);
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.pDeviceID = device_id_ptr;
    cfg.capture.format = ma_format_s16;
    cfg.capture.channels = channels;
    cfg.sampleRate = sample_rate;
    cfg.dataCallback = audio_capture_callback;
    cfg.pUserData = cap;

    if (ma_device_init(&cap->context, &cfg, &cap->device) != MA_SUCCESS) {
        ma_context_uninit(&cap->context);
        ma_pcm_rb_uninit(&cap->ring);
        free(cap);
        return YETTY_ERR(yetty_yplatform_audio_capture_ptr,
                         "audio-capture: ma_device_init failed (no mic backend?)");
    }
    yinfo("audio-capture: device opened %u Hz × %u ch (%s backend)", sample_rate, channels,
          ma_get_backend_name(cap->device.pContext->backend));
    return YETTY_OK(yetty_yplatform_audio_capture_ptr, cap);
}

void yetty_yplatform_audio_capture_destroy(struct yetty_yplatform_audio_capture *cap)
{
    if (!cap) {
        return;
    }
    if (atomic_load_explicit(&cap->started, memory_order_relaxed)) {
        ma_device_stop(&cap->device);
    }
    ma_device_uninit(&cap->device);
    ma_context_uninit(&cap->context);
    ma_pcm_rb_uninit(&cap->ring);
    free(cap);
}

struct yetty_ycore_void_result yetty_yplatform_audio_capture_start(
    struct yetty_yplatform_audio_capture *cap)
{
    if (!cap) {
        return YETTY_ERR(yetty_ycore_void, "audio-capture: start on null device");
    }
    if (atomic_load_explicit(&cap->started, memory_order_relaxed)) {
        return YETTY_OK_VOID();
    }
    if (ma_device_start(&cap->device) != MA_SUCCESS) {
        return YETTY_ERR(yetty_ycore_void, "audio-capture: ma_device_start failed");
    }
    atomic_store_explicit(&cap->started, 1, memory_order_release);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplatform_audio_capture_stop(
    struct yetty_yplatform_audio_capture *cap)
{
    if (!cap) {
        return YETTY_ERR(yetty_ycore_void, "audio-capture: stop on null device");
    }
    if (!atomic_load_explicit(&cap->started, memory_order_relaxed)) {
        return YETTY_OK_VOID();
    }
    if (ma_device_stop(&cap->device) != MA_SUCCESS) {
        return YETTY_ERR(yetty_ycore_void, "audio-capture: ma_device_stop failed");
    }
    atomic_store_explicit(&cap->started, 0, memory_order_release);
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_yplatform_audio_capture_read_s16(
    struct yetty_yplatform_audio_capture *cap, int16_t *pcm, size_t frames)
{
    if (!cap || !pcm) {
        return YETTY_ERR(yetty_ycore_size, "audio-capture: read_s16 null arg");
    }
    size_t read = 0u;
    while (read < frames) {
        ma_uint32 want = (ma_uint32)(frames - read);
        void *src = NULL;
        if (ma_pcm_rb_acquire_read(&cap->ring, &want, &src) != MA_SUCCESS || want == 0u) {
            break;
        }
        memcpy(pcm + read * cap->channels, src, (size_t)want * cap->channels * sizeof(int16_t));
        ma_pcm_rb_commit_read(&cap->ring, want);
        read += want;
    }
    return YETTY_OK(yetty_ycore_size, read);
}

uint32_t yetty_yplatform_audio_capture_sample_rate(const struct yetty_yplatform_audio_capture *cap)
{
    return cap ? cap->sample_rate : 0u;
}

uint32_t yetty_yplatform_audio_capture_channels(const struct yetty_yplatform_audio_capture *cap)
{
    return cap ? cap->channels : 0u;
}

struct yetty_yplatform_audio_capture_list_ptr_result yetty_yplatform_audio_capture_list_create(void)
{
    /* A context is the backend handle miniaudio enumerates through — the
     * same object ma_device_init opens implicitly when passed NULL. NULL
     * backend list means "probe the platform default order" (on Linux:
     * PulseAudio → ALSA → JACK → sndio). */
    ma_context context;
    if (ma_context_init(NULL, 0u, NULL, &context) != MA_SUCCESS) {
        return YETTY_ERR(yetty_yplatform_audio_capture_list_ptr,
                         "audio-capture: ma_context_init failed (no backend?)");
    }

    ma_device_info *playback_infos = NULL;
    ma_uint32 playback_count = 0u;
    ma_device_info *capture_infos = NULL;
    ma_uint32 capture_count = 0u;
    if (ma_context_get_devices(&context, &playback_infos, &playback_count, &capture_infos,
                               &capture_count) != MA_SUCCESS) {
        ma_context_uninit(&context);
        return YETTY_ERR(yetty_yplatform_audio_capture_list_ptr,
                         "audio-capture: ma_context_get_devices failed");
    }

    struct yetty_yplatform_audio_capture_list *list = calloc(1u, sizeof(*list));
    if (!list) {
        ma_context_uninit(&context);
        return YETTY_ERR(yetty_yplatform_audio_capture_list_ptr,
                         "audio-capture: list alloc failed");
    }
    snprintf(list->backend, sizeof(list->backend), "%s", ma_get_backend_name(context.backend));

    if (capture_count > 0u) {
        list->devices = calloc((size_t)capture_count, sizeof(*list->devices));
        if (!list->devices) {
            free(list);
            ma_context_uninit(&context);
            return YETTY_ERR(yetty_yplatform_audio_capture_list_ptr,
                             "audio-capture: device array alloc failed");
        }
    }

    /* Copy names out now: the ma_device_info array is owned by the context
     * and freed by ma_context_uninit below. */
    for (ma_uint32 i = 0u; i < capture_count; i++) {
        list->devices[i].index = i;
        list->devices[i].is_default = capture_infos[i].isDefault ? true : false;
        snprintf(list->devices[i].name, sizeof(list->devices[i].name), "%s", capture_infos[i].name);
    }
    list->count = (size_t)capture_count;

    ma_context_uninit(&context);
    return YETTY_OK(yetty_yplatform_audio_capture_list_ptr, list);
}

void yetty_yplatform_audio_capture_list_destroy(struct yetty_yplatform_audio_capture_list *list)
{
    if (!list) {
        return;
    }
    free(list->devices);
    free(list);
}
