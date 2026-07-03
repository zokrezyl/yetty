/*
 * ytop — a btop-style live system monitor built on the ygui widget engine.
 *
 * All OS-specific data gathering lives behind the platform abstraction layer
 * (tools/ytop/platform/<abstraction>/<os>.c); this file is the yetty-client
 * harness (libuv loop, stdout OSC transport, stdin keys, SIGWINCH) plus the
 * per-tick sampler that drives the platform monitors and hands their snapshots
 * to the UI.
 *
 *   Run inside a yetty terminal:
 *       yetty -e ./ytop
 *
 *   Keys: [c]pu [m]em [p]id [n]ame sort · [+]/[-] refresh · [q] quit
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ytrace/ytrace.h>

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <uv.h>

#include "app.h"
#include "ui.h"

#define YTOP_REFRESH_MS_DEFAULT 1500
#define YTOP_REFRESH_MS_MIN 250
#define YTOP_REFRESH_MS_MAX 10000

static void error_absorb(struct yetty_ycore_void_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
    }
}

/* ------------------------------------------------------------------ */
/* Per-tick sampler — drive every monitor into the app's snapshots.    */
/* ------------------------------------------------------------------ */

static void ytop_sample_all(struct ytop_app *app)
{
    float interval = (float)app->refresh_ms / 1000.0f;

    error_absorb(ytop_cpu_sample(&app->cpu_monitor, &app->cpu));
    error_absorb(ytop_memory_sample(&app->mem));
    error_absorb(ytop_net_sample(&app->net_monitor, &app->net, interval));
    error_absorb(ytop_disk_sample(&app->disk_monitor, &app->disk, interval));
    error_absorb(ytop_sysinfo_sample(&app->sys));
    error_absorb(ytop_process_sample(&app->proc_monitor, app->procs, YTOP_MAX_PROCS, &app->n_procs,
                                     interval, app->mem.ram_total));

    /* History advances exactly once per sample (the UI only reads it). */
    ytop_history_push(&app->cpu_hist, app->cpu.total_pct);
    float ram_pct =
        app->mem.ram_total ? 100.0f * (float)app->mem.ram_used / (float)app->mem.ram_total : 0.0f;
    ytop_history_push(&app->mem_hist, ram_pct);
    ytop_history_push(&app->net_rx_hist, (float)app->net.total_rx_rate);
    ytop_history_push(&app->net_tx_hist, (float)app->net.total_tx_rate);
}

/* ------------------------------------------------------------------ */
/* Client-mode harness — ytop runs inside a hosting yetty (`yetty -e     */
/* ./ytop`). The ygui framework attaches to the host's root figure       */
/* container over the yclass-RPC transport (stdin = RPC responses,       */
/* stdout = requests); figure/widget mutations stream out on each emit.  */
/* Real keystrokes arrive on stdin and are fed to the framework;         */
/* SIGWINCH carries the pane pixel size.                                 */
/* ------------------------------------------------------------------ */

struct ytop_client {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    uv_timer_t refresh_timer;
    struct ytop_app *app;
};

static void on_refresh_timer(uv_timer_t *timer);

/* Re-arm the repeat interval after a rate change. */
static void ytop_restart_timer(struct ytop_client *client)
{
    uv_timer_stop(&client->refresh_timer);
    uv_timer_start(&client->refresh_timer, on_refresh_timer, (uint64_t)client->app->refresh_ms,
                   (uint64_t)client->app->refresh_ms);
}

static void on_refresh_timer(uv_timer_t *timer)
{
    struct ytop_client *client = timer->data;
    ytop_sample_all(client->app);
    ytop_ui_refresh(client->app);
}

static int on_key(struct yetty_yclass_object *engine, uint32_t key, int mods, void *user)
{
    (void)engine;
    (void)mods;
    struct ytop_client *client = user;
    struct ytop_app *app = client->app;

    switch (key) {
    case 'q':
    case 'Q':
    case 0x03: /* Ctrl-C */
    case 0x04: /* Ctrl-D */
        app->running = 0;
        return 1;
    case 'c':
    case 'C':
        app->sort_mode = YTOP_SORT_CPU;
        ytop_ui_refresh(app);
        return 1;
    case 'm':
    case 'M':
        app->sort_mode = YTOP_SORT_MEM;
        ytop_ui_refresh(app);
        return 1;
    case 'p':
    case 'P':
        app->sort_mode = YTOP_SORT_PID;
        ytop_ui_refresh(app);
        return 1;
    case 'n':
    case 'N':
        app->sort_mode = YTOP_SORT_NAME;
        ytop_ui_refresh(app);
        return 1;
    case '+':
    case '=':
        app->refresh_ms -= 250;
        if (app->refresh_ms < YTOP_REFRESH_MS_MIN) {
            app->refresh_ms = YTOP_REFRESH_MS_MIN;
        }
        ytop_restart_timer(client);
        return 1;
    case '-':
    case '_':
        app->refresh_ms += 250;
        if (app->refresh_ms > YTOP_REFRESH_MS_MAX) {
            app->refresh_ms = YTOP_REFRESH_MS_MAX;
        }
        ytop_restart_timer(client);
        return 1;
    default:
        return 0;
    }
}

static void ytop_stdin_cb(uv_poll_t *h, int status, int events)
{
    struct ytop_client *client = h->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char buf[1024];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        error_absorb(yetty_ygui_framework_feed_input(client->app->engine, buf, (size_t)n));
    } else if (n == 0 && !isatty(STDIN_FILENO)) {
        client->app->running = 0;
    }
}

static void ytop_pickup_winsz(struct ytop_client *client)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        error_absorb(yetty_ygui_framework_set_viewport(client->app->engine, (float)ws.ws_xpixel,
                                                       (float)ws.ws_ypixel));
        ytop_ui_relayout(client->app);
        ytop_ui_refresh(client->app);
    }
}

static void ytop_sigwinch_cb(uv_signal_t *h, int signum)
{
    (void)signum;
    ytop_pickup_winsz((struct ytop_client *)h->data);
}

static void ytop_prep_cb(uv_prepare_t *h)
{
    struct ytop_client *client = h->data;
    if (yetty_ygui_framework_is_dirty(client->app->engine)) {
        error_absorb(yetty_ygui_framework_emit(client->app->engine));
    }
    if (!client->app->running) {
        uv_stop(h->loop);
    }
}

static void ytop_close_cb(uv_handle_t *h)
{
    (void)h;
}

/* Seed every monitor so the first sample yields real deltas. */
static struct yetty_ycore_void_result ytop_init_monitors(struct ytop_app *app)
{
    struct yetty_ycore_void_result r;
    r = ytop_cpu_monitor_init(&app->cpu_monitor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ytop_init_monitors: cpu");
    r = ytop_process_monitor_init(&app->proc_monitor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ytop_init_monitors: process");
    r = ytop_net_monitor_init(&app->net_monitor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ytop_init_monitors: net");
    r = ytop_disk_monitor_init(&app->disk_monitor);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ytop_init_monitors: disk");
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    ytrace_init();

    struct ytop_app *app = calloc(1, sizeof(*app));
    if (!app) {
        return 1;
    }
    app->running = 1;
    app->refresh_ms = YTOP_REFRESH_MS_DEFAULT;
    app->sort_mode = YTOP_SORT_CPU;

    struct yetty_ycore_void_result init_res = ytop_init_monitors(app);
    if (YETTY_IS_ERR(init_res)) {
        fprintf(stderr, "ytop: %s (Linux /proc required)\n", init_res.error.msg);
        yetty_ycore_error_destroy(init_res.error);
        free(app);
        return 1;
    }

    /* One priming sample so the UI is sized to the real core / mount counts. */
    ytop_sample_all(app);

    /* Raw tty BEFORE any OSC write, or the slave tty echoes the ESC bytes
     * back and the host yetty renders "^[" garbage. */
    struct termios saved;
    int tty_raw_ok = 0;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &saved) == 0) {
        struct termios raw = saved;
        cfmakeraw(&raw);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
            tty_raw_ok = 1;
        }
    }

    struct ytop_client client = {0};
    client.app = app;
    if (uv_loop_init(&client.loop) != 0) {
        fprintf(stderr, "ytop: uv_loop_init failed\n");
        goto cleanup;
    }
    struct yetty_yclass_object_ptr_result fr = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(fr)) {
        fprintf(stderr, "ytop: framework_create failed: %s\n", fr.error.msg);
        yetty_ycore_error_destroy(fr.error);
        goto cleanup_loop;
    }
    app->engine = fr.value;
    yetty_ygui_framework_set_key_cb(app->engine, on_key, &client);

    /* Attach to the host yetty's root figure container over the yclass-RPC DCS
     * transport (stdin = responses, stdout = requests, compressed). The
     * handshake reads stdin synchronously ONCE — it MUST run before libuv's
     * stdin poll takes the fd over. Without this the framework has no container
     * and every emit fails with "no container", so nothing renders. */
    struct yetty_ycore_void_result attach_res =
        yetty_ygui_framework_attach(app->engine, STDIN_FILENO, STDOUT_FILENO, /*compressed=*/1);
    if (YETTY_IS_ERR(attach_res)) {
        fprintf(stderr, "ytop: framework_attach failed: %s\n", attach_res.error.msg);
        yetty_ycore_error_destroy(attach_res.error);
        goto cleanup_loop;
    }

    struct yetty_yclass_object_ptr_result rr =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_OK(rr)) {
        app->root = rr.value;
        error_absorb(yetty_ygui_framework_set_root(app->engine, app->root));
        error_absorb(ytop_ui_build(app));
        ytop_ui_refresh(app);
    } else {
        yetty_ycore_error_destroy(rr.error);
    }

    if (uv_poll_init(&client.loop, &client.stdin_poll, STDIN_FILENO) == 0) {
        client.stdin_poll.data = &client;
        uv_poll_start(&client.stdin_poll, UV_READABLE, ytop_stdin_cb);
    }
    if (uv_signal_init(&client.loop, &client.sigwinch) == 0) {
        client.sigwinch.data = &client;
        uv_signal_start(&client.sigwinch, ytop_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&client.loop, &client.prep) == 0) {
        client.prep.data = &client;
        uv_prepare_start(&client.prep, ytop_prep_cb);
    }
    uv_timer_init(&client.loop, &client.refresh_timer);
    client.refresh_timer.data = &client;
    uv_timer_start(&client.refresh_timer, on_refresh_timer, (uint64_t)app->refresh_ms,
                   (uint64_t)app->refresh_ms);

    ytop_pickup_winsz(&client);

    uv_run(&client.loop, UV_RUN_DEFAULT);

    uv_timer_stop(&client.refresh_timer);
    uv_poll_stop(&client.stdin_poll);
    uv_signal_stop(&client.sigwinch);
    uv_prepare_stop(&client.prep);
    uv_close((uv_handle_t *)&client.refresh_timer, ytop_close_cb);
    uv_close((uv_handle_t *)&client.stdin_poll, ytop_close_cb);
    uv_close((uv_handle_t *)&client.sigwinch, ytop_close_cb);
    uv_close((uv_handle_t *)&client.prep, ytop_close_cb);
    uv_run(&client.loop, UV_RUN_NOWAIT);

    /* Wipe every figure ytop drew off the host pane BEFORE tearing down the
     * session — otherwise the whole dashboard is left frozen on screen when
     * ytop exits back to the shell. clear() issues yfigure clear_all on the
     * wired host container over the still-live RPC session. */
    error_absorb(yetty_ygui_framework_clear(app->engine));
    ytop_ui_free(app);
    error_absorb(yetty_ygui_framework_destroy(app->engine));

cleanup_loop:
    uv_loop_close(&client.loop);
cleanup:
    if (tty_raw_ok) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    free(app);
    return 0;
}
