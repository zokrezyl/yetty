/*
 * disk/linux.c — Linux disk backend.
 *
 * Mount usage: /proc/mounts (real block devices only — device path begins
 * "/dev/") sized with statvfs().
 *
 * I/O throughput: /proc/diskstats, summing whole-disk devices (those listed in
 * /sys/block, so partitions are not double-counted). Fields 3/6/10 are the
 * device name, sectors read, and sectors written; one sector is 512 bytes.
 */
#include "platform/disk/disk.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>

#define YTOP_DISK_SECTOR_BYTES 512u

static void read_mounts(struct ytop_disk_snapshot *out)
{
    FILE *file = fopen("/proc/mounts", "r");
    if (!file) {
        return;
    }
    char device[256], mount_point[256], fstype[128];
    int count = 0;
    while (count < YTOP_MAX_MOUNTS &&
           fscanf(file, "%255s %255s %127s %*s %*d %*d\n", device, mount_point, fstype) == 3) {
        /* Only real block devices — skips proc/sysfs/tmpfs/cgroup/overlay. */
        if (strncmp(device, "/dev/", 5) != 0) {
            continue;
        }
        struct statvfs vfs;
        if (statvfs(mount_point, &vfs) != 0) {
            continue;
        }
        uint64_t block = (uint64_t)vfs.f_frsize;
        uint64_t total = block * (uint64_t)vfs.f_blocks;
        uint64_t available = block * (uint64_t)vfs.f_bavail;
        uint64_t free_total = block * (uint64_t)vfs.f_bfree;
        if (total == 0) {
            continue;
        }
        struct ytop_disk_mount *mount = &out->mounts[count++];
        memset(mount, 0, sizeof(*mount));
        strncpy(mount->device, device, sizeof(mount->device) - 1);
        strncpy(mount->mount_point, mount_point, sizeof(mount->mount_point) - 1);
        strncpy(mount->fstype, fstype, sizeof(mount->fstype) - 1);
        mount->total_bytes = total;
        mount->available_bytes = available;
        mount->used_bytes = total - free_total;
        mount->used_pct = 100.0f * (float)mount->used_bytes / (float)total;
    }
    fclose(file);
    out->n_mounts = count;
}

/* Whole-disk test: the name is present as a directory under /sys/block. */
static int is_whole_disk(const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/block/%s", name);
    DIR *dir = opendir(path);
    if (!dir) {
        return 0;
    }
    closedir(dir);
    return 1;
}

/* Sum sectors read/written across whole disks in /proc/diskstats. */
static int read_diskstats(uint64_t *read_sectors, uint64_t *write_sectors)
{
    FILE *file = fopen("/proc/diskstats", "r");
    if (!file) {
        return -1;
    }
    *read_sectors = 0;
    *write_sectors = 0;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        unsigned int major, minor;
        char name[64];
        unsigned long long reads, read_merges, sectors_read, read_ticks;
        unsigned long long writes, write_merges, sectors_written;
        int fields = sscanf(line, "%u %u %63s %llu %llu %llu %llu %llu %llu %llu", &major, &minor,
                            name, &reads, &read_merges, &sectors_read, &read_ticks, &writes,
                            &write_merges, &sectors_written);
        if (fields < 10) {
            continue;
        }
        if (!is_whole_disk(name)) {
            continue;
        }
        *read_sectors += (uint64_t)sectors_read;
        *write_sectors += (uint64_t)sectors_written;
    }
    fclose(file);
    return 0;
}

struct yetty_ycore_void_result ytop_disk_monitor_init(struct ytop_disk_monitor *monitor)
{
    if (!monitor) {
        return YETTY_ERR(yetty_ycore_void, "ytop_disk_monitor_init: NULL monitor");
    }
    memset(monitor, 0, sizeof(*monitor));
    uint64_t read_sectors = 0, write_sectors = 0;
    if (read_diskstats(&read_sectors, &write_sectors) == 0) {
        monitor->prev_read_sectors = read_sectors;
        monitor->prev_write_sectors = write_sectors;
        monitor->have_prev = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ytop_disk_sample(struct ytop_disk_monitor *monitor,
                                                struct ytop_disk_snapshot *out,
                                                float interval_seconds)
{
    if (!monitor || !out) {
        return YETTY_ERR(yetty_ycore_void, "ytop_disk_sample: NULL argument");
    }
    memset(out, 0, sizeof(*out));

    read_mounts(out);

    uint64_t read_sectors = 0, write_sectors = 0;
    if (read_diskstats(&read_sectors, &write_sectors) == 0) {
        float interval = interval_seconds > 0.0f ? interval_seconds : 1.0f;
        if (monitor->have_prev) {
            uint64_t read_delta = (read_sectors > monitor->prev_read_sectors)
                                      ? read_sectors - monitor->prev_read_sectors
                                      : 0;
            uint64_t write_delta = (write_sectors > monitor->prev_write_sectors)
                                       ? write_sectors - monitor->prev_write_sectors
                                       : 0;
            out->io_read_rate = (double)(read_delta * YTOP_DISK_SECTOR_BYTES) / interval;
            out->io_write_rate = (double)(write_delta * YTOP_DISK_SECTOR_BYTES) / interval;
        }
        monitor->prev_read_sectors = read_sectors;
        monitor->prev_write_sectors = write_sectors;
        monitor->have_prev = 1;
    }
    return YETTY_OK_VOID();
}
