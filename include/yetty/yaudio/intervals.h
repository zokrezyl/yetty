/*
 * yaudio/intervals.h - energy-based interval detector.
 *
 * Splits an audio channel into "active" intervals (above the noise
 * floor) and silent gaps. Two-pass algorithm:
 *
 *   1. Per-frame RMS across the whole channel → noise-floor estimate
 *      via percentile of the RMS distribution.
 *   2. Hysteresis threshold (open > floor + open_db; close > floor +
 *      close_db) with min-duration and min-gap filtering.
 *
 * Intended for very long recordings (multi-hour) where the noise floor
 * is well-defined by the *typical* (15th-percentile-ish) frame energy.
 */

#ifndef YETTY_YAUDIO_INTERVALS_H
#define YETTY_YAUDIO_INTERVALS_H

#include <yetty/ycore/result.h>
#include <yetty/yaudio/wav.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yaudio_interval {
    double start_sec;
    double end_sec;
    float  peak_dbfs;     /* loudest frame RMS inside the interval */
    float  rms_dbfs;      /* mean square energy across the interval */
};

struct yetty_yaudio_intervals_config {
    uint32_t frame_samples;        /* analysis window size (default 1024) */
    uint32_t hop_samples;          /* stride between frames (default = frame_samples) */
    float    noise_percentile;     /* 0..1, default 0.15 */
    float    open_db_above_floor;  /* default +10 dB */
    float    close_db_above_floor; /* default +6  dB */
    double   min_active_sec;       /* drop intervals shorter than this; default 0.050 */
    double   min_gap_sec;          /* merge if gap to previous is shorter; default 0.150 */
};

struct yetty_yaudio_intervals {
    struct yetty_yaudio_interval *items;
    size_t                        n;

    /* analysis metadata — useful for the caller to log/inspect */
    float    noise_floor_dbfs;
    float    open_threshold_dbfs;
    float    close_threshold_dbfs;
    uint32_t frame_samples;
    uint32_t hop_samples;
    size_t   total_frames;        /* frames analysed */
};

YETTY_YRESULT_DECLARE(yetty_yaudio_intervals_ptr, struct yetty_yaudio_intervals *);

/* Fill `cfg` with sensible defaults. Caller may then override fields. */
void yetty_yaudio_intervals_config_defaults(struct yetty_yaudio_intervals_config *cfg);

/* Analyse one channel of `w`. Heap-allocates the result; destroy with
 * yetty_yaudio_intervals_destroy(). */
struct yetty_yaudio_intervals_ptr_result
yetty_yaudio_intervals_find(const struct yetty_yaudio_wav *w,
                            uint32_t channel,
                            const struct yetty_yaudio_intervals_config *cfg);

void yetty_yaudio_intervals_destroy(struct yetty_yaudio_intervals *iv);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YAUDIO_INTERVALS_H */
