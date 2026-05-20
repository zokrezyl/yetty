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
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplatform_audio_device;

YETTY_YRESULT_DECLARE(yetty_yplatform_audio_device_ptr,
                      struct yetty_yplatform_audio_device *);

/* Open a playback device for `sample_rate` Hz, `channels` channels of
 * interleaved float32. Typical: 48000, 2. Device is created in the
 * stopped state. */
struct yetty_yplatform_audio_device_ptr_result
yetty_yplatform_audio_device_create(uint32_t sample_rate, uint32_t channels);

void yetty_yplatform_audio_device_destroy(
    struct yetty_yplatform_audio_device *dev);

/* Start / stop the backend callback. write_pcm may be called either
 * before start() (the data buffers until then) or after — either order
 * is safe. */
struct yetty_ycore_void_result
yetty_yplatform_audio_device_start(struct yetty_yplatform_audio_device *dev);

struct yetty_ycore_void_result
yetty_yplatform_audio_device_stop(struct yetty_yplatform_audio_device *dev);

/* Push interleaved float32 PCM into the device's ring. `frames` is the
 * count of multi-channel frames (NOT samples); the buffer size is
 * frames * channels * sizeof(float). Returns the number of frames
 * actually consumed — may be < frames when the ring is full. */
struct yetty_ycore_size_result
yetty_yplatform_audio_device_write_pcm(
    struct yetty_yplatform_audio_device *dev,
    const float *pcm,
    size_t frames);

/* Current playback clock: seconds of audio the device has drained out
 * since start(). The yvideo A/V sync uses this as the master clock. */
double yetty_yplatform_audio_device_played_out_sec(
    const struct yetty_yplatform_audio_device *dev);

/* Discard everything currently queued in the device's ring. Used by
 * the seek path so post-seek audio doesn't mix with pre-seek bytes. */
void yetty_yplatform_audio_device_flush(
    struct yetty_yplatform_audio_device *dev);

uint32_t yetty_yplatform_audio_device_sample_rate(
    const struct yetty_yplatform_audio_device *dev);

uint32_t yetty_yplatform_audio_device_channels(
    const struct yetty_yplatform_audio_device *dev);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_AUDIO_H */
