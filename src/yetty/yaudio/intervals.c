/*
 * intervals.c - energy-based active-interval detector.
 *
 * Pass 1: stream the channel through frame_samples-wide RMS windows,
 *         storing each frame's RMS in an array. Then sort a copy to
 *         pick the noise-floor percentile.
 * Pass 2: walk the per-frame RMS array applying hysteresis thresholds
 *         (open / close) plus min-active and min-gap filters.
 *
 * The whole per-frame RMS array is kept in memory; at hop=1024, 48 kHz,
 * 24 hours is ~4M floats = 16 MB. Comfortable.
 *
 * Decoding is done in chunks (DECODE_CHUNK_FRAMES) to keep the working
 * set inside L2 — the underlying mmap means the data is paged in by
 * the OS as we touch it, not pre-faulted.
 */

#include <yetty/yaudio/intervals.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    DECODE_CHUNK_FRAMES = 65536, /* float buffer of 256 KB */
};

static const double EPSILON_RMS = 1e-12; /* clamps the log of zero */

void yetty_yaudio_intervals_config_defaults(struct yetty_yaudio_intervals_config *cfg)
{
    if (!cfg) {
        return;
    }
    cfg->frame_samples = 1024;
    cfg->hop_samples = 1024;
    cfg->noise_percentile = 0.15f;
    cfg->open_db_above_floor = 10.0f;
    cfg->close_db_above_floor = 6.0f;
    cfg->min_active_sec = 0.050;
    cfg->min_gap_sec = 0.150;
}

static float lin_to_dbfs(float lin)
{
    double v = (double)lin;
    if (v < EPSILON_RMS) {
        v = EPSILON_RMS;
    }
    return (float)(20.0 * log10(v));
}

static int cmp_float_asc(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

/* Stream-decode the channel one chunk at a time, computing per-frame
 * RMS into `rms_out`. Frames span [hop * i, hop * i + frame_samples);
 * a partial trailing frame is dropped. */
static struct yetty_ycore_void_result compute_frame_rms(const struct yetty_yaudio_wav *w,
                                                        uint32_t channel, uint32_t frame_samples,
                                                        uint32_t hop_samples, float *rms_out,
                                                        size_t total_frames,
                                                        yetty_yaudio_progress_fn progress,
                                                        void *progress_ud)
{
    if (frame_samples == 0 || hop_samples == 0) {
        return YETTY_ERR(yetty_ycore_void, "compute_frame_rms: zero frame/hop");
    }

    /* Emit ~200 progress ticks across the whole scan, independent of
     * file size (see the matching note in envelope.c). */
    const size_t progress_step = total_frames > 200 ? total_frames / 200 : 1;

    /* Buffer holds enough samples to span at least one analysis window
     * plus the next hop, so we always have frame_samples consecutive
     * samples in scope after each refill. */
    size_t buf_cap = (size_t)DECODE_CHUNK_FRAMES;
    if (buf_cap < (size_t)frame_samples * 2u) {
        buf_cap = (size_t)frame_samples * 2u;
    }
    float *buf = malloc(buf_cap * sizeof(float));
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void, "compute_frame_rms: buf alloc failed");
    }

    size_t frame_idx = 0;       /* output frame index */
    size_t buf_base_sample = 0; /* sample-index of buf[0] */
    size_t buf_fill = 0;        /* valid samples currently in buf */
    size_t src_off = 0;         /* next sample to decode from the file */
    const size_t src_total = w->frames;

    /* Initial fill. */
    {
        size_t want = buf_cap;
        if (want > src_total - src_off) {
            want = src_total - src_off;
        }
        struct yetty_ycore_size_result r =
            yetty_yaudio_wav_read_channel_f32(w, channel, src_off, buf, want);
        if (YETTY_IS_ERR(r)) {
            free(buf);
            return YETTY_ERR(yetty_ycore_void, "compute_frame_rms: initial read failed", r);
        }
        buf_fill = r.value;
        src_off += r.value;
    }

    while (frame_idx < total_frames) {
        size_t frame_start = (size_t)frame_idx * (size_t)hop_samples;
        size_t need_end = frame_start + (size_t)frame_samples;

        /* Ensure [frame_start, need_end) is inside [buf_base_sample,
         * buf_base_sample + buf_fill). */
        if (frame_start < buf_base_sample) {
            /* Should never happen — we advance monotonically. */
            free(buf);
            return YETTY_ERR(yetty_ycore_void, "compute_frame_rms: frame_start behind buffer");
        }
        if (need_end > buf_base_sample + buf_fill) {
            /* Refill: shift the unread tail down, then read more. */
            size_t drop = frame_start - buf_base_sample;
            if (drop > buf_fill) {
                drop = buf_fill;
            }
            size_t keep = buf_fill - drop;
            if (drop > 0 && keep > 0) {
                memmove(buf, buf + drop, keep * sizeof(float));
            }
            buf_base_sample += drop;
            buf_fill = keep;

            size_t want = buf_cap - buf_fill;
            if (want > src_total - src_off) {
                want = src_total - src_off;
            }
            if (want == 0) {
                /* No more data and the next frame doesn't fit — stop. */
                break;
            }
            struct yetty_ycore_size_result r =
                yetty_yaudio_wav_read_channel_f32(w, channel, src_off, buf + buf_fill, want);
            if (YETTY_IS_ERR(r)) {
                free(buf);
                return YETTY_ERR(yetty_ycore_void, "compute_frame_rms: chunk read failed", r);
            }
            buf_fill += r.value;
            src_off += r.value;
            if (need_end > buf_base_sample + buf_fill) {
                break; /* incomplete trailing frame */
            }
        }

        /* Compute RMS over the window. Sum of squares in double to
         * avoid float catastrophic cancellation on quiet stretches. */
        const float *win = buf + (frame_start - buf_base_sample);
        double sum_sq = 0.0;
        for (uint32_t i = 0; i < frame_samples; i++) {
            double s = (double)win[i];
            sum_sq += s * s;
        }
        rms_out[frame_idx] = (float)sqrt(sum_sq / (double)frame_samples);
        frame_idx++;

        if (progress && (frame_idx % progress_step) == 0) {
            progress(progress_ud, (double)frame_idx / (double)total_frames);
        }
    }

    free(buf);
    if (frame_idx != total_frames) {
        ywarn("compute_frame_rms: produced %zu / %zu frames", frame_idx, total_frames);
    }
    return YETTY_OK_VOID();
}

static float percentile_of(const float *rms, size_t n, float pct)
{
    if (n == 0) {
        return 0.0f;
    }
    float *sorted = malloc(n * sizeof(float));
    if (!sorted) {
        /* Degrade gracefully: return mean instead. */
        double acc = 0.0;
        for (size_t i = 0; i < n; i++) {
            acc += (double)rms[i];
        }
        return (float)(acc / (double)n);
    }
    memcpy(sorted, rms, n * sizeof(float));
    qsort(sorted, n, sizeof(float), cmp_float_asc);

    if (pct < 0.0f) {
        pct = 0.0f;
    }
    if (pct > 1.0f) {
        pct = 1.0f;
    }
    size_t idx = (size_t)((double)pct * (double)(n - 1));
    float v = sorted[idx];
    free(sorted);
    return v;
}

/* Second pass: hysteresis + duration filters. */
static struct yetty_yaudio_intervals_ptr_result build_intervals(
    const float *rms, size_t n, float open_lin, float close_lin, uint32_t hop_samples,
    uint32_t frame_samples, uint32_t sample_rate, double min_active_sec, double min_gap_sec,
    float noise_floor_dbfs, float open_thr_dbfs, float close_thr_dbfs)
{
    /* Provisional intervals: produced by hysteresis only.
     * Then merge across short gaps, then drop short actives. */

    /* Worst-case: half the frames flip — but n is bounded, just over-
     * allocate the upper bound (n/2 + 1) and shrink at the end. */
    size_t cap = n / 2 + 4;
    struct yetty_yaudio_interval *items = malloc(cap * sizeof(*items));
    if (!items) {
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "build_intervals: items alloc failed");
    }
    size_t count = 0;

    bool active = false;
    size_t start_frame = 0;
    float peak_lin = 0.0f;
    double sum_sq = 0.0;
    size_t inside_n = 0;

    const double sr_d = (double)sample_rate;
    const double hop_d = (double)hop_samples;
    const double frame_dur = (double)frame_samples / sr_d;

    for (size_t i = 0; i < n; i++) {
        float v = rms[i];
        if (!active) {
            if (v >= open_lin) {
                active = true;
                start_frame = i;
                peak_lin = v;
                sum_sq = (double)v * (double)v;
                inside_n = 1;
            }
        } else {
            inside_n++;
            sum_sq += (double)v * (double)v;
            if (v > peak_lin) {
                peak_lin = v;
            }
            if (v < close_lin) {
                /* Close: emit interval. */
                double start_sec = (double)start_frame * hop_d / sr_d;
                double end_sec = (double)i * hop_d / sr_d + frame_dur;
                if (count == cap) {
                    cap *= 2;
                    struct yetty_yaudio_interval *nb = realloc(items, cap * sizeof(*items));
                    if (!nb) {
                        free(items);
                        return YETTY_ERR(yetty_yaudio_intervals_ptr,
                                         "build_intervals: realloc failed");
                    }
                    items = nb;
                }
                float rms_lin = (float)sqrt(sum_sq / (double)inside_n);
                items[count].start_sec = start_sec;
                items[count].end_sec = end_sec;
                items[count].peak_dbfs = lin_to_dbfs(peak_lin);
                items[count].rms_dbfs = lin_to_dbfs(rms_lin);
                count++;
                active = false;
            }
        }
    }
    /* Trailing active interval — close at end. */
    if (active) {
        double start_sec = (double)start_frame * hop_d / sr_d;
        double end_sec = (double)(n - 1) * hop_d / sr_d + frame_dur;
        if (count == cap) {
            cap *= 2;
            struct yetty_yaudio_interval *nb = realloc(items, cap * sizeof(*items));
            if (!nb) {
                free(items);
                return YETTY_ERR(yetty_yaudio_intervals_ptr, "build_intervals: realloc failed");
            }
            items = nb;
        }
        float rms_lin = (float)sqrt(sum_sq / (double)inside_n);
        items[count].start_sec = start_sec;
        items[count].end_sec = end_sec;
        items[count].peak_dbfs = lin_to_dbfs(peak_lin);
        items[count].rms_dbfs = lin_to_dbfs(rms_lin);
        count++;
    }

    /* Merge across short gaps (linear sweep in place). */
    if (min_gap_sec > 0.0 && count > 1) {
        size_t w = 0;
        for (size_t r = 0; r < count; r++) {
            if (w == 0) {
                items[w++] = items[r];
                continue;
            }
            double gap = items[r].start_sec - items[w - 1].end_sec;
            if (gap < min_gap_sec) {
                /* Merge: extend prev, take max peak. RMS is approximated
                 * (proper merge would need per-interval sum_sq carried
                 * forward — good enough for log/display). */
                if (items[r].end_sec > items[w - 1].end_sec) {
                    items[w - 1].end_sec = items[r].end_sec;
                }
                if (items[r].peak_dbfs > items[w - 1].peak_dbfs) {
                    items[w - 1].peak_dbfs = items[r].peak_dbfs;
                }
                if (items[r].rms_dbfs > items[w - 1].rms_dbfs) {
                    items[w - 1].rms_dbfs = items[r].rms_dbfs;
                }
            } else {
                items[w++] = items[r];
            }
        }
        count = w;
    }

    /* Drop too-short intervals. */
    if (min_active_sec > 0.0) {
        size_t w = 0;
        for (size_t r = 0; r < count; r++) {
            double dur = items[r].end_sec - items[r].start_sec;
            if (dur >= min_active_sec) {
                items[w++] = items[r];
            }
        }
        count = w;
    }

    struct yetty_yaudio_intervals *iv = calloc(1, sizeof(*iv));
    if (!iv) {
        free(items);
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "build_intervals: result alloc failed");
    }
    iv->items = items;
    iv->n = count;
    iv->noise_floor_dbfs = noise_floor_dbfs;
    iv->open_threshold_dbfs = open_thr_dbfs;
    iv->close_threshold_dbfs = close_thr_dbfs;
    iv->frame_samples = frame_samples;
    iv->hop_samples = hop_samples;
    iv->total_frames = n;
    return YETTY_OK(yetty_yaudio_intervals_ptr, iv);
}

struct yetty_yaudio_intervals_ptr_result yetty_yaudio_intervals_find(
    const struct yetty_yaudio_wav *w, uint32_t channel,
    const struct yetty_yaudio_intervals_config *cfg_in, yetty_yaudio_progress_fn progress,
    void *progress_ud)
{
    if (!w) {
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "intervals_find: NULL wav");
    }
    if (channel >= w->channels) {
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "intervals_find: channel out of range");
    }

    struct yetty_yaudio_intervals_config cfg;
    yetty_yaudio_intervals_config_defaults(&cfg);
    if (cfg_in) {
        cfg = *cfg_in;
        if (cfg.frame_samples == 0) {
            cfg.frame_samples = 1024;
        }
        if (cfg.hop_samples == 0) {
            cfg.hop_samples = cfg.frame_samples;
        }
    }

    if ((size_t)cfg.frame_samples > w->frames) {
        return YETTY_ERR(yetty_yaudio_intervals_ptr,
                         "intervals_find: file shorter than one analysis frame");
    }

    /* Number of complete frames we can extract. */
    size_t total_frames = (w->frames - cfg.frame_samples) / (size_t)cfg.hop_samples + 1;

    float *rms = malloc(total_frames * sizeof(float));
    if (!rms) {
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "intervals_find: rms alloc failed");
    }

    struct yetty_ycore_void_result rr = compute_frame_rms(
        w, channel, cfg.frame_samples, cfg.hop_samples, rms, total_frames, progress, progress_ud);
    if (YETTY_IS_ERR(rr)) {
        free(rms);
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "intervals_find: pass 1 failed", rr);
    }

    float noise_floor_lin = percentile_of(rms, total_frames, cfg.noise_percentile);
    float floor_dbfs = lin_to_dbfs(noise_floor_lin);
    float open_thr_dbfs = floor_dbfs + cfg.open_db_above_floor;
    float close_thr_dbfs = floor_dbfs + cfg.close_db_above_floor;
    float open_lin = (float)pow(10.0, (double)open_thr_dbfs / 20.0);
    float close_lin = (float)pow(10.0, (double)close_thr_dbfs / 20.0);

    yinfo("intervals_find: ch=%u frames=%zu floor=%.1f dBFS open=%.1f close=%.1f", channel,
          total_frames, floor_dbfs, open_thr_dbfs, close_thr_dbfs);

    struct yetty_yaudio_intervals_ptr_result ir = build_intervals(
        rms, total_frames, open_lin, close_lin, cfg.hop_samples, cfg.frame_samples, w->sample_rate,
        cfg.min_active_sec, cfg.min_gap_sec, floor_dbfs, open_thr_dbfs, close_thr_dbfs);
    free(rms);
    if (YETTY_IS_ERR(ir)) {
        return YETTY_ERR(yetty_yaudio_intervals_ptr, "intervals_find: pass 2 failed", ir);
    }
    return ir;
}

void yetty_yaudio_intervals_destroy(struct yetty_yaudio_intervals *iv)
{
    if (!iv) {
        return;
    }
    free(iv->items);
    free(iv);
}
