/*
 * memory/linux.c — Linux memory backend. Reads /proc/meminfo (values are in
 * kibibytes) and converts to bytes.
 */
#include "platform/memory/memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse one "Key:   1234 kB" line into bytes if the key matches. */
static int match_kb(const char *line, const char *key, uint64_t *out_bytes)
{
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0) {
        return 0;
    }
    unsigned long kib = strtoul(line + key_len, NULL, 10);
    *out_bytes = (uint64_t)kib * 1024u;
    return 1;
}

struct yetty_ycore_void_result ytop_memory_sample(struct ytop_memory_snapshot *out)
{
    if (!out) {
        return YETTY_ERR(yetty_ycore_void, "ytop_memory_sample: NULL out");
    }
    memset(out, 0, sizeof(*out));

    FILE *file = fopen("/proc/meminfo", "r");
    if (!file) {
        return YETTY_ERR(yetty_ycore_void, "ytop_memory_sample: cannot read /proc/meminfo");
    }

    uint64_t buffers = 0, cached = 0, sreclaimable = 0, swap_free = 0;
    int have_available = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        uint64_t value = 0;
        if (match_kb(line, "MemTotal:", &value)) {
            out->ram_total = value;
        } else if (match_kb(line, "MemFree:", &value)) {
            out->ram_free = value;
        } else if (match_kb(line, "MemAvailable:", &value)) {
            out->ram_available = value;
            have_available = 1;
        } else if (match_kb(line, "Buffers:", &value)) {
            buffers = value;
        } else if (match_kb(line, "Cached:", &value)) {
            cached = value;
        } else if (match_kb(line, "SReclaimable:", &value)) {
            sreclaimable = value;
        } else if (match_kb(line, "SwapTotal:", &value)) {
            out->swap_total = value;
        } else if (match_kb(line, "SwapFree:", &value)) {
            swap_free = value;
        }
    }
    fclose(file);

    out->ram_cached = cached + buffers + sreclaimable;
    /* Older kernels lack MemAvailable; approximate it from free + cached. */
    if (!have_available) {
        out->ram_available = out->ram_free + out->ram_cached;
    }
    out->ram_used = (out->ram_total > out->ram_available) ? out->ram_total - out->ram_available : 0;
    out->swap_used = (out->swap_total > swap_free) ? out->swap_total - swap_free : 0;
    return YETTY_OK_VOID();
}
