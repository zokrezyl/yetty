/*
 * cpu/linux.c — Linux CPU backend.
 *
 * Sources:
 *   /proc/stat                              — per-core + aggregate jiffies
 *   /proc/loadavg                           — 1/5/15 load average
 *   /sys/devices/system/cpu/cpuN/cpufreq/…  — current frequency
 *   /sys/class/thermal/thermal_zoneN/temp   — package temperature
 *   /proc/cpuinfo                           — model name
 */
#include "platform/cpu/cpu.h"

#include <stdio.h>
#include <string.h>

/* Total of all buckets except idle/iowait — the "busy" portion. */
static uint64_t cpu_busy(const struct ytop_cpu_times *t)
{
    return t->user + t->nice + t->system + t->irq + t->softirq + t->steal;
}

static uint64_t cpu_total(const struct ytop_cpu_times *t)
{
    return cpu_busy(t) + t->idle + t->iowait;
}

/* Percentage of the interval spent busy, 0 when the counters did not advance
 * (first sample, or a core that was offline). */
static float cpu_delta_pct(const struct ytop_cpu_times *prev, const struct ytop_cpu_times *curr)
{
    uint64_t total_prev = cpu_total(prev);
    uint64_t total_curr = cpu_total(curr);
    if (total_curr <= total_prev) {
        return 0.0f;
    }
    uint64_t total_delta = total_curr - total_prev;
    uint64_t busy_delta = cpu_busy(curr) - cpu_busy(prev);
    float pct = 100.0f * (float)busy_delta / (float)total_delta;
    if (pct < 0.0f) {
        pct = 0.0f;
    }
    if (pct > 100.0f) {
        pct = 100.0f;
    }
    return pct;
}

/* Read /proc/stat into curr[0..n], where index 0 is the aggregate "cpu" line
 * and 1..n are "cpuN" lines. Returns the number of per-core lines, or -1. */
static int read_proc_stat(struct ytop_cpu_times *curr, int max_lines)
{
    FILE *file = fopen("/proc/stat", "r");
    if (!file) {
        return -1;
    }
    char line[512];
    int index = 0;
    while (index < max_lines && fgets(line, sizeof(line), file)) {
        if (strncmp(line, "cpu", 3) != 0) {
            break;
        }
        /* Stop at the first non-cpu line ("intr", "ctxt", …). */
        if (line[3] != ' ' && (line[3] < '0' || line[3] > '9')) {
            break;
        }
        struct ytop_cpu_times sample = {0};
        int fields = sscanf(line, "%*s %lu %lu %lu %lu %lu %lu %lu %lu", &sample.user, &sample.nice,
                            &sample.system, &sample.idle, &sample.iowait, &sample.irq,
                            &sample.softirq, &sample.steal);
        if (fields < 4) {
            break;
        }
        curr[index++] = sample;
    }
    fclose(file);
    if (index == 0) {
        return -1;
    }
    return index - 1; /* per-core count (excludes the aggregate line) */
}

static void read_loadavg(float load_avg[3])
{
    load_avg[0] = load_avg[1] = load_avg[2] = 0.0f;
    FILE *file = fopen("/proc/loadavg", "r");
    if (!file) {
        return;
    }
    if (fscanf(file, "%f %f %f", &load_avg[0], &load_avg[1], &load_avg[2]) != 3) {
        load_avg[0] = load_avg[1] = load_avg[2] = 0.0f;
    }
    fclose(file);
}

static uint64_t read_core_freq_khz(int core)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", core);
    FILE *file = fopen(path, "r");
    if (!file) {
        return 0;
    }
    unsigned long khz = 0;
    if (fscanf(file, "%lu", &khz) != 1) {
        khz = 0;
    }
    fclose(file);
    return (uint64_t)khz;
}

/* Best-effort package temperature. Walk the thermal zones and prefer a zone
 * whose type looks like a CPU/package sensor; otherwise fall back to zone 0. */
static float read_cpu_temp(void)
{
    float fallback = -1.0f;
    for (int zone = 0; zone < 16; zone++) {
        char type_path[128];
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/thermal_zone%d/type", zone);
        FILE *type_file = fopen(type_path, "r");
        if (!type_file) {
            break; /* zones are contiguous — first miss ends the walk */
        }
        char type[64] = {0};
        if (!fgets(type, sizeof(type), type_file)) {
            type[0] = '\0';
        }
        fclose(type_file);

        char temp_path[128];
        snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/thermal_zone%d/temp", zone);
        FILE *temp_file = fopen(temp_path, "r");
        if (!temp_file) {
            continue;
        }
        long millideg = 0;
        int got = fscanf(temp_file, "%ld", &millideg);
        fclose(temp_file);
        if (got != 1) {
            continue;
        }
        float celsius = (float)millideg / 1000.0f;
        if (fallback < 0.0f) {
            fallback = celsius; /* remember zone 0 as a default */
        }
        if (strstr(type, "cpu") || strstr(type, "x86_pkg") || strstr(type, "coretemp") ||
            strstr(type, "Package") || strstr(type, "pkg")) {
            return celsius;
        }
    }
    return fallback;
}

static void read_cpu_model(char *out, size_t out_len)
{
    out[0] = '\0';
    FILE *file = fopen("/proc/cpuinfo", "r");
    if (!file) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "model name", 10) != 0) {
            continue;
        }
        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        char *value = colon + 1;
        while (*value == ' ' || *value == '\t') {
            value++;
        }
        size_t len = strlen(value);
        while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
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

struct yetty_ycore_void_result ytop_cpu_monitor_init(struct ytop_cpu_monitor *monitor)
{
    if (!monitor) {
        return YETTY_ERR(yetty_ycore_void, "ytop_cpu_monitor_init: NULL monitor");
    }
    memset(monitor, 0, sizeof(*monitor));
    int n_cores = read_proc_stat(monitor->prev, YTOP_MAX_CORES + 1);
    if (n_cores < 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_cpu_monitor_init: cannot read /proc/stat");
    }
    monitor->n_cores = n_cores;
    monitor->have_prev = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ytop_cpu_sample(struct ytop_cpu_monitor *monitor,
                                               struct ytop_cpu_snapshot *out)
{
    if (!monitor || !out) {
        return YETTY_ERR(yetty_ycore_void, "ytop_cpu_sample: NULL argument");
    }
    memset(out, 0, sizeof(*out));
    out->temp_celsius = -1.0f;

    struct ytop_cpu_times curr[YTOP_MAX_CORES + 1] = {0};
    int n_cores = read_proc_stat(curr, YTOP_MAX_CORES + 1);
    if (n_cores < 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_cpu_sample: cannot read /proc/stat");
    }
    out->n_cores = n_cores;

    if (monitor->have_prev) {
        out->total_pct = cpu_delta_pct(&monitor->prev[0], &curr[0]);
        for (int core = 0; core < n_cores; core++) {
            out->cores[core].usage_pct = cpu_delta_pct(&monitor->prev[core + 1], &curr[core + 1]);
        }
    }
    for (int core = 0; core < n_cores; core++) {
        out->cores[core].freq_khz = read_core_freq_khz(core);
    }

    read_loadavg(out->load_avg);
    out->temp_celsius = read_cpu_temp();
    read_cpu_model(out->model, sizeof(out->model));

    memcpy(monitor->prev, curr, sizeof(struct ytop_cpu_times) * (size_t)(n_cores + 1));
    monitor->n_cores = n_cores;
    monitor->have_prev = 1;
    return YETTY_OK_VOID();
}
