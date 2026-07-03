/*
 * sysinfo/linux.c — Linux host-facts backend.
 *
 * Sources:
 *   gethostname()          — hostname
 *   /etc/os-release        — PRETTY_NAME
 *   uname()                — kernel release
 *   /proc/uptime           — seconds since boot
 */
#include "platform/sysinfo/sysinfo.h"

#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static void read_os_name(char *out, size_t out_len)
{
    out[0] = '\0';
    FILE *file = fopen("/etc/os-release", "r");
    if (!file) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "PRETTY_NAME=", 12) != 0) {
            continue;
        }
        char *value = line + 12;
        /* Strip surrounding quotes and trailing newline. */
        if (*value == '"') {
            value++;
        }
        size_t len = strlen(value);
        while (len > 0 &&
               (value[len - 1] == '\n' || value[len - 1] == '\r' || value[len - 1] == '"')) {
            len--;
        }
        if (len >= out_len) {
            len = out_len - 1;
        }
        memcpy(out, value, len);
        out[len] = '\0';
        break;
    }
    fclose(file);
}

struct yetty_ycore_void_result ytop_sysinfo_sample(struct ytop_sysinfo_snapshot *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ytop_sysinfo_sample: NULL out");
    }
    memset(out, 0, sizeof(*out));

    if (gethostname(out->hostname, sizeof(out->hostname)) != 0) {
        out->hostname[0] = '\0';
    }
    out->hostname[sizeof(out->hostname) - 1] = '\0';

    read_os_name(out->os_name, sizeof(out->os_name));

    struct utsname uts;
    if (uname(&uts) == 0) {
        strncpy(out->kernel, uts.release, sizeof(out->kernel) - 1);
    }

    FILE *file = fopen("/proc/uptime", "r");
    if (file) {
        double uptime = 0.0;
        if (fscanf(file, "%lf", &uptime) == 1) {
            out->uptime_sec = (uint64_t)uptime;
        }
        fclose(file);
    }
    return YETTY_OK_VOID();
}
