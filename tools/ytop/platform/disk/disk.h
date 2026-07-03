/*
 * disk.h — disk abstraction: filesystem usage per mounted device plus an
 * aggregate read/write throughput derived from block-device counters.
 *
 * Usage figures are self-contained per sample; the I/O rates need the previous
 * counters, so the monitor carries them.
 */
#ifndef YTOP_PLATFORM_DISK_DISK_H
#define YTOP_PLATFORM_DISK_DISK_H

#include <stdint.h>

#include <yetty/ycore/result.h>

#include "platform/limits.h"

struct ytop_disk_mount {
    char device[64];
    char mount_point[128];
    char fstype[32];
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint64_t available_bytes;
    float used_pct; /* 0..100 */
};

struct ytop_disk_snapshot {
    int n_mounts;
    struct ytop_disk_mount mounts[YTOP_MAX_MOUNTS];
    double io_read_rate;  /* bytes/second, aggregate over whole disks */
    double io_write_rate; /* bytes/second, aggregate over whole disks */
};

struct ytop_disk_monitor {
    int have_prev;
    uint64_t prev_read_sectors;
    uint64_t prev_write_sectors;
};

/* Seed the monitor with a first I/O reading (safe on a zeroed struct). */
struct yetty_ycore_void_result ytop_disk_monitor_init(struct ytop_disk_monitor *monitor);

/* Fill *out with per-mount usage and aggregate I/O rates over
 * `interval_seconds`, then refresh the monitor's previous I/O counters. */
struct yetty_ycore_void_result ytop_disk_sample(struct ytop_disk_monitor *monitor,
                                                struct ytop_disk_snapshot *out,
                                                float interval_seconds);

#endif /* YTOP_PLATFORM_DISK_DISK_H */
