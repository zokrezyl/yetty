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
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include "telnet-protocol.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

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

    /* Endpoint */
    char *host;
    uint16_t port;

    /* Decoded-output pipe — terminal-side reads via register_pty_pipe. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;

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

static void telnet_on_connect(void *ctx, struct yetty_ycore_conn *conn)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    pty->conn = conn;
    pty->connected = 1;

    yinfo("telnet: connected to %s:%u", pty->host, pty->port);
    /* Window size is shipped via NAWS once the server negotiates it
     * (DO NAWS in the WILL/DO exchange below — see telnet_handle_do). */
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

    /* iOS sandbox swallows stderr so ydebug isn't observable; mirror to
     * the same input log the iOS keyboard tracer writes to. */
    {
        const char *home = getenv("HOME");
        char path[1024];
        snprintf(path, sizeof(path), "%s/tmp/yetty-input.log", home ? home : ".");
        int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (fd >= 0) {
            char buf[256];
            int n = snprintf(buf, sizeof(buf),
                             "%ld telnet_pty_write len=%zu connected=%d byte0=0x%02x\n",
                             (long)time(NULL), len, pty->connected,
                             len > 0 ? (unsigned char)data[0] : 0u);
            write(fd, buf, n);
            close(fd);
        }
    }

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
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result telnet_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

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
    struct yetty_yplatform_pty_factory base;
    char *host;
    uint16_t port;
};

static void telnet_pty_factory_destroy(struct yetty_yplatform_pty_factory *self)
{
    struct yetty_ytelnet_telnet_pty_factory *factory = (struct yetty_ytelnet_telnet_pty_factory *)self;
    free(factory->host);
    free(factory);
}

static struct yetty_yplatform_pty_result telnet_pty_factory_create_pty(
    struct yetty_yplatform_pty_factory *self, struct yetty_yplatform_event_loop *event_loop)
{
    struct yetty_ytelnet_telnet_pty_factory *factory = (struct yetty_ytelnet_telnet_pty_factory *)self;
    return yetty_ytelnet_telnet_pty_create(factory->host, factory->port, event_loop);
}

static const struct yetty_yplatform_pty_factory_ops telnet_pty_factory_ops = {
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
