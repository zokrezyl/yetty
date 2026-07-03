/*
 * app.h — the central ytop application state.
 *
 * `struct ytop_app` owns everything with a program lifetime: the ygui engine
 * and widget tree, one monitor per OS abstraction (holding the previous sample
 * for delta/rate computation), the latest snapshots those monitors produce, and
 * the interaction config (sort mode, refresh cadence, running flag). It is
 * heap-allocated once in main() and threaded by pointer through the harness,
 * the sampler, and the UI.
 */
#ifndef YTOP_APP_H
#define YTOP_APP_H

#include "history.h"
#include "platform/platform.h"

struct yetty_yclass_object;
struct ytop_ui;

/* Process-table sort order, cycled by hotkeys. */
enum ytop_sort_mode {
    YTOP_SORT_CPU = 0,
    YTOP_SORT_MEM,
    YTOP_SORT_PID,
    YTOP_SORT_NAME,
};

struct ytop_app {
    /* ygui engine + root of the widget tree. */
    struct yetty_yclass_object *engine;
    struct yetty_yclass_object *root;
    struct ytop_ui *ui; /* opaque widget handles, owned by ui.c */

    /* One monitor per abstraction (carry previous samples). */
    struct ytop_cpu_monitor cpu_monitor;
    struct ytop_process_monitor proc_monitor;
    struct ytop_net_monitor net_monitor;
    struct ytop_disk_monitor disk_monitor;

    /* Latest readings. */
    struct ytop_cpu_snapshot cpu;
    struct ytop_memory_snapshot mem;
    struct ytop_net_snapshot net;
    struct ytop_disk_snapshot disk;
    struct ytop_sysinfo_snapshot sys;
    struct ytop_proc_entry procs[YTOP_MAX_PROCS];
    int n_procs;

    /* Rolling history behind the inline sparklines (0..100 for cpu/mem,
     * bytes/sec for network). */
    struct ytop_history cpu_hist;
    struct ytop_history mem_hist;
    struct ytop_history net_rx_hist;
    struct ytop_history net_tx_hist;

    /* Interaction / config. */
    enum ytop_sort_mode sort_mode;
    int refresh_ms;
    int running;
};

#endif /* YTOP_APP_H */
