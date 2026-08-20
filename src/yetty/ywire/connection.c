/*
 * connection.c — the multiplexed wire link (see connection.h).
 *
 * Owns the reactor-capable transport + one wire_statemachine for inbound demux
 * + the channel array. pump_readable() reads the fd and drives the state
 * machine, whose buffered handlers route each decoded envelope to the right
 * channel (rpc bytes buffered for the transport adapter; input events fired at
 * the input sink; raw runs at the raw sink; dynamic-channel messages through
 * the OPEN/DATA/EOF/CLOSE/WINDOW_ADJUST handler). Outbound framing for the
 * well-known lanes is done by the channels (channel.c) through this
 * connection's transport; dynamic-channel DATA goes through the fair outbound
 * scheduler (pump_outbound) with per-channel windows and chunking.
 */

#include <yetty/ywire/connection.h>

#include "connection-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yterminal/client-input.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/wire-statemachine.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#endif

#define YWIRE_PUMP_TIMEOUT_MS 5000

/*===========================================================================
 * Channel wire header (the args slot of DCS YETTY_DCS_YWIRE_CHANNEL)
 *=========================================================================*/

struct ywire_channel_wire_header {
    uint32_t msg; /* enum yetty_ywire_channel_msg */
    uint32_t channel_id;
    uint32_t window; /* OPEN: opener's recv window; WINDOW_ADJUST: delta */
    uint32_t reserved;
};

static void wire_header_encode(const struct ywire_channel_wire_header *header,
                               uint8_t out[YETTY_YWIRE_CHANNEL_WIRE_HEADER_LEN])
{
    const uint32_t fields[4] = {header->msg, header->channel_id, header->window, header->reserved};
    for (size_t field = 0; field < 4; field++) {
        out[field * 4 + 0] = (uint8_t)(fields[field] & 0xff);
        out[field * 4 + 1] = (uint8_t)((fields[field] >> 8) & 0xff);
        out[field * 4 + 2] = (uint8_t)((fields[field] >> 16) & 0xff);
        out[field * 4 + 3] = (uint8_t)((fields[field] >> 24) & 0xff);
    }
}

static int wire_header_decode(const uint8_t *bytes, size_t len,
                              struct ywire_channel_wire_header *header)
{
    if (!bytes || len < YETTY_YWIRE_CHANNEL_WIRE_HEADER_LEN) {
        return 0;
    }
    uint32_t fields[4];
    for (size_t field = 0; field < 4; field++) {
        fields[field] = (uint32_t)bytes[field * 4 + 0] | ((uint32_t)bytes[field * 4 + 1] << 8) |
                        ((uint32_t)bytes[field * 4 + 2] << 16) |
                        ((uint32_t)bytes[field * 4 + 3] << 24);
    }
    header->msg = fields[0];
    header->channel_id = fields[1];
    header->window = fields[2];
    header->reserved = fields[3];
    return 1;
}

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

/* DCS YETTY_OSC_CS_CLIENT_INPUT_HOLD_ACK (no payload) → the host confirms it has
 * armed its input barrier. Client-only: the host never receives this, it emits
 * it. Sets the flag the teardown drain waits on before detaching sinks. */
static struct yetty_ycore_void_result on_input_hold_ack(void *userdata,
                                                        enum yetty_ywire_envelope_kind kind,
                                                        int code, const uint8_t *args,
                                                        size_t args_len, const uint8_t *payload,
                                                        size_t payload_len)
{
    (void)kind;
    (void)code;
    (void)args;
    (void)args_len;
    (void)payload;
    (void)payload_len;
    struct yetty_ywire_connection *connection = userdata;
    connection->input_hold_ack_seen = 1;
#ifndef _WIN32
    clock_gettime(CLOCK_MONOTONIC, &connection->input_hold_ack_time);
#endif
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
 * Dynamic-channel message handling
 *=========================================================================*/

static void fire_event(struct yetty_ywire_channel *channel, enum yetty_ywire_channel_event event)
{
    if (channel->event_cb) {
        channel->event_cb(channel->event_user, channel, event);
    }
}

void yetty_ywire_connection_maybe_release(struct yetty_ywire_channel *channel)
{
    if (!channel || !channel->in_use) {
        return;
    }
    if (!channel->close_sent || !channel->close_rcvd) {
        return;
    }
    if (channel->inbuf_off < channel->inbuf.size) {
        return; /* pull consumer still has bytes to drain */
    }
    yetty_ycore_buffer_destroy(&channel->inbuf);
    yetty_ycore_buffer_destroy(&channel->outbuf);
    memset(channel, 0, sizeof(*channel));
}

/* Ship one already-framed envelope toward the peer. Attach mode hands it
 * straight to the borrowed writer (which writes it whole); owned mode queues
 * on the reactor transport and pumps what the non-blocking writer takes now. */
static struct yetty_ycore_void_result connection_ship(struct yetty_ywire_connection *connection,
                                                      const uint8_t *bytes, size_t n)
{
    if (connection->writer) {
        return connection->writer(bytes, n, connection->writer_user);
    }
    struct yetty_yclass_transport_reactor *reactor = &connection->reactor;
    struct yetty_ycore_void_result queue_res = reactor->ops->queue(reactor->userdata, bytes, n);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, queue_res, "ywire connection_ship: queue");
    struct yetty_ycore_size_result pump_res = reactor->ops->pump_writable(reactor->userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pump_res, "ywire connection_ship: pump_writable");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_connection_frame_envelope(
    struct yetty_ywire_connection *connection, enum yetty_ywire_envelope_kind kind, int code,
    int has_args, int compressed, const void *args, size_t args_len, const void *body,
    size_t body_len)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire frame_envelope: NULL connection");
    }
    if (!connection->emit_sm) {
        /* Encode-only SM: the scanner coro is spawned lazily on feed, so
         * this never pays the inbound-side stack allocation. */
        struct yetty_ywire_wire_statemachine_ptr_result sm_res =
            yetty_ywire_wire_statemachine_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sm_res, "ywire frame_envelope: emit SM create");
        connection->emit_sm = sm_res.value;
    }
    yetty_ycore_buffer_clear(&connection->emit_buf);
    struct yetty_ycore_void_result frame_res = yetty_ywire_wire_statemachine_start_write(
        connection->emit_sm, kind, code, has_args, compressed, args, args_len,
        &connection->emit_buf);
    if (YETTY_IS_OK(frame_res) && body && body_len > 0) {
        frame_res = yetty_ywire_wire_statemachine_write(connection->emit_sm, body, body_len);
    }
    if (YETTY_IS_OK(frame_res)) {
        frame_res = yetty_ywire_wire_statemachine_finish_write(connection->emit_sm);
    }
    if (YETTY_IS_ERR(frame_res)) {
        /* The SM aborted the envelope and is reusable; drop the partial
         * bytes so no caller can ship them. */
        yetty_ycore_buffer_clear(&connection->emit_buf);
        return YETTY_ERR(yetty_ycore_void, "ywire frame_envelope: encode", frame_res);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_connection_send_control(
    struct yetty_ywire_connection *connection, enum yetty_ywire_channel_msg msg,
    uint32_t channel_id, uint32_t window)
{
    struct ywire_channel_wire_header header = {
        .msg = (uint32_t)msg, .channel_id = channel_id, .window = window, .reserved = 0};
    uint8_t header_bytes[YETTY_YWIRE_CHANNEL_WIRE_HEADER_LEN];
    wire_header_encode(&header, header_bytes);

    struct yetty_ycore_void_result build = yetty_ywire_connection_frame_envelope(
        connection, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YWIRE_CHANNEL, /*has_args=*/1,
        /*compressed=*/0, header_bytes, sizeof(header_bytes), NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, build, "ywire_connection_send_control: frame");
    struct yetty_ycore_void_result ship_res =
        connection_ship(connection, connection->emit_buf.data, connection->emit_buf.size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ship_res, "ywire_connection_send_control: ship");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_connection_grant_credit(
    struct yetty_ywire_channel *channel, size_t consumed)
{
    if (!channel || channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC || consumed == 0) {
        return YETTY_OK_VOID();
    }
    if (channel->close_sent || channel->close_rcvd || channel->remote_eof) {
        return YETTY_OK_VOID(); /* peer will send no more DATA — credit is moot */
    }
    channel->recv_consumed += (int64_t)consumed;
    if (channel->recv_consumed < channel->recv_window_initial / 2) {
        ydebug("ywire: channel %u consumed+=%zu acc=%lld (grant deferred, threshold %lld)",
               channel->id, consumed, (long long)channel->recv_consumed,
               (long long)(channel->recv_window_initial / 2));
        return YETTY_OK_VOID();
    }
    uint32_t grant = (uint32_t)channel->recv_consumed;
    channel->recv_consumed = 0;
    ydebug("ywire: channel %u grant %u", channel->id, grant);
    return yetty_ywire_connection_send_control(
        channel->connection, YETTY_YWIRE_CHANNEL_MSG_WINDOW_ADJUST, channel->id, grant);
}

static void init_dynamic_channel(struct yetty_ywire_channel *channel,
                                 struct yetty_ywire_connection *connection, uint32_t id,
                                 int64_t send_window, int64_t recv_window_initial)
{
    memset(channel, 0, sizeof(*channel));
    channel->connection = connection;
    channel->id = id;
    channel->kind = YETTY_YWIRE_CHANNEL_KIND_DYNAMIC;
    channel->wire_kind = YETTY_YWIRE_ENVELOPE_DCS;
    channel->wire_code = YETTY_DCS_YWIRE_CHANNEL;
    channel->has_args = 1;
    channel->send_window = send_window;
    channel->recv_window_initial = recv_window_initial;
    channel->in_use = 1;
}

static struct yetty_ywire_channel *find_free_slot(struct yetty_ywire_connection *connection)
{
    /* Slots 0..2 are the persistent well-known lanes. */
    for (size_t i = 3; i < YETTY_YWIRE_CHANNEL_MAX; i++) {
        if (!connection->channels[i].in_use) {
            return &connection->channels[i];
        }
    }
    return NULL;
}

static struct yetty_ycore_void_result on_channel_open(
    struct yetty_ywire_connection *connection, const struct ywire_channel_wire_header *header)
{
    if (header->channel_id < YETTY_YWIRE_CHANNEL_DYNAMIC_BASE ||
        yetty_ywire_connection_channel(connection, header->channel_id)) {
        /* Bogus or duplicate id — refuse. A CLOSE for an id the peer no longer
         * (or never) tracks is ignored on their side. */
        return yetty_ywire_connection_send_control(connection, YETTY_YWIRE_CHANNEL_MSG_CLOSE,
                                                   header->channel_id, 0);
    }
    struct yetty_ywire_channel *channel = find_free_slot(connection);
    if (!channel) {
        return yetty_ywire_connection_send_control(connection, YETTY_YWIRE_CHANNEL_MSG_CLOSE,
                                                   header->channel_id, 0);
    }
    init_dynamic_channel(channel, connection, header->channel_id,
                         /*send_window=*/header->window
                             ? (int64_t)header->window
                             : (int64_t)YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT,
                         /*recv_window_initial=*/(int64_t)YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT);
    int accepted = connection->accept_cb && connection->accept_cb(connection->accept_user, channel);
    if (!accepted) {
        memset(channel, 0, sizeof(*channel));
        return yetty_ywire_connection_send_control(connection, YETTY_YWIRE_CHANNEL_MSG_CLOSE,
                                                   header->channel_id, 0);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result on_channel_close(struct yetty_ywire_connection *connection,
                                                       struct yetty_ywire_channel *channel)
{
    channel->close_rcvd = 1;
    if (!channel->close_sent) {
        /* Peer tore the channel down — answer CLOSE and drop anything we still
         * had pending (both directions are dead per protocol). */
        channel->outbuf_off = channel->outbuf.size;
        channel->flush_mark = channel->outbuf.size;
        channel->close_requested = 1;
        struct yetty_ycore_void_result send_res = yetty_ywire_connection_send_control(
            connection, YETTY_YWIRE_CHANNEL_MSG_CLOSE, channel->id, 0);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "ywire on_channel_close: reply");
        channel->close_sent = 1;
    }
    fire_event(channel, YETTY_YWIRE_CHANNEL_EVENT_CLOSED);
    yetty_ywire_connection_maybe_release(channel);
    return YETTY_OK_VOID();
}

/* DCS YETTY_DCS_YWIRE_CHANNEL → the connection-layer message handler. */
static struct yetty_ycore_void_result on_channel_envelope(void *userdata,
                                                          enum yetty_ywire_envelope_kind kind,
                                                          int code, const uint8_t *args,
                                                          size_t args_len, const uint8_t *payload,
                                                          size_t payload_len)
{
    (void)kind;
    (void)code;
    struct yetty_ywire_connection *connection = userdata;
    struct ywire_channel_wire_header header;
    if (!wire_header_decode(args, args_len, &header)) {
        return YETTY_OK_VOID(); /* malformed header — drop, stay in sync */
    }
    if (header.msg == YETTY_YWIRE_CHANNEL_MSG_OPEN) {
        return on_channel_open(connection, &header);
    }
    struct yetty_ywire_channel *channel =
        yetty_ywire_connection_channel(connection, header.channel_id);
    if (!channel || channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
        return YETTY_OK_VOID(); /* unknown/stale id (e.g. CLOSE after release) — ignore */
    }
    switch (header.msg) {
    case YETTY_YWIRE_CHANNEL_MSG_DATA: {
        if (channel->remote_eof || payload_len == 0) {
            return YETTY_OK_VOID();
        }
        if (channel->raw_sink) {
            channel->raw_sink(channel->sink_user, payload, payload_len);
            return yetty_ywire_connection_grant_credit(channel, payload_len);
        }
        return append_inbuf(channel, payload, payload_len);
    }
    case YETTY_YWIRE_CHANNEL_MSG_EOF:
        channel->remote_eof = 1;
        fire_event(channel, YETTY_YWIRE_CHANNEL_EVENT_REMOTE_EOF);
        return YETTY_OK_VOID();
    case YETTY_YWIRE_CHANNEL_MSG_CLOSE:
        return on_channel_close(connection, channel);
    case YETTY_YWIRE_CHANNEL_MSG_WINDOW_ADJUST:
        channel->send_window += (int64_t)header.window;
        ydebug("ywire: channel %u credit +%u send_window=%lld", channel->id, header.window,
               (long long)channel->send_window);
        /* Credit may unblock pending DATA — run the scheduler now. */
        return yetty_ywire_connection_pump_outbound(connection);
    default:
        return YETTY_OK_VOID(); /* unknown message from a newer peer — ignore */
    }
}

/*===========================================================================
 * Fair outbound scheduler
 *=========================================================================*/

/* Emit one DATA chunk (or a deferred EOF/CLOSE once drained) for `channel`.
 * Returns the number of payload bytes queued (0 = nothing eligible). */
static struct yetty_ycore_size_result emit_one(struct yetty_ywire_connection *connection,
                                               struct yetty_ywire_channel *channel)
{
    if (!channel->in_use || channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    size_t pending = channel->flush_mark - channel->outbuf_off;
    if (pending == 0) {
        /* Drained — deferred half-close / teardown may go now (they must never
         * overtake this channel's own DATA). */
        if (channel->eof_requested && !channel->eof_sent) {
            struct yetty_ycore_void_result send_res = yetty_ywire_connection_send_control(
                connection, YETTY_YWIRE_CHANNEL_MSG_EOF, channel->id, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_size, send_res, "ywire emit_one: EOF");
            channel->eof_sent = 1;
        }
        if (channel->close_requested && !channel->close_sent) {
            struct yetty_ycore_void_result send_res = yetty_ywire_connection_send_control(
                connection, YETTY_YWIRE_CHANNEL_MSG_CLOSE, channel->id, 0);
            YETTY_RETURN_IF_ERR(yetty_ycore_size, send_res, "ywire emit_one: CLOSE");
            channel->close_sent = 1;
            yetty_ywire_connection_maybe_release(channel);
        }
        return YETTY_OK(yetty_ycore_size, 0);
    }

    size_t chunk =
        pending < YETTY_YWIRE_CHANNEL_CHUNK_MAX ? pending : YETTY_YWIRE_CHANNEL_CHUNK_MAX;
    if (channel->send_window >= 0 && (int64_t)chunk > channel->send_window) {
        chunk = (size_t)channel->send_window;
    }
    if (chunk == 0) {
        ydebug("ywire: channel %u window-blocked pending=%zu send_window=%lld", channel->id,
               pending, (long long)channel->send_window);
        return YETTY_OK(yetty_ycore_size, 0); /* window-blocked — WINDOW_ADJUST will wake us */
    }

    struct ywire_channel_wire_header header = {
        .msg = YETTY_YWIRE_CHANNEL_MSG_DATA, .channel_id = channel->id, .window = 0, .reserved = 0};
    uint8_t header_bytes[YETTY_YWIRE_CHANNEL_WIRE_HEADER_LEN];
    wire_header_encode(&header, header_bytes);

    struct yetty_ycore_void_result build = yetty_ywire_connection_frame_envelope(
        connection, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YWIRE_CHANNEL, /*has_args=*/1,
        connection->compressed, header_bytes, sizeof(header_bytes),
        channel->outbuf.data + channel->outbuf_off, chunk);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, build, "ywire emit_one: frame DATA");
    struct yetty_ycore_void_result ship_res;
    if (connection->writer) {
        /* Attach mode: hand the whole chunk envelope to the host writer. */
        ship_res = connection->writer(connection->emit_buf.data, connection->emit_buf.size,
                                      connection->writer_user);
    } else {
        /* Owned mode: queue only (the transport copies) — pump_outbound
         * drains the transport once per pump, after the fair round has been
         * assembled. */
        ship_res = connection->reactor.ops->queue(
            connection->reactor.userdata, connection->emit_buf.data, connection->emit_buf.size);
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_size, ship_res, "ywire emit_one: ship");

    channel->outbuf_off += chunk;
    if (channel->send_window >= 0) {
        channel->send_window -= (int64_t)chunk;
    }
    if (channel->outbuf_off >= channel->outbuf.size) {
        yetty_ycore_buffer_clear(&channel->outbuf);
        channel->outbuf_off = 0;
        channel->flush_mark = 0;
    }
    return YETTY_OK(yetty_ycore_size, chunk);
}

struct yetty_ycore_void_result yetty_ywire_connection_pump_outbound(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_pump_outbound: NULL connection");
    }
    size_t queued = 0;
    int progress = 1;
    while (progress && queued < YETTY_YWIRE_OUTBOUND_HIGH_WATER) {
        progress = 0;
        for (size_t i = 0; i < YETTY_YWIRE_CHANNEL_MAX; i++) {
            size_t slot = (connection->outbound_cursor + i) % YETTY_YWIRE_CHANNEL_MAX;
            struct yetty_ycore_size_result emit_res =
                emit_one(connection, &connection->channels[slot]);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "ywire pump_outbound: emit_one");
            if (emit_res.value > 0) {
                progress = 1;
                queued += emit_res.value;
            }
        }
    }
    /* Rotate the start slot so no channel is structurally first every pump. */
    connection->outbound_cursor = (connection->outbound_cursor + 1) % YETTY_YWIRE_CHANNEL_MAX;
    if (queued > 0 && connection->reactor.ops) {
        struct yetty_ycore_size_result pump_res =
            connection->reactor.ops->pump_writable(connection->reactor.userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pump_res, "ywire pump_outbound: pump_writable");
    }
    return YETTY_OK_VOID();
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
    /* Well-known lanes are not flow-controlled: the host side of the wire
     * predates the channel protocol and never grants credit back. */
    channel->send_window = -1;
    channel->recv_window_initial = -1;
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
    connection->sm_owned = 1;

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

    /* connection-layer lane: dynamic-channel OPEN/DATA/EOF/CLOSE/WINDOW_ADJUST
     * envelopes, wire header in the args slot. */
    struct yetty_ycore_void_result channel_reg = yetty_ywire_wire_statemachine_register_buffered(
        connection->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YWIRE_CHANNEL, /*has_args=*/1,
        on_channel_envelope, connection);
    if (YETTY_IS_ERR(channel_reg)) {
        YWIRE_CONNECTION_FAIL("ywire_connection_create: register channel", channel_reg);
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

    /* HOLD-ACK lane: the host's confirmation that it armed the input barrier.
     * Client-only in practice (the host emits, never receives), but registering
     * it here is harmless on the host — it only sets a flag. has_args=1 to
     * match the host's emit. */
    struct yetty_ycore_void_result hold_ack_reg = yetty_ywire_wire_statemachine_register_buffered(
        connection->sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_OSC_CS_CLIENT_INPUT_HOLD_ACK,
        /*has_args=*/1, on_input_hold_ack, connection);
    if (YETTY_IS_ERR(hold_ack_reg)) {
        YWIRE_CONNECTION_FAIL("ywire_connection_create: register hold-ack", hold_ack_reg);
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
    /* Attach mode borrows the SM — its owner destroys it (before this call,
     * per the attach contract, since its handler points back at us). */
    if (connection->sm && connection->sm_owned) {
        struct yetty_ycore_void_result sm_res =
            yetty_ywire_wire_statemachine_destroy(connection->sm);
        if (YETTY_IS_ERR(sm_res)) {
            result = YETTY_ERR(yetty_ycore_void, "ywire_connection_destroy: sm", sm_res);
        }
    }
    /* The outbound framer is connection-owned in both modes. */
    if (connection->emit_sm) {
        struct yetty_ycore_void_result emit_sm_res =
            yetty_ywire_wire_statemachine_destroy(connection->emit_sm);
        if (YETTY_IS_ERR(emit_sm_res)) {
            if (YETTY_IS_ERR(result)) {
                yetty_ycore_error_destroy(emit_sm_res.error);
            } else {
                result =
                    YETTY_ERR(yetty_ycore_void, "ywire_connection_destroy: emit sm", emit_sm_res);
            }
        }
    }
    yetty_ycore_buffer_destroy(&connection->emit_buf);
    yetty_ycore_buffer_destroy(&connection->input_held);
    for (size_t i = 0; i < YETTY_YWIRE_CHANNEL_MAX; i++) {
        yetty_ycore_buffer_destroy(&connection->channels[i].inbuf);
        yetty_ycore_buffer_destroy(&connection->channels[i].outbuf);
    }
    /* reactor is borrowed — the creator owns and destroys the transport. */
    free(connection);
    return result;
}

/*===========================================================================
 * Host-side attach (acceptor over a borrowed statemachine — see connection.h)
 *=========================================================================*/

struct yetty_ywire_connection_ptr_result yetty_ywire_connection_attach(
    struct yetty_ywire_wire_statemachine *sm, yetty_ywire_connection_writer_fn writer,
    void *writer_user, int compressed)
{
    if (!sm || !writer) {
        return YETTY_ERR(yetty_ywire_connection_ptr, "ywire_connection_attach: NULL sm/writer");
    }
    struct yetty_ywire_connection *connection = calloc(1, sizeof(*connection));
    if (!connection) {
        return YETTY_ERR(yetty_ywire_connection_ptr, "ywire_connection_attach: calloc");
    }
    connection->sm = sm;
    connection->sm_owned = 0;
    connection->writer = writer;
    connection->writer_user = writer_user;
    connection->compressed = compressed ? 1 : 0;
    /* The host is the acceptor by convention: its dynamic ids take the odd
     * offsets, the in-pane client keeps the even ones. */
    connection->role_acceptor = 1;

    /* Only the connection-layer lane — the SM owner's other handlers (rpc
     * server, ydraw, text default sink) stay untouched. */
    struct yetty_ycore_void_result channel_reg = yetty_ywire_wire_statemachine_register_buffered(
        sm, YETTY_YWIRE_ENVELOPE_DCS, YETTY_DCS_YWIRE_CHANNEL, /*has_args=*/1, on_channel_envelope,
        connection);
    if (YETTY_IS_ERR(channel_reg)) {
        free(connection);
        return YETTY_ERR(yetty_ywire_connection_ptr, "ywire_connection_attach: register channel",
                         channel_reg);
    }
    return YETTY_OK(yetty_ywire_connection_ptr, connection);
}

/*===========================================================================
 * Channel lookup / dynamic open
 *=========================================================================*/

struct yetty_ywire_channel *yetty_ywire_connection_channel(
    struct yetty_ywire_connection *connection, uint32_t channel_id)
{
    if (!connection) {
        return NULL;
    }
    for (size_t i = 0; i < YETTY_YWIRE_CHANNEL_MAX; i++) {
        if (connection->channels[i].in_use && connection->channels[i].id == channel_id) {
            return &connection->channels[i];
        }
    }
    return NULL;
}

struct yetty_ycore_void_result yetty_ywire_connection_set_accept_cb(
    struct yetty_ywire_connection *connection, yetty_ywire_accept_cb cb, void *user)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_set_accept_cb: NULL connection");
    }
    connection->accept_cb = cb;
    connection->accept_user = user;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_connection_set_role(
    struct yetty_ywire_connection *connection, int acceptor)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_set_role: NULL connection");
    }
    if (connection->next_dynamic_offset != 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "ywire_connection_set_role: role change after a channel was opened");
    }
    connection->role_acceptor = acceptor ? 1 : 0;
    return YETTY_OK_VOID();
}

struct yetty_ywire_channel_ptr_result yetty_ywire_connection_open_channel(
    struct yetty_ywire_connection *connection, uint32_t initial_recv_window)
{
    if (!connection) {
        return YETTY_ERR(yetty_ywire_channel_ptr, "ywire_connection_open_channel: NULL connection");
    }
    struct yetty_ywire_channel *channel = find_free_slot(connection);
    if (!channel) {
        return YETTY_ERR(yetty_ywire_channel_ptr,
                         "ywire_connection_open_channel: all channel slots in use");
    }
    uint32_t id = YETTY_YWIRE_CHANNEL_DYNAMIC_BASE + connection->next_dynamic_offset +
                  (connection->role_acceptor ? 1u : 0u);
    connection->next_dynamic_offset += 2;

    init_dynamic_channel(channel, connection, id,
                         /*send_window=*/(int64_t)YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT,
                         /*recv_window_initial=*/
                         initial_recv_window ? (int64_t)initial_recv_window
                                             : (int64_t)YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT);

    struct yetty_ycore_void_result open_res = yetty_ywire_connection_send_control(
        connection, YETTY_YWIRE_CHANNEL_MSG_OPEN, id, initial_recv_window);
    if (YETTY_IS_ERR(open_res)) {
        memset(channel, 0, sizeof(*channel));
        return YETTY_ERR(yetty_ywire_channel_ptr, "ywire_connection_open_channel: send OPEN",
                         open_res);
    }
    return YETTY_OK(yetty_ywire_channel_ptr, channel);
}

/*===========================================================================
 * Reactor seam — forwarded down to the transport
 *=========================================================================*/

int yetty_ywire_connection_fd(struct yetty_ywire_connection *connection)
{
    if (!connection || !connection->reactor.ops) {
        return -1; /* attach mode: the SM's owner owns the fd */
    }
    return connection->reactor.ops->fd(connection->reactor.userdata);
}

int yetty_ywire_connection_out_fd(struct yetty_ywire_connection *connection)
{
    if (!connection || !connection->reactor.ops) {
        return -1;
    }
    return connection->reactor.ops->out_fd(connection->reactor.userdata);
}

int yetty_ywire_connection_want_write(struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return 0;
    }
    if (!connection->reactor.ops) {
        /* Attach mode ships synchronously through the writer; the only bytes
         * that can linger are window-blocked, and those wake on the peer's
         * WINDOW_ADJUST (an inbound event), not on writability. */
        return 0;
    }
    if (connection->reactor.ops->want_write(connection->reactor.userdata)) {
        return 1;
    }
    /* Sendable per-channel pending also wants the loop's writable interest.
     * Window-blocked bytes do NOT count — they can make no progress until the
     * peer's WINDOW_ADJUST arrives (a readable event), so arming writable
     * interest for them would spin the loop. */
    for (size_t i = 0; i < YETTY_YWIRE_CHANNEL_MAX; i++) {
        const struct yetty_ywire_channel *channel = &connection->channels[i];
        if (!channel->in_use || channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
            continue;
        }
        if (channel->flush_mark > channel->outbuf_off && channel->send_window != 0) {
            return 1;
        }
        if ((channel->eof_requested && !channel->eof_sent) ||
            (channel->close_requested && !channel->close_sent)) {
            return 1;
        }
    }
    return 0;
}

int yetty_ywire_connection_is_eof(struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return 1;
    }
    if (!connection->reactor.ops) {
        return 0; /* attach mode: link liveness belongs to the SM's owner */
    }
    return connection->reactor.ops->is_eof(connection->reactor.userdata);
}

struct yetty_ycore_void_result yetty_ywire_connection_enable_raw_mode(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_connection_enable_raw_mode: NULL connection");
    }
    if (!connection->reactor.ops) {
        return YETTY_ERR(yetty_ycore_void,
                         "ywire_connection_enable_raw_mode: attach mode owns no fd");
    }
    return connection->reactor.ops->enable_raw_mode(connection->reactor.userdata);
}

struct yetty_ycore_size_result yetty_ywire_connection_pump_writable(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_size, "ywire_connection_pump_writable: NULL connection");
    }
    /* Top the transport queue up from per-channel pending first, so writable
     * readiness drains channel backlogs and not just already-framed bytes. */
    struct yetty_ycore_void_result outbound_res = yetty_ywire_connection_pump_outbound(connection);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, outbound_res, "ywire_connection_pump_writable: outbound");
    if (!connection->reactor.ops) {
        return YETTY_OK(yetty_ycore_size, 0); /* attach mode ships through the writer */
    }
    return connection->reactor.ops->pump_writable(connection->reactor.userdata);
}

static int connection_open_dynamic_count(const struct yetty_ywire_connection *connection)
{
    int open = 0;
    for (size_t index = 0; index < YETTY_YWIRE_CHANNEL_MAX; ++index) {
        const struct yetty_ywire_channel *channel = &connection->channels[index];
        if (channel->in_use && channel->kind == YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
            ++open;
        }
    }
    return open;
}

/* Has the armed barrier's absolute host-side deadline passed? On Windows (not a
 * terminal-client target) there is no CLOCK_MONOTONIC deadline — never expires. */
static int barrier_deadline_passed(const struct yetty_ywire_connection *connection)
{
#ifdef _WIN32
    (void)connection;
    return 0;
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    long long now_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    long long deadline_ms = (long long)connection->input_barrier_deadline.tv_sec * 1000 +
                            connection->input_barrier_deadline.tv_nsec / 1000000;
    return now_ms >= deadline_ms;
#endif
}

/* Disarm and hand the held bytes back exactly once (moves them into `out`).
 * Returns the byte count; 0 when nothing was held. The caller has already
 * decided release is warranted. */
static int barrier_take(struct yetty_ywire_connection *connection, struct yetty_ycore_buffer *out)
{
    connection->input_barrier_armed = 0;
    size_t held = connection->input_held.size;
    if (held == 0) {
        return 0;
    }
    if (out) {
        struct yetty_ycore_void_result move_res =
            yetty_ycore_buffer_write(out, connection->input_held.data, held);
        if (YETTY_IS_ERR(move_res)) {
            yetty_ycore_error_destroy(move_res.error);
            held = 0;
        }
    }
    yetty_ycore_buffer_clear(&connection->input_held);
    return (int)held;
}

void yetty_ywire_connection_input_barrier_arm(struct yetty_ywire_connection *connection,
                                              int deadline_ms)
{
    /* Arm the barrier BEFORE the client closes its channels and drains — so a
     * keystroke the user types anywhere in the teardown window is held, even
     * one the host writes before it processes the client's first CLOSE. Stamp an
     * ABSOLUTE host-side deadline: past it the barrier stops holding and
     * releases, so a client that never completes teardown cannot make the host
     * retain input forever. Re-arming (a fresh client's HOLD) resets it. */
    if (!connection) {
        return;
    }
    connection->input_barrier_armed = 1;
#ifndef _WIN32
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        now.tv_sec += deadline_ms / 1000;
        now.tv_nsec += (long)(deadline_ms % 1000) * 1000000L;
        if (now.tv_nsec >= 1000000000L) {
            now.tv_sec += 1;
            now.tv_nsec -= 1000000000L;
        }
        connection->input_barrier_deadline = now;
    }
#else
    (void)deadline_ms;
#endif
}

int yetty_ywire_connection_input_barrier_hold(struct yetty_ywire_connection *connection,
                                              const void *bytes, size_t len)
{
    /* HOLD host→pane input only while the client is tearing down. The bytes are
     * the host's OWN user keystrokes (the terminal's keystroke sink), never
     * anything from pane output — so this can never let child output synthesize
     * input; it only DEFERS the host's real user input. While armed AND within
     * the deadline the hold ALWAYS takes the bytes (the buffer grows): there is
     * no size-cap bypass, because declining would make the caller write the
     * overflow straight into the stream the client's close drain consumes —
     * re-losing a large paste. PAST the deadline the hold refuses (the caller
     * forwards, and releases the backlog) so retention stays bounded. Only a
     * genuine allocation failure, an unarmed barrier, or an expired deadline
     * declines. */
    if (!connection || !connection->input_barrier_armed) {
        return 0;
    }
    if (barrier_deadline_passed(connection)) {
        return 0;
    }
    struct yetty_ycore_void_result append_res =
        yetty_ycore_buffer_write(&connection->input_held, bytes, len);
    if (YETTY_IS_ERR(append_res)) {
        yetty_ycore_error_destroy(append_res.error);
        return 0;
    }
    return 1;
}

int yetty_ywire_connection_input_barrier_release(struct yetty_ywire_connection *connection,
                                                 struct yetty_ycore_buffer *out)
{
    /* Release the held keystrokes once the client is GONE (no dynamic channels
     * remain) OR the barrier's host-side deadline has passed (a wedged client) —
     * the pane reads them exactly once. Nothing releases while the client still
     * has a channel open AND the deadline holds, so a still-running client never
     * gets its own input replayed early. */
    if (!connection || !connection->input_barrier_armed) {
        return 0;
    }
    int client_gone = connection_open_dynamic_count(connection) == 0;
    int expired = barrier_deadline_passed(connection);
    if (!client_gone && !expired) {
        return 0;
    }
    return barrier_take(connection, out);
}

int yetty_ywire_connection_input_barrier_release_forced(struct yetty_ywire_connection *connection,
                                                        struct yetty_ycore_buffer *out)
{
    /* Unconditional release for owner death / PTY EOF: the pane fd is gone, so
     * an ungraceful client death (no CLOSE, deadline not yet passed) must still
     * recover the held bytes rather than strand them. Exactly once — barrier_take
     * disarms. */
    if (!connection || !connection->input_barrier_armed) {
        return 0;
    }
    return barrier_take(connection, out);
}

int yetty_ywire_connection_pending_close_count(struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return 0;
    }
    int pending = 0;
    for (size_t index = 0; index < YETTY_YWIRE_CHANNEL_MAX; ++index) {
        const struct yetty_ywire_channel *channel = &connection->channels[index];
        if (!channel->in_use || channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
            continue;
        }
        /* Awaiting close COMPLETION: the local close was requested (whether
         * or not the scheduler has put it on the wire yet) and the peer's
         * answering CLOSE has not arrived. A released slot (both sides
         * closed, inbuf drained) was memset back to free. */
        if (channel->close_requested && !channel->close_rcvd) {
            ++pending;
        }
    }
    return pending;
}

static struct yetty_ycore_void_result feed_chunk(struct yetty_ywire_connection *connection,
                                                 const uint8_t *bytes, size_t n);

#ifndef _WIN32
/* Milliseconds of CLOCK_MONOTONIC left until `deadline`; 0 when passed. */
static int drain_remaining_ms(const struct timespec *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    long long remaining = (long long)(deadline->tv_sec - now.tv_sec) * 1000 +
                          (deadline->tv_nsec - now.tv_nsec) / 1000000;
    if (remaining <= 0) {
        return 0;
    }
    return remaining > 1000000 ? 1000000 : (int)remaining;
}
#endif

int yetty_ywire_connection_drain_closes(struct yetty_ywire_connection *connection, int deadline_ms)
{
    if (!connection) {
        return 0;
    }
    int pending = yetty_ywire_connection_pending_close_count(connection);
    if (pending == 0) {
        return 0; /* nothing expected — read NOTHING */
    }
    int fd = yetty_ywire_connection_fd(connection);
    if (fd < 0) {
        return pending;
    }
#ifdef _WIN32
    /* The byte-wise teardown drain is POSIX-only (poll/read on a terminal
     * fd); the terminal clients that need it are not built on Windows. */
    (void)deadline_ms;
    return pending;
#else
    /* ABSOLUTE wall-clock deadline (CLOCK_MONOTONIC): recomputed after EVERY
     * iteration — reads, EINTR, everything — so a peer that keeps the fd
     * readable without ever sending the expected CLOSE (key-repeat flood, a
     * babbling host) cannot hold teardown open past the bound. */
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return pending;
    }
    deadline.tv_sec += deadline_ms / 1000;
    deadline.tv_nsec += (long)(deadline_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    /* One byte per read: the drain must stop EXACTLY at the final
     * framed-completion boundary. A raw user key queued right after the
     * final CLOSE echo — even in the SAME kernel-readable window — is then
     * never consumed; a bulk read would slurp frame and key together. The
     * traffic here is a handful of close echoes, so byte-wise cost is nil.
     * The loop runs until completion + parser GROUND: the CLOSE handler
     * fires BEFORE the envelope's ST terminator bytes are consumed, so the
     * tail must be read for the stop to land ON the framed boundary. */
    for (;;) {
        int done = pending == 0 && yetty_ywire_wire_statemachine_idle(connection->sm);
        if (done) {
            break;
        }
        int remaining_ms = drain_remaining_ms(&deadline);
        if (remaining_ms == 0) {
            break; /* wall-clock bound — teardown never hangs */
        }
        struct pollfd drain_poll = {.fd = fd, .events = POLLIN};
        int slice_ms = remaining_ms < 50 ? remaining_ms : 50;
        int ready = poll(&drain_poll, 1, slice_ms);
        if (ready <= 0) {
            continue; /* timeout/EINTR — the absolute deadline still governs */
        }
        uint8_t byte = 0;
        ssize_t got = read(fd, &byte, 1);
        if (got == 0) {
            break; /* peer hung up — no more completions can arrive */
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            break;
        }
        struct yetty_ycore_void_result feed_res = feed_chunk(connection, &byte, 1);
        if (YETTY_IS_ERR(feed_res)) {
            yetty_ycore_error_destroy(feed_res.error);
            break;
        }
        pending = yetty_ywire_connection_pending_close_count(connection);
    }
    return pending;
#endif
}

int yetty_ywire_connection_drain_until_hold_ack(struct yetty_ywire_connection *connection,
                                                int deadline_ms)
{
    if (!connection) {
        return 0;
    }
    if (connection->input_hold_ack_seen) {
        return 1; /* host already confirmed the arm */
    }
    int fd = yetty_ywire_connection_fd(connection);
    if (fd < 0) {
        return 0;
    }
#ifdef _WIN32
    /* POSIX-only, like the close drain — the terminal clients that need it are
     * not built on Windows. */
    (void)deadline_ms;
    return 0;
#else
    /* ABSOLUTE wall-clock deadline (CLOCK_MONOTONIC), recomputed every
     * iteration: a host that never answers the HOLD must not hang teardown. */
    struct timespec deadline;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
        return 0;
    }
    deadline.tv_sec += deadline_ms / 1000;
    deadline.tv_nsec += (long)(deadline_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    /* Read inbound byte-wise and feed the parser until the host's HOLD-ACK
     * envelope is dispatched (on_input_hold_ack sets the flag). The client's
     * sinks are still attached here BY DESIGN: an echo of a pre-arm key the
     * host forwarded before it armed is handled by the still-live client on
     * this path rather than dropped. Post-arm keys never reach this wire — the
     * host holds them — so there is nothing here to preserve for the shell, and
     * stopping the instant the flag flips is safe (any trailing ST bytes are
     * consumed by the close drain that follows). */
    for (;;) {
        if (connection->input_hold_ack_seen) {
            break;
        }
        int remaining_ms = drain_remaining_ms(&deadline);
        if (remaining_ms == 0) {
            break; /* wall-clock bound — teardown never hangs */
        }
        struct pollfd ack_poll = {.fd = fd, .events = POLLIN};
        int slice_ms = remaining_ms < 50 ? remaining_ms : 50;
        int ready = poll(&ack_poll, 1, slice_ms);
        if (ready <= 0) {
            continue; /* timeout/EINTR — the absolute deadline still governs */
        }
        uint8_t byte = 0;
        ssize_t got = read(fd, &byte, 1);
        if (got == 0) {
            break; /* peer hung up — no ACK can arrive */
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            break;
        }
        struct yetty_ycore_void_result feed_res = feed_chunk(connection, &byte, 1);
        if (YETTY_IS_ERR(feed_res)) {
            yetty_ycore_error_destroy(feed_res.error);
            break;
        }
    }
    return connection->input_hold_ack_seen ? 1 : 0;
#endif
}

int yetty_ywire_connection_hold_ack_lease_valid(struct yetty_ywire_connection *connection,
                                                int lease_ms)
{
    /* The ACK is only a lease: valid while the host is still guaranteed armed.
     * Pick lease_ms comfortably BELOW the host's barrier deadline so that, even
     * with one-way ACK latency, the client stops draining before the host could
     * expire and resume forwarding. */
    if (!connection || !connection->input_hold_ack_seen) {
        return 0;
    }
#ifdef _WIN32
    (void)lease_ms;
    return 1; /* no monotonic clock / no host deadline on Windows */
#else
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    long long now_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
    long long ack_ms = (long long)connection->input_hold_ack_time.tv_sec * 1000 +
                       connection->input_hold_ack_time.tv_nsec / 1000000;
    return (now_ms - ack_ms) <= lease_ms ? 1 : 0;
#endif
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
    if (!connection->reactor.ops) {
        return YETTY_ERR(yetty_ycore_size,
                         "ywire_connection_pump_readable: attach mode — the statemachine's "
                         "owner feeds it; process() fires the channel handler");
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
    if (!connection || !connection->reactor.ops) {
        return 0; /* attach mode: no fd to block on — host consumers use sinks */
    }
    struct yetty_yclass_transport_reactor *reactor = &connection->reactor;
    if (reactor->ops->is_eof(reactor->userdata)) {
        return 0;
    }
    /* Keep the outbound side moving too — pending channel bytes (e.g. just
     * unblocked by a WINDOW_ADJUST decoded in the previous step) must reach
     * the wire or the peer response we are blocking on never comes. */
    struct yetty_ycore_void_result outbound_res = yetty_ywire_connection_pump_outbound(connection);
    if (YETTY_IS_ERR(outbound_res)) {
        yetty_ycore_error_destroy(outbound_res.error);
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
    if (!connection->on_resize || !connection->reactor.ops) {
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
