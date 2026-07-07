/*
 * channel.c — one logical lane of a yetty_ywire_connection (see channel.h).
 *
 * write() coalesces outbound bytes; flush() frames them onto the wire through
 * the connection's transport (an envelope for the rpc lane, verbatim for raw).
 * read()/recv_blocking() hand out bytes the connection's demux deposited.
 */

#include <yetty/ywire/channel.h>

#include "connection-internal.h"

#include <yetty/yclass/transport.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h>

#include <stdlib.h>
#include <string.h>

uint32_t yetty_ywire_channel_id(const struct yetty_ywire_channel *channel)
{
    return channel ? channel->id : 0u;
}

enum yetty_ywire_channel_kind yetty_ywire_channel_kind_of(const struct yetty_ywire_channel *channel)
{
    return channel ? channel->kind : YETTY_YWIRE_CHANNEL_KIND_DYNAMIC;
}

struct yetty_ycore_size_result yetty_ywire_channel_write(struct yetty_ywire_channel *channel,
                                                         const void *bytes, size_t len)
{
    if (!channel) {
        return YETTY_ERR(yetty_ycore_size, "ywire_channel_write: NULL channel");
    }
    if (!channel->in_use) {
        return YETTY_ERR(yetty_ycore_size, "ywire_channel_write: channel is closed (released)");
    }
    if (channel->eof_requested || channel->close_requested || channel->close_rcvd) {
        return YETTY_ERR(yetty_ycore_size, "ywire_channel_write: channel is half-closed/closing");
    }
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (!bytes) {
        return YETTY_ERR(yetty_ycore_size, "ywire_channel_write: NULL bytes with len > 0");
    }
    struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(&channel->outbuf, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, wr, "ywire_channel_write: outbuf grow");
    return YETTY_OK(yetty_ycore_size, len);
}

/* Queue framed bytes onto the connection's transport, then push as much as the
 * non-blocking writer accepts now (the loop re-arms writable interest for the
 * rest via want_write). */
static struct yetty_ycore_void_result channel_emit(struct yetty_ywire_connection *connection,
                                                   const void *data, size_t size)
{
    struct yetty_yclass_transport_reactor *reactor = &connection->reactor;
    struct yetty_ycore_void_result qr = reactor->ops->queue(reactor->userdata, data, size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, qr, "ywire_channel: queue");
    struct yetty_ycore_size_result pr = reactor->ops->pump_writable(reactor->userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "ywire_channel: pump_writable");
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_channel_flush(struct yetty_ywire_channel *channel)
{
    if (!channel || !channel->connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_flush: NULL channel");
    }
    if (channel->kind == YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
        /* Dynamic lane: mark everything coalesced so far as wire-eligible and
         * let the connection's fair scheduler chunk it out — one large flush
         * must not head-of-line-block the other channels, and DATA beyond the
         * peer's window stays here until credit comes back. */
        channel->flush_mark = channel->outbuf.size;
        return yetty_ywire_connection_pump_outbound(channel->connection);
    }
    if (channel->outbuf.size == 0) {
        return YETTY_OK_VOID();
    }

    if (channel->kind == YETTY_YWIRE_CHANNEL_KIND_RAW) {
        /* Verbatim control bytes (e.g. the DEC ?1500/?1501 card-mouse enable),
         * tmux-wrapped so they survive a multiplexer. Rare — a transient
         * buffer per flush is fine here. */
        struct yetty_ycore_buffer framed = {0};
        struct yetty_ycore_void_result build = yetty_ywire_tmux_wrap(
            (const char *)channel->outbuf.data, channel->outbuf.size, &framed);
        if (YETTY_IS_ERR(build)) {
            yetty_ycore_buffer_destroy(&framed);
            return YETTY_ERR(yetty_ycore_void, "ywire_channel_flush: frame", build);
        }
        struct yetty_ycore_void_result emit =
            channel_emit(channel->connection, framed.data, framed.size);
        yetty_ycore_buffer_destroy(&framed);
        yetty_ycore_buffer_clear(&channel->outbuf);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, emit, "ywire_channel_flush: emit");
        return YETTY_OK_VOID();
    }
    if (channel->wire_code < 0) {
        /* The input lane has no outbound framing — drop the coalesced bytes
         * rather than emit a malformed frame. */
        yetty_ycore_buffer_clear(&channel->outbuf);
        return YETTY_OK_VOID();
    }

    /* Envelope lane (rpc): one DCS/OSC envelope carrying the coalesced frame
     * bytes. The host-side receiver's contract is one envelope = one rpc
     * frame, so this lane is never chunked. Framed through the connection's
     * long-lived emit SM — this path runs at UI rate (every ygui framework
     * flush), so it must not churn a transient SM per flush. */
    struct yetty_ycore_void_result build = yetty_ywire_connection_frame_envelope(
        channel->connection, channel->wire_kind, channel->wire_code, channel->has_args,
        channel->connection->compressed, NULL, 0, channel->outbuf.data, channel->outbuf.size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, build, "ywire_channel_flush: frame");

    struct yetty_ycore_void_result emit = channel_emit(
        channel->connection, channel->connection->emit_buf.data, channel->connection->emit_buf.size);
    yetty_ycore_buffer_clear(&channel->outbuf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit, "ywire_channel_flush: emit");
    return YETTY_OK_VOID();
}

/* Hand out from inbuf, reclaiming space once fully drained. Draining is what
 * grants the peer new flow-control credit on a dynamic channel — and what
 * releases the slot of a channel whose close handshake already finished. */
static size_t channel_take(struct yetty_ywire_channel *channel, void *buf, size_t max)
{
    if (channel->inbuf_off >= channel->inbuf.size) {
        return 0;
    }
    size_t avail = channel->inbuf.size - channel->inbuf_off;
    size_t n = avail < max ? avail : max;
    memcpy(buf, channel->inbuf.data + channel->inbuf_off, n);
    channel->inbuf_off += n;
    if (channel->inbuf_off >= channel->inbuf.size) {
        yetty_ycore_buffer_clear(&channel->inbuf);
        channel->inbuf_off = 0;
    }
    if (channel->kind == YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
        struct yetty_ycore_void_result credit_res = yetty_ywire_connection_grant_credit(channel, n);
        if (YETTY_IS_ERR(credit_res)) {
            /* The drain itself succeeded; a failed credit grant only delays the
             * peer and the next drain retries. Absorb. */
            yetty_ycore_error_destroy(credit_res.error);
        }
        yetty_ywire_connection_maybe_release(channel);
    }
    return n;
}

struct yetty_ycore_size_result yetty_ywire_channel_read(struct yetty_ywire_channel *channel,
                                                        void *buf, size_t max)
{
    if (!channel || !buf) {
        return YETTY_ERR(yetty_ycore_size, "ywire_channel_read: NULL arg");
    }
    if (max == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    return YETTY_OK(yetty_ycore_size, channel_take(channel, buf, max));
}

struct yetty_ycore_size_result yetty_ywire_channel_recv_blocking(
    struct yetty_ywire_channel *channel, void *buf, size_t max)
{
    if (!channel || !channel->connection || !buf) {
        return YETTY_ERR(yetty_ycore_size, "ywire_channel_recv_blocking: NULL arg");
    }
    if (max == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    /* Flush before blocking on read: the rpc pattern is write*, then read the
     * reply — without flushing first the request never reaches the wire and we
     * deadlock waiting for a response that was never asked for. */
    struct yetty_ycore_void_result fr = yetty_ywire_channel_flush(channel);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, fr, "ywire_channel_recv_blocking: flush");

    for (;;) {
        size_t n = channel_take(channel, buf, max);
        if (n > 0) {
            return YETTY_OK(yetty_ycore_size, n);
        }
        if (!yetty_ywire_connection_pump_blocking_once(channel->connection)) {
            return YETTY_OK(yetty_ycore_size, 0); /* EOF / timeout */
        }
    }
}

struct yetty_ycore_void_result yetty_ywire_channel_set_envelope_sink(
    struct yetty_ywire_channel *channel, yetty_ywire_channel_envelope_sink sink, void *user)
{
    if (!channel) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_set_envelope_sink: NULL channel");
    }
    channel->env_sink = sink;
    channel->sink_user = user;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_channel_set_raw_sink(struct yetty_ywire_channel *channel,
                                                                yetty_ywire_channel_raw_sink sink,
                                                                void *user)
{
    if (!channel) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_set_raw_sink: NULL channel");
    }
    channel->raw_sink = sink;
    channel->sink_user = user;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Dynamic-channel lifecycle
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ywire_channel_set_event_cb(struct yetty_ywire_channel *channel,
                                                                yetty_ywire_channel_event_cb cb,
                                                                void *user)
{
    if (!channel) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_set_event_cb: NULL channel");
    }
    channel->event_cb = cb;
    channel->event_user = user;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ywire_channel_send_eof(struct yetty_ywire_channel *channel)
{
    if (!channel || !channel->connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_send_eof: NULL channel");
    }
    if (channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_send_eof: not a dynamic channel");
    }
    if (channel->eof_requested) {
        return YETTY_OK_VOID(); /* idempotent */
    }
    /* Implicit flush: everything written so far is sealed under the EOF, which
     * the scheduler emits only once this pending data has drained. */
    channel->flush_mark = channel->outbuf.size;
    channel->eof_requested = 1;
    return yetty_ywire_connection_pump_outbound(channel->connection);
}

struct yetty_ycore_void_result yetty_ywire_channel_close(struct yetty_ywire_channel *channel)
{
    if (!channel || !channel->connection) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_close: NULL channel");
    }
    if (channel->kind != YETTY_YWIRE_CHANNEL_KIND_DYNAMIC) {
        return YETTY_ERR(yetty_ycore_void, "ywire_channel_close: not a dynamic channel");
    }
    if (channel->close_requested) {
        return YETTY_OK_VOID(); /* idempotent */
    }
    /* Close is immediate, not graceful: pending outbound is discarded so a
     * window-blocked channel cannot wedge the teardown. For a graceful end,
     * send_eof() first and close on the peer's CLOSED/REMOTE_EOF echo. */
    channel->outbuf_off = channel->outbuf.size;
    channel->flush_mark = channel->outbuf.size;
    channel->close_requested = 1;
    return yetty_ywire_connection_pump_outbound(channel->connection);
}

int64_t yetty_ywire_channel_send_window(const struct yetty_ywire_channel *channel)
{
    return channel ? channel->send_window : 0;
}

int yetty_ywire_channel_remote_eof(const struct yetty_ywire_channel *channel)
{
    return channel ? channel->remote_eof : 1;
}

size_t yetty_ywire_channel_pending_out(const struct yetty_ywire_channel *channel)
{
    if (!channel) {
        return 0;
    }
    return channel->outbuf.size - channel->outbuf_off;
}

/*===========================================================================
 * channel -> yclass transport adapter (so the RPC session binds a channel)
 *=========================================================================*/

struct ywire_channel_transport {
    struct yetty_yclass_transport base;
    struct yetty_ywire_channel *channel;
};

static struct yetty_ycore_size_result channel_transport_send(struct yetty_yclass_transport *base,
                                                             const void *bytes, size_t len)
{
    struct ywire_channel_transport *adapter = (struct ywire_channel_transport *)base;
    return yetty_ywire_channel_write(adapter->channel, bytes, len);
}

static struct yetty_ycore_size_result channel_transport_recv(struct yetty_yclass_transport *base,
                                                             void *buf, size_t max)
{
    struct ywire_channel_transport *adapter = (struct ywire_channel_transport *)base;
    return yetty_ywire_channel_recv_blocking(adapter->channel, buf, max);
}

static struct yetty_ycore_void_result channel_transport_flush(struct yetty_yclass_transport *base)
{
    struct ywire_channel_transport *adapter = (struct ywire_channel_transport *)base;
    return yetty_ywire_channel_flush(adapter->channel);
}

static struct yetty_ycore_void_result channel_transport_destroy(struct yetty_yclass_transport *base)
{
    free(base); /* borrows the channel; only the adapter is freed */
    return YETTY_OK_VOID();
}

struct yetty_yclass_transport_ptr_result yetty_ywire_channel_transport(
    struct yetty_ywire_channel *channel)
{
    if (!channel) {
        return YETTY_ERR(yetty_yclass_transport_ptr, "ywire_channel_transport: NULL channel");
    }
    struct ywire_channel_transport *adapter = calloc(1, sizeof(*adapter));
    if (!adapter) {
        return YETTY_ERR(yetty_yclass_transport_ptr, "ywire_channel_transport: calloc");
    }
    static const struct yetty_yclass_transport_ops ops = {
        .send = channel_transport_send,
        .recv = channel_transport_recv,
        .destroy = channel_transport_destroy,
        .flush = channel_transport_flush,
    };
    adapter->base.ops = &ops;
    adapter->channel = channel;
    return YETTY_OK(yetty_yclass_transport_ptr, &adapter->base);
}
