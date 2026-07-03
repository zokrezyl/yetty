/*
 * process.h — process abstraction: one enumerable snapshot of running
 * processes, each with %CPU (interval delta) and %MEM (RSS vs total RAM).
 *
 * The monitor carries the previous per-pid CPU-time counters so a sample can
 * turn cumulative CPU jiffies into an interval percentage, plus the platform
 * clock-tick rate captured at init.
 */
#ifndef YTOP_PLATFORM_PROCESS_PROCESS_H
#define YTOP_PLATFORM_PROCESS_PROCESS_H

#include <stdint.h>

#include <yetty/ycore/result.h>

#include "platform/limits.h"

struct ytop_proc_entry {
    int pid;
    int ppid;
    char state; /* R/S/D/Z/T … single-letter process state */
    float cpu_pct;
    float mem_pct;
    uint64_t rss_bytes;
    uint64_t cpu_time_ticks; /* accumulated (utime+stime) in clock ticks (delta source) */
    uint64_t cpu_time_sec;   /* same, converted to seconds for display */
    int num_threads;
    uint32_t uid;
    char user[32];
    char comm[64];     /* short name from /proc/<pid>/stat */
    char cmdline[256]; /* full command line, falls back to comm */
};

/* Compact previous-sample entry: enough to compute the next %CPU delta. */
struct ytop_process_prev_entry {
    int pid;
    uint64_t cpu_jiffies;
};

struct ytop_process_monitor {
    long clock_ticks; /* sysconf(_SC_CLK_TCK), captured at init */
    int n_prev;
    struct ytop_process_prev_entry prev[YTOP_MAX_PROCS];
};

/* Capture the clock-tick rate. Safe to call on a zeroed struct. */
struct yetty_ycore_void_result ytop_process_monitor_init(struct ytop_process_monitor *monitor);

/* Enumerate processes into out[0..max), writing the count to *n_out. %CPU is
 * computed against the monitor's previous sample over `interval_seconds`; %MEM
 * uses `ram_total_bytes` (0 disables %MEM). The monitor's prev table is
 * refreshed for the next call. */
struct yetty_ycore_void_result ytop_process_sample(struct ytop_process_monitor *monitor,
                                                   struct ytop_proc_entry *out, int max, int *n_out,
                                                   float interval_seconds,
                                                   uint64_t ram_total_bytes);

/* Send `signal` to `pid` (e.g. SIGTERM/SIGKILL). Backs a future "kill from the
 * process box" interaction. */
struct yetty_ycore_void_result ytop_process_signal(int pid, int signal);

#endif /* YTOP_PLATFORM_PROCESS_PROCESS_H */
