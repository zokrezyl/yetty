/* websocket-pty.c — raw shell bytes over a message-framed transport.
 *
 * The thin sibling of ytelnet/telnet-pty.c: same transport-polymorphic
 * shape (async open, output pipe registered with the event loop via
 * pipe_source, producer-side overflow ring), but no protocol state
 * machine — the server side is a real PTY, so output bytes pass
 * through untouched and the only control traffic is the resize
 * message described in websocket-pty.h.
 */

#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ytransport/conn-transport.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <yetty/ypty/websocket-pty.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct yetty_ypty_websocket_pty {
    struct yetty_platform_pty base;
    struct yetty_platform_pty_pipe_source pipe_source;

    /* Owned. Holds the endpoint binding (the ws:// URL). */
    struct yetty_ytransport_conn_transport *transport;

    /* Connection handle from on_connect; NULL until connected. */
    struct yetty_yevent_conn *conn;
    int transport_open; /* transport->open() succeeded */
    int connected;      /* on_connect fired */

    /* Output pipe — terminal-side reads via register_pty_pipe. */
    struct yetty_ycore_xthread_event_pipe *output_pipe;

    /* Terminal size, latched until the connection is up. */
    uint32_t cols;
    uint32_t rows;
    uint32_t pixel_w;
    uint32_t pixel_h;
    int have_size; /* a resize() arrived before connect — replay it */

    /* Producer-side overflow for the output pipe. Same rationale as
     * telnet-pty: producer (transport on_data) and consumer (terminal
     * pipe reader) share one thread, so pipe writes must never block.
     * Whatever the non-blocking write can't push lands here and is
     * retried on the next delivery. */
    uint8_t *tx_overflow;
    size_t tx_overflow_cap;
    size_t tx_overflow_head; /* read cursor (drain from here) */
    size_t tx_overflow_tail; /* write cursor (append here) */
};

/* ---- Output-pipe overflow ring ----------------------------------- */

/* Push the head of the overflow ring into the pipe via a single
 * non-blocking write. Returns the bytes the pipe accepted. */
static size_t websocket_pty_drain_overflow_once(struct yetty_ypty_websocket_pty *pty)
{
    if (pty->tx_overflow_tail == pty->tx_overflow_head) {
        return 0;
    }
    size_t avail = pty->tx_overflow_tail - pty->tx_overflow_head;
    struct yetty_ycore_size_result write_res = pty->output_pipe->ops->write(
        pty->output_pipe, pty->tx_overflow + pty->tx_overflow_head, avail);
    if (YETTY_IS_ERR(write_res)) {
        ywarn("websocket-pty: drain overflow: pipe write error %s", write_res.error.msg);
        yetty_ycore_error_destroy(write_res.error);
        return 0;
    }
    pty->tx_overflow_head += write_res.value;
    if (pty->tx_overflow_head == pty->tx_overflow_tail) {
        pty->tx_overflow_head = 0;
        pty->tx_overflow_tail = 0;
    }
    return write_res.value;
}

/* Append `len` bytes to the overflow ring, growing on demand. */
static void websocket_pty_overflow_append(struct yetty_ypty_websocket_pty *pty, const uint8_t *src,
                                          size_t len)
{
    if (pty->tx_overflow_head > 0) {
        size_t live = pty->tx_overflow_tail - pty->tx_overflow_head;
        memmove(pty->tx_overflow, pty->tx_overflow + pty->tx_overflow_head, live);
        pty->tx_overflow_tail = live;
        pty->tx_overflow_head = 0;
    }
    if (pty->tx_overflow_tail + len > pty->tx_overflow_cap) {
        size_t need = pty->tx_overflow_tail + len;
        size_t newcap = pty->tx_overflow_cap ? pty->tx_overflow_cap : 4096;
        while (newcap < need) {
            newcap *= 2;
        }
        uint8_t *grown = realloc(pty->tx_overflow, newcap);
        if (!grown) {
            ywarn("websocket-pty: overflow realloc to %zu bytes failed; dropping %zu bytes",
                  newcap, len);
            return;
        }
        pty->tx_overflow = grown;
        pty->tx_overflow_cap = newcap;
    }
    memcpy(pty->tx_overflow + pty->tx_overflow_tail, src, len);
    pty->tx_overflow_tail += len;
}

/* ---- Outbound control/data messages ------------------------------ */

/* Send one whole protocol message (type byte + payload). Drops (with
 * a debug note) when the connection isn't up yet. */
static int websocket_pty_send_message(struct yetty_ypty_websocket_pty *pty, uint8_t type,
                                      const uint8_t *payload, size_t payload_len)
{
    if (!pty->connected || !pty->conn || !pty->transport) {
        return -1;
    }

    /* Small messages (resize, typical keystrokes) go through a stack
     * buffer; anything bigger (large pastes) takes a transient heap
     * copy. The type byte must share the message with the payload —
     * one transport send == one WebSocket message. */
    uint8_t stack_buf[1024];
    uint8_t *message = stack_buf;
    size_t message_len = 1 + payload_len;
    if (message_len > sizeof(stack_buf)) {
        message = malloc(message_len);
        if (!message) {
            ywarn("websocket-pty: dropping %zu-byte message (oom)", message_len);
            return -1;
        }
    }
    message[0] = type;
    if (payload_len > 0) {
        memcpy(message + 1, payload, payload_len);
    }

    struct yetty_ycore_size_result send_res =
        pty->transport->ops->send(pty->transport, pty->conn, message, message_len);
    if (message != stack_buf) {
        free(message);
    }
    if (YETTY_IS_ERR(send_res)) {
        ywarn("websocket-pty: send failed: %s", send_res.error.msg);
        yetty_ycore_error_destroy(send_res.error);
        return -1;
    }
    return 0;
}

static void websocket_pty_send_resize(struct yetty_ypty_websocket_pty *pty)
{
    uint8_t payload[8];
    payload[0] = (uint8_t)((pty->cols >> 8) & 0xff);
    payload[1] = (uint8_t)(pty->cols & 0xff);
    payload[2] = (uint8_t)((pty->rows >> 8) & 0xff);
    payload[3] = (uint8_t)(pty->rows & 0xff);
    payload[4] = (uint8_t)((pty->pixel_w >> 8) & 0xff);
    payload[5] = (uint8_t)(pty->pixel_w & 0xff);
    payload[6] = (uint8_t)((pty->pixel_h >> 8) & 0xff);
    payload[7] = (uint8_t)(pty->pixel_h & 0xff);

    if (websocket_pty_send_message(pty, YETTY_YPTY_WEBSOCKET_PTY_MSG_RESIZE, payload,
                                   sizeof(payload)) == 0) {
        ydebug("websocket-pty: sent resize %ux%u (%ux%u px)", pty->cols, pty->rows, pty->pixel_w,
               pty->pixel_h);
    }
}

/* ---- Transport callbacks ----------------------------------------- */

static void websocket_pty_on_connect(void *ctx, struct yetty_yevent_conn *conn)
{
    struct yetty_ypty_websocket_pty *pty = ctx;
    pty->conn = conn;
    pty->connected = 1;
    yinfo("websocket-pty: transport connected");
    /* Replay the freshest size so the server-side PTY starts at the
     * real terminal geometry instead of the 80x24 default. */
    if (pty->have_size) {
        websocket_pty_send_resize(pty);
    }
}

static void websocket_pty_on_connect_error(void *ctx, const char *error)
{
    struct yetty_ypty_websocket_pty *pty = ctx;
    yerror("websocket-pty: transport connect failed: %s", error ? error : "(unknown)");
    pty->transport_open = 0;
}

static void websocket_pty_on_data(void *ctx, struct yetty_yevent_conn *conn, const char *data,
                                  long nread)
{
    struct yetty_ypty_websocket_pty *pty = ctx;
    (void)conn;
    if (nread <= 0) {
        return;
    }
    /* Server → client messages are raw PTY output, no type byte. */
    websocket_pty_overflow_append(pty, (const uint8_t *)data, (size_t)nread);
    (void)websocket_pty_drain_overflow_once(pty);
}

static void websocket_pty_on_disconnect(void *ctx)
{
    struct yetty_ypty_websocket_pty *pty = ctx;
    yinfo("websocket-pty: transport disconnected");
    pty->connected = 0;
    pty->conn = NULL;
}

/* ---- PTY ops ------------------------------------------------------ */

static struct yetty_ycore_size_result websocket_pty_read(struct yetty_platform_pty *self,
                                                         char *buf, size_t max_len)
{
    struct yetty_ypty_websocket_pty *pty = (struct yetty_ypty_websocket_pty *)self;

    if (max_len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    return pty->output_pipe->ops->read(pty->output_pipe, buf, max_len);
}

static struct yetty_ycore_size_result websocket_pty_write(struct yetty_platform_pty *self,
                                                          const char *data, size_t len)
{
    struct yetty_ypty_websocket_pty *pty = (struct yetty_ypty_websocket_pty *)self;

    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (!pty->connected) {
        ydebug("websocket-pty: write %zu bytes dropped (not connected)", len);
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (websocket_pty_send_message(pty, YETTY_YPTY_WEBSOCKET_PTY_MSG_INPUT, (const uint8_t *)data,
                                   len) < 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_void_result websocket_pty_resize(struct yetty_platform_pty *self,
                                                           uint32_t cols, uint32_t rows,
                                                           uint32_t pixel_w, uint32_t pixel_h)
{
    struct yetty_ypty_websocket_pty *pty = (struct yetty_ypty_websocket_pty *)self;

    pty->cols = cols;
    pty->rows = rows;
    pty->pixel_w = pixel_w;
    pty->pixel_h = pixel_h;
    pty->have_size = 1;

    if (pty->connected) {
        websocket_pty_send_resize(pty);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result websocket_pty_stop(struct yetty_platform_pty *self)
{
    struct yetty_ypty_websocket_pty *pty = (struct yetty_ypty_websocket_pty *)self;

    if (pty->transport_open && pty->transport) {
        pty->transport->ops->close(pty->transport, pty->conn);
        pty->transport_open = 0;
        pty->connected = 0;
        pty->conn = NULL;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result websocket_pty_destroy(struct yetty_platform_pty *self)
{
    struct yetty_ypty_websocket_pty *pty = (struct yetty_ypty_websocket_pty *)self;

    struct yetty_ycore_void_result stop_res = websocket_pty_stop(self);

    if (pty->output_pipe) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        pty->output_pipe = NULL;
    }
    if (pty->transport) {
        pty->transport->ops->destroy(pty->transport);
        pty->transport = NULL;
    }
    free(pty->tx_overflow);
    free(pty);

    if (YETTY_IS_ERR(stop_res)) {
        return YETTY_ERR(yetty_ycore_void, "websocket_pty_destroy: stop failed", stop_res);
    }
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *websocket_pty_pipe_source(
    struct yetty_platform_pty *self)
{
    struct yetty_ypty_websocket_pty *pty = (struct yetty_ypty_websocket_pty *)self;
    return &pty->pipe_source;
}

static const struct yetty_platform_pty_ops *websocket_pty_ops(void)
{
    static const struct yetty_platform_pty_ops ops = {
        .destroy = websocket_pty_destroy,
        .read = websocket_pty_read,
        .write = websocket_pty_write,
        .resize = websocket_pty_resize,
        .stop = websocket_pty_stop,
        .pipe_source = websocket_pty_pipe_source,
    };
    return &ops;
}

struct yetty_yplatform_pty_ptr_result yetty_ypty_websocket_pty_create(
    struct yetty_ytransport_conn_transport *transport)
{
    if (!transport || !transport->ops) {
        return YETTY_ERR(yetty_yplatform_pty_ptr, "websocket_pty_create: transport required");
    }

    struct yetty_ypty_websocket_pty *pty = calloc(1, sizeof(struct yetty_ypty_websocket_pty));
    if (!pty) {
        /* Ownership of `transport` is ours; release it on every
         * failure path so callers don't leak. */
        transport->ops->destroy(transport);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to allocate websocket pty");
    }

    pty->base.ops = websocket_pty_ops();
    pty->transport = transport;
    pty->cols = 80;
    pty->rows = 24;

    struct yetty_yplatform_input_pipe_result pipe_res = yetty_platform_input_pipe_create();
    if (!pipe_res.ok) {
        transport->ops->destroy(transport);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to create output pipe");
    }
    pty->output_pipe = pipe_res.value;

    struct yetty_ycore_int_result fd_res = pty->output_pipe->ops->read_fd(pty->output_pipe);
    if (!fd_res.ok) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        transport->ops->destroy(transport);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "failed to obtain pipe read fd");
    }
    pty->pipe_source.abstract = (uintptr_t)fd_res.value;

    /* Producer and consumer share one thread — pipe writes must not
     * block (see the overflow-ring comment above). */
    if (pty->output_pipe->ops->set_nonblocking_write) {
        struct yetty_ycore_void_result nonblocking_res =
            pty->output_pipe->ops->set_nonblocking_write(pty->output_pipe);
        if (YETTY_IS_ERR(nonblocking_res)) {
            ywarn("websocket_pty_create: set_nonblocking_write failed: %s — large "
                  "payloads may deadlock",
                  nonblocking_res.error.msg);
            yetty_ycore_error_destroy(nonblocking_res.error);
        }
    }

    struct yetty_yevent_tcp_client_callbacks callbacks = {
        .ctx = pty,
        .on_connect = websocket_pty_on_connect,
        .on_connect_error = websocket_pty_on_connect_error,
        .on_data = websocket_pty_on_data,
        .on_disconnect = websocket_pty_on_disconnect,
    };

    if (transport->ops->open(transport, &callbacks) != 0) {
        pty->output_pipe->ops->destroy(pty->output_pipe);
        transport->ops->destroy(transport);
        free(pty);
        return YETTY_ERR(yetty_yplatform_pty_ptr, "transport open failed");
    }
    pty->transport_open = 1;

    yinfo("websocket-pty: transport open (async connect in flight)");
    return YETTY_OK(yetty_yplatform_pty_ptr, &pty->base);
}
