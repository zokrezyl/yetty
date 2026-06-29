/*
 * connection.c — the multiplexed wire link (see connection.h).
 *
 * Owns the reactor-capable transport + one wire_statemachine for inbound demux
 * + the channel array. pump_readable() reads the fd and drives the state
 * machine, whose buffered handlers route each decoded envelope to the right
 * channel (rpc bytes buffered for the transport adapter; input events fired at
 * the input sink; raw runs at the raw sink). Outbound framing is done by the
 * channels (channel.c) through this connection's transport.
 */

#include <yetty/ywire/connection.h>

#include "connection-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/wire-statemachine.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <poll.h>
#include <sys/ioctl.h>
#endif

#define YWIRE_PUMP_TIMEOUT_MS 5000

/*===========================================================================
 * Inbound demux handlers (fired by the state machine during process())
 *=========================================================================*/

static struct yetty_ycore_void_result append_inbuf(struct yetty_ywire_channel *channel,
                                                   const uint8_t *data, size_t len)
{
    /* Reclaim space once everything that landed earlier has been consumed. */
    if (channel->inbuf_off >= channel->inbuf.size) {
        yetty_ycore_buffer_clear(&channel->inbuf);
        channel->inbuf_off = 0;
    }
    return yetty_ycore_buffer_write(&channel->inbuf, data, len);
}

/* DCS YETTY_DCS_YCLASS_RPC → rpc channel inbuf (drained by the transport
 * adapter's recv). */
static struct yetty_ycore_void_result on_rpc_envelope(void *userdata,
                                                      enum yetty_ywire_envelope_kind kind, int code,
                                                      const uint8_t *args, size_t args_len,
                                                      const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)code;
    (void)args;
    (void)args_len;
    struct yetty_ywire_connection *connection = userdata;
    return append_inbuf(connection->chan_rpc, payload, payload_len);
}

/* OSC YETTY_OSC_SC_CLIENT_INPUT_* → input channel sink (mouse/key/resize). With
 * no sink the event is dropped (input is consumed via the sink in practice). */
static struct yetty_ycore_void_result on_input_envelope(void *userdata,
                                                        enum yetty_ywire_envelope_kind kind,
                                                        int code, const uint8_t *args,
                                                        size_t args_len, const uint8_t *payload,
                                                        size_t payload_len)
{
    (void)kind;
    struct yetty_ywire_connection *connection = userdata;
    struct yetty_ywire_channel *channel = connection->chan_input;
    if (channel->env_sink) {
        channel->env_sink(channel->sink_user, code, args, args_len, payload, payload_len);
    }
    return YETTY_OK_VOID();
}

/* Raw bytes outside any envelope → raw channel: real keystrokes / child output.
 * Fire the raw sink, or buffer for a pull consumer when none is set. */
static struct yetty_ycore_void_result on_raw_bytes(void *userdata, const uint8_t *bytes, size_t n)
{
    struct yetty_ywire_connection *connection = userdata;
    struct yetty_ywire_channel *channel = connection->chan_raw;
    if (channel->raw_sink) {
        channel->raw_sink(channel->sink_user, bytes, n);
        return YETTY_OK_VOID();
    }
    return append_inbuf(channel, bytes, n);
}

/*===========================================================================
 * Create / destroy
 *=========================================================================*/

static void init_channel(struct yetty_ywire_channel *channel,
                         struct yetty_ywire_connection *connection, uint32_t id,
                         enum yetty_ywire_channel_kind kind, int wire_kind, int wire_code,
                         int has_args)
{
    channel->connection = connection;
    channel->id = id;
    channel->kind = kind;
    channel->wire_kind = wire_kind;
    channel->wire_code = wire_code;
    channel->has_args = has_args;
    channel->in_use = 1;
}

struct yetty_ywire_connection_ptr_result yetty_ywire_connection_create(
    struct yetty_yclass_transport_reactor reactor, int compressed)
{
    if (!reactor.ops) {
        return YETTY_ERR(yetty_ywire_connection_ptr, "ywire_connection_create: NULL reactor ops");
    }
    struct yetty_ywire_connection *connection = calloc(1, sizeof(*connection));
    if (!connection) {
        return YETTY_ERR(yetty_ywire_connection_ptr, "ywire_connection_create: calloc");
    }
    connection->reactor = reactor;
    connection->compressed = compressed ? 1 : 0;

    /* NULL pty — bytes are pushed in via wire_statemachine_feed from pump. */
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    if (YETTY_IS_ERR(sm_res)) {
        free(connection);
        return YETTY_ERR(yetty_ywire_connection_ptr, "ywire_connection_create: sm_create", sm_res);
    }
    connection->sm = sm_res.value;

    connection->chan_rpc = &connection->channels[0];
    connection->chan_input = &connection->channels[1];
    connection->chan_raw = &connection->channels[2];
    init_channel(connection->chan_rpc, connection, YETTY_YWIRE_CHANNEL_RPC,
                 YETTY_YWIRE_CHANNEL_KIND_RPC, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YCLASS_RPC,
                 /*has_args=*/0);
    init_channel(connection->chan_input, connection, YETTY_YWIRE_CHANNEL_INPUT,
                 YETTY_YWIRE_CHANNEL_KIND_INPUT, YETTY_YWIRE_ENVELOPE_OSC, /*wire_code=*/-1,
                 /*has_args=*/1);
    init_channel(connection->chan_raw, connection, YETTY_YWIRE_CHANNEL_RAW,
                 YETTY_YWIRE_CHANNEL_KIND_RAW, /*wire_kind=*/0, /*wire_code=*/-1, /*has_args=*/0);
    connection->channel_count = 3;

#define YWIRE_CONNECTION_FAIL(msg, res)                                                            \
    do {                                                                                           \
        struct yetty_ycore_void_result destroy_res =                                               \
            yetty_ywire_wire_statemachine_destroy(connection->sm);                                 \
        if (YETTY_IS_ERR(destroy_res)) {                                                           \
            yetty_ycore_error_destroy(destroy_res.error);                                          \
        }                                                                                          \
        free(connection);                                                                          \
        return YETTY_ERR(yetty_ywire_connection_ptr, msg, res);                                    \
    } while (0)

    /* rpc lane: one DCS code, no args slot. */
    struct yetty_ycore_void_result rpc_reg = yetty_ywire_wire_statemachine_register_buffered(
        connection->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YCLASS_RPC, /*has_args=*/0,
        on_rpc_envelope, connection);
    if (YETTY_IS_ERR(rpc_reg)) {
        YWIRE_CONNECTION_FAIL("ywire_connection_create: register rpc", rpc_reg);
    }

    /* input lane: every client-input OSC code (figure-tagged + pane-wide). The
     * host always emits these with an (empty) args slot via yface, so has_args=1. */
    static const int input_codes[] = {
        YETTY_OSC_SC_CLIENT_INPUT_FIGURE_MOUSE, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE,
        YETTY_OSC_SC_CLIENT_INPUT_FIGURE_FOCUS, YETTY_OSC_SC_CLIENT_INPUT_FIGURE_KEY,
        YETTY_OSC_SC_CLIENT_INPUT_MOUSE,        YETTY_OSC_SC_CLIENT_INPUT_RESIZE,
        YETTY_OSC_SC_CLIENT_INPUT_KEY,
    };
    for (size_t i = 0; i < sizeof(input_codes) / sizeof(input_codes[0]); i++) {
        struct yetty_ycore_void_result input_reg = yetty_ywire_wire_statemachine_register_buffered(
            connection->sm, YETTY_YWIRE_ENVELOPE_OSC, input_codes[i], /*has_args=*/1,
            on_input_envelope, connection);
        if (YETTY_IS_ERR(input_reg)) {
            YWIRE_CONNECTION_FAIL("ywire_connection_create: register input", input_reg);
        }
    }

    /* raw lane: the default sink. */
    struct yetty_ycore_void_result raw_reg = yetty_ywire_wire_statemachine_set_default_buffered(
        connection->sm, on_raw_bytes, connection);
    if (YETTY_IS_ERR(raw_reg)) {
        YWIRE_CONNECTION_FAIL("ywire_connection_create: set raw default", raw_reg);
    }
#undef YWIRE_CONNECTION_FAIL

    return YETTY_OK(yetty_ywire_connection_ptr, connection);
}

struct yetty_ycore_void_result yetty_ywire_connection_destroy(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    if (connection->sm) {
        struct yetty_ycore_void_result sm_res =
            yetty_ywire_wire_statemachine_destroy(connection->sm);
        if (YETTY_IS_ERR(sm_res)) {
            result = YETTY_ERR(yetty_ycore_void, "ywire_connection_destroy: sm", sm_res);
        }
    }
    for (size_t i = 0; i < connection->channel_count; i++) {
        yetty_ycore_buffer_destroy(&connection->channels[i].inbuf);
        yetty_ycore_buffer_destroy(&connection->channels[i].outbuf);
    }
    /* reactor is borrowed — the creator owns and destroys the transport. */
    free(connection);
    return result;
}

/*===========================================================================
 * Channel lookup
 *=========================================================================*/

struct yetty_ywire_channel *yetty_ywire_connection_channel(
    struct yetty_ywire_connection *connection, uint32_t channel_id)
{
    if (!connection) {
        return NULL;
    }
    for (size_t i = 0; i < connection->channel_count; i++) {
        if (connection->channels[i].in_use && connection->channels[i].id == channel_id) {
            return &connection->channels[i];
        }
    }
    return NULL;
}

/*===========================================================================
 * Reactor seam — forwarded down to the transport
 *=========================================================================*/

int yetty_ywire_connection_fd(struct yetty_ywire_connection *connection)
{
    return connection ? connection->reactor.ops->fd(connection->reactor.userdata) : -1;
}

int yetty_ywire_connection_out_fd(struct yetty_ywire_connection *connection)
{
    return connection ? connection->reactor.ops->out_fd(connection->reactor.userdata) : -1;
}

int yetty_ywire_connection_want_write(struct yetty_ywire_connection *connection)
{
    return connection ? connection->reactor.ops->want_write(connection->reactor.userdata) : 0;
}

int yetty_ywire_connection_is_eof(struct yetty_ywire_connection *connection)
{
    return connection ? connection->reactor.ops->is_eof(connection->reactor.userdata) : 1;
}

struct yetty_ycore_void_result yetty_ywire_connection_enable_raw_mode(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_enable_raw_mode: NULL connection");
    }
    return connection->reactor.ops->enable_raw_mode(connection->reactor.userdata);
}

struct yetty_ycore_size_result yetty_ywire_connection_pump_writable(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_size, "ywire_connection_pump_writable: NULL connection");
    }
    return connection->reactor.ops->pump_writable(connection->reactor.userdata);
}

/* Feed one chunk into the state machine and run it. */
static struct yetty_ycore_void_result feed_chunk(struct yetty_ywire_connection *connection,
                                                 const uint8_t *bytes, size_t n)
{
    struct yetty_ycore_void_result fr =
        yetty_ywire_wire_statemachine_feed(connection->sm, (const char *)bytes, n);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "ywire_connection: sm feed");
    struct yetty_ycore_void_result pr = yetty_ywire_wire_statemachine_process(connection->sm);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ywire_connection: sm process");
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_ywire_connection_pump_readable(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_size, "ywire_connection_pump_readable: NULL connection");
    }
    struct yetty_yclass_transport_reactor *reactor = &connection->reactor;
    size_t total = 0;
    for (;;) {
        uint8_t buf[4096];
        struct yetty_ycore_size_result rr =
            reactor->ops->read_available(reactor->userdata, buf, sizeof(buf));
        YETTY_RETURN_IF_ERR(yetty_ycore_size, rr, "ywire_connection_pump_readable: read");
        if (rr.value == 0) {
            break; /* nothing more available now (or EOF) */
        }
        total += rr.value;
        struct yetty_ycore_void_result fr = feed_chunk(connection, buf, rr.value);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, fr, "ywire_connection_pump_readable: feed");
    }
    return YETTY_OK(yetty_ycore_size, total);
}

/* One blocking inbound step for the synchronous handshake. Returns 1 while the
 * link is alive (even if this step decoded nothing yet), 0 on EOF / timeout /
 * error. Errors are absorbed here — the caller is a blocking recv that surfaces
 * a short read as a failed handshake. */
int yetty_ywire_connection_pump_blocking_once(struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return 0;
    }
    struct yetty_yclass_transport_reactor *reactor = &connection->reactor;
    if (reactor->ops->is_eof(reactor->userdata)) {
        return 0;
    }
#ifndef _WIN32
    int fd = reactor->ops->fd(reactor->userdata);
    if (fd >= 0) {
        struct pollfd poll_fd = {.fd = fd, .events = POLLIN};
        int poll_result = poll(&poll_fd, 1, YWIRE_PUMP_TIMEOUT_MS);
        if (poll_result < 0) {
            return errno == EINTR ? 1 : 0;
        }
        if (poll_result == 0) {
            return 0; /* timed out waiting for the peer */
        }
        if ((poll_fd.revents & (POLLHUP | POLLERR | POLLNVAL)) && !(poll_fd.revents & POLLIN)) {
            return 0;
        }
    }
#endif
    uint8_t buf[4096];
    struct yetty_ycore_size_result rr =
        reactor->ops->read_available(reactor->userdata, buf, sizeof(buf));
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return 0;
    }
    if (rr.value == 0) {
        return !reactor->ops->is_eof(reactor->userdata);
    }
    struct yetty_ycore_void_result fr = feed_chunk(connection, buf, rr.value);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        return 0;
    }
    return 1;
}

/*===========================================================================
 * SIGWINCH → viewport
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ywire_connection_set_resize_cb(
    struct yetty_ywire_connection *connection, yetty_ywire_resize_cb cb, void *user)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_set_resize_cb: NULL connection");
    }
    connection->on_resize = cb;
    connection->resize_user = user;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_connection_pickup_winsize(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_pickup_winsize: NULL connection");
    }
    if (!connection->on_resize) {
        return YETTY_OK_VOID();
    }
#ifndef _WIN32
    int fd = connection->reactor.ops->fd(connection->reactor.userdata);
    struct winsize ws;
    if (fd >= 0 && ioctl(fd, TIOCGWINSZ, &ws) == 0) {
        connection->on_resize(connection->resize_user, (int)ws.ws_xpixel, (int)ws.ws_ypixel,
                              (int)ws.ws_col, (int)ws.ws_row);
    }
#endif
    return YETTY_OK_VOID();
}
