/* Telnet PTY - TCP/Telnet as PTY backend
 *
 * Provides a PTY interface over TCP using telnet protocol.
 * Used for connecting to QEMU or other telnet servers.
 *
 * Implements:
 * - RFC 854  - Telnet protocol
 * - RFC 856  - Binary transmission
 * - RFC 858  - Suppress Go Ahead
 * - RFC 1073 - NAWS (window size)
 *
 * Networking is driven entirely by libuv via the platform event loop's
 * TCP client API (create_tcp_client / tcp_send / tcp_close). Connect is
 * asynchronous: telnet_pty_create returns immediately and on_connect
 * fires later on the loop thread. Decoded bytes are written to a
 * yetty_yplatform_input_pipe whose read fd is registered with the loop
 * via register_pty_pipe — same path that fork-pty / conpty use.
 */

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/platform/platform-input-pipe.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/event.h>
#include <yetty/yconfig.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace.h>

#include "telnet-protocol.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 3s grace after connect before injecting `stty cols X rows Y` for servers
 * that don't speak NAWS (QEMU's telnet chardev). Long enough for the guest
 * kernel to boot and the shell to print its prompt. */
#define TELNET_NAWS_FALLBACK_MS 3000

/* Telnet protocol state machine */
enum yetty_ytelnet_telnet_state {
    YETTY_YTELNET_STATE_DATA,   /* Normal data */
    YETTY_YTELNET_STATE_IAC,    /* Received IAC */
    YETTY_YTELNET_STATE_WILL,   /* Received IAC WILL */
    YETTY_YTELNET_STATE_WONT,   /* Received IAC WONT */
    YETTY_YTELNET_STATE_DO,     /* Received IAC DO */
    YETTY_YTELNET_STATE_DONT,   /* Received IAC DONT */
    YETTY_YTELNET_STATE_SB,     /* In subnegotiation */
    YETTY_YTELNET_STATE_SB_IAC, /* IAC in subnegotiation */
};

struct yetty_ytelnet_telnet_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    /* Loop + libuv handles */
    struct yetty_yplatform_event_loop *event_loop;
    yetty_ycore_tcp_client_id tcp_client_id;
    struct yetty_ycore_conn *conn; /* set in on_connect */
    int tcp_client_active;       /* create_tcp_client succeeded */
    int connected;               /* on_connect fired ok */

    /* NAWS-fallback timer (one-shot, started in on_connect) */
    yetty_ycore_timer_id naws_timer_id;
    int naws_timer_active;
    struct yetty_ycore_event_listener naws_listener;

    /* Endpoint */
    char *host;
    uint16_t port;

    /* Decoded-output pipe — terminal-side reads via register_pty_pipe. */
    struct yetty_ycore_input_pipe *output_pipe;

    /* Read buffer for libuv on_alloc — only one read in flight at a time. */
    char read_buf[65536];

    /* Terminal size (latest known, latched until we can ship it) */
    uint32_t cols;
    uint32_t rows;

    /* Telnet decoder state */
    enum yetty_ytelnet_telnet_state state;
    uint8_t subneg_buf[256];
    size_t subneg_len;

    /* Negotiated options */
    int naws_enabled;
    int binary_enabled;
    int sga_enabled;

    /* QEMU's telnet chardev doesn't speak NAWS. Once the timer fires we
     * inject `stty cols X rows Y\r` directly to the guest shell.
     * stty_initial_sent flips on first inject; subsequent resizes inject
     * immediately. */
    int stty_initial_sent;
};

/* Forward declarations */
static struct yetty_ycore_void_result telnet_pty_destroy(struct yetty_platform_pty *self);
static struct yetty_ycore_size_result telnet_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len);
static struct yetty_ycore_size_result telnet_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len);
static struct yetty_ycore_void_result telnet_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows);
static struct yetty_ycore_void_result telnet_pty_stop(struct yetty_platform_pty *self);
static struct yetty_platform_pty_pipe_source *telnet_pty_pipe_source(
    struct yetty_platform_pty *self);

static const struct yetty_platform_pty_ops telnet_pty_ops = {
    .destroy = telnet_pty_destroy,
    .read = telnet_pty_read,
    .write = telnet_pty_write,
    .resize = telnet_pty_resize,
    .stop = telnet_pty_stop,
    .pipe_source = telnet_pty_pipe_source,
};

/* Send raw bytes on the libuv TCP connection. Drops if not yet connected. */
static int telnet_send_raw(struct yetty_ytelnet_telnet_pty *pty, const uint8_t *data, size_t len)
{
    if (!pty->connected || !pty->conn) {
        return -1;
    }

    struct yetty_ycore_size_result r = pty->event_loop->ops->tcp_send(pty->conn, data, len);
    if (!r.ok) {
        return -1;
    }
    return 0;
}

static void telnet_send_cmd(struct yetty_ytelnet_telnet_pty *pty, uint8_t cmd, uint8_t opt)
{
    uint8_t buf[3] = {TELNET_IAC, cmd, opt};
    telnet_send_raw(pty, buf, 3);
}

/* Inject `stty cols X rows Y\r` into the guest shell — NAWS fallback. */
static void telnet_inject_stty(struct yetty_ytelnet_telnet_pty *pty)
{
    if (pty->cols == 0 || pty->rows == 0) {
        return;
    }
    char cmd[80];
    int n = snprintf(cmd, sizeof(cmd), "\rstty cols %u rows %u\r", pty->cols, pty->rows);
    if (n > 0 && (size_t)n < sizeof(cmd)) {
        telnet_send_raw(pty, (const uint8_t *)cmd, (size_t)n);
    }
    yinfo("telnet: injected stty cols %u rows %u (NAWS fallback)", pty->cols, pty->rows);
}

static void telnet_send_naws(struct yetty_ytelnet_telnet_pty *pty)
{
    if (!pty->naws_enabled) {
        return;
    }

    uint8_t buf[9];
    buf[0] = TELNET_IAC;
    buf[1] = TELNET_SB;
    buf[2] = TELOPT_NAWS;
    buf[3] = (pty->cols >> 8) & 0xff;
    buf[4] = pty->cols & 0xff;
    buf[5] = (pty->rows >> 8) & 0xff;
    buf[6] = pty->rows & 0xff;
    buf[7] = TELNET_IAC;
    buf[8] = TELNET_SE;

    telnet_send_raw(pty, buf, 9);
    ydebug("telnet: sent NAWS %ux%u", pty->cols, pty->rows);
}

static void telnet_handle_will(struct yetty_ytelnet_telnet_pty *pty, uint8_t opt)
{
    yinfo("telnet: received WILL %u", (unsigned)opt);
    switch (opt) {
    case TELOPT_ECHO:
        telnet_send_cmd(pty, TELNET_DO, opt);
        break;
    case TELOPT_SGA:
        telnet_send_cmd(pty, TELNET_DO, opt);
        pty->sga_enabled = 1;
        break;
    case TELOPT_BINARY:
        telnet_send_cmd(pty, TELNET_DO, opt);
        pty->binary_enabled = 1;
        break;
    default:
        telnet_send_cmd(pty, TELNET_DONT, opt);
        break;
    }
}

static void telnet_handle_do(struct yetty_ytelnet_telnet_pty *pty, uint8_t opt)
{
    yinfo("telnet: received DO %u", (unsigned)opt);
    switch (opt) {
    case TELOPT_NAWS:
        telnet_send_cmd(pty, TELNET_WILL, opt);
        pty->naws_enabled = 1;
        telnet_send_naws(pty);
        break;
    case TELOPT_TTYPE:
        telnet_send_cmd(pty, TELNET_WILL, opt);
        break;
    case TELOPT_BINARY:
        telnet_send_cmd(pty, TELNET_WILL, opt);
        pty->binary_enabled = 1;
        break;
    case TELOPT_SGA:
        telnet_send_cmd(pty, TELNET_WILL, opt);
        pty->sga_enabled = 1;
        break;
    default:
        telnet_send_cmd(pty, TELNET_WONT, opt);
        break;
    }
}

static void telnet_handle_subneg(struct yetty_ytelnet_telnet_pty *pty)
{
    if (pty->subneg_len < 1) {
        return;
    }

    uint8_t opt = pty->subneg_buf[0];

    if (opt == TELOPT_TTYPE && pty->subneg_len >= 2 && pty->subneg_buf[1] == TTYPE_SEND) {
        const char *ttype = "xterm-256color";
        size_t tlen = strlen(ttype);
        uint8_t buf[64];
        size_t i = 0;

        buf[i++] = TELNET_IAC;
        buf[i++] = TELNET_SB;
        buf[i++] = TELOPT_TTYPE;
        buf[i++] = TTYPE_IS;
        memcpy(buf + i, ttype, tlen);
        i += tlen;
        buf[i++] = TELNET_IAC;
        buf[i++] = TELNET_SE;

        telnet_send_raw(pty, buf, i);
        ydebug("telnet: sent TTYPE %s", ttype);
    }
}

static void telnet_emit_byte(struct yetty_ytelnet_telnet_pty *pty, uint8_t byte)
{
    pty->output_pipe->ops->write(pty->output_pipe, &byte, 1);
}

static void telnet_process_byte(struct yetty_ytelnet_telnet_pty *pty, uint8_t byte)
{
    switch (pty->state) {
    case YETTY_YTELNET_STATE_DATA:
        if (byte == TELNET_IAC) {
            pty->state = YETTY_YTELNET_STATE_IAC;
        } else {
            telnet_emit_byte(pty, byte);
        }
        break;

    case YETTY_YTELNET_STATE_IAC:
        switch (byte) {
        case TELNET_IAC:
            telnet_emit_byte(pty, byte);
            pty->state = YETTY_YTELNET_STATE_DATA;
            break;
        case TELNET_WILL:
            pty->state = YETTY_YTELNET_STATE_WILL;
            break;
        case TELNET_WONT:
            pty->state = YETTY_YTELNET_STATE_WONT;
            break;
        case TELNET_DO:
            pty->state = YETTY_YTELNET_STATE_DO;
            break;
        case TELNET_DONT:
            pty->state = YETTY_YTELNET_STATE_DONT;
            break;
        case TELNET_SB:
            pty->state = YETTY_YTELNET_STATE_SB;
            pty->subneg_len = 0;
            break;
        default:
            pty->state = YETTY_YTELNET_STATE_DATA;
            break;
        }
        break;

    case YETTY_YTELNET_STATE_WILL:
        telnet_handle_will(pty, byte);
        pty->state = YETTY_YTELNET_STATE_DATA;
        break;

    case YETTY_YTELNET_STATE_WONT:
        pty->state = YETTY_YTELNET_STATE_DATA;
        break;

    case YETTY_YTELNET_STATE_DO:
        telnet_handle_do(pty, byte);
        pty->state = YETTY_YTELNET_STATE_DATA;
        break;

    case YETTY_YTELNET_STATE_DONT:
        telnet_send_cmd(pty, TELNET_WONT, byte);
        pty->state = YETTY_YTELNET_STATE_DATA;
        break;

    case YETTY_YTELNET_STATE_SB:
        if (byte == TELNET_IAC) {
            pty->state = YETTY_YTELNET_STATE_SB_IAC;
        } else if (pty->subneg_len < sizeof(pty->subneg_buf)) {
            pty->subneg_buf[pty->subneg_len++] = byte;
        }
        break;

    case YETTY_YTELNET_STATE_SB_IAC:
        if (byte == TELNET_SE) {
            telnet_handle_subneg(pty);
            pty->state = YETTY_YTELNET_STATE_DATA;
        } else if (byte == TELNET_IAC) {
            if (pty->subneg_len < sizeof(pty->subneg_buf)) {
                pty->subneg_buf[pty->subneg_len++] = TELNET_IAC;
            }
            pty->state = YETTY_YTELNET_STATE_SB;
        } else {
            pty->state = YETTY_YTELNET_STATE_DATA;
        }
        break;
    }
}

/* libuv TCP client callbacks (all run on the loop thread) */

static void telnet_on_alloc(void *ctx, size_t suggested, char **buf, size_t *len)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    (void)suggested;
    *buf = pty->read_buf;
    *len = sizeof(pty->read_buf);
}

static void telnet_on_data(void *ctx, struct yetty_ycore_conn *conn, const char *data, long nread)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    (void)conn;
    if (nread <= 0) {
        return;
    }
    for (long i = 0; i < nread; i++) {
        telnet_process_byte(pty, (uint8_t)data[i]);
    }
}

static struct yetty_ycore_int_result telnet_naws_timer_handler(
    struct yetty_ycore_event_listener *listener, const struct yetty_yui_event *event)
{
    (void)event;
    struct yetty_ytelnet_telnet_pty *pty =
        (struct yetty_ytelnet_telnet_pty *)((char *)listener - offsetof(struct yetty_ytelnet_telnet_pty, naws_listener));

    /* One-shot: stop ourselves regardless of NAWS state. */
    if (pty->naws_timer_active) {
        pty->event_loop->ops->stop_timer(pty->event_loop, pty->naws_timer_id);
        pty->naws_timer_active = 0;
    }

    if (!pty->naws_enabled && pty->connected) {
        telnet_inject_stty(pty);
        pty->stty_initial_sent = 1;
    }

    return YETTY_OK(yetty_ycore_int, 1);
}

static void telnet_on_connect(void *ctx, struct yetty_ycore_conn *conn)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    pty->conn = conn;
    pty->connected = 1;

    yinfo("telnet: connected to %s:%u", pty->host, pty->port);

    /* Originally we proactively offered WILL NAWS here. We've dropped that
     * because the qemu chardev is now a *raw* TCP socket (telnet=on
     * segfaults in our MSYS2 CLANG64 build of QEMU 11.0.0-rc4). On a raw
     * socket, the IAC bytes don't get parsed off — they arrive at the
     * guest's virtio-console as input garbage. The NAWS-fallback timer
     * below injects `stty cols X rows Y` after the boot grace period,
     * which is what actually configures the guest tty. */

    /* Arm the one-shot fallback timer. config_timer + start_timer set up
     * a periodic timer; the handler stops it after the first fire. */
    struct yetty_ycore_void_result vr = pty->event_loop->ops->config_timer(
        pty->event_loop, pty->naws_timer_id, TELNET_NAWS_FALLBACK_MS);
    if (vr.ok) {
        vr = pty->event_loop->ops->start_timer(pty->event_loop, pty->naws_timer_id);
        if (vr.ok) {
            pty->naws_timer_active = 1;
        } else {
            yerror("telnet: start_timer failed: %s", vr.error.msg);
        }
    } else {
        yerror("telnet: config_timer failed: %s", vr.error.msg);
    }
}

static void telnet_on_connect_error(void *ctx, const char *error)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    yerror("telnet: connect to %s:%u failed: %s", pty->host, pty->port,
           error ? error : "(unknown)");
    pty->tcp_client_active = 0;
}

static void telnet_on_disconnect(void *ctx)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    yinfo("telnet: disconnected from %s:%u", pty->host, pty->port);
    pty->connected = 0;
    pty->conn = NULL;
}

/* PTY ops */

static struct yetty_ycore_size_result telnet_pty_read(struct yetty_platform_pty *self, char *buf,
                                                      size_t max_len)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

    if (max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    return pty->output_pipe->ops->read(pty->output_pipe, buf, max_len);
}

static struct yetty_ycore_size_result telnet_pty_write(struct yetty_platform_pty *self,
                                                       const char *data, size_t len)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    if (!pty->connected) {
        ydebug("telnet: write %zu bytes dropped (not connected)", len);
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Escape IAC bytes inline — bounded by 2x worst case. */
    uint8_t buf[4096];
    size_t j = 0;
    for (size_t i = 0; i < len && j + 1 < sizeof(buf); i++) {
        uint8_t c = (uint8_t)data[i];
        if (c == TELNET_IAC) {
            buf[j++] = TELNET_IAC;
            buf[j++] = TELNET_IAC;
        } else {
            buf[j++] = c;
        }
    }

    if (telnet_send_raw(pty, buf, j) < 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result telnet_pty_resize(struct yetty_platform_pty *self,
                                                        uint32_t cols, uint32_t rows)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

    pty->cols = cols;
    pty->rows = rows;

    if (!pty->connected) {
        return YETTY_OK_VOID();
    }

    if (pty->naws_enabled) {
        telnet_send_naws(pty);
    } else if (pty->stty_initial_sent) {
        telnet_inject_stty(pty);
    }
    /* else: NAWS-fallback timer is still pending — first inject happens there. */

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result telnet_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

    if (pty->naws_timer_active) {
        pty->event_loop->ops->stop_timer(pty->event_loop, pty->naws_timer_id);
        pty->naws_timer_active = 0;
    }

    if (pty->tcp_client_active) {
        pty->event_loop->ops->stop_tcp_client(pty->event_loop, pty->tcp_client_id);
        pty->tcp_client_active = 0;
        pty->connected = 0;
        pty->conn = NULL;
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result telnet_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

    struct yetty_ycore_void_result stop_r = telnet_pty_stop(self);

    /* destroy_timer is independent of stop_timer — close the uv_timer_t. */
    pty->event_loop->ops->destroy_timer(pty->event_loop, pty->naws_timer_id);

    if (pty->output_pipe) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        pty->output_pipe = NULL;
    }

    free(pty->host);
    free(pty);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "telnet_pty_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *telnet_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;
    return &pty->pipe_source;
}

struct yetty_yplatform_pty_result yetty_ytelnet_telnet_pty_create(const char *host, uint16_t port,
                                                    struct yetty_yplatform_event_loop *event_loop)
{
    if (!event_loop || !event_loop->ops) {
        return YETTY_ERR(yetty_yplatform_pty, "telnet_pty_create: event_loop required");
    }

    struct yetty_ytelnet_telnet_pty *pty = calloc(1, sizeof(struct yetty_ytelnet_telnet_pty));
    if (!pty) {
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate telnet pty");
    }

    pty->base.ops = &telnet_pty_ops;
    pty->event_loop = event_loop;
    pty->cols = 80;
    pty->rows = 24;
    pty->state = YETTY_YTELNET_STATE_DATA;
    pty->naws_listener.handler = telnet_naws_timer_handler;

    pty->host = strdup(host);
    pty->port = port;
    if (!pty->host) {
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate host string");
    }

    /* Decoded-output pipe — terminal reads its fd via register_pty_pipe. */
    struct yetty_yplatform_input_pipe_result pr = yetty_platform_input_pipe_create();
    if (!pr.ok) {
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to create output pipe");
    }
    pty->output_pipe = pr.value;

    struct yetty_ycore_int_result fdr = pty->output_pipe->ops->read_fd(pty->output_pipe);
    if (!fdr.ok) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to obtain pipe read fd");
    }
    pty->pipe_source.abstract = (uintptr_t)fdr.value;

    /* NAWS-fallback timer (created up front, started in on_connect). */
    struct yetty_ycore_timer_id_result tres = event_loop->ops->create_timer(event_loop);
    if (!tres.ok) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "create_timer failed");
    }
    pty->naws_timer_id = tres.value;

    struct yetty_ycore_void_result vres = event_loop->ops->register_timer_listener(
        event_loop, pty->naws_timer_id, &pty->naws_listener);
    if (!vres.ok) {
        event_loop->ops->destroy_timer(event_loop, pty->naws_timer_id);
        pty->output_pipe->ops->destroy(pty->output_pipe);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "register_timer_listener failed");
    }

    /* Kick off async TCP connect. on_connect / on_connect_error will fire
     * later on the loop thread; we return success now and the terminal
     * registers the pipe and renders an empty screen until data arrives. */
    struct yetty_ycore_client_callbacks callbacks = {
        .ctx = pty,
        .on_connect = telnet_on_connect,
        .on_connect_error = telnet_on_connect_error,
        .on_alloc = telnet_on_alloc,
        .on_data = telnet_on_data,
        .on_disconnect = telnet_on_disconnect,
    };

    struct yetty_ycore_tcp_client_id_result cres =
        event_loop->ops->create_tcp_client(event_loop, host, (int)port, &callbacks);
    if (!cres.ok) {
        event_loop->ops->destroy_timer(event_loop, pty->naws_timer_id);
        pty->output_pipe->ops->destroy(pty->output_pipe);
        free(pty->host);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "create_tcp_client failed");
    }
    pty->tcp_client_id = cres.value;
    pty->tcp_client_active = 1;

    yinfo("telnet: connecting to %s:%u (async)", host, port);
    return YETTY_OK(yetty_yplatform_pty, &pty->base);
}

/* Factory */

struct yetty_ytelnet_telnet_pty_factory {
    struct yetty_platform_pty_factory base;
    char *host;
    uint16_t port;
};

static void telnet_pty_factory_destroy(struct yetty_platform_pty_factory *self)
{
    struct yetty_ytelnet_telnet_pty_factory *factory = (struct yetty_ytelnet_telnet_pty_factory *)self;
    free(factory->host);
    free(factory);
}

static struct yetty_yplatform_pty_result telnet_pty_factory_create_pty(
    struct yetty_platform_pty_factory *self, struct yetty_yplatform_event_loop *event_loop)
{
    struct yetty_ytelnet_telnet_pty_factory *factory = (struct yetty_ytelnet_telnet_pty_factory *)self;
    return yetty_ytelnet_telnet_pty_create(factory->host, factory->port, event_loop);
}

static const struct yetty_platform_pty_factory_ops telnet_pty_factory_ops = {
    .destroy = telnet_pty_factory_destroy,
    .create_pty = telnet_pty_factory_create_pty,
};

struct yetty_yplatform_pty_factory_result yetty_ytelnet_telnet_pty_factory_create(const char *host, uint16_t port)
{
    struct yetty_ytelnet_telnet_pty_factory *factory;

    factory = calloc(1, sizeof(struct yetty_ytelnet_telnet_pty_factory));
    if (!factory) {
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate telnet pty factory");
    }

    factory->base.ops = &telnet_pty_factory_ops;
    factory->host = strdup(host);
    factory->port = port;

    if (!factory->host) {
        free(factory);
        return YETTY_ERR(yetty_yplatform_pty_factory, "failed to allocate host string");
    }

    return YETTY_OK(yetty_yplatform_pty_factory, &factory->base);
}
