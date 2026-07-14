/*
 * yplatform/audio.h - cross-platform audio playback device.
 *
 * Thin wrapper around miniaudio (mackron, MIT-0). The backend picks a
 * per-OS API at compile time:
 *   Linux:    PulseAudio → ALSA → JACK → sndio (auto-fallback)
 *   macOS/iOS/tvOS: CoreAudio / AudioUnit
 *   Windows:  WASAPI → DirectSound → WinMM
 *   Android:  AAudio → OpenSL ES
 *   Web:      WebAudio
 *
 * Yetty consumers see only the abstract `yetty_yplatform_audio_device`
 * type — miniaudio's symbols stay confined to the implementation TU
 * (src/yetty/yplatform/audio/default.c, the one place that defines
 * MINIAUDIO_IMPLEMENTATION).
 *
 * Lifecycle:
 *
 *   create(sample_rate, channels)        → device (stopped)
 *   start(device)                         → audio callback fires
 *   write_pcm(device, frames, frame_count) → push interleaved float32 PCM
 *   stop(device)
 *   destroy(device)
 *
 * write_pcm is non-blocking: it copies into the device's internal ring.
 * If the ring is full, the surplus frames are dropped and the call
 * returns the actual count written. The audio callback drains the ring
 * on its own thread; lock-free w/ a single producer (the decode-side
 * caller of write_pcm) and a single consumer (the backend's callback).
 *
 * The "stream PTS" — used by yvideo for A/V sync — is the monotonic
 * sample count the device has *played out* since start(). Reported in
 * seconds via played_out_sec(). On wall-clock terms: each frame the
 * backend drains advances the counter by 1/sample_rate; the
 * device-side clock is the master of A/V sync.
 */

#ifndef YETTY_YPLATFORM_AUDIO_H
#define YETTY_YPLATFORM_AUDIO_H

#include <yetty/ycore/result.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_audio_device;

YETTY_YRESULT_DECLARE(yetty_yplatform_audio_device_ptr, struct yetty_yplatform_audio_device *);

/* Open a playback device for `sample_rate` Hz, `channels` channels of
 * interleaved float32. Typical: 48000, 2. Device is created in the
 * stopped state. */
struct yetty_yplatform_audio_device_ptr_result yetty_yplatform_audio_device_create(
    uint32_t sample_rate, uint32_t channels);

void yetty_yplatform_audio_device_destroy(struct yetty_yplatform_audio_device *dev);

/* Start / stop the backend callback. write_pcm may be called either
 * before start() (the data buffers until then) or after — either order
 * is safe. */
struct yetty_ycore_void_result yetty_yplatform_audio_device_start(
    struct yetty_yplatform_audio_device *dev);

struct yetty_ycore_void_result yetty_yplatform_audio_device_stop(
    struct yetty_yplatform_audio_device *dev);

/* Push interleaved float32 PCM into the device's ring. `frames` is the
 * count of multi-channel frames (NOT samples); the buffer size is
 * frames * channels * sizeof(float). Returns the number of frames
 * actually consumed — may be < frames when the ring is full. */
struct yetty_ycore_size_result yetty_yplatform_audio_device_write_pcm(
    struct yetty_yplatform_audio_device *dev, const float *pcm, size_t frames);

/* Current playback clock: seconds of audio the device has drained out
 * since start(). The yvideo A/V sync uses this as the master clock. */
double yetty_yplatform_audio_device_played_out_sec(const struct yetty_yplatform_audio_device *dev);

/* Discard everything currently queued in the device's ring. Used by
 * the seek path so post-seek audio doesn't mix with pre-seek bytes. */
void yetty_yplatform_audio_device_flush(struct yetty_yplatform_audio_device *dev);

uint32_t yetty_yplatform_audio_device_sample_rate(const struct yetty_yplatform_audio_device *dev);

uint32_t yetty_yplatform_audio_device_channels(const struct yetty_yplatform_audio_device *dev);

/*
 * Capture-device API — parallel to the playback API above. Opens the
 * system's default microphone with a signed-16 interleaved format,
 * because AAC / Opus encoders and the mp4 recorder consume s16 natively.
 *
 * The backend fills an internal ring on its own thread; the consumer
 * calls read_s16 from wherever it wants (poll loop, encoder pump). No
 * callback delivery — that would drag threading concerns into every
 * caller and doesn't buy anything the ring can't provide.
 */
struct yetty_yplatform_audio_capture;

YETTY_YRESULT_DECLARE(yetty_yplatform_audio_capture_ptr, struct yetty_yplatform_audio_capture *);

/* Open a capture device. `device_sel` picks the input:
 *   - NULL / "" / "default"  → the backend's default microphone.
 *   - an all-digit string     → the device at that enumeration index
 *                               (see yetty_yplatform_audio_capture_list_create
 *                               / the -i/--info listing).
 *   - any other string        → the first device whose name contains it
 *                               (case-insensitive substring).
 * A non-empty selector that matches nothing is an error (the caller asked
 * for a specific mic and it isn't there) — no silent fallback to default. */
struct yetty_yplatform_audio_capture_ptr_result yetty_yplatform_audio_capture_create(
    uint32_t sample_rate, uint32_t channels, const char *device_sel);

void yetty_yplatform_audio_capture_destroy(struct yetty_yplatform_audio_capture *cap);

struct yetty_ycore_void_result yetty_yplatform_audio_capture_start(
    struct yetty_yplatform_audio_capture *cap);

struct yetty_ycore_void_result yetty_yplatform_audio_capture_stop(
    struct yetty_yplatform_audio_capture *cap);

/* Drain up to `frames` multi-channel s16 frames into `pcm`. Returns the
 * count actually read — may be < frames if the ring is empty (returns 0
 * in that case, never blocks). Buffer size is frames * channels *
 * sizeof(int16_t). */
struct yetty_ycore_size_result yetty_yplatform_audio_capture_read_s16(
    struct yetty_yplatform_audio_capture *cap, int16_t *pcm, size_t frames);

uint32_t yetty_yplatform_audio_capture_sample_rate(const struct yetty_yplatform_audio_capture *cap);

uint32_t yetty_yplatform_audio_capture_channels(const struct yetty_yplatform_audio_capture *cap);

/*
 * Capture-device enumeration — the backend's list of microphones/inputs.
 *
 * miniaudio identifies a device by a per-backend `ma_device_id` union (an
 * ALSA/PulseAudio name string, a WASAPI wide string, a CoreAudio UID, an
 * integer on AAudio/WinMM, …) — there is no single cross-platform string
 * form. The portable handle a caller can surface to a user is therefore
 * the human-readable `name` (or the enumeration `index`); a future
 * device-selection flag resolves that back to a device by matching the
 * enumerated list.
 */
struct yetty_yplatform_audio_capture_info {
    uint32_t index;  /* position in the enumerated list */
    bool is_default; /* the device opened when none is requested */
    char name[256];  /* human-readable label from the backend */
};

struct yetty_yplatform_audio_capture_list {
    struct yetty_yplatform_audio_capture_info *devices; /* `count` entries */
    size_t count;
    char backend[64]; /* backend that produced the list, e.g. "PulseAudio" */
};

YETTY_YRESULT_DECLARE(yetty_yplatform_audio_capture_list_ptr,
                      struct yetty_yplatform_audio_capture_list *);

/* Enumerate the system's capture (input) devices. Opens a throwaway
 * backend context, queries it, and copies the results out — the returned
 * list owns its own storage and stays valid after the context is torn
 * down. Free with yetty_yplatform_audio_capture_list_destroy. */
struct yetty_yplatform_audio_capture_list_ptr_result yetty_yplatform_audio_capture_list_create(
    void);

void yetty_yplatform_audio_capture_list_destroy(struct yetty_yplatform_audio_capture_list *list);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_AUDIO_H */
