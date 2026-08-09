/*
 * ydu — a visual disk-usage explorer built on the ygui widget engine.
 *
 * The filesystem scan (scan.c) builds an aggregated size tree; ui.c renders it
 * as a squarified treemap beside a synchronized sortable table. This file is
 * the yetty-client harness (libuv loop, stdout OSC transport, stdin keys,
 * SIGWINCH) plus keyboard navigation over the scan tree.
 *
 *   Run inside a yetty terminal:
 *       yetty -e './ydu /some/path'
 *
 *   Or headless (structured text, no GUI):
 *       ./ydu --print /some/path
 *
 *   Keys: [j/k] move · [enter/l] open · [u/h] up · [s] sort · [r] rescan · [q] quit
 */

#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h> /* YMGUI_WIRE_VERSION */
#include <yetty/ygui/ygui.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/ytrace/ytrace.h>

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
#include "scan.h"
#include "ui.h"

static void error_absorb(struct yetty_ycore_void_result result)
{
    if (YETTY_IS_ERR(result)) {
        yetty_ycore_error_destroy(result.error);
    }
}

/* ------------------------------------------------------------------ */
/* Headless text mode (consumption mode 3, and a test path)            */
/* ------------------------------------------------------------------ */

static void ydu_print(struct ydu_node *root, const struct ydu_scan_stats *stats)
{
    char size[32];
    ydu_fmt_bytes(root->disk_bytes, size, sizeof(size));
    printf("%s\n", root->name);
    printf("  total %s  in %llu items  (%llu dirs, %llu files, %llu skipped)\n\n", size,
           (unsigned long long)root->item_count, (unsigned long long)stats->dirs,
           (unsigned long long)stats->files, (unsigned long long)stats->errors);

    ydu_node_sort_children(root, YDU_SORT_DISK);
    for (size_t i = 0; i < root->n_children; i++) {
        const struct ydu_node *node = root->children[i];
        ydu_fmt_bytes(node->disk_bytes, size, sizeof(size));
        printf("  %-10s  %s%s\n", size, node->name, node->is_dir ? "/" : "");
    }
}

static void ydu_usage(void)
{
    printf("ydu — visual disk-usage explorer\n\n"
           "usage: ydu [options] [path]\n"
           "  path            directory to scan (default: current directory)\n"
           "  -p, --print     scan and print a text summary, then exit (no GUI)\n"
           "  -h, --help      show this help\n\n"
           "interactive keys: [j/k] move  [enter/l] open  [u/h] up  "
           "[s] sort  [r] rescan  [q] quit\n");
}

/* ------------------------------------------------------------------ */
/* Navigation over the scan tree                                       */
/* ------------------------------------------------------------------ */

static void nav_move(struct ydu_app *app, int delta)
{
    if (!app->cwd || app->cwd->n_children == 0) {
        return;
    }
    int count = (int)app->cwd->n_children;
    app->selected += delta;
    if (app->selected < 0) {
        app->selected = 0;
    }
    if (app->selected >= count) {
        app->selected = count - 1;
    }
    ydu_ui_refresh(app);
}

static void nav_drill_in(struct ydu_app *app)
{
    if (!app->cwd || app->selected < 0 || (size_t)app->selected >= app->cwd->n_children) {
        return;
    }
    struct ydu_node *child = app->cwd->children[app->selected];
    if (!child->is_dir || child->n_children == 0) {
        return;
    }
    app->cwd = child;
    app->selected = 0;
    ydu_node_sort_children(app->cwd, app->sort_mode);
    ydu_ui_refresh(app);
}

static void nav_go_up(struct ydu_app *app)
{
    if (!app->cwd || !app->cwd->parent) {
        return;
    }
    struct ydu_node *came_from = app->cwd;
    app->cwd = app->cwd->parent;
    ydu_node_sort_children(app->cwd, app->sort_mode);
    app->selected = 0;
    for (size_t i = 0; i < app->cwd->n_children; i++) {
        if (app->cwd->children[i] == came_from) {
            app->selected = (int)i;
            break;
        }
    }
    ydu_ui_refresh(app);
}

static void nav_cycle_sort(struct ydu_app *app)
{
    app->sort_mode = (enum ydu_sort_mode)((app->sort_mode + 1) % YDU_SORT_MODE_COUNT);
    ydu_node_sort_children(app->cwd, app->sort_mode);
    app->selected = 0;
    ydu_ui_refresh(app);
}

static void nav_rescan(struct ydu_app *app)
{
    struct ydu_node *fresh = NULL;
    struct ydu_scan_stats stats;
    struct yetty_ycore_void_result scan_res = ydu_scan(app->root_path, &fresh, &stats);
    if (YETTY_IS_ERR(scan_res)) {
        yetty_ycore_error_destroy(scan_res.error);
        return;
    }
    ydu_node_destroy(app->tree);
    app->tree = fresh;
    app->cwd = fresh;
    app->stats = stats;
    app->selected = 0;
    ydu_node_sort_children(app->cwd, app->sort_mode);
    ydu_ui_refresh(app);
}

/* ------------------------------------------------------------------ */
/* Client-mode harness (ydu runs inside a hosting yetty)               */
/* ------------------------------------------------------------------ */

struct ydu_client {
    uv_loop_t loop;
    uv_poll_t stdin_poll;
    uv_signal_t sigwinch;
    uv_prepare_t prep;
    /* Stdin is fronted by a yface: it splits the byte stream into OSC
     * envelopes (pane geometry from the host) and raw keystrokes, which is
     * how the host's content_scale reaches us at all. */
    struct yetty_yface *yface;
    struct ydu_app *app;
};

/* Host HiDPI factor with the 1.0 guard — see ydu_app::content_scale. */
static float ydu_scale(const struct ydu_app *app)
{
    return (app && app->content_scale > 0.0f) ? app->content_scale : 1.0f;
}

static int on_key(struct yetty_yclass_object *engine, uint32_t key, int mods, void *user)
{
    (void)engine;
    (void)mods;
    struct ydu_client *client = user;
    struct ydu_app *app = client->app;

    switch (key) {
    case 'q':
    case 'Q':
    case 0x03: /* Ctrl-C */
    case 0x04: /* Ctrl-D */
        app->running = 0;
        return 1;
    case 'j':
    case 'J':
        nav_move(app, 1);
        return 1;
    case 'k':
    case 'K':
        nav_move(app, -1);
        return 1;
    case 'l':
    case 'L':
    case 0x0d: /* Enter (CR) */
    case 0x0a: /* Enter (LF) */
        nav_drill_in(app);
        return 1;
    case 'h':
    case 'H':
    case 'u':
    case 'U':
    case 0x7f: /* Backspace (DEL) */
    case 0x08: /* Backspace (BS) */
        nav_go_up(app);
        return 1;
    case 's':
    case 'S':
        nav_cycle_sort(app);
        return 1;
    case 'r':
    case 'R':
        nav_rescan(app);
        return 1;
    case 'g':
        app->selected = 0;
        ydu_ui_refresh(app);
        return 1;
    case 'G':
        if (app->cwd && app->cwd->n_children) {
            app->selected = (int)app->cwd->n_children - 1;
            ydu_ui_refresh(app);
        }
        return 1;
    default:
        return 0;
    }
}

static void ydu_stdin_cb(uv_poll_t *handle, int status, int events)
{
    struct ydu_client *client = handle->data;
    if (status < 0 || !(events & UV_READABLE)) {
        return;
    }
    char buf[1024];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
        /* Split envelopes from keystrokes: on_raw feeds the framework, on_osc
         * picks up the pane geometry + HiDPI factor. */
        yetty_yface_feed_bytes(client->yface, (const uint8_t *)buf, (size_t)n);
    } else if (n == 0 && !isatty(STDIN_FILENO)) {
        client->app->running = 0;
    }
}

/* Raw keystrokes (everything outside an envelope) go straight to ygui. */
static void ydu_on_raw(void *user, const char *bytes, size_t n)
{
    struct ydu_client *client = user;
    error_absorb(yetty_ygui_framework_feed_input(client->app->engine, bytes, n));
}

/* Pane geometry from the host. width/height are FRAMEBUFFER px and
 * content_scale is the display's HiDPI factor; ygui lays out in LOGICAL px and
 * the host scales absolute-coords figures back up when rendering, so divide
 * once here. Without this the tree is built for a content_scale-times canvas
 * and overflows the pane. */
static void ydu_on_osc(void *user, int osc_code, const uint8_t *args, size_t args_len,
                       const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct ydu_client *client = user;
    if (osc_code != YETTY_OSC_SC_CLIENT_INPUT_RESIZE &&
        osc_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE) {
        return;
    }
    if (payload_len < sizeof(struct yetty_client_input_resize)) {
        return;
    }
    const struct yetty_client_input_resize *rz =
        (const struct yetty_client_input_resize *)payload;
    if (rz->magic != YETTY_CLIENT_INPUT_RESIZE_MAGIC || rz->width <= 0.0f || rz->height <= 0.0f) {
        return;
    }
    if (rz->content_scale > 0.0f) {
        client->app->content_scale = rz->content_scale;
    }
    const float scale = ydu_scale(client->app);
    error_absorb(yetty_ygui_framework_set_viewport(client->app->engine, rz->width / scale,
                                                   rz->height / scale));
    ydu_ui_relayout(client->app);
    ydu_ui_refresh(client->app);
}

/* Ask the host for pane geometry only: size + HiDPI factor, no mouse or key
 * forwarding (that would take the wheel away from the terminal's scrollback
 * and reroute our keystrokes). Written as a bare envelope on stdout — the
 * same transport every client-to-host emit uses. */
static void ydu_subscribe_geometry(void)
{
    struct yetty_client_input_sub msg = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = YETTY_CLIENT_INPUT_SUB_RESIZE,
        ._pad0 = 0,
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emitted = yetty_yface_emit(
        YETTY_OSC_CS_CLIENT_INPUT_SUB, /*compressed=*/0, NULL, 0, &msg, sizeof(msg), &envelope);
    if (YETTY_IS_OK(emitted) && envelope.size > 0) {
        ssize_t written = write(STDOUT_FILENO, envelope.data, envelope.size);
        (void)written;
    } else if (YETTY_IS_ERR(emitted)) {
        yetty_ycore_error_destroy(emitted.error);
    }
    yetty_ycore_buffer_destroy(&envelope);
}

static void ydu_pickup_winsz(struct ydu_client *client)
{
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        /* TIOCGWINSZ pixels are FRAMEBUFFER px (cols x cell, and the cell
         * stride already carries content_scale) — divide like the resize
         * envelope so this fallback can't clobber the logical viewport. */
        const float scale = ydu_scale(client->app);
        error_absorb(yetty_ygui_framework_set_viewport(
            client->app->engine, (float)ws.ws_xpixel / scale, (float)ws.ws_ypixel / scale));
        ydu_ui_relayout(client->app);
        ydu_ui_refresh(client->app);
    }
}

static void ydu_sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)signum;
    ydu_pickup_winsz((struct ydu_client *)handle->data);
}

static void ydu_prep_cb(uv_prepare_t *handle)
{
    struct ydu_client *client = handle->data;
    if (yetty_ygui_framework_is_dirty(client->app->engine)) {
        error_absorb(yetty_ygui_framework_emit(client->app->engine));
    }
    if (!client->app->running) {
        uv_stop(handle->loop);
    }
}

static void ydu_close_cb(uv_handle_t *handle)
{
    (void)handle;
}

static int ydu_run_client(struct ydu_app *app)
{
    /* Raw tty BEFORE any OSC write, or the slave tty echoes the ESC bytes back
     * and the host yetty renders "^[" garbage. */
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

    struct ydu_client client = {0};
    client.app = app;
    if (uv_loop_init(&client.loop) != 0) {
        fprintf(stderr, "ydu: uv_loop_init failed\n");
        goto cleanup;
    }

    struct yetty_yclass_object_ptr_result framework_res = yetty_ygui_framework_create(NULL);
    if (YETTY_IS_ERR(framework_res)) {
        fprintf(stderr, "ydu: framework_create failed: %s\n", framework_res.error.msg);
        yetty_ycore_error_destroy(framework_res.error);
        goto cleanup_loop;
    }
    app->engine = framework_res.value;
    yetty_ygui_framework_set_key_cb(app->engine, on_key, &client);

    /* Attach to the host yetty's root figure container over the yclass-RPC
     * transport. The handshake reads stdin synchronously ONCE — it MUST run
     * before libuv's stdin poll takes the fd, or every emit fails with
     * "no container" and nothing renders. */
    struct yetty_ycore_void_result attach_res =
        yetty_ygui_framework_attach(app->engine, STDIN_FILENO, STDOUT_FILENO, /*compressed=*/1);
    if (YETTY_IS_ERR(attach_res)) {
        fprintf(stderr, "ydu: framework_attach failed: %s\n", attach_res.error.msg);
        yetty_ycore_error_destroy(attach_res.error);
        goto cleanup_loop;
    }

    struct yetty_yclass_object_ptr_result root_res =
        yetty_ygui_widget_new(yetty_ygui_vbox_class_get().value);
    if (YETTY_IS_OK(root_res)) {
        app->root_widget = root_res.value;
        error_absorb(yetty_ygui_framework_set_root(app->engine, app->root_widget));
        error_absorb(ydu_ui_build(app));
        ydu_ui_refresh(app);
    } else {
        yetty_ycore_error_destroy(root_res.error);
    }

    /* Envelope splitter in front of stdin, then ask the host for pane geometry.
     * The subscription must go out before the first winsz pickup so the scale
     * is known by the time anything sizes itself. */
    {
        struct yetty_yface_ptr_result yface_res = yetty_yface_create();
        if (YETTY_IS_ERR(yface_res)) {
            fprintf(stderr, "ydu: yface_create failed: %s\n", yface_res.error.msg);
            yetty_ycore_error_destroy(yface_res.error);
            goto cleanup_loop;
        }
        client.yface = yface_res.value;
        yetty_yface_set_handlers(client.yface, ydu_on_osc, ydu_on_raw, &client);
        ydu_subscribe_geometry();
    }

    if (uv_poll_init(&client.loop, &client.stdin_poll, STDIN_FILENO) == 0) {
        client.stdin_poll.data = &client;
        uv_poll_start(&client.stdin_poll, UV_READABLE, ydu_stdin_cb);
    }
    if (uv_signal_init(&client.loop, &client.sigwinch) == 0) {
        client.sigwinch.data = &client;
        uv_signal_start(&client.sigwinch, ydu_sigwinch_cb, SIGWINCH);
    }
    if (uv_prepare_init(&client.loop, &client.prep) == 0) {
        client.prep.data = &client;
        uv_prepare_start(&client.prep, ydu_prep_cb);
    }

    ydu_pickup_winsz(&client);

    uv_run(&client.loop, UV_RUN_DEFAULT);

    uv_poll_stop(&client.stdin_poll);
    uv_signal_stop(&client.sigwinch);
    uv_prepare_stop(&client.prep);
    uv_close((uv_handle_t *)&client.stdin_poll, ydu_close_cb);
    uv_close((uv_handle_t *)&client.sigwinch, ydu_close_cb);
    uv_close((uv_handle_t *)&client.prep, ydu_close_cb);
    uv_run(&client.loop, UV_RUN_NOWAIT);

    /* Wipe every figure ydu drew off the host pane before tearing down the
     * session, so the dashboard is not left frozen on screen after exit. */
    error_absorb(yetty_ygui_framework_clear(app->engine));
    ydu_ui_free(app);
    error_absorb(yetty_ygui_framework_destroy(app->engine));

cleanup_loop:
    if (client.yface) {
        yetty_yface_destroy(client.yface);
        client.yface = NULL;
    }
    uv_loop_close(&client.loop);
cleanup:
    if (tty_raw_ok) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = ".";
    int print_mode = 0;
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            ydu_usage();
            return 0;
        }
        if (strcmp(arg, "-p") == 0 || strcmp(arg, "--print") == 0) {
            print_mode = 1;
        } else if (arg[0] != '-') {
            path = arg;
        }
    }

    ytrace_init();

    struct ydu_node *tree = NULL;
    struct ydu_scan_stats stats;
    struct yetty_ycore_void_result scan_res = ydu_scan(path, &tree, &stats);
    if (YETTY_IS_ERR(scan_res)) {
        fprintf(stderr, "ydu: %s: %s\n", path, scan_res.error.msg);
        yetty_ycore_error_destroy(scan_res.error);
        return 1;
    }

    if (print_mode) {
        ydu_print(tree, &stats);
        ydu_node_destroy(tree);
        return 0;
    }

    struct ydu_app *app = calloc(1, sizeof(*app));
    if (!app) {
        ydu_node_destroy(tree);
        return 1;
    }
    app->running = 1;
    app->sort_mode = YDU_SORT_DISK;
    app->tree = tree;
    app->cwd = tree;
    app->stats = stats;
    snprintf(app->root_path, sizeof(app->root_path), "%s", path);
    ydu_node_sort_children(app->cwd, app->sort_mode);

    int rc = ydu_run_client(app);

    ydu_node_destroy(app->tree);
    free(app);
    return rc;
}
