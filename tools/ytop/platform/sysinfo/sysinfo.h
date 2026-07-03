/*
 * sysinfo.h — miscellaneous host facts that frame the dashboard: hostname,
 * OS pretty-name, kernel release, and uptime. All self-contained per sample.
 */
#ifndef YTOP_PLATFORM_SYSINFO_SYSINFO_H
#define YTOP_PLATFORM_SYSINFO_SYSINFO_H

#include <stdint.h>

#include <yetty/ycore/result.h>

struct ytop_sysinfo_snapshot {
    char hostname[128];
    char os_name[128]; /* e.g. "Ubuntu 24.04 LTS" */
    char kernel[128];  /* uname release */
    uint64_t uptime_sec;
};

/* Fill *out with a fresh host reading. */
struct yetty_ycore_void_result ytop_sysinfo_sample(struct ytop_sysinfo_snapshot *out);

#endif /* YTOP_PLATFORM_SYSINFO_SYSINFO_H */
