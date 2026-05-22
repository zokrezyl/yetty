/*
 * envelope.c - per-frame RMS envelope of a WAV channel.
 *
 * Decodes the channel in chunks (DECODE_CHUNK_FRAMES) so the working
 * set stays inside L2; mmap pages are demand-faulted by the kernel as
 * we touch them. Sum-of-squares is accumulated in double to avoid
 * float catastrophic cancellation on long quiet stretches.
 */

#include <yetty/yaudio/envelope.h>
#include <yetty/ytrace/ytrace.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    DECODE_CHUNK_FRAMES = 65536,   /* 256 KB float buffer */
};

struct yetty_yaudio_envelope_ptr_result
yetty_yaudio_envelope_create(const struct yetty_yaudio_wav *w,
                             uint32_t channel,
                             uint32_t frame_samples,
                             uint32_t hop_samples)
{
    if (!w) {
        return YETTY_ERR(yetty_yaudio_envelope_ptr,
                         "envelope_create: NULL wav");
    }
    if (channel >= w->channels) {
        return YETTY_ERR(yetty_yaudio_envelope_ptr,
                         "envelope_create: channel out of range");
    }
    if (frame_samples == 0) frame_samples = 1024;
    if (hop_samples == 0)   hop_samples   = frame_samples;
    if ((size_t)frame_samples > w->frames) {
        return YETTY_ERR(yetty_yaudio_envelope_ptr,
                         "envelope_create: file shorter than one frame");
    }

    size_t total_frames = (w->frames - frame_samples) / (size_t)hop_samples + 1;

    struct yetty_yaudio_envelope *env = calloc(1, sizeof(*env));
    if (!env) {
        return YETTY_ERR(yetty_yaudio_envelope_ptr,
                         "envelope_create: env alloc failed");
    }
    env->rms = malloc(total_frames * sizeof(float));
    if (!env->rms) {
        free(env);
        return YETTY_ERR(yetty_yaudio_envelope_ptr,
                         "envelope_create: rms alloc failed");
    }
    env->n             = total_frames;
    env->sample_rate   = w->sample_rate;
    env->frame_samples = frame_samples;
    env->hop_samples   = hop_samples;
    env->channel       = channel;

    /* Buffer holds at least one analysis window plus the next hop. */
    size_t buf_cap = DECODE_CHUNK_FRAMES;
    if (buf_cap < (size_t)frame_samples * 2u) {
        buf_cap = (size_t)frame_samples * 2u;
    }
    float *buf = malloc(buf_cap * sizeof(float));
    if (!buf) {
        free(env->rms);
        free(env);
        return YETTY_ERR(yetty_yaudio_envelope_ptr,
                         "envelope_create: buf alloc failed");
    }

    size_t frame_idx = 0;
    size_t buf_base_sample = 0;
    size_t buf_fill = 0;
    size_t src_off = 0;
    const size_t src_total = w->frames;

    /* Initial fill */
    {
        size_t want = buf_cap;
        if (want > src_total - src_off) want = src_total - src_off;
        struct yetty_ycore_size_result r =
            yetty_yaudio_wav_read_channel_f32(w, channel, src_off, buf, want);
        if (YETTY_IS_ERR(r)) {
            free(buf);
            free(env->rms);
            free(env);
            return YETTY_ERR(yetty_yaudio_envelope_ptr,
                             "envelope_create: initial read failed", r);
        }
        buf_fill = r.value;
        src_off += r.value;
    }

    while (frame_idx < total_frames) {
        size_t frame_start = frame_idx * (size_t)hop_samples;
        size_t need_end    = frame_start + (size_t)frame_samples;

        if (need_end > buf_base_sample + buf_fill) {
            size_t drop = frame_start - buf_base_sample;
            if (drop > buf_fill) drop = buf_fill;
            size_t keep = buf_fill - drop;
            if (drop > 0 && keep > 0) {
                memmove(buf, buf + drop, keep * sizeof(float));
            }
            buf_base_sample += drop;
            buf_fill = keep;

            size_t want = buf_cap - buf_fill;
            if (want > src_total - src_off) want = src_total - src_off;
            if (want == 0) break;

            struct yetty_ycore_size_result r =
                yetty_yaudio_wav_read_channel_f32(w, channel, src_off,
                                                  buf + buf_fill, want);
            if (YETTY_IS_ERR(r)) {
                free(buf);
                free(env->rms);
                free(env);
                return YETTY_ERR(yetty_yaudio_envelope_ptr,
                                 "envelope_create: chunk read failed", r);
            }
            buf_fill += r.value;
            src_off  += r.value;
            if (need_end > buf_base_sample + buf_fill) break;
        }

        const float *win = buf + (frame_start - buf_base_sample);
        double sum_sq = 0.0;
        for (uint32_t i = 0; i < frame_samples; i++) {
            double s = (double)win[i];
            sum_sq += s * s;
        }
        env->rms[frame_idx] = (float)sqrt(sum_sq / (double)frame_samples);
        frame_idx++;
    }

    free(buf);

    if (frame_idx != total_frames) {
        ywarn("envelope_create: produced %zu / %zu frames",
              frame_idx, total_frames);
        env->n = frame_idx;
    }
    yinfo("envelope_create: ch=%u frames=%zu (%.1f s)",
          channel, env->n,
          yetty_yaudio_envelope_frame_to_sec(env, env->n));
    return YETTY_OK(yetty_yaudio_envelope_ptr, env);
}

void yetty_yaudio_envelope_destroy(struct yetty_yaudio_envelope *env)
{
    if (!env) return;
    free(env->rms);
    free(env);
}
