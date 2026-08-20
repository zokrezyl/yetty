/*
 * main.c — `ymux`: the tmux equivalent for yetty (#695). ONE binary, the
 * tmux model exactly: the first invocation forks the server from itself
 * (no separate daemon executable); commands, aliases, and flags follow
 * tmux's grammar verbatim:
 *
 *   ymux [-2CDluvV] [-c shell-command] [-f file] [-L socket-name]
 *        [-S socket-path] [command [flags]]
 *
 *   new-session (new) [-AdDEPX] [-s session-name] [-x width] [-y height]
 *   attach-session (attach) [-t target-session]
 *   detach-client (detach) [-s target-session]
 *   has-session (has) [-t target-session]
 *   kill-server
 *   kill-session [-a] [-t target-session]
 *   list-sessions (ls)
 *   list-commands (lscm)
 *   rename-session (rename) [-t target-session] new-name
 *   send-keys (send) [-t target-pane] key ...
 *
 * Unambiguous command prefixes resolve like tmux (`ymux att`, `ymux ls`).
 * The default socket is /tmp/ymux-<uid>/<name> with name "default" (-L
 * picks the name, -S an explicit path) — tmux's /tmp/tmux-<uid> layout.
 *
 * Sessions (shells, scrollback, rich content) live in the forked server
 * and survive every client and every yetty. The attach DISPLAY bridge
 * (server paint → the pane's yscene over the standard figure wire) is the
 * in-progress piece; commands that need it say so instead of pretending.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/yclass/transport-pty.h>
#include <yetty/yface/yface.h>
#include <yetty/ymgui/wire.h>
#include <yetty/yterminal/client-input.h>
#include "yetty/ymux/key-encode.h"
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/yfigure/kind.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/yplatform/pty.h>

#include "../../src/yetty/ymux/rich-format.h"

#include <yetty/api/yfigure/container.h>
#include <yetty/api/yfigure/figure.h>
#include <yetty/api/ymux/client.h>
#include <yetty/api/ymux/daemon.h>
#include <yetty/api/ymux/engine.h>

/* Module-private wire enum (YMUX_TERM_CAP_*) — the tool is the reference ymux
 * client, so it advertises the capability profile of its libvterm-backed sink. */
#include "../../src/yetty/ymux/proto.h"
#include <yetty/api/yscene/scene.h>
#include <yetty/api/yterminal/terminal.h>
#include <yetty/ytrace/ytrace.h>

#define YMUX_VERSION_STRING "ymux 0.1"

/*===========================================================================
 * Server half (runs in the forked child — same binary, tmux-style).
 *===========================================================================*/

/* forkpty.c resolves its command line through the yconfig ops vtable
 * (get_shell_argv); serve $SHELL through it. */
struct ymux_server_config {
    struct yetty_yconfig_config base;
};

static struct yetty_ycore_void_result server_config_get_shell_argv(
    const struct yetty_yconfig_config *self, struct yetty_yconfig_shell_argv *out)
{
    (void)self;
    memset(out, 0, sizeof(*out));
    const char *shell = getenv("SHELL");
    if (!shell || !shell[0]) {
        shell = "/bin/sh";
    }
    snprintf(out->buf, sizeof(out->buf), "%s", shell);
    out->argv[0] = out->buf;
    out->argv[1] = NULL;
    out->argc = 1;
    return YETTY_OK_VOID();
}

static const char *server_config_get_string(const struct yetty_yconfig_config *self,
                                            const char *path, const char *default_value)
{
    (void)self;
    (void)path;
    return default_value;
}

static int server_config_get_int(const struct yetty_yconfig_config *self, const char *path,
                                 int default_value)
{
    (void)self;
    (void)path;
    return default_value;
}

static int server_config_get_bool(const struct yetty_yconfig_config *self, const char *path,
                                  int default_value)
{
    (void)self;
    (void)path;
    return default_value;
}

static int server_config_has(const struct yetty_yconfig_config *self, const char *path)
{
    (void)self;
    (void)path;
    return 0;
}

static const struct yetty_yconfig_config_ops *server_config_ops(void)
{
    static const struct yetty_yconfig_config_ops ops = {
        .get_string = server_config_get_string,
        .get_int = server_config_get_int,
        .get_bool = server_config_get_bool,
        .has = server_config_has,
        .get_shell_argv = server_config_get_shell_argv,
    };
    return &ops;
}

static struct yetty_yplatform_pty_ptr_result server_spawn(uint32_t rows, uint32_t cols,
                                                          void *userdata)
{
    struct ymux_server_config *config = userdata;
    struct yetty_yplatform_pty_ptr_result pty_res = yetty_yplatform_fork_pty_create(&config->base);
    YETTY_RETURN_IF_ERR(yetty_yplatform_pty_ptr, pty_res, "ymux server: forkpty");
    struct yetty_ycore_void_result resize_res =
        pty_res.value->ops->resize(pty_res.value, cols, rows, 0, 0);
    if (YETTY_IS_ERR(resize_res)) {
        yetty_ycore_error_destroy(resize_res.error);
    }
    return pty_res;
}

static int server_run(const char *socket_path)
{
    signal(SIGPIPE, SIG_IGN);
    /* Spawned shells inherit this environment; the pane engine is an
     * xterm-class emulator — declare it regardless of how the server was
     * started (same default the ssh PTY path uses). */
    setenv("TERM", "xterm-256color", 1);
    setenv("COLORTERM", "truecolor", 1);
    setenv("TERM_PROGRAM", "yetty", 1);

    struct ymux_server_config config = {.base = {.ops = server_config_ops()}};
    struct yetty_ymux_daemon_host host = {.spawn = server_spawn, .userdata = &config};
    struct yetty_yclass_object_ptr_result daemon_res =
        yetty_ymux_daemon_make(socket_path, 24, 80, &host);
    if (YETTY_IS_ERR(daemon_res)) {
        fprintf(stderr, "ymux: server: %s\n",
                daemon_res.error.msg ? daemon_res.error.msg : "create failed");
        yetty_ycore_error_destroy(daemon_res.error);
        return 1;
    }
    struct yetty_yclass_object *daemon = daemon_res.value;

    int exit_code = 0;
    for (;;) {
        struct yetty_ycore_int_result step_res = yetty_ymux_daemon_step(daemon);
        if (YETTY_IS_ERR(step_res)) {
            fprintf(stderr, "ymux: server step failed: %s\n",
                    step_res.error.msg ? step_res.error.msg : "unknown");
            yetty_ycore_error_destroy(step_res.error);
            exit_code = 1;
            break;
        }
        struct yetty_ycore_int_result shutdown_res = yetty_ymux_daemon_shutdown_requested(daemon);
        if (YETTY_IS_OK(shutdown_res) && shutdown_res.value) {
            break;
        }
        if (YETTY_IS_ERR(shutdown_res)) {
            yetty_ycore_error_destroy(shutdown_res.error);
        }
        if (step_res.value == 0) {
            struct timespec nap = {.tv_sec = 0, .tv_nsec = 2 * 1000 * 1000};
            nanosleep(&nap, NULL);
        }
    }
    struct yetty_ycore_void_result dispose_res = yetty_ymux_daemon_dispose(daemon);
    if (YETTY_IS_ERR(dispose_res)) {
        yetty_ycore_error_destroy(dispose_res.error);
        exit_code = 1;
    }
    return exit_code;
}

/*===========================================================================
 * Server bootstrap (tmux model: fork the server from this binary).
 *===========================================================================*/

static int socket_alive(const char *path)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    int alive = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return alive;
}

/* Probe a live server for protocol compatibility. A daemon from an older build
 * (before a YMUX_PROTO_VERSION bump) refuses attach on version FIRST; a current
 * daemon refuses the bogus probe session on something else (or not at all). So a
 * version refuse == "stale running daemon a rebuild didn't replace". Returns 1
 * when compatible (or undeterminable), 0 when it refused on version. */
static int server_compatible(const char *socket_path)
{
    struct yetty_yclass_object_ptr_result client_res = yetty_ymux_client_make(socket_path);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 1;
    }
    struct yetty_yclass_object *client = client_res.value;
    struct yetty_ycore_void_result attach_res =
        yetty_ymux_client_attach(client, "\x01ymux-probe", 0, 1, 1, 0, 0, "probe");
    if (YETTY_IS_ERR(attach_res)) {
        yetty_ycore_error_destroy(attach_res.error);
    }
    uint32_t refuse = 0;
    for (int attempt = 0; attempt < 40 && refuse == 0; ++attempt) {
        struct yetty_ycore_int_result step_res = yetty_ymux_client_step(client);
        if (YETTY_IS_ERR(step_res)) {
            yetty_ycore_error_destroy(step_res.error);
            break;
        }
        struct yetty_ycore_uint32_result refuse_res = yetty_ymux_client_last_refuse(client);
        if (YETTY_IS_OK(refuse_res)) {
            refuse = refuse_res.value;
        } else {
            yetty_ycore_error_destroy(refuse_res.error);
        }
        if (refuse == 0) {
            struct timespec nap = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
            nanosleep(&nap, NULL);
        }
    }
    struct yetty_ycore_void_result dispose_res = yetty_ymux_client_dispose(client);
    if (YETTY_IS_ERR(dispose_res)) {
        yetty_ycore_error_destroy(dispose_res.error);
    }
    return refuse != 1 /* YMUX_PROTO_REFUSE_VERSION */;
}

/* Shut down the daemon on `socket_path` and wait for its socket to disappear. */
static void server_shutdown(const char *socket_path)
{
    struct yetty_yclass_object_ptr_result client_res = yetty_ymux_client_make(socket_path);
    if (YETTY_IS_OK(client_res)) {
        struct yetty_ycore_void_result shutdown_res =
            yetty_ymux_client_shutdown_server(client_res.value);
        if (YETTY_IS_ERR(shutdown_res)) {
            yetty_ycore_error_destroy(shutdown_res.error);
        }
        struct yetty_ycore_int_result step_res = yetty_ymux_client_step(client_res.value);
        if (YETTY_IS_ERR(step_res)) {
            yetty_ycore_error_destroy(step_res.error);
        }
        struct yetty_ycore_void_result dispose_res = yetty_ymux_client_dispose(client_res.value);
        if (YETTY_IS_ERR(dispose_res)) {
            yetty_ycore_error_destroy(dispose_res.error);
        }
    } else {
        yetty_ycore_error_destroy(client_res.error);
    }
    for (int attempt = 0; attempt < 40 && socket_alive(socket_path); ++attempt) {
        struct timespec nap = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000};
        nanosleep(&nap, NULL);
    }
    unlink(socket_path);
}

static int ensure_server(const char *socket_path)
{
    if (socket_alive(socket_path)) {
        if (server_compatible(socket_path)) {
            return 0;
        }
        /* A stale daemon from an older build is holding the socket — replace it
         * so the rebuilt binary's behaviour actually takes effect. */
        fprintf(stderr, "ymux: replacing an incompatible running server (rebuilt binary)\n");
        server_shutdown(socket_path);
    }
    char *slash = strrchr(socket_path, '/');
    if (slash && slash != socket_path) {
        char dir[256];
        size_t dir_len = (size_t)(slash - socket_path);
        if (dir_len >= sizeof(dir)) {
            fprintf(stderr, "ymux: socket path too long\n");
            return -1;
        }
        memcpy(dir, socket_path, dir_len);
        dir[dir_len] = 0;
        mkdir(dir, 0700);
    }
    pid_t child = fork();
    if (child < 0) {
        fprintf(stderr, "ymux: fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (child == 0) {
        /* The server: same binary, detached — exactly tmux. */
        setsid();
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, 0);
            dup2(null_fd, 1);
            dup2(null_fd, 2);
            if (null_fd > 2) {
                close(null_fd);
            }
        }
        _exit(server_run(socket_path));
    }
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (socket_alive(socket_path)) {
            return 0;
        }
        struct timespec nap = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000};
        nanosleep(&nap, NULL);
    }
    fprintf(stderr, "ymux: server failed to start on %s\n", socket_path);
    return -1;
}

/*===========================================================================
 * Control connection: send a verb, wait for the reply.
 *===========================================================================*/

struct control {
    struct yetty_yclass_object *client;
    uint64_t last_reply_generation;
};

static int control_open(struct control *control, const char *socket_path)
{
    struct yetty_yclass_object_ptr_result client_res = yetty_ymux_client_make(socket_path);
    if (YETTY_IS_ERR(client_res)) {
        fprintf(stderr, "ymux: can't connect to server on %s\n", socket_path);
        yetty_ycore_error_destroy(client_res.error);
        return -1;
    }
    control->client = client_res.value;
    control->last_reply_generation = 0;
    return 0;
}

static void control_close(struct control *control)
{
    if (control->client) {
        struct yetty_ycore_void_result dispose_res = yetty_ymux_client_dispose(control->client);
        if (YETTY_IS_ERR(dispose_res)) {
            yetty_ycore_error_destroy(dispose_res.error);
        }
        control->client = NULL;
    }
}

/* Pump until a session reply arrives (~2s). Returns reply status, -1 on
 * timeout. `out_text` borrows from the client. A REFUSE while waiting
 * means the listener speaks another protocol generation (a stale server
 * from an older build holding the socket) — say so instead of timing
 * out. */
static int control_wait_reply(struct control *control, const char **out_text)
{
    for (int attempt = 0; attempt < 200; ++attempt) {
        struct yetty_ycore_int_result step_res = yetty_ymux_client_step(control->client);
        if (YETTY_IS_ERR(step_res)) {
            fprintf(stderr, "ymux: server connection lost\n");
            yetty_ycore_error_destroy(step_res.error);
            return -1;
        }
        const char *text = NULL;
        uint32_t status = 0;
        struct yetty_ycore_uint64_result reply_res =
            yetty_ymux_client_session_reply(control->client, &text, &status);
        if (YETTY_IS_OK(reply_res) && reply_res.value != control->last_reply_generation) {
            control->last_reply_generation = reply_res.value;
            if (out_text) {
                *out_text = text;
            }
            return (int)status;
        }
        if (YETTY_IS_ERR(reply_res)) {
            yetty_ycore_error_destroy(reply_res.error);
        }
        struct yetty_ycore_uint32_result refuse_res =
            yetty_ymux_client_last_refuse(control->client);
        if (YETTY_IS_OK(refuse_res) && refuse_res.value != 0) {
            fprintf(stderr, "ymux: server refused the command (protocol mismatch — a stale "
                            "server from an older build is holding the socket; run "
                            "`ymux kill-server` or remove the socket and retry)\n");
            return -1;
        }
        if (YETTY_IS_ERR(refuse_res)) {
            yetty_ycore_error_destroy(refuse_res.error);
        }
        struct timespec nap = {.tv_sec = 0, .tv_nsec = 10 * 1000 * 1000};
        nanosleep(&nap, NULL);
    }
    fprintf(stderr, "ymux: server did not reply\n");
    return -1;
}

/*===========================================================================
 * Attach takeover — the display bridge (#695): mint a yscene figure in
 * THIS pane over the standard yclass-RPC wire (stdin/stdout DCS
 * envelopes — the same channel every figure producer uses; zero
 * yetty-side special-casing) and replay the server's paint transactions
 * into it. Keyboard passes through raw to the session; tmux prefix
 * C-b d detaches.
 *===========================================================================*/

/* Hand-written client setters (raw fn-ptr, not RPC-marshalled methods,
 * so they live outside the generated header). */
void yetty_ymux_client_set_paint_sink(struct yetty_yclass_object *obj,
                                      void (*sink)(const uint32_t *, size_t, const uint32_t *,
                                                   size_t, void *),
                                      void *userdata);
void yetty_ymux_client_enable_vtsink(struct yetty_yclass_object *obj,
                                     void (*emit)(uint64_t generation, const uint8_t *bytes,
                                                  size_t len, void *userdata),
                                     void *userdata);
void yetty_ymux_client_vtsink_defer_ack(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_client_vtsink_ack(struct yetty_yclass_object *obj,
                                                            uint64_t generation);
struct yetty_yclass_object *yetty_ymux_client_vtsink_object(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_ymux_vtsink_applied(struct yetty_yclass_object *obj);
int yetty_ymux_client_route_overlay_input(struct yetty_yclass_object *obj, uint32_t input_class,
                                          uint32_t figure_id, uint32_t overlay_figure_id);
void yetty_ymux_client_overlay_input_deliver(struct yetty_yclass_object *obj, uint32_t input_class,
                                             const uint8_t *bytes, size_t len);
void yetty_ymux_client_set_overlay_input_handler(struct yetty_yclass_object *obj,
                                                 void (*handler)(uint32_t input_class,
                                                                 const uint8_t *bytes, size_t len,
                                                                 void *userdata),
                                                 void *userdata);
void yetty_ymux_client_set_overlay_input_active(struct yetty_yclass_object *obj, int active);
void yetty_ymux_client_set_vtsink_reset_handler(struct yetty_yclass_object *obj,
                                                int (*handler)(void *userdata), void *userdata);
uint32_t yetty_ymux_client_pane_modes(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_client_send_tty_response(struct yetty_yclass_object *obj,
                                                                   const uint8_t *bytes,
                                                                   uint32_t byte_count);
struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(struct yetty_yclass_object *obj,
                                                                    uint32_t sequence,
                                                                    uint32_t input_class,
                                                                    const uint8_t *bytes,
                                                                    uint32_t byte_count);
void yetty_ymux_client_overlay_classify_input(
    struct yetty_yclass_object *obj, const uint8_t *bytes, size_t len,
    void (*emit)(uint32_t input_class, const uint8_t *bytes, size_t len, void *userdata),
    void *userdata);
/* Snapshot / restore the classifier's mutable state for a non-destructive
 * measure pass (see attach_overlay_measure_run). */
void yetty_ymux_client_overlay_classify_save(struct yetty_yclass_object *obj, uint8_t *carry_out,
                                             uint32_t *carry_len_out, int *paste_open_out);
void yetty_ymux_client_overlay_classify_restore(struct yetty_yclass_object *obj,
                                                const uint8_t *carry, uint32_t carry_len,
                                                int paste_open);
/* Figure-surface RPC proxy (#695: ygui/ygreeter). Plain hand-written functions
 * on the ymux client, forward-declared here (see enable_vtsink). */
void yetty_ymux_client_set_rpc_relay_sink(struct yetty_yclass_object *obj,
                                          void (*sink)(uint32_t channel_id, const uint8_t *bytes,
                                                       size_t len, void *userdata),
                                          void *userdata);
struct yetty_ycore_void_result yetty_ymux_client_rpc_relay(struct yetty_yclass_object *obj,
                                                           uint32_t channel_id,
                                                           const uint8_t *bytes, size_t len);
struct yetty_ycore_void_result yetty_ymux_client_rpc_relay_close(struct yetty_yclass_object *obj,
                                                                 uint32_t channel_id);
struct yetty_ycore_void_result yetty_ymux_client_figure_input(struct yetty_yclass_object *obj,
                                                              uint32_t wire_code,
                                                              const uint8_t *bytes, size_t len);
struct yetty_ycore_void_result yetty_ymux_client_drain(struct yetty_yclass_object *obj);
void yetty_ymux_client_set_rpc_relay_close_sink(struct yetty_yclass_object *obj,
                                                void (*sink)(uint32_t channel_id, void *userdata),
                                                void *userdata);

enum { ATTACH_SCENE_CHILD_ID = 7001, ATTACH_OVERLAY_CHILD_ID = 7002, ATTACH_MAX_RPC_CHANNELS = 16 };

struct attach_bridge {
    struct yetty_yclass_rpc_session *rpc;
    struct yetty_yclass_object *terminal_proxy;  /* free() */
    struct yetty_yclass_object *container_proxy; /* free() */
    struct yetty_yclass_object *scene_proxy;     /* free() */
    /* #699.4 two-scene compositor: the OVERLAY scene seated ABOVE the
     * content scene at the same rect. Empty (transparent) until overlay
     * chrome (copy-mode selections, messages) stages into it; being the
     * topmost child it receives pointer input FIRST (topmost-wins hit
     * test), and unconsumed input falls through to the content path. Its
     * creation is a REQUIRED attach invariant (review #10) — no degraded
     * single-scene mode. */
    struct yetty_yclass_object *overlay_proxy; /* free() */
    /* Drag selection (#699.5): anchor cell of an active left-button drag on
     * the CONTENT scene; drives the grid's inverted span via RPC. */
    int selection_dragging;
    int selection_pending;
    uint32_t selection_pending_span[4];
    uint32_t selection_pending_active;
    uint32_t selection_anchor_row;
    uint32_t selection_anchor_col;
    struct yetty_yclass_object *client; /* ymux control client */
    struct termios saved_termios;
    int termios_saved;
    uint32_t rows;
    uint32_t cols;
    float cell_width;
    float cell_height;
    float pixel_width;
    float pixel_height;
    uint64_t applied_paint_generation;
    uint64_t applied_rich_generation;
    /* VT redraw bytes pending a scene write, ACCUMULATED across the frames a
     * single client_step drained. The daemon's VT frames are incremental
     * deltas (tmux-style: only changed cells + minimal cursor moves), so they
     * must be applied in order and in full — dropping one desyncs the client.
     * We therefore concatenate the frames of one drain and flush them as ONE
     * terminal_grid_write RPC per poll iteration: lossless, yet still off the
     * stdin keystroke path (one RPC per iteration, not one per frame). */
    uint8_t *pending_vt;
    size_t pending_vt_len;
    size_t pending_vt_cap;
    /* Deferred delivery ACK (#699.6): the feed ACK is reported only after the
     * scene write covering those bytes COMPLETED, so the daemon's window
     * tracks true end-to-end application. `covered` = the sink's applied
     * generation whose scene write has completed; `reported` = the last
     * generation ACKed to the daemon. */
    uint64_t vt_write_covered_generation;
    uint64_t vt_ack_reported_generation;
    /* Per-write ACK ledger: each queued scene write records the session
     * completion count at which it is applied (responses drain in request
     * order) and the vtsink generation it covers. A write ACKs as soon as
     * ITS completion arrives — gating the ACK on the WHOLE pipeline being
     * empty starved it during a sustained flood (`find /`): every loop
     * iteration queued the next write before the check, the daemon's
     * unacked window jammed, and the screen froze for the flood's whole
     * duration. Ring overflow folds into the newest entry (ACK later,
     * never earlier). */
    struct attach_vt_ack_entry {
        uint64_t completion_target;
        uint64_t covered_generation;
    } vt_ack_ring[32];
    uint32_t vt_ack_ring_head;
    uint32_t vt_ack_ring_count;

    /* The #380/#676 transport stack for terminal output: the pane PTY carried by
     * a single-owner pty transport, a multiplexed ywire connection over it, and
     * the yclass RPC session on one dynamic channel of that connection (async ->
     * terminal_grid_write pipelines, no sync round trip per write). The RAW
     * well-known channel demuxes the user's keystrokes out of the same PTY into
     * attach_raw_sink -> pending_keys (no separate stdin reader). The legacy DCS
     * RPC transport is retired for terminal output here. */
    struct yetty_yclass_transport_pty *transport_pty; /* owned */
    struct yetty_ywire_connection *connection;        /* owned (borrows the pty) */
    uint8_t *pending_keys;
    /* Drain-poll gating (review #15): set when the current loop iteration
     * carried feed traffic (daemon frames / scene writes); polls run only
     * on idle iterations, every 16th. */
    int loop_carried_feed;
    int drain_poll_countdown;
    /* CONSUMED-but-undrained overlay events (review #16): the input drain
     * runs while this is nonzero REGARDLESS of current focus — a focus
     * release must not strand already-consumed events. Focus gates the
     * admission of new events, never the delivery of old ones. */
    uint32_t consumed_undrained;
    /* Client-side CHROME BACKLOG (review #17): consumed chrome events that
     * could not be sent to the daemon at flush time ([class u32][len u32]
     * [bytes] records, arrival order). Flushed by the drain leg with pure
     * client-side sends — no scene RPC anywhere on the retry path. */
    uint8_t *chrome_backlog;
    size_t chrome_backlog_len;
    size_t chrome_backlog_cap;
    /* DYNAMIC retry queue (cycle-22 P0): consumed events the ownership queue
     * could not retain (its soft cap reached) are held here — a growable
     * FIFO of [class][len][bytes] records, same framing as the backlog, with
     * NO fixed size or single-slot limit (the old 2048B slot dropped events
     * larger than it, and any second failed run from one chunk). While the
     * retry queue is non-empty the ordered-input drain STOPS popping new
     * input — real backpressure. Only a true realloc OOM can still drop, and
     * it is counted + logged. */
    uint8_t *overlay_retry_queue;
    size_t overlay_retry_len;
    size_t overlay_retry_cap;
    /* Reply polls are ARMED only when the projected stream just carried a
     * terminal QUERY (scanned at write time). In production the stream
     * carries none, so the steady state runs ZERO value polls — the
     * PTY/OSC wedge class cannot trigger at all (review #17). */
    uint32_t reply_poll_armed;
    /* Query-scanner join tail (review #19): the last bytes of the previous
     * publication, prepended to the next scan so a query split across yRPC
     * publications still arms the poll. */
    uint8_t query_tail[15];
    size_t query_tail_len;
    /* Overlay-event ownership (review #17/#19): EVERY consumed chrome event
     * is RETAINED in chrome_backlog until the daemon's sequence-bearing ACK
     * arrives — one event in flight, resent (same sequence; the server role
     * deduplicates) when the ACK does not arrive within the resend window.
     * Local send success is transport acceptance, never ownership
     * transfer. */
    uint32_t overlay_seq_next;
    uint32_t overlay_seq_inflight;     /* 0 = none */
    struct timespec overlay_sent_at;   /* resend clock for the inflight event */
    uint32_t overlay_dropped_events;   /* backlog hard-cap overflow count */
    uint32_t overlay_nack_handled_seq; /* one immediate retry per NACK */
    /* Chrome DOM revision (review #19 resize): bumped on every (re)stage so
     * the scene replaces the strip record instead of ignoring a stale
     * revision; also flags "strip currently staged" for the narrow-pane
     * clear. */
    uint32_t chrome_revision;
    /* Receiver-reset epoch markers (review #16): monotonic epoch stamped on
     * every applied receiver reset; the first scene feed after each reset
     * logs once — the harness asserts marker ordering. */
    uint32_t reset_epoch;
    int feed_after_reset_logged;
    /* Receiver-reset BARRIER (review #17): the pump-context reset handler
     * only REQUESTS; the loop performs the grid recreate and VALUE-POLLS
     * the remote grid generation until it advances — the republish is
     * withheld (handler returns 0, client retries every step) until the
     * fresh parser is REMOTE-OBSERVED. */
    int receiver_reset_requested;
    int receiver_reset_done;
    size_t pending_keys_len;
    size_t pending_keys_cap;
    /* Overflow buffer for figure-pointer events the ordered queue could not take
     * (transient realloc failure): retried each tick so a pointer is never
     * dropped on a single OOM — the counterpart of pending_keys for the raw
     * channel. */
    struct attach_pending_pointer {
        float x, y;
        uint32_t kind, button, mods, pressed;
    } *pending_pointers;
    size_t pending_pointers_len;
    size_t pending_pointers_cap;
    int rpc_error_logged; /* surfaced the first scene-RPC failure already */
    /* The VT (terminal-byte) stream became unusable — a delta could not be
     * buffered (OOM) or its pipelined grid write failed. The stateful stream
     * must NOT continue incrementally; the loop discards the pending bytes and
     * asks the daemon for a fresh complete redraw. */
    int vt_desynced;
    /* A detach chord decoded from a TRANSLATED figure-key envelope (the
     * OSC sink cannot return the verdict) — honored by the bridge loop. */
    int detach_requested;
    /* The persistent key-stream state machine (tmux prefix, UTF-8 reassembly
     * across chunk boundaries, structured cursor/nav decode, and the split-escape
     * carry) lives in key-encode.c so the whole path is unit-testable. A stranded
     * carry (a lone ESC, or a prefix that never completes) flushes as raw
     * codepoints on its own deadline below — tmux's escape-timeout behavior. */
    struct yetty_ymux_key_stream key_stream;
    struct timespec esc_carry_at; /* when the carry was armed — its own 10ms deadline */
    /* DOWN/CHAR correlation for Ctrl chords by control-byte IDENTITY (cycle-25
     * P1): the control byte the last figure-key DOWN folded to (0..0x7F), or -1
     * when none is pending. Only a CHAR whose own control byte EQUALS this is
     * recognised as the duplicate and suppressed — a mismatched or composed CHAR
     * is kept. Set by a folding DOWN, consumed by the paired CHAR, and reset to
     * -1 by any UP / non-key event. */
    int pending_ctrl_control;
    /* Splits the RAW pane stream into structured-input OSC envelopes (the
     * CLIENT_INPUT_FIGURE_MOUSE events yetty forwards once we enable the
     * ?1500/?1501 card-mouse modes) vs verbatim keystrokes. */
    struct yetty_yface *in_face;
    /* Figure-surface RPC proxy (#695: ygui/ygreeter). Each proxied pane channel
     * (keyed by the daemon-side channel_id) is piped to a FRESH dynamic channel
     * this tool opens on its own yetty connection — the fresh id can't collide
     * with the tool's own scene channel. yetty serves it + renders the figure;
     * responses ride back via the response sink. */
    struct attach_rpc_channel {
        int in_use;
        int closing;              /* daemon-initiated close in flight — suppress the echo notify */
        uint32_t pane_channel_id; /* daemon-side id (the map key) */
        struct yetty_ywire_channel *yetty_channel; /* our channel to yetty */
        struct attach_bridge *bridge;              /* back-pointer for the response relay */
    } rpc_channels[ATTACH_MAX_RPC_CHANNELS];
};

static void attach_stage_overlay_chrome(struct attach_bridge *bridge);

/* Does this byte run carry a terminal QUERY the receiving grid will answer?
 * DCS (\eP...), CSI finals n/c/p (DSR/DA/DECRQM — short sequences, scanned
 * to their real final), and OSC color queries (\e]10;? / \e]11;?). */
static int attach_stream_carries_query(const uint8_t *bytes, size_t len)
{
    for (size_t scan = 0; scan + 1 < len; ++scan) {
        if (bytes[scan] != 0x1b) {
            continue;
        }
        uint8_t next_byte = bytes[scan + 1];
        if (next_byte == 'P') {
            return 1; /* DCS query candidate */
        }
        if (next_byte == '[') {
            for (size_t fin = scan + 2; fin < len; ++fin) {
                uint8_t final_byte = bytes[fin];
                if (final_byte >= 0x40 && final_byte <= 0x7E) {
                    if (final_byte == 'n' || final_byte == 'c' || final_byte == 'p') {
                        return 1;
                    }
                    break;
                }
            }
            continue;
        }
        if (next_byte == ']') {
            /* OSC query: digits then ";?" (color get: \e]10;?\e\\). */
            size_t cursor = scan + 2;
            while (cursor < len && bytes[cursor] >= '0' && bytes[cursor] <= '9') {
                ++cursor;
            }
            if (cursor + 1 < len && bytes[cursor] == ';' && bytes[cursor + 1] == '?') {
                return 1;
            }
        }
    }
    return 0;
}

/* Pane geometry from THIS process's tty: the ymux client runs inside the
 * pane's shell, so TIOCGWINSZ is the pane grid, and yetty populates
 * ws_xpixel/ws_ypixel with the real pixel area (pty resize contract). */
static void attach_read_winsize(struct attach_bridge *bridge)
{
    struct winsize winsize_now;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &winsize_now) != 0) {
        bridge->rows = 24;
        bridge->cols = 80;
        bridge->cell_width = 11.0f;
        bridge->cell_height = 23.0f;
        bridge->pixel_width = bridge->cell_width * 80.0f;
        bridge->pixel_height = bridge->cell_height * 24.0f;
        return;
    }
    bridge->rows = winsize_now.ws_row ? winsize_now.ws_row : 24;
    bridge->cols = winsize_now.ws_col ? winsize_now.ws_col : 80;
    if (winsize_now.ws_xpixel && winsize_now.ws_ypixel) {
        bridge->pixel_width = (float)winsize_now.ws_xpixel;
        bridge->pixel_height = (float)winsize_now.ws_ypixel;
        bridge->cell_width = bridge->pixel_width / (float)bridge->cols;
        bridge->cell_height = bridge->pixel_height / (float)bridge->rows;
    } else {
        bridge->cell_width = 11.0f;
        bridge->cell_height = 23.0f;
        bridge->pixel_width = bridge->cell_width * (float)bridge->cols;
        bridge->pixel_height = bridge->cell_height * (float)bridge->rows;
    }
}

/* No-op: installed for SIGWINCH purely so a window resize interrupts the
 * bridge's poll() with EINTR and it re-reads the geometry promptly. No state
 * is touched here — the loop polls the winsize on the next iteration. */
static void attach_sigwinch(int signum)
{
    (void)signum;
}

/* A window resize delivers SIGWINCH on the pane PTY. Re-read the geometry and,
 * if it changed, reflow the client grid, re-cover the pane with the scene
 * figure, and resize the daemon engine so the application sees the new size.
 * The cell pitch is held CONSTANT: the client grid keeps its create-time
 * metrics (terminal_grid_resize does not touch them) and rich-row reservation
 * on the daemon was computed against that pitch, so only rows/cols and the
 * pane rectangle move — re-deriving the pitch here would drift the two ends
 * apart and reintroduce the reservation gap. */
static void attach_apply_resize(struct attach_bridge *bridge)
{
    struct winsize winsize_now;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &winsize_now) != 0) {
        return;
    }
    uint32_t new_rows = winsize_now.ws_row ? winsize_now.ws_row : bridge->rows;
    uint32_t new_cols = winsize_now.ws_col ? winsize_now.ws_col : bridge->cols;
    float new_pixel_width =
        winsize_now.ws_xpixel ? (float)winsize_now.ws_xpixel : bridge->cell_width * (float)new_cols;
    float new_pixel_height = winsize_now.ws_ypixel ? (float)winsize_now.ws_ypixel
                                                   : bridge->cell_height * (float)new_rows;
    if (new_rows == bridge->rows && new_cols == bridge->cols &&
        new_pixel_width == bridge->pixel_width && new_pixel_height == bridge->pixel_height) {
        return;
    }
    bridge->rows = new_rows;
    bridge->cols = new_cols;
    bridge->pixel_width = new_pixel_width;
    bridge->pixel_height = new_pixel_height;

    struct yetty_ycore_rectangle pane_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = new_pixel_width, .y = new_pixel_height},
    };
    /* ONE resize transaction (review #19): bracket every layout call in the
     * scene's layout barrier — reseats, grid resize, and the chrome restage
     * pipeline in order and render as a SINGLE frame at barrier end. */
    if (bridge->scene_proxy) {
        struct yetty_ycore_void_result barrier_res =
            yetty_yscene_layout_barrier_begin(bridge->scene_proxy);
        if (YETTY_IS_ERR(barrier_res)) {
            yetty_ycore_error_destroy(barrier_res.error);
        }
    }
    if (bridge->overlay_proxy) {
        struct yetty_ycore_void_result overlay_barrier_res =
            yetty_yscene_layout_barrier_begin(bridge->overlay_proxy);
        if (YETTY_IS_ERR(overlay_barrier_res)) {
            yetty_ycore_error_destroy(overlay_barrier_res.error);
        }
    }
    if (bridge->container_proxy) {
        struct yetty_ycore_void_result seat_res =
            yetty_yfigure_seat_overlay(bridge->container_proxy, ATTACH_SCENE_CHILD_ID, pane_rect);
        if (YETTY_IS_ERR(seat_res)) {
            yetty_ycore_error_destroy(seat_res.error);
        }
        if (bridge->overlay_proxy) {
            struct yetty_ycore_void_result overlay_seat_res = yetty_yfigure_seat_overlay(
                bridge->container_proxy, ATTACH_OVERLAY_CHILD_ID, pane_rect);
            if (YETTY_IS_ERR(overlay_seat_res)) {
                yetty_ycore_error_destroy(overlay_seat_res.error);
            }
        }
    }
    if (bridge->scene_proxy) {
        struct yetty_ycore_void_result grid_res =
            yetty_yscene_terminal_grid_resize(bridge->scene_proxy, new_rows, new_cols);
        if (YETTY_IS_ERR(grid_res)) {
            yetty_ycore_error_destroy(grid_res.error);
        }
    }
    if (bridge->client) {
        struct yetty_ycore_void_result resize_res =
            yetty_ymux_client_resize(bridge->client, new_rows, new_cols);
        if (YETTY_IS_ERR(resize_res)) {
            yetty_ycore_error_destroy(resize_res.error);
        }
    }
    /* One resize transaction covers overlay CHROME layout too (review #19):
     * re-derive the strip from the NEW columns (or clear it when too
     * narrow) in the same pass as the scene reseats and the grid/view
     * resize — never left at the attach-time x. */
    attach_stage_overlay_chrome(bridge);
    /* Close the barrier — everything above renders as one frame. */
    if (bridge->overlay_proxy) {
        struct yetty_ycore_void_result overlay_barrier_end_res =
            yetty_yscene_layout_barrier_end(bridge->overlay_proxy);
        if (YETTY_IS_ERR(overlay_barrier_end_res)) {
            yetty_ycore_error_destroy(overlay_barrier_end_res.error);
        }
    }
    if (bridge->scene_proxy) {
        struct yetty_ycore_void_result barrier_end_res =
            yetty_yscene_layout_barrier_end(bridge->scene_proxy);
        if (YETTY_IS_ERR(barrier_end_res)) {
            yetty_ycore_error_destroy(barrier_end_res.error);
        }
    }
}

/* Bounded flush of the pane wire's outbound buffer. ywire writes only coalesce
 * into the connection outbuf; connection_destroy drops whatever is unsent. Pump
 * writable until the outbuf drains or a spin bound (forced-close fallback), so
 * the peer Yetty actually observes queued channel CLOSE frames. Pure writable
 * pump — it must NOT touch bridge->rpc, which is already gone by teardown. */
static void attach_emit_input_sub(struct yetty_ywire_channel *raw_channel, uint32_t flags);

static void attach_flush_connection(struct attach_bridge *bridge)
{
    if (!bridge->connection) {
        return;
    }
    int connection_fd = yetty_ywire_connection_fd(bridge->connection);
    for (int spin = 0; spin < 200; ++spin) {
        struct yetty_ycore_size_result write_res =
            yetty_ywire_connection_pump_writable(bridge->connection);
        if (YETTY_IS_ERR(write_res)) {
            yetty_ycore_error_destroy(write_res.error);
            break; /* transport gone — nothing more can be flushed */
        }
        if (!yetty_ywire_connection_want_write(bridge->connection)) {
            break; /* fully drained */
        }
        struct pollfd flush_poll = {.fd = connection_fd, .events = POLLOUT};
        if (poll(&flush_poll, 1, 50) <= 0) {
            break; /* not writable within the bound — forced-close fallback */
        }
    }
}

static void attach_cleanup(struct attach_bridge *bridge)
{
    /* UNSUBSCRIBE the host terminal FIRST: disable the ?1500/?1501
     * card-mouse modes enabled at attach (symmetric route — the RAW
     * channel). Without this, yetty keeps converting every physical mouse
     * event into CLIENT_INPUT_FIGURE_MOUSE envelopes on the PTY after the
     * bridge exits — and the RESUMED SHELL reads them as typed input,
     * flooding the terminal with base64 soup. */
    if (bridge->connection) {
        struct yetty_ywire_channel *raw_channel =
            yetty_ywire_connection_channel(bridge->connection, YETTY_YWIRE_CHANNEL_RAW);
        if (raw_channel) {
            static const char mouse_disable[] = "\x1b[?1501l\x1b[?1500l";
            attach_emit_input_sub(raw_channel, 0);
            struct yetty_ycore_size_result disable_res =
                yetty_ywire_channel_write(raw_channel, mouse_disable, sizeof(mouse_disable) - 1);
            if (YETTY_IS_ERR(disable_res)) {
                yetty_ycore_error_destroy(disable_res.error);
            }
            struct yetty_ycore_void_result disable_flush_res =
                yetty_ywire_channel_flush(raw_channel);
            if (YETTY_IS_ERR(disable_flush_res)) {
                yetty_ycore_error_destroy(disable_flush_res.error);
            }
        }
    }
    /* Retire every proxied figure-RPC channel with a real two-sided close before
     * the connection is torn down. yetty_ywire_connection_destroy() (below) frees
     * channel objects but emits no protocol CLOSE and fires no CLOSED callbacks,
     * so relying on it would strand both peer channels: the upstream Yetty channel
     * would linger and the ymux daemon would keep its pane-side mapping until an
     * unrelated later event. While client + connection are still live here, close
     * each upstream channel and tell the daemon to drop the pane-side mapping. */
    for (uint32_t index = 0; index < ATTACH_MAX_RPC_CHANNELS; ++index) {
        struct attach_rpc_channel *entry = &bridge->rpc_channels[index];
        if (entry->in_use) {
            if (entry->yetty_channel) {
                struct yetty_ycore_void_result close_res =
                    yetty_ywire_channel_close(entry->yetty_channel);
                if (YETTY_IS_ERR(close_res)) {
                    yetty_ycore_error_destroy(close_res.error);
                }
            }
            if (bridge->client && !entry->closing) {
                struct yetty_ycore_void_result relay_res =
                    yetty_ymux_client_rpc_relay_close(bridge->client, entry->pane_channel_id);
                if (YETTY_IS_ERR(relay_res)) {
                    yetty_ycore_error_destroy(relay_res.error);
                }
            }
        }
        entry->in_use = 0;
        entry->closing = 0;
        entry->yetty_channel = NULL;
    }
    if (bridge->overlay_proxy && bridge->container_proxy) {
        struct yetty_ycore_void_result overlay_delete_res =
            yetty_yfigure_delete_child(bridge->container_proxy, ATTACH_OVERLAY_CHILD_ID);
        if (YETTY_IS_ERR(overlay_delete_res)) {
            yetty_ycore_error_destroy(overlay_delete_res.error);
        }
    }
    if (bridge->scene_proxy && bridge->container_proxy) {
        struct yetty_ycore_void_result delete_res =
            yetty_yfigure_delete_child(bridge->container_proxy, ATTACH_SCENE_CHILD_ID);
        if (YETTY_IS_ERR(delete_res)) {
            yetty_ycore_error_destroy(delete_res.error);
        }
    }
    /* Flush the teardown calls OUT and consume their completions IN while
     * the session is still alive. The bridge exits right after cleanup;
     * any response frame left unread in the PTY (delete_child completions,
     * credit grants) would be typed into the resumed shell as envelope
     * soup. Bounded: stop when the pipeline is empty, the wire goes quiet
     * for 50 ms, or after ~500 ms total. */
    if (bridge->connection && bridge->rpc) {
        attach_flush_connection(bridge);
        int drain_fd = yetty_ywire_connection_fd(bridge->connection);
        for (int spin = 0; spin < 10; ++spin) {
            struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(bridge->rpc);
            if (YETTY_IS_ERR(pump_res)) {
                yetty_ycore_error_destroy(pump_res.error);
                break;
            }
            if (yetty_yclass_rpc_session_pending(bridge->rpc) == 0) {
                break; /* every queued call completed — the wire owes nothing */
            }
            struct pollfd drain_poll = {.fd = drain_fd, .events = POLLIN};
            if (poll(&drain_poll, 1, 50) <= 0) {
                break; /* quiet — nothing more inbound within the bound */
            }
            struct yetty_ycore_size_result readable_res =
                yetty_ywire_connection_pump_readable(bridge->connection);
            if (YETTY_IS_ERR(readable_res)) {
                yetty_ycore_error_destroy(readable_res.error);
                break;
            }
        }
    }
    if (bridge->in_face) {
        struct yetty_ycore_void_result face_res = yetty_yface_destroy(bridge->in_face);
        if (YETTY_IS_ERR(face_res)) {
            yetty_ycore_error_destroy(face_res.error);
        }
        bridge->in_face = NULL;
    }
    free(bridge->pending_vt);
    free(bridge->pending_keys);
    free(bridge->pending_pointers);
    bridge->pending_pointers = NULL;
    bridge->pending_pointers_len = 0;
    bridge->pending_pointers_cap = 0;
    free(bridge->chrome_backlog);
    bridge->chrome_backlog = NULL;
    bridge->chrome_backlog_len = 0;
    free(bridge->overlay_retry_queue);
    bridge->overlay_retry_queue = NULL;
    bridge->overlay_retry_len = 0;
    bridge->overlay_retry_cap = 0;
    bridge->chrome_backlog_cap = 0;
    bridge->pending_vt = NULL;
    bridge->pending_vt_len = 0;
    bridge->pending_vt_cap = 0;
    bridge->pending_keys = NULL;
    bridge->pending_keys_len = 0;
    bridge->pending_keys_cap = 0;
    /* The RPC proxies (root/container/scene) are session-owned via
     * connect_channel navigation — disconnect destroys the session, which frees
     * every proxy and the channel-adapter transport. Then the connection (SM +
     * channels) and finally the pty transport (which restores the tty). */
    if (bridge->terminal_proxy) {
        struct yetty_ycore_void_result disconnect_res =
            yetty_yclass_rpc_disconnect(bridge->terminal_proxy);
        if (YETTY_IS_ERR(disconnect_res)) {
            yetty_ycore_error_destroy(disconnect_res.error);
        }
        bridge->terminal_proxy = NULL;
        bridge->container_proxy = NULL;
        bridge->scene_proxy = NULL;
        bridge->rpc = NULL;
    }
    if (bridge->connection) {
        /* Flush the outbound wire before destroying it. The relay-channel CLOSE
         * frames (above) and the RPC-disconnect teardown only COALESCE into the
         * connection outbuf; connection_destroy drops whatever is still unsent, so
         * without this bounded drain the peer Yetty never observes the channel
         * closes. The daemon-side RPC_RELAY_CLOSE rides the separate ymux client
         * transport and is flushed by yetty_ymux_client_drain() before dispose. */
        attach_flush_connection(bridge);
        struct yetty_ycore_void_result connection_res =
            yetty_ywire_connection_destroy(bridge->connection);
        if (YETTY_IS_ERR(connection_res)) {
            yetty_ycore_error_destroy(connection_res.error);
        }
        bridge->connection = NULL;
    }
    if (bridge->transport_pty) {
        /* FINAL inbound discard: the connection teardown above emitted
         * channel CLOSE frames; yetty ACKs them on the PTY after this
         * process stops parsing wire traffic. Anything left unread lands
         * in the resumed shell as a typed envelope fragment. Drain and
         * discard until the wire is quiet (bounded ~250 ms). */
        {
            char discard[4096];
            for (int spin = 0; spin < 5; ++spin) {
                struct pollfd discard_poll = {.fd = STDIN_FILENO, .events = POLLIN};
                if (poll(&discard_poll, 1, 50) <= 0) {
                    break; /* quiet — nothing more inbound */
                }
                ssize_t discarded = read(STDIN_FILENO, discard, sizeof(discard));
                if (discarded <= 0) {
                    break;
                }
            }
        }
        struct yetty_ycore_void_result pty_res =
            yetty_yclass_transport_pty_destroy(bridge->transport_pty);
        if (YETTY_IS_ERR(pty_res)) {
            yetty_ycore_error_destroy(pty_res.error);
        }
        bridge->transport_pty = NULL;
    }
    if (bridge->client) {
        struct yetty_ycore_void_result detach_res = yetty_ymux_client_detach(bridge->client);
        if (YETTY_IS_ERR(detach_res)) {
            yetty_ycore_error_destroy(detach_res.error);
        }
        /* Bounded blocking drain BEFORE dispose. The relay-close and DETACH frames
         * were enqueued via the client's NONBLOCKING flush, which can leave them in
         * the client tx buffer under backpressure; dispose closes the socket and
         * frees that buffer, dropping them. Drain the client transport so the daemon
         * receives the closes (forced-close fallback after the bound). */
        struct yetty_ycore_void_result drain_res = yetty_ymux_client_drain(bridge->client);
        if (YETTY_IS_ERR(drain_res)) {
            yetty_ycore_error_destroy(drain_res.error);
        }
        struct yetty_ycore_void_result dispose_res = yetty_ymux_client_dispose(bridge->client);
        if (YETTY_IS_ERR(dispose_res)) {
            yetty_ycore_error_destroy(dispose_res.error);
        }
        bridge->client = NULL;
    }
    if (bridge->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &bridge->saved_termios);
        bridge->termios_saved = 0;
    }
}

/* Per-frame VT sink (installed on the client): APPEND the daemon's VT delta
 * bytes to the pending buffer; the poll loop flushes the accumulated stream to
 * the scene once per iteration (attach_flush_content). The frames are incremental
 * deltas, so every byte must reach the client in order — we concatenate rather
 * than overwrite, then send one RPC per iteration to stay off the keystroke
 * path without losing any update. */
static void attach_vt_sink(const uint8_t *bytes, size_t len, void *userdata)
{
    struct attach_bridge *bridge = userdata;
    if (!bytes || len == 0) {
        return;
    }
    size_t needed = bridge->pending_vt_len + len;
    if (needed > bridge->pending_vt_cap) {
        size_t new_cap = bridge->pending_vt_cap ? bridge->pending_vt_cap * 2 : 4096;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(bridge->pending_vt, new_cap);
        if (!grown) {
            /* Can't buffer this stateful delta — mark the stream desynced so the
             * loop discards the pending bytes and requests a complete redraw,
             * rather than silently dropping it and continuing incrementally. */
            bridge->vt_desynced = 1;
            return;
        }
        bridge->pending_vt = grown;
        bridge->pending_vt_cap = new_cap;
    }
    memcpy(bridge->pending_vt + bridge->pending_vt_len, bytes, len);
    bridge->pending_vt_len = needed;
}

/* vtsink lane emit (#699.2): the daemon's typed ordered feed() delivery. Same
 * accumulation as the legacy per-frame VT sink — the generation is the
 * daemon's flow-control tag, ACKed by the client core after dispatch, so the
 * bridge only routes the bytes. */
static void attach_vtsink_emit(uint64_t generation, const uint8_t *bytes, size_t len,
                               void *userdata)
{
    (void)generation;
    attach_vt_sink(bytes, len, userdata);
}

/* The VT stream desynced (a delta was dropped or its write failed): discard the
 * obsolete queued bytes and ask the daemon for a fresh complete redraw. The
 * daemon invalidates the projector, so the next VT frame is a full reset+redraw
 * that re-establishes the receiving grid — never a delta onto a hole. */
/* RESET THE RECEIVER (#699.6, review #13): discard the pending VT stream and
 * rebuild the receiving grid — a discarded epoch may have ended mid-CSI/OSC/
 * UTF-8 or inside the alternate screen, and a fresh redraw into that stale
 * parser/mode state is not guaranteed to recover it. terminal_grid_create
 * disposes the old grid and builds a NEW one (fresh libvterm, primary screen,
 * default modes); pipelined, so it is ordered BEFORE the redraw content that
 * follows. Shared by the client-driven resync AND the daemon-driven epoch
 * reset (VTSINK_RESET). */
static int attach_reset_receiver(struct attach_bridge *bridge)
{
    /* PUMP CONTEXT — request only. The LOOP performs the reset and
     * remote-observes it (attach_perform_receiver_reset); until then the
     * republish stays withheld and the client retries this handler every
     * step (review #17). */
    if (bridge->receiver_reset_done) {
        bridge->receiver_reset_done = 0;
        bridge->receiver_reset_requested = 0;
        return 1; /* remote-confirmed — the fresh epoch may open */
    }
    if (!bridge->receiver_reset_requested) {
        bridge->receiver_reset_requested = 1;
        bridge->pending_vt_len = 0; /* the dead epoch's queued bytes */
        bridge->vt_desynced = 1;
    }
    return 0;
}

static void attach_drain_rpc(struct attach_bridge *bridge);

/* LOOP side of the receiver-reset barrier: enqueue the grid recreate, drain
 * the pipeline, and VALUE-POLL the remote grid generation until it advances
 * — proof the receiver's parser was actually replaced. Only then does the
 * retried reset handler report success and the vtsink republish. */
static void attach_perform_receiver_reset(struct attach_bridge *bridge)
{
    if (!bridge->receiver_reset_requested || bridge->receiver_reset_done) {
        return;
    }
    /* NEGATIVE-CONTROL seam (tests only): never perform the reset — the
     * republish stays withheld forever, the fresh epoch never opens, and
     * post-recovery content never renders. The e2e fails on RECEIVER
     * STATE, not on a log line. */
    if (getenv("YMUX_YTEST_SKIP_RECEIVER_RESET")) {
        return;
    }
    if (!bridge->scene_proxy || !bridge->rpc) {
        return;
    }
    /* Value-call legality: only with the pipeline drained (same rule as
     * every other loop-side value call). */
    struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(bridge->rpc);
    if (YETTY_IS_ERR(pump_res)) {
        yetty_ycore_error_destroy(pump_res.error);
        return;
    }
    if (yetty_yclass_rpc_session_pending(bridge->rpc) != 0) {
        return; /* retry next iteration */
    }
    struct yetty_ycore_uint32_result before_res =
        yetty_yscene_terminal_grid_generation(bridge->scene_proxy);
    if (YETTY_IS_ERR(before_res)) {
        yetty_ycore_error_destroy(before_res.error);
        return; /* retry next iteration */
    }
    struct yetty_ycore_void_result create_res = yetty_yscene_terminal_grid_create(
        bridge->scene_proxy, bridge->rows, bridge->cols, bridge->cell_width, bridge->cell_height);
    if (YETTY_IS_ERR(create_res)) {
        yetty_ycore_error_destroy(create_res.error);
        return; /* transient — retry next iteration */
    }
    attach_drain_rpc(bridge);
    struct yetty_ycore_uint32_result after_res =
        yetty_yscene_terminal_grid_generation(bridge->scene_proxy);
    if (YETTY_IS_ERR(after_res)) {
        yetty_ycore_error_destroy(after_res.error);
        return;
    }
    if (after_res.value <= before_res.value) {
        return; /* not yet applied remotely — poll again next iteration */
    }
    bridge->vt_desynced = 0;
    bridge->vt_write_covered_generation = 0;
    bridge->vt_ack_reported_generation = 0;
    bridge->vt_ack_ring_head = 0;
    bridge->vt_ack_ring_count = 0; /* the dead epoch's ledgered writes */
    ++bridge->reset_epoch;
    bridge->feed_after_reset_logged = 0;
    bridge->receiver_reset_done = 1;
    fprintf(stderr, "ymux-bridge: receiver-reset applied epoch=%u remote-generation=%u\r\n",
            bridge->reset_epoch, after_res.value);
}

/* VTSINK_RESET notification (daemon-driven epoch cancel): the receiver
 * barrier runs before the client re-publishes the sink, so the fresh epoch's
 * complete redraw lands in a virgin grid. */
static int attach_on_vtsink_reset(void *userdata)
{
    struct attach_bridge *bridge = userdata;
    return attach_reset_receiver(bridge);
}

static void attach_request_resync(struct attach_bridge *bridge)
{
    (void)attach_reset_receiver(bridge);
    if (!bridge->client) {
        return;
    }
    struct yetty_ycore_void_result resync_res = yetty_ymux_client_resync(bridge->client);
    if (YETTY_IS_ERR(resync_res)) {
        yetty_ycore_error_destroy(resync_res.error);
    }
}

/* Publish the terminal (VT) and rich halves of a content update TOGETHER in one
 * atomic scene call (#699/#4), so a frame never shows the text update without
 * its paired rich update (or vice versa). Either half may be empty. */
static void attach_flush_content(struct attach_bridge *bridge)
{
    if (!bridge->scene_proxy) {
        return;
    }
    struct yetty_ycore_buffer vt = {0};
    if (bridge->pending_vt_len) {
        vt.data = bridge->pending_vt;
        vt.size = bridge->pending_vt_len;
        vt.capacity = bridge->pending_vt_len;
    }
    struct yetty_ycore_buffer rich = {0};
    uint64_t rich_gen = bridge->applied_rich_generation;
    if (bridge->client) {
        uint64_t generation = yetty_ymux_client_rich_generation(bridge->client).value;
        if (generation > bridge->applied_rich_generation) {
            uint32_t word_count = 0;
            struct yetty_ycore_const_uint32_ptr_result body_res =
                yetty_ymux_client_rich_body(bridge->client, &word_count);
            if (YETTY_IS_OK(body_res) && body_res.value && word_count) {
                rich.data = (void *)body_res.value;
                rich.size = (size_t)word_count * sizeof(uint32_t);
                rich.capacity = rich.size;
            } else if (YETTY_IS_ERR(body_res)) {
                yetty_ycore_error_destroy(body_res.error);
            }
            rich_gen = generation; /* absorb the generation even if the body is empty */
        }
    }
    if (vt.size == 0 && rich.size == 0) {
        bridge->applied_rich_generation = rich_gen;
        return;
    }
    if (bridge->reset_epoch > 0 && !bridge->feed_after_reset_logged && vt.size > 0) {
        bridge->feed_after_reset_logged = 1;
        fprintf(stderr, "ymux-bridge: first feed after reset epoch=%u\r\n", bridge->reset_epoch);
    }
    /* Arm reply polling when the outgoing stream carries a query the grid's
     * terminal will answer. FRAGMENTATION-SAFE (review #19): yRPC may split
     * a publication at every byte, so the tail of the previous publication
     * is prepended to a join window and both are scanned — a DSR split as
     * "\e[" | "6n" across publications still arms. OSC color queries
     * (\e]10;?/\e]11;?) arm too. */
    if (vt.size > 0) {
        uint8_t join_window[32];
        size_t join_len = bridge->query_tail_len;
        memcpy(join_window, bridge->query_tail, bridge->query_tail_len);
        size_t take = vt.size < 16 ? vt.size : 16;
        memcpy(join_window + join_len, vt.data, take);
        join_len += take;
        if (attach_stream_carries_query(join_window, join_len) ||
            attach_stream_carries_query(vt.data, vt.size)) {
            bridge->reply_poll_armed = 4;
        }
        size_t keep = vt.size < 15 ? vt.size : 15;
        memcpy(bridge->query_tail, vt.data + vt.size - keep, keep);
        bridge->query_tail_len = keep;
    }
    struct yetty_ycore_void_result res =
        yetty_yscene_terminal_write_content(bridge->scene_proxy, vt, rich);
    if (YETTY_IS_ERR(res)) {
        if (!bridge->rpc_error_logged) {
            ydebug("ymux bridge: terminal_write_content failed: %s",
                   res.error.msg ? res.error.msg : "unknown");
            bridge->rpc_error_logged = 1;
        }
        yetty_ycore_error_destroy(res.error);
        bridge->vt_desynced = 1; /* a lost content frame -> request a complete redraw */
    }
    if (vt.size && bridge->client && bridge->rpc && !bridge->vt_desynced) {
        /* Ledger the queued write: the generation it covers, and the session
         * completion count at which the write is applied remotely (ordered
         * pipeline: everything queued before it + itself). ACKed per-write
         * by attach_report_applied — never gated on a fully-drained
         * pipeline. */
        struct yetty_yclass_object *sink = yetty_ymux_client_vtsink_object(bridge->client);
        if (sink) {
            struct yetty_ycore_uint64_result applied_res = yetty_ymux_vtsink_applied(sink);
            if (YETTY_IS_OK(applied_res)) {
                uint64_t completion_target =
                    yetty_yclass_rpc_session_completed_total(bridge->rpc) +
                    (uint64_t)yetty_yclass_rpc_session_pending(bridge->rpc);
                uint32_t ring_capacity =
                    (uint32_t)(sizeof(bridge->vt_ack_ring) / sizeof(bridge->vt_ack_ring[0]));
                if (bridge->vt_ack_ring_count == ring_capacity) {
                    /* Full: fold into the newest entry — ACK later, never
                     * earlier. */
                    uint32_t newest =
                        (bridge->vt_ack_ring_head + bridge->vt_ack_ring_count - 1) % ring_capacity;
                    bridge->vt_ack_ring[newest].completion_target = completion_target;
                    bridge->vt_ack_ring[newest].covered_generation = applied_res.value;
                } else {
                    uint32_t slot =
                        (bridge->vt_ack_ring_head + bridge->vt_ack_ring_count) % ring_capacity;
                    bridge->vt_ack_ring[slot].completion_target = completion_target;
                    bridge->vt_ack_ring[slot].covered_generation = applied_res.value;
                    ++bridge->vt_ack_ring_count;
                }
            } else {
                yetty_ycore_error_destroy(applied_res.error);
            }
        }
    }
    bridge->pending_vt_len = 0;
    bridge->applied_rich_generation = rich_gen;
}

/* Report the deferred delivery ACK (#699.6): a queued scene write is applied
 * END TO END once ITS pipelined completion drains (ordered responses) — pop
 * every ledgered write whose completion arrived and ACK the newest covered
 * generation. Never gated on the whole pipeline being empty: a sustained
 * flood keeps the pipeline busy forever, and that gate froze the daemon's
 * projection window for the flood's whole duration. */
static void attach_report_applied(struct attach_bridge *bridge)
{
    if (!bridge->client || !bridge->rpc || bridge->vt_desynced) {
        return;
    }
    struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(bridge->rpc);
    if (YETTY_IS_ERR(pump_res)) {
        yetty_ycore_error_destroy(pump_res.error);
        return;
    }
    uint64_t completed_total = yetty_yclass_rpc_session_completed_total(bridge->rpc);
    uint32_t ring_capacity =
        (uint32_t)(sizeof(bridge->vt_ack_ring) / sizeof(bridge->vt_ack_ring[0]));
    while (bridge->vt_ack_ring_count > 0) {
        struct attach_vt_ack_entry *oldest = &bridge->vt_ack_ring[bridge->vt_ack_ring_head];
        if (oldest->completion_target > completed_total) {
            break; /* this write's completion has not drained yet */
        }
        bridge->vt_write_covered_generation = oldest->covered_generation;
        bridge->vt_ack_ring_head = (bridge->vt_ack_ring_head + 1) % ring_capacity;
        --bridge->vt_ack_ring_count;
    }
    if (bridge->vt_write_covered_generation <= bridge->vt_ack_reported_generation) {
        return;
    }
    struct yetty_ycore_void_result ack_res =
        yetty_ymux_client_vtsink_ack(bridge->client, bridge->vt_write_covered_generation);
    if (YETTY_IS_ERR(ack_res)) {
        yetty_ycore_error_destroy(ack_res.error);
        return;
    }
    bridge->vt_ack_reported_generation = bridge->vt_write_covered_generation;
}

static void attach_drain_rpc(struct attach_bridge *bridge);
static void attach_overlay_flush(struct attach_bridge *bridge);
static int attach_process_keys(struct attach_bridge *bridge, const uint8_t *keys, size_t count);

/* Poll the scene's scalar-word drains (review #15): grid-parsed terminal
 * REPLIES forward through the attachment input path (the single controlling
 * attachment answers — never N projections), and CONSUMED overlay input
 * events forward to the daemon's chrome seat (OVERLAY_INPUT). Value RPCs —
 * runs only from the loop with the pipeline drained. */
static void attach_poll_scene_drains(struct attach_bridge *bridge)
{
    if (!bridge->scene_proxy || !bridge->client || !bridge->rpc) {
        return;
    }
    /* Drain polls are VALUE round-trips on the same transport that carries
     * the render feed. Two hard rules, learned from a live deadlock (the
     * bridge blocked writing a grid update into a full PTY while yetty
     * blocked writing our poll's response into the full OSC channel):
     *   1. NEVER poll on an iteration that carried feed traffic — only on
     *      IDLE iterations, and throttled (every 16th), so a recovery
     *      redraw burst never interleaves with poll responses.
     *   2. Same legality rule as attach_report_applied: value calls only
     *      with NO pipelined completions pending. */
    if (bridge->reply_poll_armed == 0 && bridge->consumed_undrained == 0) {
        return; /* nothing to poll for — the steady state runs NO value calls */
    }
    if (bridge->loop_carried_feed) {
        bridge->drain_poll_countdown = 16;
        return;
    }
    if (bridge->drain_poll_countdown > 0) {
        --bridge->drain_poll_countdown;
        return;
    }
    /* Re-arm SHORT between idle drains (the 50ms pending-work poll makes
     * this ~100ms cadence): the burst protection is the feed reset above,
     * not this — a long re-arm made one-in-flight overlay forwarding crawl
     * at one event per 0.8s (review #17). */
    bridge->drain_poll_countdown = 2;
    struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(bridge->rpc);
    if (YETTY_IS_ERR(pump_res)) {
        yetty_ycore_error_destroy(pump_res.error);
        return;
    }
    if (yetty_yclass_rpc_session_pending(bridge->rpc) != 0) {
        /* Pipelined scene writes in flight: with a steady feed (cursor
         * blink repaints every ~500ms) a bail-out here practically NEVER
         * finds a clean window (review #17 — the drain starved). Drain the
         * completions actively (bounded), as every dispatch site does. */
        attach_drain_rpc(bridge);
        if (yetty_yclass_rpc_session_pending(bridge->rpc) != 0) {
            return;
        }
    }
    /* Terminal replies — only while ARMED by a written query. */
    if (bridge->reply_poll_armed == 0) {
        goto input_drain;
    }
    --bridge->reply_poll_armed;
    struct yetty_ycore_uint32_result pending_res =
        yetty_yscene_terminal_reply_pending(bridge->scene_proxy);
    if (YETTY_IS_OK(pending_res) && pending_res.value > 0) {
        /* RAW response bytes -> THIS attachment's daemon-side response
         * parser (review #16): preserved exactly, never keyboard input,
         * never the application PTY. */
        uint32_t pending = pending_res.value;
        uint8_t response_bytes[1024];
        if (pending > sizeof(response_bytes)) {
            pending = (uint32_t)sizeof(response_bytes);
        }
        for (uint32_t offset = 0; offset < pending; offset += 8) {
            struct yetty_ycore_uint64_result word_res =
                yetty_yscene_terminal_reply_word(bridge->scene_proxy, offset / 8);
            if (YETTY_IS_ERR(word_res)) {
                yetty_ycore_error_destroy(word_res.error);
                return;
            }
            uint32_t chunk = pending - offset < 8 ? pending - offset : 8;
            for (uint32_t byte_index = 0; byte_index < chunk; ++byte_index) {
                response_bytes[offset + byte_index] = (uint8_t)(word_res.value >> (byte_index * 8));
            }
        }
        struct yetty_ycore_void_result send_res =
            yetty_ymux_client_send_tty_response(bridge->client, response_bytes, pending);
        if (YETTY_IS_ERR(send_res)) {
            /* NOT sent: the bytes stay in the scene — retried next poll
             * (review #17: replies are retained until transport commit). */
            yetty_ycore_error_destroy(send_res.error);
            bridge->reply_poll_armed = 2; /* keep the poll armed to retry */
            return;
        }
        struct yetty_ycore_void_result consume_res =
            yetty_yscene_terminal_reply_consume(bridge->scene_proxy, pending);
        if (YETTY_IS_ERR(consume_res)) {
            yetty_ycore_error_destroy(consume_res.error);
        }
        /* DRAIN UNTIL EMPTY (review #19): a successful forward re-arms —
         * the poll count decays only on EMPTY polls, so a reply larger
         * than one 1 KiB chunk (or one produced while draining) keeps
         * flowing instead of stranding past a fixed four-poll budget. */
        bridge->reply_poll_armed = 4;
    } else if (YETTY_IS_ERR(pending_res)) {
        yetty_ycore_error_destroy(pending_res.error);
    }
input_drain:
    /* The ownership queue is pumped by attach_overlay_flush (one retained
     * event in flight, popped only on the daemon's sequence ACK). */
    attach_overlay_flush(bridge);
}

static void attach_queue_selection(struct attach_bridge *bridge, uint32_t start_row,
                                   uint32_t start_col, uint32_t end_row, uint32_t end_col,
                                   uint32_t active);

/* Stage the PRODUCTION overlay chrome (review #17): a scroll-mode
 * indicator strip in the pane's top-right corner — real opaque DOM content
 * in the overlay scene. Pressing it claims chrome input focus (the press
 * verdict resolves to this node); arrow keys then drive the daemon-role
 * scroll consumer; pressing terminal content releases focus. */
static void attach_stage_overlay_chrome(struct attach_bridge *bridge)
{
    if (!bridge->overlay_proxy) {
        return;
    }
    if (bridge->cols < 10) {
        /* Too narrow for the strip. If one is staged from a wider layout,
         * CLEAR it (record_count 0 clears the rich world) — a shrink must
         * not leave the strip outside the viewport but still hittable. */
        if (bridge->chrome_revision > 0) {
            uint32_t empty_body[3] = {YMUX_RICH_MAGIC, YMUX_RICH_VERSION, 0};
            struct yetty_ycore_buffer empty_frame = {.data = (uint8_t *)empty_body,
                                                     .size = sizeof(empty_body),
                                                     .capacity = sizeof(empty_body)};
            struct yetty_ycore_void_result clear_res =
                yetty_yscene_apply_content_transaction(bridge->overlay_proxy, empty_frame);
            if (YETTY_IS_ERR(clear_res)) {
                yetty_ycore_error_destroy(clear_res.error);
            }
        }
        return;
    }
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
        return;
    }
    struct yetty_ydraw_drawable_list *list = list_res.value;
    float cell_w = bridge->cell_width > 0.0f ? bridge->cell_width : 9.0f;
    float cell_h = bridge->cell_height > 0.0f ? bridge->cell_height : 18.0f;
    float strip_w = 6.0f * cell_w;
    float strip_x = (float)bridge->cols * cell_w - strip_w - cell_w * 0.5f;
    struct yetty_ysdf_box geometry = {
        .center_x = strip_x + strip_w * 0.5f,
        .center_y = cell_h * 0.5f,
        .half_width = strip_w * 0.5f,
        .half_height = cell_h * 0.45f,
        .corner_radius = 3.0f,
    };
    /* Observable geometry (harness + operator debugging): the staged strip
     * rect in pane pixels plus the metrics it was derived from. */
    fprintf(stderr,
            "ymux-bridge: chrome staged strip=[%.1f,%.1f %.1fx%.1f] cell=(%.2f,%.2f) "
            "grid=%ux%u px=(%.0f,%.0f)\r\n",
            strip_x, geometry.center_y - geometry.half_height, strip_w, geometry.half_height * 2.0f,
            cell_w, cell_h, bridge->cols, bridge->rows, bridge->pixel_width, bridge->pixel_height);
    /* Brand accent mint (#6BA892), ABGR. */
    /* id MUST be 0: a nonzero drawable id prefixes an id record the DOM
     * span parser does not consume (frame rejected). */
    struct yetty_ycore_void_result add_res = yetty_ydraw_drawable_list_add_cmd_add_box(
        list, /*id=*/0, /*z_order=*/0, /*fill=*/0xff92a86bu, /*stroke=*/0, /*stroke_w=*/0.0f,
        &geometry);
    if (YETTY_IS_ERR(add_res)) {
        yetty_ycore_error_destroy(add_res.error);
        yetty_ydraw_drawable_list_destroy(list);
        return;
    }
    size_t list_bytes = yetty_ydraw_drawable_list_size(list);
    size_t list_words = list_bytes / sizeof(uint32_t);
    size_t frame_words = YMUX_RICH_HEADER_WORDS + YMUX_RICH_RECORD_HEADER_WORDS + 6 + list_words;
    uint32_t *words = malloc(frame_words * sizeof(uint32_t));
    if (!words) {
        yetty_ydraw_drawable_list_destroy(list);
        return;
    }
    size_t offset = 0;
    words[offset++] = YMUX_RICH_MAGIC;
    words[offset++] = YMUX_RICH_VERSION;
    words[offset++] = 1;                         /* one record: the indicator */
    words[offset++] = 0x43524F4Du;               /* rich id lo ("MORC") — bridge-owned chrome id */
    words[offset++] = 0;                         /* rich id hi */
    words[offset++] = ++bridge->chrome_revision; /* fresh revision per (re)stage */
    words[offset++] = 0;                         /* anchor row */
    words[offset++] = 0;                         /* anchor col */
    words[offset++] = 0;                         /* flags */
    words[offset++] = (uint32_t)(6 + list_words);
    words[offset++] = 0x31425059u; /* YPB1 magic — DOM path */
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = 0;
    words[offset++] = (uint32_t)list_bytes;
    memcpy(&words[offset], yetty_ydraw_drawable_list_data(list), list_bytes);
    offset += list_words;
    struct yetty_ycore_buffer frame = {.data = (uint8_t *)words,
                                       .size = offset * sizeof(uint32_t),
                                       .capacity = offset * sizeof(uint32_t)};
    struct yetty_ycore_void_result apply_res =
        yetty_yscene_apply_content_transaction(bridge->overlay_proxy, frame);
    if (YETTY_IS_ERR(apply_res)) {
        fprintf(stderr, "ymux-bridge: chrome stage apply FAILED: %s\r\n",
                apply_res.error.msg ? apply_res.error.msg : "?");
        yetty_ycore_error_destroy(apply_res.error);
    } else {
        /* Post-apply probe: the staged strip must be hittable at its own
         * center — 0 here means the DOM never materialized (the strip is
         * then invisible AND unclickable; review #17 click-miss). A MOVE
         * dispatch (kind 0) resolves the hit without claiming focus. The
         * apply above pipelines as a void call — DRAIN its completion
         * first (async-mode rule: no value call with completions in
         * flight). */
        attach_drain_rpc(bridge);
        struct yetty_ycore_uint64_result probe_res =
            yetty_yscene_dispatch_pointer(bridge->overlay_proxy, (uint32_t)geometry.center_x,
                                          (uint32_t)geometry.center_y, 0, 0, 0, 0);
        if (YETTY_IS_ERR(probe_res)) {
            fprintf(stderr, "ymux-bridge: chrome stage hit-probe ERROR: %s\r\n",
                    probe_res.error.msg ? probe_res.error.msg : "?");
            yetty_ycore_error_destroy(probe_res.error);
        } else {
            fprintf(stderr, "ymux-bridge: chrome stage hit-probe=%llu\r\n",
                    (unsigned long long)probe_res.value);
        }
    }
    free(words);
    yetty_ydraw_drawable_list_destroy(list);
}

/* Overlay chrome focus ownership (#699.4): chrome claims the user's
 * keystroke/paste stream by activating overlay input focus — key chunks
 * then route through the overlay seat. Driven by the pointer-press
 * verdict in the ordered dispatch. */
static void attach_set_overlay_chrome_active(struct attach_bridge *bridge, int active)
{
    if (!bridge->client) {
        return;
    }
    /* Observable transition (harness + operator debugging). */
    fprintf(stderr, "ymux-bridge: chrome focus %s\r\n", active ? "claimed" : "released");
    yetty_ymux_client_set_overlay_input_active(bridge->client, active);
}

static void attach_queue_selection(struct attach_bridge *bridge, uint32_t start_row,
                                   uint32_t start_col, uint32_t end_row, uint32_t end_col,
                                   uint32_t active)
{
    bridge->selection_pending_span[0] = start_row;
    bridge->selection_pending_span[1] = start_col;
    bridge->selection_pending_span[2] = end_row;
    bridge->selection_pending_span[3] = end_col;
    bridge->selection_pending_active = active;
    bridge->selection_pending = 1; /* latest wins — drag chatter coalesces */
}

static void attach_flush_selection(struct attach_bridge *bridge)
{
    if (!bridge->selection_pending || !bridge->scene_proxy) {
        return;
    }
    bridge->selection_pending = 0;
    struct yetty_ycore_void_result selection_res = yetty_yscene_set_terminal_selection(
        bridge->scene_proxy, bridge->selection_pending_span[0], bridge->selection_pending_span[1],
        bridge->selection_pending_span[2], bridge->selection_pending_span[3],
        bridge->selection_pending_active);
    if (YETTY_IS_ERR(selection_res)) {
        yetty_ycore_error_destroy(selection_res.error);
    }
}

/* Encode ONE figure-key envelope to terminal bytes with DOWN/CHAR correlation
 * (cycle-24 P1): a DOWN that folds a Ctrl chord to its control byte arms
 * pending_ctrl_fold so the matching CHAR — a duplicate of that byte — is
 * suppressed; a CHAR whose DOWN did NOT fold (layout / composed input) is
 * encoded and kept. Shared by both figure-key paths so they can never diverge.
 * Returns the encoded length (0 = nothing to emit for this envelope). */
static size_t attach_encode_figure_key(struct attach_bridge *bridge,
                                       const struct yetty_client_input_key *key_msg, uint8_t *out,
                                       size_t out_cap)
{
    int encode_kind = yetty_ymux_key_encode_kind_from_wire((uint32_t)key_msg->kind);
    /* Run the correlation state machine for EVERY event (UP/unknown included) so
     * the pending fold identity tracks the stream correctly; a matched CHAR is
     * suppressed. UP / unknown kinds emit nothing (encode_kind < 0). */
    uint32_t correlate_kind =
        encode_kind < 0 ? (uint32_t)YETTY_YMUX_KEY_ENCODE_UP : (uint32_t)encode_kind;
    int suppress = yetty_ymux_key_correlate(&bridge->pending_ctrl_control, correlate_kind,
                                            (uint32_t)key_msg->key, key_msg->codepoint,
                                            (uint32_t)key_msg->mods);
    if (encode_kind < 0 || suppress) {
        return 0;
    }
    return yetty_ymux_key_encode((uint32_t)encode_kind, (uint32_t)key_msg->key, key_msg->codepoint,
                                 (uint32_t)key_msg->mods, out, out_cap);
}

/* PRODUCTION overlay input handler (#699.4, review #12): consumed pointer
 * events decode into the client's ring here (inside the OSC pump) and the
 * bridge loop DISPATCHES them to the overlay scene via RPC — a value call
 * must never run re-entrant inside the pump. Key/paste deliveries stay on
 * the accounting counters until chrome consumes them. */
/* Raw-overflow sink (defined below): the figure-key ingress routes a failed
 * ordered-queue push here so the keystroke lands in pending_keys, never dropped. */
static void attach_on_raw(void *userdata, const char *bytes, size_t count);

/* Pointer ingress (defined below): push to the ordered queue, stash on a full
 * queue for loop-driven retry (backpressure), detach on a true stash OOM. Once
 * an event is accepted it is never silently dropped. */
static void attach_pointer_enqueue(struct attach_bridge *bridge, float x, float y, uint32_t kind,
                                   uint32_t button, uint32_t mods, uint32_t pressed);

static void attach_overlay_input_handler(uint32_t input_class, const uint8_t *bytes, size_t len,
                                         void *userdata)
{
    struct attach_bridge *bridge = userdata;
    if (!bridge->client) {
        return;
    }
    if (input_class == YMUX_INPUT_CLASS_KEY && len >= sizeof(struct yetty_client_input_key)) {
        /* FIGURE-KEY envelope: while a figure holds yetty's key focus the
         * keystroke arrives ONLY here — yetty consumes it, the raw PTY
         * channel never carries it (review #17: arrows in chrome mode
         * vanished). Translate to terminal bytes and push as an ordered
         * RAW run — it then takes the normal classifier -> overlay ->
         * daemon path. */
        const struct yetty_client_input_key *key_msg = (const struct yetty_client_input_key *)bytes;
        if (key_msg->magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
            return;
        }
        /* DOWN encodes, CHAR encodes text (suppressed only when it duplicates a
         * folded DOWN — cycle-24 correlation), UP / unknown emit NOTHING —
         * collapsing UP to DOWN would double every keystroke (cycle-22 P0). */
        uint8_t run[YETTY_YMUX_KEY_ENCODE_MAX];
        uint32_t run_len = (uint32_t)attach_encode_figure_key(bridge, key_msg, run, sizeof(run));
        if (run_len > 0) {
            int yetty_ymux_client_ordered_push_raw(struct yetty_yclass_object * queue_obj,
                                                   const uint8_t *queue_bytes, uint32_t queue_len);
            if (!yetty_ymux_client_ordered_push_raw(bridge->client, run, run_len)) {
                /* Ordered ring full/OOM: route the encoded keystroke to the raw
                 * OVERFLOW buffer (attach_on_raw → pending_keys) instead of
                 * dropping it — the same lossless fallback the raw channel uses,
                 * processed in order after the queue drains (cycle-26). */
                attach_on_raw(bridge, (const char *)run, run_len);
            }
        }
        return;
    }
    if (input_class != YMUX_INPUT_CLASS_POINTER || len < sizeof(struct yetty_client_input_mouse)) {
        return;
    }
    const struct yetty_client_input_mouse *mouse = (const struct yetty_client_input_mouse *)bytes;
    if (mouse->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
        return;
    }
    attach_pointer_enqueue(bridge, mouse->x, mouse->y, (uint32_t)mouse->kind,
                           (uint32_t)mouse->button, (uint32_t)mouse->mods,
                           mouse->pressed ? 1u : 0u);
}

/* Retry pointer events the ordered queue could not take earlier. Stops at the
 * first re-push that still fails (backpressure) — the survivors stay queued. */
/* Enqueue a pointer event without ever dropping it once accepted: push to the
 * ordered queue, and on a full queue STASH it for loop-driven retry — that is
 * the backpressure (attach_flush_pending_pointers drains as the queue frees).
 * Only a genuine allocation failure growing the stash is unrecoverable; there
 * we DETACH explicitly rather than continue after losing an accepted event
 * (silently dropping input, or letting focus latch on a lost release press, is
 * not tmux semantics). */
static void attach_pointer_enqueue(struct attach_bridge *bridge, float x, float y, uint32_t kind,
                                   uint32_t button, uint32_t mods, uint32_t pressed)
{
    int yetty_ymux_client_ordered_push_pointer(
        struct yetty_yclass_object * queue_obj, float queue_x, float queue_y, uint32_t queue_kind,
        uint32_t queue_button, uint32_t queue_mods, uint32_t queue_pressed);
    if (!bridge->client) {
        return;
    }
    if (yetty_ymux_client_ordered_push_pointer(bridge->client, x, y, kind, button, mods, pressed)) {
        return;
    }
    if (bridge->pending_pointers_len == bridge->pending_pointers_cap) {
        size_t grown_cap = bridge->pending_pointers_cap ? bridge->pending_pointers_cap * 2 : 64;
        struct attach_pending_pointer *grown =
            realloc(bridge->pending_pointers, grown_cap * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "ymux-bridge: pointer backpressure OOM — detaching\r\n");
            bridge->detach_requested = 1;
            return;
        }
        bridge->pending_pointers = grown;
        bridge->pending_pointers_cap = grown_cap;
    }
    struct attach_pending_pointer *slot = &bridge->pending_pointers[bridge->pending_pointers_len++];
    slot->x = x;
    slot->y = y;
    slot->kind = kind;
    slot->button = button;
    slot->mods = mods;
    slot->pressed = pressed;
}

static void attach_flush_pending_pointers(struct attach_bridge *bridge)
{
    int yetty_ymux_client_ordered_push_pointer(
        struct yetty_yclass_object * queue_obj, float queue_x, float queue_y, uint32_t queue_kind,
        uint32_t queue_button, uint32_t queue_mods, uint32_t queue_pressed);
    size_t flushed = 0;
    while (flushed < bridge->pending_pointers_len) {
        struct attach_pending_pointer *slot = &bridge->pending_pointers[flushed];
        if (!yetty_ymux_client_ordered_push_pointer(bridge->client, slot->x, slot->y, slot->kind,
                                                    slot->button, slot->mods, slot->pressed)) {
            break;
        }
        ++flushed;
    }
    if (flushed > 0) {
        memmove(bridge->pending_pointers, bridge->pending_pointers + flushed,
                (bridge->pending_pointers_len - flushed) * sizeof(*bridge->pending_pointers));
        bridge->pending_pointers_len -= flushed;
    }
}

/* Emit a CLIENT_INPUT_SUB envelope (full desired subscription state) onto the
 * RAW channel toward the hosting yetty. The KEY_FANOUT bit is the figure-KEY
 * opt-in: the host consumes keystrokes for a click-focused figure and
 * delivers them as CLIENT_INPUT_FIGURE_KEY envelopes. Purely envelope-driven
 * — no DEC mode, no terminal-emulation involvement (review #21: yvterm must
 * stay untouched). */
static void attach_emit_input_sub(struct yetty_ywire_channel *raw_channel, uint32_t flags)
{
    struct yetty_client_input_sub sub = {
        .magic = YETTY_CLIENT_INPUT_SUB_MAGIC,
        .version = YMGUI_WIRE_VERSION,
        .flags = flags,
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res = yetty_yface_emit(
        YETTY_OSC_CS_CLIENT_INPUT_SUB, /*compressed=*/0, NULL, 0, &sub, sizeof(sub), &envelope);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_error_destroy(emit_res.error);
        yetty_ycore_buffer_destroy(&envelope);
        return;
    }
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(raw_channel, envelope.data, envelope.size);
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
    }
    yetty_ycore_buffer_destroy(&envelope);
}

/* yface raw handler: bytes on the RAW channel that are NOT a structured-input
 * OSC envelope — the user's verbatim keystrokes. Buffer them; attach_flush_keys
 * feeds the session from the loop (never re-enter the session from the pump). */
static void attach_on_raw(void *userdata, const char *bytes, size_t count)
{
    struct attach_bridge *bridge = userdata;
    if (!bytes || count == 0) {
        return;
    }
    /* ORDERED queue first (review #16): raw chunks and pointer events keep
     * wire arrival order. pending_keys is the overflow path only — those
     * bytes process AFTER the queue, so order still holds. */
    int yetty_ymux_client_ordered_push_raw(struct yetty_yclass_object * queue_obj,
                                           const uint8_t *queue_bytes, uint32_t queue_len);
    if (bridge->client && yetty_ymux_client_ordered_push_raw(bridge->client, (const uint8_t *)bytes,
                                                             (uint32_t)count)) {
        return;
    }
    size_t needed = bridge->pending_keys_len + count;
    if (needed > bridge->pending_keys_cap) {
        size_t new_cap = bridge->pending_keys_cap ? bridge->pending_keys_cap * 2 : 512;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(bridge->pending_keys, new_cap);
        if (!grown) {
            /* Out of memory retaining accepted keystrokes: detach explicitly
             * rather than silently lose this run (dropping input mid-stream is
             * not tmux semantics). */
            fprintf(stderr, "ymux-bridge: raw-input backpressure OOM — detaching\r\n");
            bridge->detach_requested = 1;
            return;
        }
        bridge->pending_keys = grown;
        bridge->pending_keys_cap = new_cap;
    }
    memcpy(bridge->pending_keys + bridge->pending_keys_len, bytes, count);
    bridge->pending_keys_len = needed;
}

/* yface OSC handler: structured input yetty forwards once we enable the DEC
 * ?1500/?1501 card-mouse modes — the pane's pixel-precise mouse position/button/
 * wheel, delivered as a YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE envelope carrying
 * a yetty_client_input_mouse (pane-local pixels). Convert to cells and forward
 * to the daemon (permission-gated; the engine re-encodes per the app's real
 * mouse mode, so events the app didn't ask for are dropped there). */
static void attach_on_osc(void *userdata, int wire_code, const uint8_t *args, size_t args_len,
                          const uint8_t *payload, size_t payload_len)
{
    struct attach_bridge *bridge = userdata;
    (void)args;
    (void)args_len;
    if (!bridge->client) {
        return;
    }
    if (wire_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE &&
        wire_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY) {
        return;
    }
    /* Which figure was hit? The input structs lead with magic/version/figure_id;
     * mouse and key share the figure_id slot. */
    uint32_t figure_id = 0;
    if (wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE) {
        if (payload_len < sizeof(struct yetty_client_input_mouse)) {
            return;
        }
        const struct yetty_client_input_mouse *check =
            (const struct yetty_client_input_mouse *)payload;
        if (check->magic != YETTY_CLIENT_INPUT_MOUSE_MAGIC) {
            return;
        }
        figure_id = check->figure_id;
    } else {
        if (payload_len < sizeof(struct yetty_client_input_key)) {
            return;
        }
        const struct yetty_client_input_key *check = (const struct yetty_client_input_key *)payload;
        if (check->magic != YETTY_CLIENT_INPUT_KEY_MAGIC) {
            return;
        }
        figure_id = check->figure_id;
    }
    /* Input for a PROXIED figure (an in-pane ygui/ygreeter, not our own terminal
     * scene grid): relay it verbatim to the pane app, whose ygui framework
     * consumes it (the daemon re-emits it on the app's PTY). */
    if (figure_id != ATTACH_SCENE_CHILD_ID && figure_id != ATTACH_OVERLAY_CHILD_ID) {
        struct yetty_ycore_void_result relay_res = yetty_ymux_client_figure_input(
            bridge->client, (uint32_t)wire_code, payload, payload_len);
        if (YETTY_IS_ERR(relay_res)) {
            yetty_ycore_error_destroy(relay_res.error);
        }
        return;
    }
    /* Else: our own scenes — OVERLAY-FIRST dispatch with a consumed result
     * (#699.4, review #11). A pointer hit that resolved to the overlay figure
     * means opaque chrome at that point (its hit_opaque yields everywhere
     * else): the overlay CONSUMES it — delivered to the overlay seat, never
     * to the terminal. Key events on the figure channel likewise go to the
     * overlay when it holds input focus; unconsumed figure-key events stay
     * dropped because the PTY raw channel already carries the keystrokes
     * (forwarding both would double-input the application). */
    uint32_t input_class = wire_code == YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE
                               ? YMUX_INPUT_CLASS_POINTER
                               : YMUX_INPUT_CLASS_KEY;
    if (figure_id == ATTACH_OVERLAY_CHILD_ID &&
        yetty_ymux_client_route_overlay_input(bridge->client, input_class, figure_id,
                                              ATTACH_OVERLAY_CHILD_ID)) {
        yetty_ymux_client_overlay_input_deliver(bridge->client, input_class, payload, payload_len);
        return;
    }
    if (wire_code != YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE) {
        /* UNCONSUMED figure-KEY envelope (chrome inactive): with the ?1503
         * fan-out the host CONSUMED this keystroke — it is NOT also on the
         * raw channel. Translate it back into raw input for the daemon
         * (review: dropping it made post-release typing dead whenever the
         * content figure held host click-focus). */
        const struct yetty_client_input_key *key_msg =
            (const struct yetty_client_input_key *)payload;
        /* Same DOWN/CHAR correlation as the consumed path (cycle-24 P1): UP /
         * unknown emit nothing; a CHAR is kept unless it duplicates a folded
         * DOWN. */
        uint8_t encoded[YETTY_YMUX_KEY_ENCODE_MAX];
        size_t encoded_len = attach_encode_figure_key(bridge, key_msg, encoded, sizeof(encoded));
        if (encoded_len > 0 && attach_process_keys(bridge, encoded, encoded_len)) {
            bridge->detach_requested = 1;
        }
        return;
    }
    const struct yetty_client_input_mouse *mouse = (const struct yetty_client_input_mouse *)payload;
    /* Focus RELEASE (review #15): a button press the host routed to the
     * CONTENT child is by definition a chrome MISS — while the overlay
     * holds input focus it must observe that miss, or focus latches and
     * every later keystroke keeps routing to the chrome. Push the press
     * into the overlay ring: the dispatch loop resolves it (hit 0), the
     * scene releases key focus, and the bridge clears routing focus from
     * the same verdict. */
    if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && mouse->pressed &&
        yetty_ymux_client_route_overlay_input(bridge->client, YMUX_INPUT_CLASS_KEY, 0, 0)) {
        /* The focus-release press must reach the overlay ring, or key focus
         * latches on the chrome and every later keystroke misroutes — enqueue
         * through the backpressure path so a full queue retries instead of
         * losing it. */
        attach_pointer_enqueue(bridge, mouse->x, mouse->y, (uint32_t)mouse->kind,
                               (uint32_t)mouse->button, (uint32_t)mouse->mods, 1u);
    }
    uint32_t col = bridge->cell_width > 0.0f ? (uint32_t)(mouse->x / bridge->cell_width) : 0u;
    uint32_t row = bridge->cell_height > 0.0f ? (uint32_t)(mouse->y / bridge->cell_height) : 0u;
    /* PRODUCTION selection caller (#699.5, review #12): a left-button drag on
     * the content scene paints the grid's inverted span (queued — the RPC
     * runs from the loop, never inside this pump; here only the anchor/flag
     * state advances and the span is pushed like a pointer event). */
    /* Selection OWNERSHIP (review #13): when the pane app subscribes mouse
     * (PANE_MODES bit0), the drag belongs to the application — no local
     * selection; the events forward as before and the engine routes them. */
    int app_owns_mouse = bridge->client && (yetty_ymux_client_pane_modes(bridge->client) & 1u) != 0;
    /* Ownership TRANSITION during a drag (review #14): the moment the app
     * subscribes mouse, the local drag is cancelled and its span cleared —
     * a later unsubscribe must not resume a stale selection. The left
     * release always clears the drag flag regardless of ownership. */
    if (app_owns_mouse && bridge->selection_dragging) {
        bridge->selection_dragging = 0;
        attach_queue_selection(bridge, 0, 0, 0, 0, 0);
    }
    if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && mouse->button == 0 && !mouse->pressed) {
        bridge->selection_dragging = 0;
    }
    if (bridge->scene_proxy && !app_owns_mouse) {
        if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && mouse->button == 0) {
            if (mouse->pressed) {
                bridge->selection_dragging = 1;
                bridge->selection_anchor_row = row;
                bridge->selection_anchor_col = col;
                attach_queue_selection(bridge, row, col, row, col, 0); /* click clears */
            }
        } else if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_POS && bridge->selection_dragging) {
            attach_queue_selection(bridge, bridge->selection_anchor_row,
                                   bridge->selection_anchor_col, row, col, 1);
        }
    }
    struct yetty_ycore_void_result res = YETTY_OK_VOID();
    if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_POS) {
        res = yetty_ymux_client_input_mouse_move(bridge->client, row, col, mouse->mods);
    } else if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON) {
        /* Position the engine cursor at the click cell first, then the button
         * transition — libvterm's mouse button uses the last move position. */
        res = yetty_ymux_client_input_mouse_move(bridge->client, row, col, mouse->mods);
        if (YETTY_IS_ERR(res)) {
            yetty_ycore_error_destroy(res.error);
            res = YETTY_OK_VOID();
        }
        /* yetty buttons 0=left,1=right,2=middle -> libvterm 1=left,2=mid,3=right. */
        int vt_button = mouse->button == 0 ? 1 : (mouse->button == 1 ? 3 : 2);
        res = yetty_ymux_client_input_mouse_button(bridge->client, vt_button, mouse->pressed,
                                                   mouse->mods);
    } else if (mouse->kind == YETTY_YMGUI_INPUT_MOUSE_WHEEL) {
        /* xterm wheel: button 4 = up, 5 = down (a momentary press). */
        int vt_button = mouse->wheel_dy > 0.0f ? 4 : 5;
        res = yetty_ymux_client_input_mouse_move(bridge->client, row, col, mouse->mods);
        if (YETTY_IS_ERR(res)) {
            yetty_ycore_error_destroy(res.error);
            res = YETTY_OK_VOID();
        }
        res = yetty_ymux_client_input_mouse_button(bridge->client, vt_button, 1, mouse->mods);
    }
    if (YETTY_IS_ERR(res)) {
        char chain_buf[512];
        yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), res.error);
        yerror("ymux attach: forward mouse input to session failed: %s", chain_buf);
        yetty_ycore_error_destroy(res.error);
    }
}

/* RAW-channel sink (fires during connection_pump_readable): feed the bytes to
 * the yface, which splits structured-input OSC envelopes (mouse) from verbatim
 * keystrokes and dispatches attach_on_osc / attach_on_raw. */
static void attach_raw_sink(void *userdata, const uint8_t *bytes, size_t count)
{
    struct attach_bridge *bridge = userdata;
    if (!bytes || count == 0 || !bridge->in_face) {
        return;
    }
    struct yetty_ycore_void_result feed_res =
        yetty_yface_feed_bytes(bridge->in_face, (const char *)bytes, count);
    if (YETTY_IS_ERR(feed_res)) {
        yetty_ycore_error_destroy(feed_res.error);
    }
}

/* Response sink on a proxied yetty channel (#695: ygui/ygreeter figure). yetty's
 * reply bytes for this figure go back to the daemon — which writes them to the
 * pane app's channel — via RPC_RELAY. Signature = yetty_ywire_channel_raw_sink. */
YETTY_EXTERNAL_CALLBACK
static void attach_yetty_rpc_response(void *user, const uint8_t *bytes, size_t n)
{
    struct attach_rpc_channel *entry = user;
    if (!entry || !entry->in_use || !bytes || n == 0 || !entry->bridge->client) {
        return;
    }
    struct yetty_ycore_void_result relay_res =
        yetty_ymux_client_rpc_relay(entry->bridge->client, entry->pane_channel_id, bytes, n);
    if (YETTY_IS_ERR(relay_res)) {
        yetty_ycore_error_destroy(relay_res.error);
    }
}

/* Event cb on a proxied upstream channel: on CLOSE, release the slot and — when
 * yetty (not the daemon) initiated it — tell the daemon to drop its pairing.
 * Signature dictated by yetty_ywire_channel_event_cb. */
YETTY_EXTERNAL_CALLBACK
static void attach_yetty_rpc_event(void *user, struct yetty_ywire_channel *channel,
                                   enum yetty_ywire_channel_event event)
{
    (void)channel;
    struct attach_rpc_channel *entry = user;
    if (event != YETTY_YWIRE_CHANNEL_EVENT_CLOSED || !entry || !entry->in_use) {
        return;
    }
    uint32_t pane_channel_id = entry->pane_channel_id;
    int daemon_initiated = entry->closing;
    entry->in_use = 0;
    entry->closing = 0;
    entry->yetty_channel = NULL;
    if (!daemon_initiated && entry->bridge->client) {
        struct yetty_ycore_void_result close_res =
            yetty_ymux_client_rpc_relay_close(entry->bridge->client, pane_channel_id);
        if (YETTY_IS_ERR(close_res)) {
            yetty_ycore_error_destroy(close_res.error);
        }
    }
}

/* Close sink (daemon -> client): the pane app closed a proxied channel. Close
 * our matching upstream channel; the event cb above then frees the slot. */
static void attach_rpc_relay_close(uint32_t channel_id, void *userdata)
{
    struct attach_bridge *bridge = userdata;
    if (!bridge) {
        return;
    }
    for (uint32_t index = 0; index < ATTACH_MAX_RPC_CHANNELS; ++index) {
        struct attach_rpc_channel *entry = &bridge->rpc_channels[index];
        if (!entry->in_use || entry->pane_channel_id != channel_id) {
            continue;
        }
        entry->closing = 1; /* the daemon told us — don't echo the close back */
        if (entry->yetty_channel) {
            struct yetty_ycore_void_result close_res =
                yetty_ywire_channel_close(entry->yetty_channel);
            if (YETTY_IS_ERR(close_res)) {
                yetty_ycore_error_destroy(close_res.error);
                entry->in_use = 0; /* close failed to fire the event — free the slot now */
                entry->yetty_channel = NULL;
            }
        } else {
            entry->in_use = 0;
        }
        break;
    }
}

/* ymux client RPC-relay sink: a proxied pane channel's REQUEST bytes (the pane
 * app's ygui/ygreeter yclass-RPC, tunnelled by the daemon). Pipe them to a
 * FRESH dynamic channel on our own yetty connection — opened once per pane
 * channel; its id can't collide with the tool's scene channel — so yetty serves
 * the RPC and renders the figure. Its responses ride back via the sink above. */
static void attach_rpc_relay(uint32_t channel_id, const uint8_t *bytes, size_t len, void *userdata)
{
    struct attach_bridge *bridge = userdata;
    if (!bridge || !bridge->connection || !bytes || len == 0) {
        return;
    }
    struct attach_rpc_channel *entry = NULL;
    for (uint32_t index = 0; index < ATTACH_MAX_RPC_CHANNELS; ++index) {
        if (bridge->rpc_channels[index].in_use &&
            bridge->rpc_channels[index].pane_channel_id == channel_id) {
            entry = &bridge->rpc_channels[index];
            break;
        }
    }
    if (!entry) {
        for (uint32_t index = 0; index < ATTACH_MAX_RPC_CHANNELS; ++index) {
            if (!bridge->rpc_channels[index].in_use) {
                entry = &bridge->rpc_channels[index];
                break;
            }
        }
        if (!entry) {
            return; /* table full — bounded proxy, drop */
        }
        struct yetty_ywire_channel_ptr_result open_res =
            yetty_ywire_connection_open_channel(bridge->connection, 0);
        if (YETTY_IS_ERR(open_res)) {
            yetty_ycore_error_destroy(open_res.error);
            return;
        }
        entry->in_use = 1;
        entry->closing = 0;
        entry->pane_channel_id = channel_id;
        entry->yetty_channel = open_res.value;
        entry->bridge = bridge;
        struct yetty_ycore_void_result sink_res = yetty_ywire_channel_set_raw_sink(
            entry->yetty_channel, attach_yetty_rpc_response, entry);
        if (YETTY_IS_ERR(sink_res)) {
            yetty_ycore_error_destroy(sink_res.error);
        }
        struct yetty_ycore_void_result event_res =
            yetty_ywire_channel_set_event_cb(entry->yetty_channel, attach_yetty_rpc_event, entry);
        if (YETTY_IS_ERR(event_res)) {
            yetty_ycore_error_destroy(event_res.error);
        }
    }
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(entry->yetty_channel, bytes, len);
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
        return;
    }
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(entry->yetty_channel);
    if (YETTY_IS_ERR(flush_res)) {
        yetty_ycore_error_destroy(flush_res.error);
    }
}

/* Pipelined-call failure sink: in async mode a failed scene call (a
 * terminal_write_content publication, a grid resize, an overlay seat) arrives
 * here out of band instead of at the call site. ANY failure on this session
 * means the receiving scene may have missed stateful content, so the whole
 * stream desyncs: request a resync (which also RESETS the receiver grid) and
 * CANCEL the un-acked coverage so a generation whose publication failed is
 * never reported as applied (#699.6 exact applied-generation contract). */
static void attach_rpc_error_sink(const char *qualified_name, struct yetty_ycore_error *error,
                                  void *userdata)
{
    struct attach_bridge *bridge = userdata;
    if (!bridge->rpc_error_logged) {
        ydebug("ymux bridge: pipelined RPC '%s' failed: %s", qualified_name ? qualified_name : "?",
               error && error->msg ? error->msg : "unknown");
        bridge->rpc_error_logged = 1;
    }
    bridge->vt_desynced = 1;
    bridge->vt_write_covered_generation = bridge->vt_ack_reported_generation;
    if (error) {
        yetty_ycore_error_destroy(*error);
    }
}

/* Send one fully-decoded codepoint to the session, carrying any modifiers the
 * decoder recovered (CSI-u / modifyOtherKeys) so the daemon re-encodes it
 * against the pane's extended-key mode via vterm_keyboard_unichar. */
static void attach_send_codepoint(struct attach_bridge *bridge, uint32_t codepoint, int mods)
{
    struct yetty_ycore_void_result input_res =
        yetty_ymux_client_input_char(bridge->client, codepoint, mods);
    if (YETTY_IS_ERR(input_res)) {
        yetty_ycore_error_destroy(input_res.error);
    }
}

/* Deliver a STRUCTURED special key to the pane (cycle-22 P0). The daemon
 * routes INPUT_KEY through libvterm's vterm_keyboard_key, which encodes it
 * against the pane's CURRENT terminal modes — so an Up arrow becomes SS3
 * `\eOA` under application cursor mode (DECCKM) and CSI `\e[A` otherwise. The
 * bridge cannot do this itself: it does not track the pane's DECCKM. */
static void attach_send_key(struct attach_bridge *bridge, int ymux_key, int mods)
{
    struct yetty_ycore_void_result input_res =
        yetty_ymux_client_input_key(bridge->client, ymux_key, mods);
    if (YETTY_IS_ERR(input_res)) {
        yetty_ycore_error_destroy(input_res.error);
    }
}

/* Is buf[offset..len) exactly a carriable INCOMPLETE escape prefix that reaches
 * the end of the buffer — i.e. it could still become one of the sequences
 * yetty_ymux_tty_key_decode recognizes if more bytes arrive? Returns the prefix
 * length (1..3) to carry, or 0 when it is not a continuation candidate (a lone
 * ESC with a trailing non-`[`/`O` byte, an already-complete/decodable run, or
 * a `~`-key that already has its final byte). */
/* Adapter: map the key-stream's decoded output to the bridge's session sends.
 * DETACH is signalled by the feed() return value, so it is a no-op here. */
static void attach_key_stream_emit(void *userdata, enum yetty_ymux_key_stream_action action,
                                   uint32_t value, unsigned mods)
{
    struct attach_bridge *bridge = userdata;
    switch (action) {
    case YETTY_YMUX_KEY_STREAM_CODEPOINT:
        attach_send_codepoint(bridge, value, (int)mods);
        break;
    case YETTY_YMUX_KEY_STREAM_KEY:
        attach_send_key(bridge, (int)value, (int)mods);
        break;
    case YETTY_YMUX_KEY_STREAM_DETACH:
        break;
    }
}

/* Feed raw keystroke bytes to the session through the persistent key-stream
 * state machine (key-encode.c): UTF-8 reassembly, structured cursor/nav decode,
 * split-escape carry, and the tmux prefix (C-b d detaches, C-b C-b = literal
 * C-b). Arms the escape-timeout deadline whenever a partial sequence is left
 * carried. Returns 1 when the user detached. */
static int attach_process_keys(struct attach_bridge *bridge, const uint8_t *keys, size_t count)
{
    int detach = yetty_ymux_key_stream_feed(&bridge->key_stream, keys, count,
                                            attach_key_stream_emit, bridge);
    /* A stranded carry must flush on its OWN timer (tmux escape-time = 10 ms),
     * not only when the whole poll idles — otherwise continuous peer activity
     * would hold a lone ESC indefinitely (cycle-24 P0). */
    if (bridge->key_stream.esc_carry_len > 0) {
        clock_gettime(CLOCK_MONOTONIC, &bridge->esc_carry_at);
    }
    return detach;
}

/* tmux's default `escape-time` (options-table.c): a lone ESC or an incomplete
 * key prefix is held at most this long for its continuation before being
 * flushed as literal bytes. */
enum { ATTACH_ESCAPE_TIMEOUT_MS = 10 };

/* Flush a stranded escape carry as raw codepoints — the tmux escape-timeout
 * behavior. Called once the carry's independent 10 ms deadline elapses so a
 * lone ESC (or a prefix that never completes) reaches the pane instead of
 * being held forever. */
static void attach_flush_pending_escape(struct attach_bridge *bridge)
{
    yetty_ymux_key_stream_flush_carry(&bridge->key_stream, attach_key_stream_emit, bridge);
}

/* Deliver keystrokes the transport raw sink captured during an RPC round-trip.
 * Returns 1 when the buffered input contained a detach chord. */
struct attach_flush_context {
    struct attach_bridge *bridge;
    int detach;
    int retain_failed; /* a retain append OOM'd despite the reservation (cycle-25) */
};

/* Per-run sink for the client's stateful classifier (review #14): each KEY
 * or PASTE run is offered to the overlay scene, which returns CONSUMED
 * (queued for the chrome consumer) or UNCONSUMED — unconsumed runs fall
 * through to the daemon exactly as if the chrome were absent, including
 * detach-chord processing. */
/* Retain one consumed chrome event in the client-side ownership queue. The
 * event leaves the queue ONLY when the daemon's sequence-bearing ACK covers
 * it (attach_overlay_flush). Returns 1 when the queue took OWNERSHIP of the
 * event; 0 when it could not (hard cap / OOM) — the caller must then keep
 * the event pending (retry slot + drain backpressure), never treat it as
 * delivered (review #21). */
enum { ATTACH_CHROME_BACKLOG_CAP = 1u << 20 };

static int attach_chrome_backlog_push(struct attach_bridge *bridge, uint32_t input_class,
                                      const uint8_t *bytes, size_t len)
{
    size_t needed = bridge->chrome_backlog_len + 8 + len;
    if (needed > ATTACH_CHROME_BACKLOG_CAP) {
        if (bridge->overlay_dropped_events == 0) {
            fprintf(stderr, "ymux-bridge: chrome backlog cap reached — input pending\r\n");
        }
        ++bridge->overlay_dropped_events;
        return 0;
    }
    if (needed > bridge->chrome_backlog_cap) {
        size_t new_cap = bridge->chrome_backlog_cap ? bridge->chrome_backlog_cap * 2 : 1024;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(bridge->chrome_backlog, new_cap);
        if (!grown) {
            if (bridge->overlay_dropped_events == 0) {
                fprintf(stderr, "ymux-bridge: chrome backlog OOM — input pending\r\n");
            }
            ++bridge->overlay_dropped_events;
            return 0;
        }
        bridge->chrome_backlog = grown;
        bridge->chrome_backlog_cap = new_cap;
    }
    uint32_t len_word = (uint32_t)len;
    memcpy(bridge->chrome_backlog + bridge->chrome_backlog_len, &input_class, 4);
    memcpy(bridge->chrome_backlog + bridge->chrome_backlog_len + 4, &len_word, 4);
    if (len > 0 && bytes) {
        memcpy(bridge->chrome_backlog + bridge->chrome_backlog_len + 8, bytes, len);
    }
    bridge->chrome_backlog_len = needed;
    ++bridge->consumed_undrained;
    return 1;
}

/* Append one [class][len][bytes] record to the dynamic retry queue. Grows as
 * needed; returns 0 only on a true realloc OOM (counted + logged). */
static int attach_overlay_retry_append(struct attach_bridge *bridge, uint32_t input_class,
                                       const uint8_t *bytes, size_t len)
{
    size_t needed = bridge->overlay_retry_len + 8 + len;
    if (needed > bridge->overlay_retry_cap) {
        size_t new_cap = bridge->overlay_retry_cap ? bridge->overlay_retry_cap * 2 : 4096;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(bridge->overlay_retry_queue, new_cap);
        if (!grown) {
            if (bridge->overlay_dropped_events == 0) {
                fprintf(stderr, "ymux-bridge: overlay retry OOM — input dropped\r\n");
            }
            ++bridge->overlay_dropped_events;
            return 0;
        }
        bridge->overlay_retry_queue = grown;
        bridge->overlay_retry_cap = new_cap;
    }
    uint32_t len_word = (uint32_t)len;
    memcpy(bridge->overlay_retry_queue + bridge->overlay_retry_len, &input_class, 4);
    memcpy(bridge->overlay_retry_queue + bridge->overlay_retry_len + 4, &len_word, 4);
    if (len > 0 && bytes) {
        memcpy(bridge->overlay_retry_queue + bridge->overlay_retry_len + 8, bytes, len);
    }
    bridge->overlay_retry_len = needed;
    return 1;
}

/* A single overlay wire frame is bounded by the client TX ring (64 KiB) and the
 * per-record backlog framing, so an event larger than this is SPLIT into
 * in-order records rather than sent whole. Kept below the 64 KiB TX cap to
 * leave room for the 8-byte overlay header + protocol framing. Large pastes are
 * chunked below this limit — NOT dropped (cycle-24: ordered paste chunking, not
 * an arbitrary user-input drop cap). */
enum { ATTACH_OVERLAY_EVENT_MAX = 48u * 1024u };

/* Guarantee the retention buffers can hold a chunk of `len` raw bytes BEFORE it
 * is committed (popped) from the ordered source. Grows the retry queue (and,
 * under its 1 MiB cap, the backlog) to the worst-case retention footprint —
 * the data plus an 8-byte record header per size-chunk piece, generously
 * slacked for the handful of class runs a chunk can split into. Returns 0 on a
 * true realloc OOM: the caller then leaves the event at the queue head and
 * applies backpressure, so nothing is consumed-then-dropped (cycle-24 P0). */
/* Exact retention footprint of ONE classified run: its data plus an 8-byte
 * [class][len] header per <=ATTACH_OVERLAY_EVENT_MAX chunk piece — matching
 * attach_overlay_retain_or_hold's chunking exactly, so a measure pass over the
 * classifier's runs yields the EXACT bytes the retain pass will append (no
 * run-count estimate — cycle-25 P0). */
static size_t attach_overlay_record_bytes(size_t len)
{
    size_t records = len ? (len + ATTACH_OVERLAY_EVENT_MAX - 1) / ATTACH_OVERLAY_EVENT_MAX : 1;
    return len + 8 * records;
}

/* Measure pass: sum the EXACT retention footprint of every classified run the
 * classifier emits from a peeked chunk (matching the retain pass's chunking),
 * so the reservation is exact for the record set the classifier ACTUALLY
 * produces — including an adversarial chunk of many alternating KEY/PASTE runs
 * (cycle-25 P0: no fixed run-count estimate). */
struct attach_overlay_measure_ctx {
    size_t total;
};

static void attach_overlay_measure_run(uint32_t input_class, const uint8_t *bytes, size_t len,
                                       void *userdata)
{
    (void)input_class;
    (void)bytes;
    struct attach_overlay_measure_ctx *measure = userdata;
    measure->total += attach_overlay_record_bytes(len);
}

/* Grow the retry queue (and, under its 1 MiB cap, the backlog) so that `extra`
 * more retention bytes are guaranteed to fit BEFORE the source is committed.
 * Returns 0 on a true realloc OOM — the caller then leaves the event at the
 * queue head and applies backpressure (cycle-24/25 P0). `extra` is the EXACT
 * footprint computed by the measure pass, not an estimate. */
static int attach_overlay_reserve(struct attach_bridge *bridge, size_t extra)
{
    size_t backlog_need = bridge->chrome_backlog_len + extra;
    if (backlog_need > bridge->chrome_backlog_cap && backlog_need <= ATTACH_CHROME_BACKLOG_CAP) {
        size_t new_cap = bridge->chrome_backlog_cap ? bridge->chrome_backlog_cap : 1024;
        while (new_cap < backlog_need) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(bridge->chrome_backlog, new_cap);
        if (!grown) {
            return 0;
        }
        bridge->chrome_backlog = grown;
        bridge->chrome_backlog_cap = new_cap;
    }
    size_t retry_need = bridge->overlay_retry_len + extra;
    if (retry_need > bridge->overlay_retry_cap) {
        size_t new_cap = bridge->overlay_retry_cap ? bridge->overlay_retry_cap : 4096;
        while (new_cap < retry_need) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(bridge->overlay_retry_queue, new_cap);
        if (!grown) {
            return 0;
        }
        bridge->overlay_retry_queue = grown;
        bridge->overlay_retry_cap = new_cap;
    }
    return 1;
}

/* Retain a consumed event: own it immediately in the backlog when it fits and
 * no retry records precede it (returns 1 = owned, caller delivers now), else
 * CHUNK it into <= ATTACH_OVERLAY_EVENT_MAX records appended to the dynamic
 * retry queue in wire order (returns 0 = held; the retry drain moves them to the
 * backlog and delivers each). EVERY append result is checked; a failure sets
 * `*out_retained = 0` so the caller can refuse to commit the source (cycle-25:
 * no silently-ignored append). With the exact measure-pass reservation the
 * appends cannot fail — this is a defensive backstop, not the normal path. */
static int attach_overlay_retain_or_hold(struct attach_bridge *bridge, uint32_t input_class,
                                         const uint8_t *bytes, size_t len, int *out_failed)
{
    if (len <= ATTACH_OVERLAY_EVENT_MAX && bridge->overlay_retry_len == 0 &&
        attach_chrome_backlog_push(bridge, input_class, bytes, len)) {
        return 1;
    }
    size_t offset = 0;
    do {
        size_t piece = len - offset;
        if (piece > ATTACH_OVERLAY_EVENT_MAX) {
            piece = ATTACH_OVERLAY_EVENT_MAX;
        }
        if (!attach_overlay_retry_append(bridge, input_class, bytes + offset, piece) &&
            out_failed) {
            *out_failed = 1; /* append OOM despite reservation — surface it (set to 1) */
        }
        offset += piece;
    } while (offset < len);
    return 0;
}

/* Drop the ACKed head record from the ownership queue. */
static void attach_chrome_backlog_pop_head(struct attach_bridge *bridge)
{
    if (bridge->chrome_backlog_len < 8) {
        bridge->chrome_backlog_len = 0;
        return;
    }
    uint32_t event_len = 0;
    memcpy(&event_len, bridge->chrome_backlog + 4, 4);
    size_t record = 8 + (size_t)event_len;
    if (record > bridge->chrome_backlog_len) {
        bridge->chrome_backlog_len = 0; /* truncated — should not happen */
        return;
    }
    memmove(bridge->chrome_backlog, bridge->chrome_backlog + record,
            bridge->chrome_backlog_len - record);
    bridge->chrome_backlog_len -= record;
    if (bridge->consumed_undrained > 0) {
        --bridge->consumed_undrained;
    }
}

/* The overlay ownership pump (review #19): ONE retained event in flight.
 * The head of chrome_backlog is sent with a fresh sequence; it pops only
 * when yetty_ymux_client_overlay_input_acked() covers that sequence. No ACK
 * within the resend window (refusal, dropped frame) resends the SAME bytes
 * with the SAME sequence — the daemon deduplicates by sequence, so a
 * SAME-CONNECTION replay is safe and order never inverts.
 *
 * SCOPE (cycle-23): this ownership guarantee is CONNECTION-SCOPED. Reconnect
 * is a fresh process (attach_takeover exits on disconnect; attach_cleanup
 * frees this backlog), so an event consumed locally but never ACKed by the
 * daemon is NOT replayed across the reconnect — there is no cross-process
 * persistence, by design (a fresh attachment starts a fresh sequence at 1 and
 * the daemon's per-connection watermark starts at 0). Lossless replay holds
 * only within one connection's lifetime. */
enum { ATTACH_OVERLAY_RESEND_MS = 500 };

static void attach_overlay_flush(struct attach_bridge *bridge)
{
    if (!bridge->client) {
        return;
    }
    struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(
        struct yetty_yclass_object * send_obj, uint32_t send_seq, uint32_t send_class,
        const uint8_t *send_bytes, uint32_t send_len);
    struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_acked(
        struct yetty_yclass_object * acked_obj);

    if (bridge->overlay_seq_inflight != 0) {
        struct yetty_ycore_uint32_result acked_res =
            yetty_ymux_client_overlay_input_acked(bridge->client);
        uint32_t acked = YETTY_IS_OK(acked_res) ? acked_res.value : 0;
        if (YETTY_IS_ERR(acked_res)) {
            yetty_ycore_error_destroy(acked_res.error);
        }
        if (acked >= bridge->overlay_seq_inflight) {
            /* Ownership transferred — NOW the event leaves the queue. */
            attach_chrome_backlog_pop_head(bridge);
            bridge->overlay_seq_inflight = 0;
        } else {
            struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_nacked(
                struct yetty_yclass_object * nacked_obj);
            struct yetty_ycore_uint32_result nacked_res =
                yetty_ymux_client_overlay_input_nacked(bridge->client);
            /* One immediate retry per observed NACK; a daemon that keeps
             * refusing falls back to the timed window (backoff). */
            int nacked_now = YETTY_IS_OK(nacked_res) &&
                             nacked_res.value == bridge->overlay_seq_inflight &&
                             nacked_res.value != bridge->overlay_nack_handled_seq;
            if (nacked_now) {
                bridge->overlay_nack_handled_seq = nacked_res.value;
            }
            if (YETTY_IS_ERR(nacked_res)) {
                yetty_ycore_error_destroy(nacked_res.error);
            }
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_ms = (now.tv_sec - bridge->overlay_sent_at.tv_sec) * 1000 +
                              (now.tv_nsec - bridge->overlay_sent_at.tv_nsec) / 1000000;
            /* A sequence-bearing NACK short-circuits the resend window:
             * the refusal is EXPLICIT, retry now (same sequence). */
            if ((nacked_now || elapsed_ms >= ATTACH_OVERLAY_RESEND_MS) &&
                bridge->chrome_backlog_len >= 8) {
                uint32_t event_class = 0, event_len = 0;
                memcpy(&event_class, bridge->chrome_backlog, 4);
                memcpy(&event_len, bridge->chrome_backlog + 4, 4);
                struct yetty_ycore_void_result resend_res = yetty_ymux_client_send_overlay_input(
                    bridge->client, bridge->overlay_seq_inflight, event_class,
                    bridge->chrome_backlog + 8, event_len);
                if (YETTY_IS_ERR(resend_res)) {
                    yetty_ycore_error_destroy(resend_res.error);
                }
                bridge->overlay_sent_at = now;
            }
            return; /* strictly one in flight — later events queue behind */
        }
    }
    if (bridge->chrome_backlog_len < 8) {
        return;
    }
    uint32_t event_class = 0, event_len = 0;
    memcpy(&event_class, bridge->chrome_backlog, 4);
    memcpy(&event_len, bridge->chrome_backlog + 4, 4);
    if (8 + (size_t)event_len > bridge->chrome_backlog_len) {
        bridge->chrome_backlog_len = 0; /* truncated — should not happen */
        return;
    }
    uint32_t sequence = ++bridge->overlay_seq_next;
    struct yetty_ycore_void_result send_res = yetty_ymux_client_send_overlay_input(
        bridge->client, sequence, event_class, bridge->chrome_backlog + 8, event_len);
    if (YETTY_IS_ERR(send_res)) {
        yetty_ycore_error_destroy(send_res.error);
        --bridge->overlay_seq_next;
        return; /* transport congested — the event stays owned, retry next pass */
    }
    bridge->overlay_seq_inflight = sequence;
    clock_gettime(CLOCK_MONOTONIC, &bridge->overlay_sent_at);
}

static void attach_flush_run(uint32_t input_class, const uint8_t *bytes, size_t len, void *userdata)
{
    struct attach_flush_context *context = userdata;
    struct attach_bridge *bridge = context->bridge;
    /* The chunk router already gated on ACTIVE chrome focus — the run is
     * chrome-consumed by definition (the scene's overlay_key_focus is the
     * same verdict, set by the pointer-press dispatch). ZERO value RPC in
     * this hot path (review #17): a value round-trip needs an idle RPC
     * window a live feed rarely offers. The scene still records its key
     * intake via the PIPELINED void note. */
    if (!bridge->overlay_proxy) {
        context->detach |= attach_process_keys(bridge, bytes, len);
        return;
    }
    /* RETAIN FIRST (review #19/#21): the event must enter the ownership
     * queue BEFORE the scene/accounting see it — a push refusal keeps it
     * in the retry slot with the drain stopped, so nothing downstream ever
     * observes an event the queue does not own. `retain_failed` surfaces an
     * append OOM (impossible after the exact reservation, but never ignored). */
    if (!attach_overlay_retain_or_hold(bridge, input_class, bytes, len, &context->retain_failed)) {
        return;
    }
    struct yetty_ycore_buffer run = {.data = (uint8_t *)bytes, .size = len, .capacity = len};
    struct yetty_ycore_void_result note_res =
        yetty_yscene_note_key_intake(bridge->overlay_proxy, input_class, run);
    if (YETTY_IS_ERR(note_res)) {
        yetty_ycore_error_destroy(note_res.error);
    }
    yetty_ymux_client_overlay_input_deliver(bridge->client, input_class, bytes, len);
    attach_overlay_flush(bridge);
}

/* Process ONE raw keystroke chunk: while the overlay chrome is active the
 * chunk routes through the client's stateful classifier and each run is
 * offered to the overlay scene (consumed -> chrome, unconsumed -> daemon);
 * otherwise the whole chunk goes to the daemon. Returns the detach flag. */
static int attach_process_key_chunk(struct attach_bridge *bridge, const uint8_t *bytes,
                                    size_t count)
{
    if (count == 0) {
        return 0;
    }
    if (yetty_ymux_client_route_overlay_input(bridge->client, YMUX_INPUT_CLASS_KEY, 0, 0)) {
        struct attach_flush_context context = {.bridge = bridge, .detach = 0, .retain_failed = 0};
        yetty_ymux_client_overlay_classify_input(bridge->client, bytes, count, attach_flush_run,
                                                 &context);
        if (context.retain_failed && bridge->overlay_dropped_events == 0) {
            /* Unreachable after the exact measure-pass reservation — surfaced
             * (never silent) so a reservation-accounting regression is visible. */
            fprintf(stderr, "ymux-bridge: overlay retain OOM after reservation\r\n");
            ++bridge->overlay_dropped_events;
        }
        return context.detach;
    }
    return attach_process_keys(bridge, bytes, count);
}

/* Overflow path only (the ordered queue was full): pending_keys holds raw
 * bytes appended AFTER every queued entry, so processing them after the
 * queue preserves order. */
static int attach_flush_keys(struct attach_bridge *bridge)
{
    if (bridge->pending_keys_len == 0) {
        return 0;
    }
    size_t count = bridge->pending_keys_len;
    bridge->pending_keys_len = 0;
    /* SNAPSHOT: the per-run dispatch pumps the transport — new raw bytes
     * can land in pending_keys while the classifier reads. */
    uint8_t *snapshot = malloc(count);
    if (!snapshot) {
        /* OOM: do NOT bypass the overlay classifier by feeding the daemon
         * directly (cycle-26) — that would route chrome-consumed keys to the
         * pane. Restore the buffer and retry next tick (backpressure, no loss). */
        bridge->pending_keys_len = count;
        return 0;
    }
    memcpy(snapshot, bridge->pending_keys, count);
    int detach = attach_process_key_chunk(bridge, snapshot, count);
    free(snapshot);
    return detach;
}

/* Drain the ORDERED input queue (review #16): raw chunks and overlay
 * pointer events dispatch in WIRE ARRIVAL order, so a click and a key from
 * one pump cannot see each other's stale focus state. Returns detach. */
static int attach_process_ordered_input(struct attach_bridge *bridge)
{
    int yetty_ymux_client_ordered_head_kind(struct yetty_yclass_object * queue_obj);
    int yetty_ymux_client_ordered_pop_raw(struct yetty_yclass_object * queue_obj,
                                          uint8_t *out_bytes, uint32_t out_capacity,
                                          uint32_t *out_len);
    int yetty_ymux_client_ordered_peek_raw(struct yetty_yclass_object * queue_obj,
                                           uint8_t *out_bytes, uint32_t out_capacity,
                                           uint32_t *out_len);
    int yetty_ymux_client_ordered_drop_head(struct yetty_yclass_object * queue_obj);
    int yetty_ymux_client_ordered_pop_pointer(
        struct yetty_yclass_object * queue_obj, float *out_x, float *out_y, uint32_t *out_kind,
        uint32_t *out_button, uint32_t *out_mods, uint32_t *out_pressed);
    int detach = 0;
    for (;;) {
        /* Backpressure (cycle-22 P0): while the dynamic retry queue is
         * non-empty the drain retries its FIFO HEAD into the ownership
         * backlog before popping any new input — so held events flow in wire
         * arrival order and nothing new overtakes them. If the head still
         * cannot be retained (backlog at cap), pop NOTHING this pass. */
        if (bridge->overlay_retry_len > 0) {
            uint32_t head_class = 0;
            uint32_t head_len = 0;
            memcpy(&head_class, bridge->overlay_retry_queue, 4);
            memcpy(&head_len, bridge->overlay_retry_queue + 4, 4);
            const uint8_t *head_bytes = bridge->overlay_retry_queue + 8;
            if (!attach_chrome_backlog_push(bridge, head_class, head_bytes, head_len)) {
                break;
            }
            if (head_class != YMUX_INPUT_CLASS_POINTER && bridge->overlay_proxy) {
                struct yetty_ycore_buffer retry_run = {
                    .data = (uint8_t *)head_bytes, .size = head_len, .capacity = head_len};
                struct yetty_ycore_void_result note_res =
                    yetty_yscene_note_key_intake(bridge->overlay_proxy, head_class, retry_run);
                if (YETTY_IS_ERR(note_res)) {
                    yetty_ycore_error_destroy(note_res.error);
                }
                yetty_ymux_client_overlay_input_deliver(bridge->client, head_class, head_bytes,
                                                        head_len);
            }
            /* Pop the retired head record; shift the remainder down. */
            size_t record = 8 + head_len;
            memmove(bridge->overlay_retry_queue, bridge->overlay_retry_queue + record,
                    bridge->overlay_retry_len - record);
            bridge->overlay_retry_len -= record;
            attach_overlay_flush(bridge);
            continue; /* retire the whole retry queue before new input */
        }
        int kind = yetty_ymux_client_ordered_head_kind(bridge->client);
        if (kind == 0) {
            break;
        }
        if (kind == 2) {
            /* Reserve the pointer's fixed retention footprint BEFORE consuming
             * it, so a consumed pointer can never be popped-then-dropped
             * (cycle-25 P0: pointer heads had no pre-reservation). If capacity
             * cannot be guaranteed, leave the pointer at the head — backpressure. */
            if (bridge->overlay_proxy &&
                !attach_overlay_reserve(bridge,
                                        attach_overlay_record_bytes(6u * sizeof(uint32_t)))) {
                break;
            }
            float local_x, local_y;
            uint32_t pointer_kind, button, mods, pressed;
            if (!yetty_ymux_client_ordered_pop_pointer(bridge->client, &local_x, &local_y,
                                                       &pointer_kind, &button, &mods, &pressed)) {
                break;
            }
            if (!bridge->overlay_proxy) {
                continue;
            }
            attach_drain_rpc(bridge);
            struct yetty_ycore_uint64_result dispatch_res = yetty_yscene_dispatch_pointer(
                bridge->overlay_proxy, (uint32_t)local_x, (uint32_t)local_y, pointer_kind, button,
                mods, pressed);
            if (YETTY_IS_ERR(dispatch_res)) {
                yetty_ycore_error_destroy(dispatch_res.error);
                continue;
            }
            if (pointer_kind == YETTY_YMGUI_INPUT_MOUSE_BUTTON && pressed) {
                attach_set_overlay_chrome_active(bridge, dispatch_res.value != 0 ? 1 : 0);
            }
            if (dispatch_res.value != 0) {
                /* Consumed pointer: forward to the daemon seat DIRECTLY
                 * (packed words — the seat retains class+payload), then
                 * pop the scene's queued copy with a PIPELINED void call
                 * (no value-window needed; leak fix, review #17). */
                uint32_t pointer_words[6];
                pointer_words[0] = pointer_kind;
                pointer_words[1] = button;
                pointer_words[2] = mods;
                pointer_words[3] = pressed;
                memcpy(&pointer_words[4], &local_x, 4);
                memcpy(&pointer_words[5], &local_y, 4);
                /* RETAIN (review #19/#21): the pointer event enters the
                 * ownership queue; attach_overlay_flush sends and pops it
                 * only on the daemon's sequence ACK. Capacity was reserved
                 * before the pop, so the retain cannot fail (out_retained NULL). */
                if (attach_overlay_retain_or_hold(bridge, YMUX_INPUT_CLASS_POINTER,
                                                  (const uint8_t *)pointer_words,
                                                  sizeof(pointer_words), NULL)) {
                    attach_overlay_flush(bridge);
                }
                struct yetty_ycore_void_result yetty_yscene_input_event_pop(
                    struct yetty_yclass_object * pop_obj);
                struct yetty_ycore_void_result pop_res =
                    yetty_yscene_input_event_pop(bridge->overlay_proxy);
                if (YETTY_IS_ERR(pop_res)) {
                    yetty_ycore_error_destroy(pop_res.error);
                }
            }
            continue;
        }
        /* PEEK the raw head WITHOUT consuming it, then guarantee retention
         * capacity, and only COMMIT (drop the head) once retention is assured.
         * A reservation OOM leaves the exact event at the queue head and stops
         * the drain — real backpressure, never a consumed-then-dropped event
         * (cycle-24 P0). The overlay-absent path needs no retention. */
        uint8_t stack_chunk[2048];
        uint32_t stored = 0;
        const uint8_t *chunk = NULL;
        uint8_t *heap_chunk = NULL;
        if (yetty_ymux_client_ordered_peek_raw(bridge->client, stack_chunk, sizeof(stack_chunk),
                                               &stored)) {
            chunk = stack_chunk;
        } else if (stored > 0) {
            heap_chunk = malloc(stored);
            if (!heap_chunk) {
                break; /* OOM peeking — leave the source, retry next pass */
            }
            if (!yetty_ymux_client_ordered_peek_raw(bridge->client, heap_chunk, stored, &stored)) {
                free(heap_chunk);
                break;
            }
            chunk = heap_chunk;
        } else {
            yetty_ymux_client_ordered_drop_head(bridge->client); /* zero-length head */
            continue;
        }
        if (bridge->overlay_proxy) {
            /* EXACT reservation via a state-neutral MEASURE pass: snapshot the
             * classifier, classify the peeked chunk to count the precise record
             * bytes, restore the classifier, then reserve exactly that. The real
             * retain pass below reproduces byte-identical runs into the reserved
             * space and cannot OOM. A reservation OOM leaves the source at the
             * head — backpressure, nothing consumed-then-dropped (cycle-25 P0). */
            uint8_t saved_carry[8];
            uint32_t saved_carry_len = 0;
            int saved_paste_open = 0;
            yetty_ymux_client_overlay_classify_save(bridge->client, saved_carry, &saved_carry_len,
                                                    &saved_paste_open);
            struct attach_overlay_measure_ctx measure = {.total = 0};
            yetty_ymux_client_overlay_classify_input(bridge->client, chunk, stored,
                                                     attach_overlay_measure_run, &measure);
            yetty_ymux_client_overlay_classify_restore(bridge->client, saved_carry, saved_carry_len,
                                                       saved_paste_open);
            if (!attach_overlay_reserve(bridge, measure.total)) {
                free(heap_chunk);
                break;
            }
        }
        yetty_ymux_client_ordered_drop_head(bridge->client); /* commit: consume now */
        detach |= attach_process_key_chunk(bridge, chunk, stored);
        free(heap_chunk);
    }
    return detach;
}

/* Synchronously quiesce the RPC pipeline: flush outbound frames and drain every
 * in-flight completion. In async mode a value-returning / admin call is only
 * legal with no completions pending, so this MUST run between a pipelined void
 * mutation (create_child / seat_overlay) and a following value navigation
 * (child_object). Bounded so a stuck peer cannot hang the attach. */
static void attach_drain_rpc(struct attach_bridge *bridge)
{
    int connection_fd = yetty_ywire_connection_fd(bridge->connection);
    for (int spin = 0; spin < 200; ++spin) {
        struct yetty_ycore_size_result write_res =
            yetty_ywire_connection_pump_writable(bridge->connection);
        if (YETTY_IS_ERR(write_res)) {
            yetty_ycore_error_destroy(write_res.error);
        }
        int want_write = yetty_ywire_connection_want_write(bridge->connection);
        struct pollfd poll_fd = {.fd = connection_fd,
                                 .events = (short)(POLLIN | (want_write ? POLLOUT : 0))};
        int poll_result = poll(&poll_fd, 1, 50);
        if (poll_result <= 0 && !want_write) {
            break; /* nothing queued outbound and nothing to read — quiescent */
        }
        if (poll_fd.revents & (POLLIN | POLLHUP | POLLERR)) {
            struct yetty_ycore_size_result read_res =
                yetty_ywire_connection_pump_readable(bridge->connection);
            if (YETTY_IS_ERR(read_res)) {
                yetty_ycore_error_destroy(read_res.error);
            }
        }
        struct yetty_ycore_void_result pump_res = yetty_yclass_rpc_session_pump(bridge->rpc);
        if (YETTY_IS_ERR(pump_res)) {
            yetty_ycore_error_destroy(pump_res.error);
        }
        if (yetty_ywire_connection_is_eof(bridge->connection)) {
            break;
        }
    }
}

/* The takeover: session must already exist. Returns the exit code. */
static int attach_takeover(const char *socket_path, const char *session_name, const char *token)
{
    struct attach_bridge bridge = {0};
    bridge.pending_ctrl_control = -1; /* no folded DOWN pending (0 is a real control byte) */
    attach_read_winsize(&bridge);

    /* Order matters: set up the RPC scene FIRST, attach to the daemon
     * LAST. The RPC handshake is several blocking round-trips over stdin;
     * if the daemon connection existed during it, the daemon would pile
     * up VT frames the blocked bridge can't drain and overflow its tx
     * buffer, dropping the connection. With no daemon connection until
     * the scene is ready, nothing accumulates. */

    /* The pty transport owns the pane PTY's raw mode + non-blocking writer (its
     * blocking recv is used only for the pre-loop handshake). #380/#676: a
     * multiplexed ywire connection rides it, and the RPC session runs on one
     * dynamic channel (async -> terminal_grid_write pipelines). The legacy DCS
     * RPC transport is not used for terminal output. */
    struct yetty_yclass_transport_pty_ptr_result pty_res =
        yetty_yclass_transport_pty_create(STDIN_FILENO, STDOUT_FILENO);
    if (YETTY_IS_ERR(pty_res)) {
        yetty_ycore_error_destroy(pty_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: not running inside a yetty pane (no RPC transport)\r\n");
        return 1;
    }
    bridge.transport_pty = pty_res.value;
    struct yetty_ycore_void_result raw_mode_res =
        yetty_yclass_transport_pty_enable_raw_mode(bridge.transport_pty);
    if (YETTY_IS_ERR(raw_mode_res)) {
        yetty_ycore_error_destroy(raw_mode_res.error);
    }
    struct yetty_ywire_connection_ptr_result connection_res = yetty_ywire_connection_create(
        yetty_yclass_transport_pty_reactor(bridge.transport_pty), /*compressed=*/0);
    if (YETTY_IS_ERR(connection_res)) {
        yetty_ycore_error_destroy(connection_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: ywire connection failed\r\n");
        return 1;
    }
    bridge.connection = connection_res.value;
    /* The user's verbatim keystrokes (everything not a ywire channel envelope)
     * demux out on the RAW well-known channel -> attach_raw_sink -> pending_keys.
     * No separate stdin reader: the connection is the single owner of the PTY. */
    /* The yface splits the RAW stream into structured-input OSC (mouse) vs
     * verbatim keystrokes — created before the sink so it is ready on the first
     * byte. */
    struct yetty_yface_ptr_result face_res = yetty_yface_create();
    if (YETTY_IS_OK(face_res)) {
        bridge.in_face = face_res.value;
        yetty_yface_set_handlers(bridge.in_face, attach_on_osc, attach_on_raw, &bridge);
    } else {
        yetty_ycore_error_destroy(face_res.error);
    }
    struct yetty_ywire_channel *raw_channel =
        yetty_ywire_connection_channel(bridge.connection, YETTY_YWIRE_CHANNEL_RAW);
    if (raw_channel) {
        struct yetty_ycore_void_result raw_sink_res =
            yetty_ywire_channel_set_raw_sink(raw_channel, attach_raw_sink, &bridge);
        if (YETTY_IS_ERR(raw_sink_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), raw_sink_res.error);
            yerror("ymux attach: install RAW keystroke sink failed: %s", chain_buf);
            yetty_ycore_error_destroy(raw_sink_res.error);
        }
        /* Enable pixel-precise mouse forwarding on the hosting yetty via its
         * native DEC ?1500 (click) / ?1501 (move) card-mouse modes. Written to
         * the RAW channel = the tool's own terminal output, which yetty's
         * libvterm text-layer processes: it flips VTERM_PROP_CARDCLICK/CARDMOVE
         * and forwards every physical mouse event to this pane as a
         * CLIENT_INPUT_FIGURE_MOUSE OSC envelope (attach_on_osc), rather than
         * the SGR-in-band reports a plain ?1000 app would get. The daemon engine
         * re-encodes per the app's real mouse mode, so enabling both here is
         * safe. (Yetty applies neither ?1000 nor the 610010 figure SUB to a
         * scene-rendered pane — ?1500/?1501 is the supported path.) */
        static const char mouse_enable[] = "\x1b[?1500h\x1b[?1501h";
        /* KEY_FANOUT (SUB envelope below): the bridge CONSUMES figure-key
         * envelopes (chrome keys once the overlay claims focus) — explicit
         * opt-in, review #19; envelope-driven per review #21. */
        attach_emit_input_sub(raw_channel, YETTY_CLIENT_INPUT_SUB_KEY_FANOUT);
        struct yetty_ycore_size_result mouse_write_res =
            yetty_ywire_channel_write(raw_channel, mouse_enable, sizeof(mouse_enable) - 1);
        if (YETTY_IS_ERR(mouse_write_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), mouse_write_res.error);
            yerror("ymux attach: enqueue card-mouse enable failed: %s", chain_buf);
            yetty_ycore_error_destroy(mouse_write_res.error);
        }
        /* write() only coalesces into the channel outbuf; the RAW lane frames
         * (tmux-wraps) and emits on flush. Without this the DECSET never leaves
         * the tool and yetty never enables mouse mode. */
        struct yetty_ycore_void_result mouse_flush_res = yetty_ywire_channel_flush(raw_channel);
        if (YETTY_IS_ERR(mouse_flush_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), mouse_flush_res.error);
            yerror("ymux attach: flush card-mouse enable to yetty failed: %s", chain_buf);
            yetty_ycore_error_destroy(mouse_flush_res.error);
        }
    }
    /* Structured client input (the CLIENT_INPUT_FIGURE_MOUSE envelopes yetty
     * emits in response to the ?1500/?1501 enable above) is demuxed by the
     * connection onto the INPUT well-known channel — NOT the RAW lane. Route
     * that channel's envelopes straight to attach_on_osc: the connection has
     * already parsed the OSC, so its (code, args, payload) callback matches the
     * yface signature exactly and no re-parse is needed. */
    struct yetty_ywire_channel *input_channel =
        yetty_ywire_connection_channel(bridge.connection, YETTY_YWIRE_CHANNEL_INPUT);
    if (input_channel) {
        struct yetty_ycore_void_result input_sink_res =
            yetty_ywire_channel_set_envelope_sink(input_channel, attach_on_osc, &bridge);
        if (YETTY_IS_ERR(input_sink_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), input_sink_res.error);
            yerror("ymux attach: install INPUT mouse-envelope sink failed: %s", chain_buf);
            yetty_ycore_error_destroy(input_sink_res.error);
        }
    }
    /* Open a dynamic RPC channel and bring up the session on it (the blocking
     * attach handshake runs here, before the loop owns the fd). */
    struct yetty_yclass_object_ptr_result root_res =
        yetty_yclass_rpc_connect_channel(bridge.connection);
    if (YETTY_IS_ERR(root_res) || !root_res.value) {
        if (YETTY_IS_ERR(root_res)) {
            yetty_ycore_error_destroy(root_res.error);
        }
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: the hosting terminal is not a yetty (no RPC root)\r\n");
        return 1;
    }
    bridge.terminal_proxy = root_res.value;
    bridge.rpc = bridge.terminal_proxy->session;
    /* Pipelined-call failures surface out of band -> our sink, not the call site. */
    yetty_yclass_rpc_session_set_error_sink(bridge.rpc, attach_rpc_error_sink, &bridge);

    struct yetty_yclass_object_ptr_result container_res =
        yetty_yterminal_figure_root_container(bridge.terminal_proxy);
    if (YETTY_IS_ERR(container_res) || !container_res.value) {
        if (YETTY_IS_ERR(container_res)) {
            yetty_ycore_error_destroy(container_res.error);
        }
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: pane root container unavailable\r\n");
        return 1;
    }
    bridge.container_proxy = container_res.value;
    /* Prime the container's method-slot table while the pipeline is empty — a
     * steady-state pipelined mutation must never mid-stream RESOLVE_SLOT. */
    {
        struct yetty_ycore_void_result prime_res =
            yetty_yclass_rpc_session_translate_class(bridge.rpc, "yetty_yfigure_container");
        if (YETTY_IS_ERR(prime_res)) {
            yetty_ycore_error_destroy(prime_res.error);
        }
    }

    /* The scene figure covering the pane. */
    struct yetty_ycore_rectangle pane_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = bridge.pixel_width, .y = bridge.pixel_height},
    };
    struct yetty_ycore_buffer empty_init = {0};
    struct yetty_ycore_void_result create_res =
        yetty_yfigure_create_child(bridge.container_proxy, yetty_yfigure_kind_token("yscene"),
                                   ATTACH_SCENE_CHILD_ID, pane_rect, empty_init);
    if (YETTY_IS_ERR(create_res)) {
        yetty_ycore_error_destroy(create_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: scene create failed\r\n");
        return 1;
    }
    /* Re-seat the grid as a FIXED FULL-PANE overlay at the pane origin. Plain
     * CREATE_CHILD adds the container's producer viewport offset and scroll
     * anchors the figure, which floats a full-pane terminal a row or two below
     * the pane top (uncovering it) and slides it with the underlying scroll. */
    struct yetty_ycore_void_result seat_res =
        yetty_yfigure_seat_overlay(bridge.container_proxy, ATTACH_SCENE_CHILD_ID, pane_rect);
    if (YETTY_IS_ERR(seat_res)) {
        yetty_ycore_error_destroy(seat_res.error);
    }
    /* create_child + seat_overlay pipelined as void calls — drain their
     * completions before the value-returning navigation below (async-mode rule:
     * no value/admin call with completions in flight). */
    attach_drain_rpc(&bridge);
    struct yetty_yclass_object_ptr_result scene_res =
        yetty_yfigure_child_object(bridge.container_proxy, ATTACH_SCENE_CHILD_ID);
    if (YETTY_IS_ERR(scene_res) || !scene_res.value) {
        if (YETTY_IS_ERR(scene_res)) {
            yetty_ycore_error_destroy(scene_res.error);
        }
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: scene navigation failed\r\n");
        return 1;
    }
    bridge.scene_proxy = scene_res.value;
    /* #699.4 two-scene compositor: the OVERLAY scene, created AFTER (= above)
     * the content scene and seated at the same fixed pane rect. It stages
     * nothing yet, so it composites as fully transparent; the topmost-wins hit
     * test already routes pointer input to it first. */
    struct yetty_ycore_void_result overlay_create_res =
        yetty_yfigure_create_child(bridge.container_proxy, yetty_yfigure_kind_token("yscene"),
                                   ATTACH_OVERLAY_CHILD_ID, pane_rect, empty_init);
    if (YETTY_IS_ERR(overlay_create_res)) {
        /* REQUIRED-OVERLAY INVARIANT (review #10): the two-scene compositor is
         * the architecture, not an enhancement — a pane without its overlay
         * scene would silently lack every overlay behavior (input-first
         * routing, future chrome), so its absence is an attach FAILURE. */
        yetty_ycore_error_destroy(overlay_create_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: overlay scene create failed\r\n");
        return 1;
    }
    struct yetty_ycore_void_result overlay_seat_res =
        yetty_yfigure_seat_overlay(bridge.container_proxy, ATTACH_OVERLAY_CHILD_ID, pane_rect);
    if (YETTY_IS_ERR(overlay_seat_res)) {
        yetty_ycore_error_destroy(overlay_seat_res.error);
    }
    attach_drain_rpc(&bridge);
    struct yetty_yclass_object_ptr_result overlay_res =
        yetty_yfigure_child_object(bridge.container_proxy, ATTACH_OVERLAY_CHILD_ID);
    if (YETTY_IS_ERR(overlay_res) || !overlay_res.value) {
        if (YETTY_IS_ERR(overlay_res)) {
            yetty_ycore_error_destroy(overlay_res.error);
        }
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: overlay scene navigation failed\r\n");
        return 1;
    }
    bridge.overlay_proxy = overlay_res.value;
    /* Prime the scene's slot table too — terminal_grid_write/resize and
     * apply_content_transaction pipeline against it in the steady state. */
    {
        struct yetty_ycore_void_result prime_res =
            yetty_yclass_rpc_session_translate_class(bridge.rpc, "yetty_yscene_scene");
        if (YETTY_IS_ERR(prime_res)) {
            yetty_ycore_error_destroy(prime_res.error);
        }
    }
    /* The PRODUCTION overlay surface (review #17): the scroll-mode
     * indicator — pressing it claims chrome focus for real input. */
    if (!getenv("YMUX_BISECT_NO_CHROME")) {
        attach_stage_overlay_chrome(&bridge);
    }

    struct yetty_ycore_void_result terminal_create_res = yetty_yscene_terminal_grid_create(
        bridge.scene_proxy, bridge.rows, bridge.cols, bridge.cell_width, bridge.cell_height);
    if (YETTY_IS_ERR(terminal_create_res)) {
        yetty_ycore_error_destroy(terminal_create_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: scene terminal grid failed\r\n");
        return 1;
    }

    /* Scene is ready — NOW connect to the daemon and attach. The sink is
     * installed first so the very first WELCOME/FULL forwards immediately
     * and nothing is ever buffered undrained. */
    struct yetty_yclass_object_ptr_result client_res = yetty_ymux_client_make(socket_path);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: can't connect to server on %s\r\n", socket_path);
        return 1;
    }
    bridge.client = client_res.value;
    /* #699.2: host the vtsink — the daemon delivers terminal bytes exclusively
     * as typed ordered feed() calls over the lane. The ACK is deferred until
     * the scene write covering the bytes completes (#699.6). */
    yetty_ymux_client_enable_vtsink(bridge.client, attach_vtsink_emit, &bridge);
    yetty_ymux_client_set_overlay_input_handler(bridge.client, attach_overlay_input_handler,
                                                &bridge);
    yetty_ymux_client_set_vtsink_reset_handler(bridge.client, attach_on_vtsink_reset, &bridge);
    /* Chrome focus starts OFF: the terminal owns keystrokes/paste until a
     * chrome consumer (copy-mode) claims them. */
    attach_set_overlay_chrome_active(&bridge, 0);
    yetty_ymux_client_vtsink_defer_ack(bridge.client);
    /* Figure-surface RPC proxy: the daemon relays a pane app's ygui/ygreeter
     * yclass-RPC here; we pipe it to fresh channels on our yetty connection. */
    yetty_ymux_client_set_rpc_relay_sink(bridge.client, attach_rpc_relay, &bridge);
    yetty_ymux_client_set_rpc_relay_close_sink(bridge.client, attach_rpc_relay_close, &bridge);
    uint32_t cell_pixel_height =
        bridge.cell_height > 0.0f ? (uint32_t)(bridge.cell_height + 0.5f) : 0u;
    /* The bridge renders text from the VT byte stream (its libvterm-backed
     * yscene grid), NOT the semantic surface — advertise VT_TEXT so the daemon
     * makes VT the sole controlling text path, and TRUECOLOR so RGB passes
     * through. */
    {
        /* Terminal identity (review #17 item 8): name the client terminal
         * so the daemon resolves the capability profile through the tmux
         * terminfo/features state model. The receiving grid is our own
         * libvterm scene — an xterm-256color-family terminal with RGB. */
        struct yetty_ycore_void_result yetty_ymux_client_set_terminal(
            struct yetty_yclass_object * term_obj, const char *term_name, const char *features);
        const char *bridge_term = getenv("YMUX_TERM");
        /* TRUTHFUL advertisement (review #21/#22): name ONLY what the receiving
         * libvterm+yscene grid actually renders — 256/RGB colour and
         * strikethrough. NOT advertised while unimplemented at the endpoint:
         *   - sync (?2026): local libvterm stores the mode bit but the yscene
         *     presentation path does not DEFER frames across RPC publications,
         *     so a begin/end pair gives no synchronized presentation (#22 P0);
         *   - usstyle (needs SGR 58 underline colour — no Setulc state; dotted/
         *     dashed collapse to curly), hyperlinks (no OSC 8 carriage),
         *     overline (no SGR 53 state).
         * Final #699 acceptance requires implementing those semantics end to
         * end (for sync: cross-publication frame deferral + safety timeout)
         * and re-adding them here. */
        struct yetty_ycore_void_result term_res = yetty_ymux_client_set_terminal(
            bridge.client, bridge_term ? bridge_term : "xterm-256color", "256,RGB,strikethrough");
        if (YETTY_IS_ERR(term_res)) {
            yetty_ycore_error_destroy(term_res.error);
        }
    }
    struct yetty_ycore_void_result attach_res = yetty_ymux_client_attach(
        bridge.client, session_name, 0, bridge.rows, bridge.cols, cell_pixel_height,
        YMUX_TERM_CAPS_XTERM_256COLOR | YMUX_TERM_CAP_VT_TEXT | YMUX_TERM_CAP_RESOURCE_REF |
            YMUX_TERM_CAP_ATTACH_PREAMBLE,
        token);
    if (YETTY_IS_ERR(attach_res)) {
        yetty_ycore_error_destroy(attach_res.error);
        attach_cleanup(&bridge);
        fprintf(stderr, "ymux: attach failed\r\n");
        return 1;
    }

    /* Bridge loop: daemon socket → (sink) scene RPC; keystrokes → session.
     * tmux prefix: C-b d detaches, C-b C-b sends a literal C-b. */
    /* Window resizes reach this pane as SIGWINCH; a no-op handler WITHOUT
     * SA_RESTART makes poll() return EINTR so the loop re-reads the geometry
     * without waiting out the 1s timeout. */
    struct sigaction winch_action = {0};
    winch_action.sa_handler = attach_sigwinch;
    sigaction(SIGWINCH, &winch_action, NULL);

    int daemon_fd = yetty_ymux_client_fd(bridge.client).value;
    int connection_fd = yetty_ywire_connection_fd(bridge.connection);
    int exit_code = 0;
    for (;;) {
        struct pollfd poll_fds[2] = {
            {.fd = connection_fd, .events = POLLIN},
            {.fd = daemon_fd, .events = POLLIN},
        };
        /* Arm writable interest on the pane wire whenever the pipelined RPC has
         * queued outbound bytes the transport has not drained yet. */
        if (yetty_ywire_connection_want_write(bridge.connection)) {
            poll_fds[0].events |= POLLOUT;
        }
        /* Pending drain work (consumed overlay input / armed reply polls)
         * needs IDLE iterations to fire — the 1s idle timeout would turn
         * the 16-iteration burst throttle into ~16 SECONDS of latency
         * (review #17: chrome arrows landed but never forwarded). */
        int poll_timeout =
            (bridge.consumed_undrained > 0 || bridge.reply_poll_armed > 0) ? 50 : 1000;
        /* A pending escape carry has its OWN 10 ms deadline (tmux escape-time):
         * cap the wait so the loop wakes to flush it even with no other work. */
        if (bridge.key_stream.esc_carry_len > 0 && poll_timeout > ATTACH_ESCAPE_TIMEOUT_MS) {
            poll_timeout = ATTACH_ESCAPE_TIMEOUT_MS;
        }
        int poll_result = poll(poll_fds, 2, poll_timeout);
        if (poll_result < 0 && errno != EINTR) {
            fprintf(stderr, "ymux: bridge poll failed: %s\r\n", strerror(errno));
            exit_code = 1;
            break;
        }
        /* Flush a stranded escape carry once its INDEPENDENT 10 ms deadline
         * elapses — regardless of poll activity. A lone ESC or an incomplete
         * sequence must reach the pane on the escape-timeout rather than be held
         * for a continuation that never comes; tying this to poll idle alone
         * meant continuous peer activity could hold it indefinitely (cycle-24
         * P0). Under real DECCKM input the continuation arrives well within
         * 10 ms and completes the carry before this fires. */
        if (bridge.key_stream.esc_carry_len > 0) {
            struct timespec escape_now;
            clock_gettime(CLOCK_MONOTONIC, &escape_now);
            long escape_elapsed_ms = (escape_now.tv_sec - bridge.esc_carry_at.tv_sec) * 1000 +
                                     (escape_now.tv_nsec - bridge.esc_carry_at.tv_nsec) / 1000000;
            if (escape_elapsed_ms >= ATTACH_ESCAPE_TIMEOUT_MS) {
                attach_flush_pending_escape(&bridge);
            }
        }
        /* SIGWINCH (or the 1s timeout) — reflow grid/figure/daemon if the
         * window changed. Cheap ioctl; returns early when nothing moved. */
        attach_apply_resize(&bridge);
        /* A VT frame was dropped or failed to write — request a complete redraw
         * before pumping more incremental bytes onto a desynced receiver. */
        if (bridge.vt_desynced) {
            attach_request_resync(&bridge);
        }
        /* Pane wire readable: the connection is the SINGLE reader of the PTY —
         * pump_readable demuxes RPC-response frames (drains pipelined
         * completions) and the user's verbatim keystrokes (RAW channel ->
         * attach_raw_sink -> pending_keys). */
        if (poll_fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            struct yetty_ycore_size_result pump_res =
                yetty_ywire_connection_pump_readable(bridge.connection);
            if (YETTY_IS_ERR(pump_res)) {
                yetty_ycore_error_destroy(pump_res.error);
            }
            if (yetty_ywire_connection_is_eof(bridge.connection)) {
                fprintf(stderr, "\r\n[disconnected]\r\n");
                break;
            }
            attach_flush_pending_pointers(&bridge);
            if (attach_process_ordered_input(&bridge) || attach_flush_keys(&bridge) ||
                bridge.detach_requested) {
                fprintf(stderr, "\r\n[detached]\r\n");
                attach_cleanup(&bridge);
                return 0;
            }
        }
        /* Push any queued outbound RPC bytes when the wire is writable. */
        if ((poll_fds[0].revents & POLLOUT) ||
            yetty_ywire_connection_want_write(bridge.connection)) {
            struct yetty_ycore_size_result write_res =
                yetty_ywire_connection_pump_writable(bridge.connection);
            if (YETTY_IS_ERR(write_res)) {
                yetty_ycore_error_destroy(write_res.error);
            }
        }
        bridge.loop_carried_feed = 0;
        if (poll_fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            bridge.loop_carried_feed = 1;
            struct yetty_ycore_int_result step_res = yetty_ymux_client_step(bridge.client);
            if (YETTY_IS_ERR(step_res)) {
                yetty_ycore_error_destroy(step_res.error);
                fprintf(stderr, "ymux: server exited\r\n");
                break;
            }
            /* Applied frames already forwarded to the scene via the sink,
             * in order, during client_step. */
            struct yetty_ycore_int_result exited_res = yetty_ymux_client_pane_exited(bridge.client);
            if (YETTY_IS_OK(exited_res) && exited_res.value) {
                fprintf(stderr, "\r\n[exited]\r\n");
                break;
            }
            if (YETTY_IS_ERR(exited_res)) {
                yetty_ycore_error_destroy(exited_res.error);
            }
            /* One ATOMIC content publish (terminal VT + rich together) for
             * everything client_step drained — a PIPELINED void RPC (no sync
             * round trip): it queues onto the wire and returns; the
             * pump_writable below flushes it and its completion drains through
             * pump_readable. Both halves render in the same frame. */
            attach_flush_content(&bridge);
            /* The write queued bytes — flush them promptly rather than waiting
             * for the next poll wake so the grid updates without added latency. */
            struct yetty_ycore_size_result flush_res =
                yetty_ywire_connection_pump_writable(bridge.connection);
            if (YETTY_IS_ERR(flush_res)) {
                yetty_ycore_error_destroy(flush_res.error);
            }
        }
        /* Deferred delivery ACK — EVERY iteration, not only on daemon-socket
         * events: the scene-write completions that make the pipeline empty
         * arrive on the YETTY connection (poll slot 0). Gating this on daemon
         * traffic deadlocks: the daemon freezes at its unacked window and goes
         * silent, so the ack that would unfreeze it never gets a chance. The
         * pump inside is non-blocking. */
        attach_report_applied(&bridge);
        /* Overlay ownership pump — every iteration: pops ACKed events,
         * resends the inflight one past its window, sends the next. */
        attach_overlay_flush(&bridge);
        attach_perform_receiver_reset(&bridge);
        if (attach_process_ordered_input(&bridge) || bridge.detach_requested) {
            fprintf(stderr, "\r\n[detached]\r\n");
            attach_cleanup(&bridge);
            return 0;
        }
        attach_poll_scene_drains(&bridge);
        attach_flush_selection(&bridge);
    }
    attach_cleanup(&bridge);
    return exit_code;
}

/*===========================================================================
 * Commands (tmux names, aliases, flags).
 *===========================================================================*/

struct command_context {
    const char *socket_path;
    int argc;
    char **argv;
};

static void warn_unimplemented_flag(const char *command, int flag)
{
    fprintf(stderr, "ymux: %s -%c not yet implemented (accepted for tmux compatibility)\n", command,
            flag);
}

static struct yetty_ycore_void_result command_new_session(struct command_context *context)
{
    const char *session_name = NULL;
    int detached = 0;
    int attach_existing = 0;
    uint32_t width = 80, height = 24;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "AdDEPXc:e:F:f:n:s:t:x:y:")) != -1) {
        switch (opt) {
        case 'A':
            attach_existing = 1;
            break;
        case 'd':
            detached = 1;
            break;
        case 's':
            session_name = optarg;
            break;
        case 'x':
            width = (uint32_t)atoi(optarg);
            break;
        case 'y':
            height = (uint32_t)atoi(optarg);
            break;
        case 'D':
        case 'E':
        case 'P':
        case 'X':
        case 'c':
        case 'e':
        case 'F':
        case 'f':
        case 'n':
        case 't':
            warn_unimplemented_flag("new-session", opt);
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: new-session [-AdDEPX] "
                                               "[-s session-name] [-x width] [-y height]");
        }
    }
    if (ensure_server(context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "new-session: can't start or reach the server");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "new-session: can't connect to the server socket");
    }
    if (attach_existing && session_name) {
        struct yetty_ycore_void_result has_res =
            yetty_ymux_client_session_has(control.client, session_name);
        if (YETTY_IS_ERR(has_res)) {
            yetty_ycore_error_destroy(has_res.error);
        }
        const char *text = NULL;
        if (control_wait_reply(&control, &text) == 0) {
            /* Exists — new-session -A degrades to attach. */
            control_close(&control);
            if (attach_takeover(context->socket_path, session_name, getenv("USER")) != 0) {
                return YETTY_ERR(yetty_ycore_void, "new-session: attach ended abnormally");
            }
            return YETTY_OK_VOID();
        }
    }
    struct yetty_ycore_void_result new_res =
        yetty_ymux_client_session_new(control.client, session_name, height, width);
    if (YETTY_IS_ERR(new_res)) {
        control_close(&control);
        return YETTY_ERR(yetty_ycore_void, "new-session: create failed", new_res);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    if (status != 0) {
        fprintf(stderr, "ymux: new-session: %s\n",
                text && text[0] ? text : "create session failed");
        control_close(&control);
        return YETTY_ERR(yetty_ycore_void, "new-session: create session failed");
    }
    if (detached) {
        control_close(&control);
        return YETTY_OK_VOID();
    }
    /* The session name came back in the reply (auto-numbered when -s was
     * absent) — attach to exactly that session. */
    char created_name[64];
    snprintf(created_name, sizeof(created_name), "%s", text ? text : "");
    control_close(&control);
    if (attach_takeover(context->socket_path, created_name[0] ? created_name : NULL,
                        getenv("USER")) != 0) {
        return YETTY_ERR(yetty_ycore_void, "new-session: attach ended abnormally");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_attach_session(struct command_context *context)
{
    const char *target = NULL;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "dErxc:f:t:")) != -1) {
        switch (opt) {
        case 't':
            target = optarg;
            break;
        case 'd':
        case 'E':
        case 'r':
        case 'x':
        case 'c':
        case 'f':
            warn_unimplemented_flag("attach-session", opt);
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: attach-session [-t target-session]");
        }
    }
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "attach-session: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "attach-session: can't connect to the server socket");
    }
    struct yetty_ycore_void_result has_res = yetty_ymux_client_session_has(control.client, target);
    if (YETTY_IS_ERR(has_res)) {
        yetty_ycore_error_destroy(has_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    control_close(&control);
    if (status != 0) {
        fprintf(stderr, "ymux: attach-session: %s\n",
                text && text[0] ? text : "can't find session");
        return YETTY_ERR(yetty_ycore_void, "attach-session: can't find session");
    }
    if (attach_takeover(context->socket_path, target, getenv("USER")) != 0) {
        return YETTY_ERR(yetty_ycore_void, "attach-session: attach ended abnormally");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_detach_client(struct command_context *context)
{
    const char *target = NULL;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "aPE:s:t:")) != -1) {
        switch (opt) {
        case 's':
            target = optarg;
            break;
        case 'a':
        case 'P':
        case 'E':
        case 't':
            warn_unimplemented_flag("detach-client", opt);
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: detach-client [-s target-session]");
        }
    }
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "detach-client: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "detach-client: can't connect to the server socket");
    }
    struct yetty_ycore_void_result detach_res =
        yetty_ymux_client_session_detach(control.client, target);
    if (YETTY_IS_ERR(detach_res)) {
        yetty_ycore_error_destroy(detach_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    control_close(&control);
    if (status != 0) {
        fprintf(stderr, "ymux: detach-client: %s\n", text && text[0] ? text : "detach failed");
        return YETTY_ERR(yetty_ycore_void, "detach-client: detach failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_has_session(struct command_context *context)
{
    const char *target = NULL;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "t:")) != -1) {
        switch (opt) {
        case 't':
            target = optarg;
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: has-session [-t target-session]");
        }
    }
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "has-session: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "has-session: can't connect to the server socket");
    }
    struct yetty_ycore_void_result has_res = yetty_ymux_client_session_has(control.client, target);
    if (YETTY_IS_ERR(has_res)) {
        yetty_ycore_error_destroy(has_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    if (status != 0 && text && text[0]) {
        fprintf(stderr, "%s\n", text);
    }
    control_close(&control);
    if (status != 0) {
        return YETTY_ERR(yetty_ycore_void, "has-session: can't find session");
    }
    return YETTY_OK_VOID();
}

/* Ops/debug: force the daemon's slow-client recovery (epoch reset) on every
 * attached client NOW — lets the attach-level harness drive the reset path
 * against a live daemon without accumulating megabytes of backlog. */
static struct yetty_ycore_void_result command_recover(struct command_context *context)
{
    struct yetty_ycore_void_result yetty_ymux_client_request_recover(struct yetty_yclass_object *
                                                                     client_obj);
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "recover: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "recover: can't connect to the server socket");
    }
    struct yetty_ycore_void_result recover_res = yetty_ymux_client_request_recover(control.client);
    if (YETTY_IS_ERR(recover_res)) {
        control_close(&control);
        return YETTY_ERR(yetty_ycore_void, "recover: request failed", recover_res);
    }
    /* Wait for the ACK: a silently ignored request must FAIL, not pass. */
    const char *ack_text = NULL;
    int ack_status = control_wait_reply(&control, &ack_text);
    if (ack_text) {
        printf("%s\n", ack_text);
    }
    control_close(&control);
    if (ack_status != 0) {
        return YETTY_ERR(yetty_ycore_void, "recover: no attached client recovered");
    }
    return YETTY_OK_VOID();
}

/* Paste the daemon's copy-mode buffer into the target pane (tmux
 * paste-buffer). Fails loudly when the buffer is empty or no pane. */
static struct yetty_ycore_void_result command_paste(struct command_context *context)
{
    struct yetty_ycore_void_result yetty_ymux_client_request_paste(struct yetty_yclass_object *
                                                                   client_obj);
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "paste: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "paste: can't connect to the server socket");
    }
    struct yetty_ycore_void_result paste_res = yetty_ymux_client_request_paste(control.client);
    if (YETTY_IS_ERR(paste_res)) {
        control_close(&control);
        return YETTY_ERR(yetty_ycore_void, "paste: request failed", paste_res);
    }
    const char *ack_text = NULL;
    int ack_status = control_wait_reply(&control, &ack_text);
    if (ack_text) {
        printf("%s\n", ack_text);
    }
    control_close(&control);
    if (ack_status != 0) {
        return YETTY_ERR(yetty_ycore_void, "paste: nothing pasted");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_kill_server(struct command_context *context)
{
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "kill-server: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "kill-server: can't connect to the server socket");
    }
    struct yetty_ycore_void_result shutdown_res = yetty_ymux_client_shutdown_server(control.client);
    if (YETTY_IS_ERR(shutdown_res)) {
        control_close(&control);
        return YETTY_ERR(yetty_ycore_void, "kill-server: shutdown send failed", shutdown_res);
    }
    control_close(&control);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_kill_session(struct command_context *context)
{
    const char *target = NULL;
    int all_but = 0;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "aCt:")) != -1) {
        switch (opt) {
        case 't':
            target = optarg;
            break;
        case 'a':
            all_but = 1;
            break;
        case 'C':
            warn_unimplemented_flag("kill-session", opt);
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: kill-session [-a] [-t target-session]");
        }
    }
    if (all_but) {
        warn_unimplemented_flag("kill-session", 'a');
        return YETTY_ERR(yetty_ycore_void, "kill-session: -a is not implemented");
    }
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "kill-session: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "kill-session: can't connect to the server socket");
    }
    struct yetty_ycore_void_result kill_res =
        yetty_ymux_client_session_kill(control.client, target);
    if (YETTY_IS_ERR(kill_res)) {
        yetty_ycore_error_destroy(kill_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    control_close(&control);
    if (status != 0) {
        fprintf(stderr, "ymux: kill-session: %s\n", text && text[0] ? text : "kill failed");
        return YETTY_ERR(yetty_ycore_void, "kill-session: kill failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_list_sessions(struct command_context *context)
{
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "F:f:")) != -1) {
        switch (opt) {
        case 'F':
        case 'f':
            warn_unimplemented_flag("list-sessions", opt);
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: list-sessions");
        }
    }
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "list-sessions: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "list-sessions: can't connect to the server socket");
    }
    struct yetty_ycore_void_result list_res = yetty_ymux_client_session_list(control.client);
    if (YETTY_IS_ERR(list_res)) {
        yetty_ycore_error_destroy(list_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    if (status == 0 && text && text[0]) {
        fputs(text, stdout);
    }
    control_close(&control);
    if (status != 0) {
        fprintf(stderr, "ymux: list-sessions: %s\n", text && text[0] ? text : "list failed");
        return YETTY_ERR(yetty_ycore_void, "list-sessions: list failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_rename_session(struct command_context *context)
{
    const char *target = NULL;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "t:")) != -1) {
        switch (opt) {
        case 't':
            target = optarg;
            break;
        default:
            return YETTY_ERR(yetty_ycore_void,
                             "usage: rename-session [-t target-session] new-name");
        }
    }
    if (optind >= context->argc) {
        return YETTY_ERR(yetty_ycore_void, "usage: rename-session [-t target-session] new-name");
    }
    const char *new_name = context->argv[optind];
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "rename-session: no server running");
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "rename-session: can't connect to the server socket");
    }
    struct yetty_ycore_void_result rename_res =
        yetty_ymux_client_session_rename(control.client, target, new_name);
    if (YETTY_IS_ERR(rename_res)) {
        yetty_ycore_error_destroy(rename_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    control_close(&control);
    if (status != 0) {
        fprintf(stderr, "ymux: rename-session: %s\n", text && text[0] ? text : "rename failed");
        return YETTY_ERR(yetty_ycore_void, "rename-session: rename failed");
    }
    return YETTY_OK_VOID();
}

/* tmux key-name → (kind, value) pair; returns pair count appended. */
static size_t send_keys_translate(const char *key, uint32_t *pairs, size_t capacity)
{
    struct {
        const char *name;
        uint32_t value;
    } named_keys[] = {
        {"Enter", YETTY_YMUX_KEY_ENTER},
        {"Tab", YETTY_YMUX_KEY_TAB},
        {"BSpace", YETTY_YMUX_KEY_BACKSPACE},
        {"Escape", YETTY_YMUX_KEY_ESCAPE},
        {"Up", YETTY_YMUX_KEY_UP},
        {"Down", YETTY_YMUX_KEY_DOWN},
        {"Left", YETTY_YMUX_KEY_LEFT},
        {"Right", YETTY_YMUX_KEY_RIGHT},
        {"IC", YETTY_YMUX_KEY_INSERT},
        {"DC", YETTY_YMUX_KEY_DELETE},
        {"Home", YETTY_YMUX_KEY_HOME},
        {"End", YETTY_YMUX_KEY_END},
        {"PPage", YETTY_YMUX_KEY_PAGE_UP},
        {"NPage", YETTY_YMUX_KEY_PAGE_DOWN},
    };
    for (size_t entry = 0; entry < sizeof(named_keys) / sizeof(named_keys[0]); ++entry) {
        if (strcmp(key, named_keys[entry].name) == 0) {
            if (capacity < 2) {
                return 0;
            }
            pairs[0] = 1;
            pairs[1] = named_keys[entry].value;
            return 2;
        }
    }
    if (strcmp(key, "Space") == 0) {
        if (capacity < 2) {
            return 0;
        }
        pairs[0] = 0;
        pairs[1] = ' ';
        return 2;
    }
    if (key[0] == 'C' && key[1] == '-' && key[2] && !key[3]) {
        /* C-x → the control codepoint, tmux-style. */
        char base = key[2];
        if (base >= 'a' && base <= 'z') {
            base = (char)(base - 'a' + 'A');
        }
        if (base >= '@' && base <= '_') {
            if (capacity < 2) {
                return 0;
            }
            pairs[0] = 0;
            pairs[1] = (uint32_t)(base - '@');
            return 2;
        }
    }
    /* Literal: each byte as a codepoint (ASCII/UTF-8 single bytes v1). */
    size_t used = 0;
    for (const char *cursor = key; *cursor && used + 2 <= capacity; ++cursor) {
        pairs[used] = 0;
        pairs[used + 1] = (uint32_t)(unsigned char)*cursor;
        used += 2;
    }
    return used;
}

static struct yetty_ycore_void_result command_send_keys(struct command_context *context)
{
    const char *target = NULL;
    int literal = 0;
    optind = 1;
    int opt;
    while ((opt = getopt(context->argc, context->argv, "FHKlMRXc:N:t:")) != -1) {
        switch (opt) {
        case 't':
            target = optarg;
            break;
        case 'l':
            literal = 1;
            break;
        case 'F':
        case 'H':
        case 'K':
        case 'M':
        case 'R':
        case 'X':
        case 'c':
        case 'N':
            warn_unimplemented_flag("send-keys", opt);
            break;
        default:
            return YETTY_ERR(yetty_ycore_void, "usage: send-keys [-l] [-t target-pane] key ...");
        }
    }
    if (!socket_alive(context->socket_path)) {
        return YETTY_ERR(yetty_ycore_void, "send-keys: no server running");
    }
    uint32_t pairs[1024];
    size_t used = 0;
    for (int arg = optind; arg < context->argc; ++arg) {
        const char *key = context->argv[arg];
        if (literal) {
            for (const char *cursor = key; *cursor && used + 2 <= sizeof(pairs) / sizeof(pairs[0]);
                 ++cursor) {
                pairs[used] = 0;
                pairs[used + 1] = (uint32_t)(unsigned char)*cursor;
                used += 2;
            }
        } else {
            used += send_keys_translate(key, pairs + used, sizeof(pairs) / sizeof(pairs[0]) - used);
        }
    }
    struct control control;
    if (control_open(&control, context->socket_path) != 0) {
        return YETTY_ERR(yetty_ycore_void, "send-keys: can't connect to the server socket");
    }
    struct yetty_ycore_void_result send_res =
        yetty_ymux_client_session_send_keys(control.client, target, pairs, (uint32_t)(used / 2));
    if (YETTY_IS_ERR(send_res)) {
        yetty_ycore_error_destroy(send_res.error);
    }
    const char *text = NULL;
    int status = control_wait_reply(&control, &text);
    control_close(&control);
    if (status != 0) {
        fprintf(stderr, "ymux: send-keys: %s\n", text && text[0] ? text : "send failed");
        return YETTY_ERR(yetty_ycore_void, "send-keys: send failed");
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result command_list_commands(struct command_context *context);

/*===========================================================================
 * Command table + tmux-style unambiguous-prefix dispatch.
 *===========================================================================*/

struct command_entry {
    const char *name;
    const char *alias;
    const char *usage;
    struct yetty_ycore_void_result (*run)(struct command_context *context);
};

/* Result carrier for command resolution — unknown/ambiguous words are errors,
 * not NULL sentinels. */
YETTY_YRESULT_DECLARE(ymux_command_entry_ptr, const struct command_entry *);

static const struct command_entry *command_table(size_t *out_count)
{
    static const struct command_entry commands[] = {
        {"attach-session", "attach", "[-t target-session]", command_attach_session},
        {"detach-client", "detach", "[-s target-session]", command_detach_client},
        {"has-session", "has", "[-t target-session]", command_has_session},
        {"kill-server", NULL, "", command_kill_server},
        {"kill-session", NULL, "[-t target-session]", command_kill_session},
        {"list-commands", "lscm", "", command_list_commands},
        {"list-sessions", "ls", "", command_list_sessions},
        {"new-session", "new", "[-AdDEPX] [-s session-name] [-x width] [-y height]",
         command_new_session},
        {"recover-clients", "recover", "", command_recover},
        {"paste-buffer", "paste", "", command_paste},
        {"rename-session", "rename", "[-t target-session] new-name", command_rename_session},
        {"send-keys", "send", "[-l] [-t target-pane] key ...", command_send_keys},
    };
    *out_count = sizeof(commands) / sizeof(commands[0]);
    return commands;
}

static struct yetty_ycore_void_result command_list_commands(struct command_context *context)
{
    (void)context;
    size_t count = 0;
    const struct command_entry *commands = command_table(&count);
    for (size_t index = 0; index < count; ++index) {
        if (commands[index].alias) {
            printf("%s (%s) %s\n", commands[index].name, commands[index].alias,
                   commands[index].usage);
        } else {
            printf("%s %s\n", commands[index].name, commands[index].usage);
        }
    }
    return YETTY_OK_VOID();
}

static struct ymux_command_entry_ptr_result resolve_command(const char *word)
{
    size_t count = 0;
    const struct command_entry *commands = command_table(&count);
    const struct command_entry *prefix_match = NULL;
    int prefix_matches = 0;
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(word, commands[index].name) == 0 ||
            (commands[index].alias && strcmp(word, commands[index].alias) == 0)) {
            return YETTY_OK(ymux_command_entry_ptr, &commands[index]);
        }
        if (strncmp(word, commands[index].name, strlen(word)) == 0) {
            prefix_match = &commands[index];
            ++prefix_matches;
        }
    }
    if (prefix_matches == 1) {
        return YETTY_OK(ymux_command_entry_ptr, prefix_match);
    }
    if (prefix_matches > 1) {
        fprintf(stderr, "ymux: ambiguous command: %s\n", word);
        return YETTY_ERR(ymux_command_entry_ptr, "ambiguous command");
    }
    fprintf(stderr, "ymux: unknown command: %s\n", word);
    return YETTY_ERR(ymux_command_entry_ptr, "unknown command");
}

/*===========================================================================
 * Entry: global flags, socket resolution, dispatch.
 *===========================================================================*/

int main(int argc, char **argv)
{
    const char *socket_name = "default";
    const char *socket_override = NULL;
    int foreground_server = 0;

    int opt;
    while ((opt = getopt(argc, argv, "+2CDluvVc:f:L:S:")) != -1) {
        switch (opt) {
        case 'L':
            socket_name = optarg;
            break;
        case 'S':
            socket_override = optarg;
            break;
        case 'V':
            printf("%s\n", YMUX_VERSION_STRING);
            return 0;
        case 'D':
            foreground_server = 1;
            break;
        case 'f':
        case 'c':
            warn_unimplemented_flag("ymux", opt);
            break;
        case '2':
        case 'C':
        case 'l':
        case 'u':
        case 'v':
            break; /* terminal-capability / verbosity hints — harmless */
        default:
            fprintf(stderr, "usage: ymux [-2CDluvV] [-f file] [-L socket-name] "
                            "[-S socket-path] [command [flags]]\n");
            return 1;
        }
    }

    char socket_path[256];
    if (socket_override) {
        snprintf(socket_path, sizeof(socket_path), "%s", socket_override);
    } else {
        snprintf(socket_path, sizeof(socket_path), "/tmp/ymux-%d/%s", (int)getuid(), socket_name);
    }

    if (foreground_server) {
        char *slash = strrchr(socket_path, '/');
        if (slash && slash != socket_path) {
            char dir[256];
            size_t dir_len = (size_t)(slash - socket_path);
            memcpy(dir, socket_path, dir_len);
            dir[dir_len] = 0;
            mkdir(dir, 0700);
        }
        return server_run(socket_path);
    }

    struct command_context context = {
        .socket_path = socket_path,
        .argc = argc - optind,
        .argv = argv + optind,
    };
    if (context.argc == 0) {
        /* tmux: no command = new-session. main() is the END CONSUMER of the
         * command Result chain: success = exit 0, error = print + exit 1. */
        static char *default_argv[] = {(char *)"new-session", NULL};
        context.argc = 1;
        context.argv = default_argv;
        struct yetty_ycore_void_result run_res = command_new_session(&context);
        if (YETTY_IS_ERR(run_res)) {
            yetty_ycore_error_print(stderr, "ymux", run_res.error);
            yetty_ycore_error_destroy(run_res.error);
            return 1;
        }
        return 0;
    }

    struct ymux_command_entry_ptr_result command_res = resolve_command(context.argv[0]);
    if (YETTY_IS_ERR(command_res)) {
        /* resolve already printed the user-facing diagnosis; the chain is for
         * the machine-readable trail. */
        yetty_ycore_error_destroy(command_res.error);
        return 1;
    }
    struct yetty_ycore_void_result run_res = command_res.value->run(&context);
    if (YETTY_IS_ERR(run_res)) {
        yetty_ycore_error_print(stderr, "ymux", run_res.error);
        yetty_ycore_error_destroy(run_res.error);
        return 1;
    }
    return 0;
}
