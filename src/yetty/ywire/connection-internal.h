/*
 * connection-internal.h — module-private struct definitions shared by
 * connection.c and channel.c. NOT installed; the public types stay opaque in
 * <yetty/ywire/connection.h> and <yetty/ywire/channel.h>.
 *
 * connection.c owns the transport + state machine + the channel array and does
 * the demux (writing into / firing channel sinks); channel.c owns the per-lane
 * write/flush/read and frames outbound through the connection's transport. Both
 * need the full structs, so they live here.
 */

#ifndef YETTY_YWIRE_CONNECTION_INTERNAL_H
#define YETTY_YWIRE_CONNECTION_INTERNAL_H

#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>

#include <yetty/yclass/transport-reactor.h>
#include <yetty/ycore/types.h> /* struct yetty_ycore_buffer */

#include <stddef.h>
#include <stdint.h>

struct yetty_ywire_wire_statemachine;

#define YETTY_YWIRE_CHANNEL_MAX 16

struct yetty_ywire_channel {
    struct yetty_ywire_connection *connection; /* owning connection (back-ref) */
    uint32_t id;
    enum yetty_ywire_channel_kind kind;
    int in_use;

    /* Outbound wire mapping. For envelope channels (rpc): wire_kind is the
     * yetty_ywire_envelope_kind and wire_code the DCS/OSC code. For the raw
     * channel: wire_code < 0 → emitted verbatim (tmux-wrapped). */
    int wire_kind;
    int wire_code;
    int has_args;

    /* Inbound demuxed bytes for pull consumers (rpc; raw fallback). */
    struct yetty_ycore_buffer inbuf;
    size_t inbuf_off;
    /* Outbound coalescing buffer (one frame's worth between flushes). */
    struct yetty_ycore_buffer outbuf;

    /* Push sinks. */
    yetty_ywire_channel_envelope_sink env_sink;
    yetty_ywire_channel_raw_sink raw_sink;
    void *sink_user;

    /* Reserved per-channel flow-control windows (SSH CHANNEL_WINDOW_ADJUST
     * analog) — not yet negotiated on the wire. */
    int32_t send_window;
    int32_t recv_window;
};

struct yetty_ywire_connection {
    struct yetty_yclass_transport_reactor reactor; /* borrowed fd owner */
    int compressed;

    struct yetty_ywire_wire_statemachine *sm; /* inbound demux */

    struct yetty_ywire_channel channels[YETTY_YWIRE_CHANNEL_MAX];
    size_t channel_count;
    struct yetty_ywire_channel *chan_rpc;
    struct yetty_ywire_channel *chan_input;
    struct yetty_ywire_channel *chan_raw;

    yetty_ywire_resize_cb on_resize;
    void *resize_user;
};

/* Drive ONE blocking inbound step: poll the inbound fd, read what's there, and
 * demux it to channels. Returns 1 if it made progress, 0 on EOF/timeout.
 * Defined in connection.c; used by yetty_ywire_channel_recv_blocking. */
int yetty_ywire_connection_pump_blocking_once(struct yetty_ywire_connection *connection);

#endif /* YETTY_YWIRE_CONNECTION_INTERNAL_H */
