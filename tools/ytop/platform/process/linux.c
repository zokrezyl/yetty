/*
 * process/linux.c — Linux process backend.
 *
 * Sources per pid:
 *   /proc/<pid>/stat    — comm, state, ppid, utime, stime, num_threads
 *   /proc/<pid>/status  — VmRSS, Uid
 *   /proc/<pid>/cmdline — full argv (NUL-separated)
 */
#include "platform/process/process.h"

#include <dirent.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Parse /proc/<pid>/stat. Fills comm/state/ppid/num_threads and the raw
 * cumulative CPU jiffies (utime+stime). Returns 0 on success. */
static int parse_pid_stat(int pid, struct ytop_proc_entry *entry, uint64_t *cpu_jiffies_out)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        return -1;
    }
    char buf[2048];
    size_t n = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);
    if (n == 0) {
        return -1;
    }
    buf[n] = '\0';

    /* comm is parenthesised and may itself contain spaces or parens; take
     * everything between the first '(' and the LAST ')'. */
    char *lparen = strchr(buf, '(');
    char *rparen = strrchr(buf, ')');
    if (!lparen || !rparen || rparen <= lparen) {
        return -1;
    }
    entry->pid = pid;
    size_t comm_len = (size_t)(rparen - lparen - 1);
    if (comm_len >= sizeof(entry->comm)) {
        comm_len = sizeof(entry->comm) - 1;
    }
    memcpy(entry->comm, lparen + 1, comm_len);
    entry->comm[comm_len] = '\0';

    /* Fields after ") ", space-separated, using 1-based stat(5) positions:
     *   3 state, 4 ppid, 14 utime, 15 stime, 20 num_threads. */
    char *cursor = rparen + 2; /* skip ") " -> field 3 (state) */
    entry->state = *cursor;

    /* Collect fields 4..20 by walking spaces. We index from field 3. */
    int field = 3;
    unsigned long utime = 0, stime = 0;
    while (*cursor) {
        char *space = strchr(cursor, ' ');
        if (!space) {
            break;
        }
        *space = '\0';
        /* cursor now holds field `field`. */
        if (field == 4) {
            entry->ppid = (int)strtol(cursor, NULL, 10);
        } else if (field == 14) {
            utime = strtoul(cursor, NULL, 10);
        } else if (field == 15) {
            stime = strtoul(cursor, NULL, 10);
        } else if (field == 20) {
            entry->num_threads = (int)strtol(cursor, NULL, 10);
            break;
        }
        cursor = space + 1;
        field++;
    }
    *cpu_jiffies_out = (uint64_t)utime + (uint64_t)stime;
    return 0;
}

static void parse_pid_status(int pid, struct ytop_proc_entry *entry)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            entry->rss_bytes = (uint64_t)strtoul(line + 6, NULL, 10) * 1024u;
        } else if (strncmp(line, "Uid:", 4) == 0) {
            entry->uid = (uint32_t)strtoul(line + 4, NULL, 10);
        }
    }
    fclose(file);

    struct passwd *pw = getpwuid((uid_t)entry->uid);
    if (pw && pw->pw_name) {
        strncpy(entry->user, pw->pw_name, sizeof(entry->user) - 1);
    } else {
        snprintf(entry->user, sizeof(entry->user), "%u", entry->uid);
    }
}

/* Read the full command line; NUL separators become spaces. Falls back to the
 * bracketed comm (kernel threads have an empty cmdline). */
static void parse_pid_cmdline(int pid, struct ytop_proc_entry *entry)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
    FILE *file = fopen(path, "r");
    if (file) {
        size_t n = fread(entry->cmdline, 1, sizeof(entry->cmdline) - 1, file);
        fclose(file);
        if (n > 0) {
            for (size_t i = 0; i < n; i++) {
                if (entry->cmdline[i] == '\0') {
                    entry->cmdline[i] = ' ';
                }
            }
            /* Trim a trailing space left by the final NUL-turned-space. */
            while (n > 0 && entry->cmdline[n - 1] == ' ') {
                n--;
            }
            entry->cmdline[n] = '\0';
            if (entry->cmdline[0] != '\0') {
                return;
            }
        }
    }
    snprintf(entry->cmdline, sizeof(entry->cmdline), "[%s]", entry->comm);
}

/* Look up a pid's previous cumulative jiffies for delta computation. */
static uint64_t prev_jiffies_for(const struct ytop_process_monitor *monitor, int pid)
{
    for (int i = 0; i < monitor->n_prev; i++) {
        if (monitor->prev[i].pid == pid) {
            return monitor->prev[i].cpu_jiffies;
        }
    }
    return 0;
}

struct yetty_ycore_void_result ytop_process_monitor_init(struct ytop_process_monitor *monitor)
{
    if (!monitor) {
        return YETTY_ERR(yetty_ycore_void, "ytop_process_monitor_init: NULL monitor");
    }
    memset(monitor, 0, sizeof(*monitor));
    monitor->clock_ticks = sysconf(_SC_CLK_TCK);
    if (monitor->clock_ticks <= 0) {
        monitor->clock_ticks = 100;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ytop_process_sample(struct ytop_process_monitor *monitor,
                                                   struct ytop_proc_entry *out, int max, int *n_out,
                                                   float interval_seconds, uint64_t ram_total_bytes)
{
    if (!monitor || !out || !n_out || max <= 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_process_sample: bad argument");
    }
    *n_out = 0;

    DIR *dir = opendir("/proc");
    if (!dir) {
        return YETTY_ERR(yetty_ycore_void, "ytop_process_sample: cannot open /proc");
    }

    float ticks_per_sec = (float)monitor->clock_ticks;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) && count < max) {
        if (de->d_name[0] < '0' || de->d_name[0] > '9') {
            continue;
        }
        char *end = NULL;
        long pid = strtol(de->d_name, &end, 10);
        if (!end || *end != '\0' || pid <= 0) {
            continue;
        }

        struct ytop_proc_entry entry = {0};
        uint64_t cpu_jiffies = 0;
        if (parse_pid_stat((int)pid, &entry, &cpu_jiffies) < 0) {
            continue;
        }
        parse_pid_status((int)pid, &entry);
        parse_pid_cmdline((int)pid, &entry);

        entry.cpu_time_ticks = cpu_jiffies;
        entry.cpu_time_sec =
            (monitor->clock_ticks > 0) ? cpu_jiffies / (uint64_t)monitor->clock_ticks : 0;

        uint64_t prev = prev_jiffies_for(monitor, (int)pid);
        uint64_t delta = (cpu_jiffies > prev) ? cpu_jiffies - prev : 0;
        entry.cpu_pct = (ticks_per_sec > 0.0f && interval_seconds > 0.0f)
                            ? 100.0f * (float)delta / (ticks_per_sec * interval_seconds)
                            : 0.0f;
        entry.mem_pct =
            (ram_total_bytes > 0) ? 100.0f * (float)entry.rss_bytes / (float)ram_total_bytes : 0.0f;

        out[count++] = entry;
    }
    closedir(dir);
    *n_out = count;

    /* Refresh the previous-jiffy table for the next interval. */
    int prev_count = count < YTOP_MAX_PROCS ? count : YTOP_MAX_PROCS;
    for (int i = 0; i < prev_count; i++) {
        monitor->prev[i].pid = out[i].pid;
        monitor->prev[i].cpu_jiffies = out[i].cpu_time_ticks;
    }
    monitor->n_prev = prev_count;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ytop_process_signal(int pid, int signal)
{
    if (pid <= 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_process_signal: bad pid");
    }
    if (kill((pid_t)pid, signal) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_process_signal: kill failed");
    }
    return YETTY_OK_VOID();
}
