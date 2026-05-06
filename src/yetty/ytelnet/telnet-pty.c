/* Telnet PTY — telnet protocol over a polymorphic byte transport.
 *
 * Implements:
 * - RFC 854  - Telnet protocol
 * - RFC 856  - Binary transmission
 * - RFC 858  - Suppress Go Ahead
 * - RFC 1073 - NAWS (window size)
 *
 * The protocol code (NAWS / IAC / options) is transport-agnostic.
 * The byte transport is supplied at construction time:
 *
 *   - desktop / iOS / android: a TCP transport (libuv-backed) wrapping
 *     the event loop's create_tcp_client / tcp_send / tcp_close.
 *   - webasm: an iframe-postMessage transport that ferries bytes to
 *     a slirp-injected connection inside the tinyemu wasm, allowing
 *     multiple yetty terminals to multiplex onto one in-VM telnetd.
 *
 * Connect is asynchronous: telnet_pty_create returns immediately after
 * kicking off transport->open(); the connection completes when the
 * underlying transport fires on_connect. Decoded bytes are written to
 * a yetty_yplatform_input_pipe whose read fd is registered with the
 * loop via register_pty_pipe — same path that fork-pty / conpty use.
 */

#include <yetty/platform/pty.h>
#include <yetty/platform/pty-factory.h>
#include <yetty/platform/platform-input-pipe.h>
#include <yetty/ycore/conn-transport.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/event.h>
#include <yetty/ycore/tcp-transport.h>
#include <yetty/yconfig/config.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include "telnet-pty.h"
#include "telnet-protocol.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    /* Owned. The transport encapsulates the endpoint binding (host+port
     * for TCP, sessionId for the webasm iframe transport, etc.) so we
     * don't need to know any of those details here. */
    struct yetty_yconn_transport *transport;

    /* Connection handle handed to us by transport via on_connect.
     * NULL until on_connect fires; reset to NULL on disconnect / stop. */
    struct yetty_ycore_conn *conn;
    int transport_open;          /* transport->open() succeeded */
    int connected;               /* on_connect fired ok */

    /* Decoded-output pipe — terminal-side reads via register_pty_pipe. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;

    /* Read buffer for transport on_alloc — only one read in flight at a time. */
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

/* Send raw bytes on the underlying transport. Drops if not yet connected. */
static int telnet_send_raw(struct yetty_ytelnet_telnet_pty *pty, const uint8_t *data, size_t len)
{
    if (!pty->connected || !pty->conn || !pty->transport) {
        return -1;
    }

    struct yetty_ycore_size_result r =
        pty->transport->ops->send(pty->transport, pty->conn, data, len);
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
    static int n_emit;
    n_emit++;
    if (n_emit <= 8 || (n_emit % 64) == 0) {
        ydebug("telnet: emit byte #%d 0x%02x ('%c') -> output_pipe",
               n_emit, byte, (byte >= 32 && byte < 127) ? byte : '.');
    }
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
    ydebug("telnet: on_data nread=%ld first=0x%02x last=0x%02x",
           nread, (uint8_t)data[0], (uint8_t)data[nread - 1]);
    for (long i = 0; i < nread; i++) {
        telnet_process_byte(pty, (uint8_t)data[i]);
    }
}

static void telnet_on_connect(void *ctx, struct yetty_ycore_conn *conn)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    pty->conn = conn;
    pty->connected = 1;

    yinfo("telnet: transport connected");
    /* Window size is shipped via NAWS once the server negotiates it
     * (DO NAWS in the WILL/DO exchange below — see telnet_handle_do). */
}

static void telnet_on_connect_error(void *ctx, const char *error)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    yerror("telnet: transport connect failed: %s", error ? error : "(unknown)");
    pty->transport_open = 0;
}

static void telnet_on_disconnect(void *ctx)
{
    struct yetty_ytelnet_telnet_pty *pty = ctx;
    yinfo("telnet: transport disconnected");
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
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result telnet_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_ytelnet_telnet_pty *pty = (struct yetty_ytelnet_telnet_pty *)self;

    if (pty->transport_open && pty->transport) {
        pty->transport->ops->close(pty->transport, pty->conn);
        pty->transport_open = 0;
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

    if (pty->transport) {
        pty->transport->ops->destroy(pty->transport);
        pty->transport = NULL;
    }

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

struct yetty_yplatform_pty_result yetty_ytelnet_telnet_pty_create(
    struct yetty_yconn_transport *transport)
{
    if (!transport || !transport->ops) {
        return YETTY_ERR(yetty_yplatform_pty, "telnet_pty_create: transport required");
    }

    struct yetty_ytelnet_telnet_pty *pty = calloc(1, sizeof(struct yetty_ytelnet_telnet_pty));
    if (!pty) {
        /* The caller passed ownership of `transport` to us; free it on
         * failure paths so they don't leak on every error return. */
        transport->ops->destroy(transport);
        return YETTY_ERR(yetty_yplatform_pty, "failed to allocate telnet pty");
    }

    pty->base.ops = &telnet_pty_ops;
    pty->transport = transport;
    pty->cols = 80;
    pty->rows = 24;
    pty->state = YETTY_YTELNET_STATE_DATA;

    /* Decoded-output pipe — terminal reads its fd via register_pty_pipe. */
    struct yetty_yplatform_input_pipe_result pr = yetty_platform_input_pipe_create();
    if (!pr.ok) {
        transport->ops->destroy(transport);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to create output pipe");
    }
    pty->output_pipe = pr.value;

    struct yetty_ycore_int_result fdr = pty->output_pipe->ops->read_fd(pty->output_pipe);
    if (!fdr.ok) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        transport->ops->destroy(transport);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "failed to obtain pipe read fd");
    }
    pty->pipe_source.abstract = (uintptr_t)fdr.value;

    /* Kick off async transport open. on_connect / on_connect_error will
     * fire later (loop-thread under TCP, postMessage-handler thread
     * under iframe-transport — both single-threaded relative to the
     * pty); we return success now and the terminal registers the pipe
     * and renders an empty screen until data arrives. */
    struct yetty_ycore_client_callbacks callbacks = {
        .ctx = pty,
        .on_connect = telnet_on_connect,
        .on_connect_error = telnet_on_connect_error,
        .on_alloc = telnet_on_alloc,
        .on_data = telnet_on_data,
        .on_disconnect = telnet_on_disconnect,
    };

    if (transport->ops->open(transport, &callbacks) != 0) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        transport->ops->destroy(transport);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty, "transport open failed");
    }
    pty->transport_open = 1;

    yinfo("telnet: transport open (async connect in flight)");
    return YETTY_OK(yetty_yplatform_pty, &pty->base);
}

struct yetty_yplatform_pty_result yetty_ytelnet_telnet_pty_create_tcp(
    const char *host, uint16_t port,
    struct yetty_yplatform_event_loop *event_loop)
{
    struct yetty_yconn_transport *transport =
        yetty_ycore_tcp_transport_create(host, port, event_loop);
    if (!transport) {
        return YETTY_ERR(yetty_yplatform_pty,
                         "telnet_pty_create_tcp: tcp_transport_create failed");
    }
    return yetty_ytelnet_telnet_pty_create(transport);
}

/* Convenience factory — desktop telnet path. Mints a TCP transport
 * from (host, port, event_loop) per create_pty call, then hands it to
 * the transport-polymorphic yetty_ytelnet_telnet_pty_create. The
 * webasm telnet path will skip this factory entirely — its
 * platform-specific factory builds an iframe-transport instead and
 * calls telnet_pty_create directly. */

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

    struct yetty_yconn_transport *transport =
        yetty_ycore_tcp_transport_create(factory->host, factory->port, event_loop);
    if (!transport) {
        return YETTY_ERR(yetty_yplatform_pty,
                         "telnet_pty_factory_create_pty: tcp_transport_create failed");
    }
    /* Ownership of `transport` is transferred to telnet_pty_create —
     * it destroys on every failure path, so no leak here either way. */
    return yetty_ytelnet_telnet_pty_create(transport);
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
