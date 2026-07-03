/*
 * history.h — fixed-capacity rolling ring buffer of floats, used to feed the
 * inline sparkline graphs (CPU / memory / network throughput over time).
 *
 * Header-only + inline: pushing a sample is O(1); linearising copies the
 * samples oldest→newest into a caller buffer the sparkline builder walks.
 */
#ifndef YTOP_HISTORY_H
#define YTOP_HISTORY_H

#include <string.h>

#define YTOP_HISTORY_CAP 120

struct ytop_history {
    float samples[YTOP_HISTORY_CAP];
    int count; /* valid samples, saturating at YTOP_HISTORY_CAP */
    int head;  /* index of the next write slot */
};

static inline void ytop_history_reset(struct ytop_history *history)
{
    memset(history, 0, sizeof(*history));
}

static inline void ytop_history_push(struct ytop_history *history, float value)
{
    history->samples[history->head] = value;
    history->head = (history->head + 1) % YTOP_HISTORY_CAP;
    if (history->count < YTOP_HISTORY_CAP) {
        history->count++;
    }
}

/* Copy the retained samples oldest→newest into out[0..max). Returns the number
 * written (<= min(count, max)). */
static inline int ytop_history_linearize(const struct ytop_history *history, float *out, int max)
{
    int count = history->count < max ? history->count : max;
    /* The oldest sample sits `count` slots behind head (mod cap). */
    int start = (history->head - count + YTOP_HISTORY_CAP) % YTOP_HISTORY_CAP;
    for (int i = 0; i < count; i++) {
        out[i] = history->samples[(start + i) % YTOP_HISTORY_CAP];
    }
    return count;
}

#endif /* YTOP_HISTORY_H */
