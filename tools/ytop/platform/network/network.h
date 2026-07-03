/*
 * network.h — network abstraction: per-interface receive/transmit byte
 * counters turned into per-second rates over the sampling interval.
 *
 * The monitor stores the previous cumulative counters keyed by interface name;
 * ytop_net_sample() computes rx_rate/tx_rate against them.
 */
#ifndef YTOP_PLATFORM_NETWORK_NETWORK_H
#define YTOP_PLATFORM_NETWORK_NETWORK_H

#include <stdint.h>

#include <yetty/ycore/result.h>

#include "platform/limits.h"

struct ytop_net_interface {
    char name[32];
    uint64_t rx_bytes; /* cumulative bytes received */
    uint64_t tx_bytes; /* cumulative bytes transmitted */
    double rx_rate;    /* bytes/second over the last interval */
    double tx_rate;    /* bytes/second over the last interval */
    int is_loopback;
};

struct ytop_net_snapshot {
    int n_interfaces;
    struct ytop_net_interface interfaces[YTOP_MAX_INTERFACES];
    double total_rx_rate; /* sum over non-loopback interfaces */
    double total_tx_rate;
};

struct ytop_net_prev_entry {
    char name[32];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
};

struct ytop_net_monitor {
    int have_prev;
    int n_prev;
    struct ytop_net_prev_entry prev[YTOP_MAX_INTERFACES];
};

/* Seed the monitor with a first reading (safe on a zeroed struct). */
struct yetty_ycore_void_result ytop_net_monitor_init(struct ytop_net_monitor *monitor);

/* Fill *out with per-interface counters and rates computed over
 * `interval_seconds`, then refresh the monitor's previous counters. */
struct yetty_ycore_void_result ytop_net_sample(struct ytop_net_monitor *monitor,
                                               struct ytop_net_snapshot *out,
                                               float interval_seconds);

#endif /* YTOP_PLATFORM_NETWORK_NETWORK_H */
