/*
 * yaudio/envelope.h - per-frame RMS envelope of one audio channel.
 *
 * The envelope is the foundation for both display (yplot of waveform
 * amplitude) and analysis (interval detector picks its noise floor
 * from the envelope's percentile, then walks it with hysteresis). The
 * detector and the GUI share the same array.
 */

#ifndef YETTY_YAUDIO_ENVELOPE_H
#define YETTY_YAUDIO_ENVELOPE_H

#include <yetty/ycore/result.h>
#include <yetty/yaudio/wav.h>
#include <yetty/yaudio/progress.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yaudio_envelope {
    float *rms; /* n floats, linear [0..1] */
    size_t n;
    uint32_t sample_rate;
    uint32_t frame_samples;
    uint32_t hop_samples;
    uint32_t channel;
};

YETTY_YRESULT_DECLARE(yetty_yaudio_envelope_ptr, struct yetty_yaudio_envelope *);

/* Compute the RMS envelope. Pass 0 for frame_samples / hop_samples to
 * use the defaults (1024 each — same as intervals_find). `progress` (may
 * be NULL) is called periodically with a [0, 1] fraction as the channel
 * is streamed; `progress_ud` is passed through to it. */
struct yetty_yaudio_envelope_ptr_result yetty_yaudio_envelope_create(
    const struct yetty_yaudio_wav *w, uint32_t channel, uint32_t frame_samples,
    uint32_t hop_samples, yetty_yaudio_progress_fn progress, void *progress_ud);

void yetty_yaudio_envelope_destroy(struct yetty_yaudio_envelope *env);

/* Seconds <-> envelope-frame helpers. */
static inline double yetty_yaudio_envelope_frame_to_sec(const struct yetty_yaudio_envelope *env,
                                                        size_t frame)
{
    return (double)frame * (double)env->hop_samples / (double)env->sample_rate;
}

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YAUDIO_ENVELOPE_H */
