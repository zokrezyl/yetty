/*
 * network/linux.c — Linux network backend. Parses /proc/net/dev, whose
 * per-interface line is:
 *
 *   iface: rx_bytes rx_packets ... tx_bytes tx_packets ...
 *
 * Field 1 (0-based after the colon) is rx_bytes; field 9 is tx_bytes.
 */
#include "platform/network/network.h"

#include <stdio.h>
#include <string.h>

static uint64_t prev_rx_for(const struct ytop_net_monitor *monitor, const char *name,
                            uint64_t *tx_out)
{
    for (int i = 0; i < monitor->n_prev; i++) {
        if (strcmp(monitor->prev[i].name, name) == 0) {
            *tx_out = monitor->prev[i].tx_bytes;
            return monitor->prev[i].rx_bytes;
        }
    }
    *tx_out = 0;
    return 0;
}

/* Read /proc/net/dev into out->interfaces[]. Rates left at 0. Returns 0 or -1. */
static int read_net_dev(struct ytop_net_snapshot *out)
{
    FILE *file = fopen("/proc/net/dev", "r");
    if (!file) {
        return -1;
    }
    char line[512];
    /* Skip the two header lines. */
    if (!fgets(line, sizeof(line), file) || !fgets(line, sizeof(line), file)) {
        fclose(file);
        return -1;
    }
    int count = 0;
    while (count < YTOP_MAX_INTERFACES && fgets(line, sizeof(line), file)) {
        char *colon = strchr(line, ':');
        if (!colon) {
            continue;
        }
        *colon = '\0';
        char *name = line;
        while (*name == ' ' || *name == '\t') {
            name++;
        }

        unsigned long long rx = 0, tx = 0;
        unsigned long long skip;
        /* rx_bytes then 7 skipped counters, then tx_bytes. */
        int fields = sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu", &rx, &skip,
                            &skip, &skip, &skip, &skip, &skip, &skip, &tx);
        if (fields < 9) {
            continue;
        }

        struct ytop_net_interface *iface = &out->interfaces[count++];
        memset(iface, 0, sizeof(*iface));
        strncpy(iface->name, name, sizeof(iface->name) - 1);
        iface->rx_bytes = (uint64_t)rx;
        iface->tx_bytes = (uint64_t)tx;
        iface->is_loopback = (strcmp(name, "lo") == 0);
    }
    fclose(file);
    out->n_interfaces = count;
    return 0;
}

struct yetty_ycore_void_result ytop_net_monitor_init(struct ytop_net_monitor *monitor)
{
    if (!monitor) {
        return YETTY_ERR(yetty_ycore_void, "ytop_net_monitor_init: NULL monitor");
    }
    memset(monitor, 0, sizeof(*monitor));

    struct ytop_net_snapshot seed = {0};
    if (read_net_dev(&seed) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_net_monitor_init: cannot read /proc/net/dev");
    }
    for (int i = 0; i < seed.n_interfaces; i++) {
        strncpy(monitor->prev[i].name, seed.interfaces[i].name, sizeof(monitor->prev[i].name) - 1);
        monitor->prev[i].rx_bytes = seed.interfaces[i].rx_bytes;
        monitor->prev[i].tx_bytes = seed.interfaces[i].tx_bytes;
    }
    monitor->n_prev = seed.n_interfaces;
    monitor->have_prev = 1;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result ytop_net_sample(struct ytop_net_monitor *monitor,
                                               struct ytop_net_snapshot *out,
                                               float interval_seconds)
{
    if (!monitor || !out) {
        return YETTY_ERR(yetty_ycore_void, "ytop_net_sample: NULL argument");
    }
    memset(out, 0, sizeof(*out));
    if (read_net_dev(out) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ytop_net_sample: cannot read /proc/net/dev");
    }

    float interval = interval_seconds > 0.0f ? interval_seconds : 1.0f;
    for (int i = 0; i < out->n_interfaces; i++) {
        struct ytop_net_interface *iface = &out->interfaces[i];
        if (monitor->have_prev) {
            uint64_t prev_tx = 0;
            uint64_t prev_rx = prev_rx_for(monitor, iface->name, &prev_tx);
            uint64_t rx_delta = (iface->rx_bytes > prev_rx) ? iface->rx_bytes - prev_rx : 0;
            uint64_t tx_delta = (iface->tx_bytes > prev_tx) ? iface->tx_bytes - prev_tx : 0;
            iface->rx_rate = (double)rx_delta / interval;
            iface->tx_rate = (double)tx_delta / interval;
            if (!iface->is_loopback) {
                out->total_rx_rate += iface->rx_rate;
                out->total_tx_rate += iface->tx_rate;
            }
        }
    }

    /* Refresh previous counters. */
    for (int i = 0; i < out->n_interfaces; i++) {
        strncpy(monitor->prev[i].name, out->interfaces[i].name, sizeof(monitor->prev[i].name) - 1);
        monitor->prev[i].name[sizeof(monitor->prev[i].name) - 1] = '\0';
        monitor->prev[i].rx_bytes = out->interfaces[i].rx_bytes;
        monitor->prev[i].tx_bytes = out->interfaces[i].tx_bytes;
    }
    monitor->n_prev = out->n_interfaces;
    monitor->have_prev = 1;
    return YETTY_OK_VOID();
}
