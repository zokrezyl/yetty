/*
 * cpu.h — CPU abstraction: per-core utilisation, aggregate load, frequency,
 * temperature, and the CPU model string.
 *
 * The monitor holds the previous jiffy sample so a call to ytop_cpu_sample()
 * can turn two cumulative /proc/stat-style readings into instantaneous
 * percentages. The caller embeds the monitor (see struct ytop_app) — no
 * hidden global state.
 */
#ifndef YTOP_PLATFORM_CPU_CPU_H
#define YTOP_PLATFORM_CPU_CPU_H

#include <stdint.h>

#include <yetty/ycore/result.h>

#include "platform/limits.h"

/* One CPU time bucket. Field names mirror the Linux /proc/stat columns, but
 * the struct is platform-independent — every backend fills what it can and
 * leaves the rest zero. */
struct ytop_cpu_times {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
};

struct ytop_cpu_core {
    float usage_pct;   /* 0..100 over the last interval */
    uint64_t freq_khz; /* current frequency, 0 if unknown */
};

/* A full CPU reading for one tick. */
struct ytop_cpu_snapshot {
    int n_cores;     /* number of logical cores populated in cores[] */
    float total_pct; /* aggregate utilisation 0..100 */
    struct ytop_cpu_core cores[YTOP_MAX_CORES];
    float load_avg[3];  /* 1 / 5 / 15 minute load average */
    float temp_celsius; /* package temperature, -1 if unknown */
    char model[128];    /* CPU model string, empty if unknown */
};

/* Owns the previous sample so deltas can be computed. Caller-embedded;
 * zero-initialise then call ytop_cpu_monitor_init() once. */
struct ytop_cpu_monitor {
    int have_prev;
    int n_cores;
    /* index 0 is the aggregate line, 1..n_cores are per-core. */
    struct ytop_cpu_times prev[YTOP_MAX_CORES + 1];
};

/* Seed the monitor with a first reading so the first sample yields a real
 * delta rather than a spike. Safe to call on a zeroed struct. */
struct yetty_ycore_void_result ytop_cpu_monitor_init(struct ytop_cpu_monitor *monitor);

/* Take a reading: fills *out with per-core + aggregate percentages (computed
 * against the monitor's previous sample), load average, frequency, and
 * temperature, then stores the raw counters back into the monitor. */
struct yetty_ycore_void_result ytop_cpu_sample(struct ytop_cpu_monitor *monitor,
                                               struct ytop_cpu_snapshot *out);

#endif /* YTOP_PLATFORM_CPU_CPU_H */
