/*
 * channel.h — one logical lane of a yetty_ywire_connection.
 *
 * A channel is an independent, ordered, framed byte sub-stream multiplexed over
 * the single terminal byte stream — SSH's "channel". One connection owns many
 * channels. There is ONE channel type; the kinds (rpc / input / raw) are just
 * well-known channel ids opened at startup, mapped onto the existing yetty wire
 * codes (rpc = DCS YETTY_DCS_YCLASS_RPC, input = OSC YETTY_OSC_SC_CLIENT_INPUT_*,
 * raw = the wire-statemachine default sink). Everything else is a dynamic
 * channel opened on demand (the OPEN/DATA/EOF/CLOSE message set + per-channel
 * windows are reserved here; the dynamic-channel wire handshake is a follow-on).
 *
 * Inbound delivery is push (a sink fires per decoded message) OR pull (bytes
 * buffer and are drained via read()). Outbound bytes are coalesced via write()
 * and framed onto the wire by flush().
 */

#ifndef YETTY_YWIRE_CHANNEL_H
#define YETTY_YWIRE_CHANNEL_H

#include <yetty/yclass/transport.h> /* yetty_yclass_transport_ptr_result */
#include <yetty/ycore/result.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ywire_channel;

/* Well-known channel ids, opened at connection create. Dynamic channels start
 * at _DYNAMIC_BASE. */
#define YETTY_YWIRE_CHANNEL_RPC 1u
#define YETTY_YWIRE_CHANNEL_INPUT 2u
#define YETTY_YWIRE_CHANNEL_RAW 3u
#define YETTY_YWIRE_CHANNEL_DYNAMIC_BASE 16u

enum yetty_ywire_channel_kind {
    YETTY_YWIRE_CHANNEL_KIND_RPC = 0, /* yclass RPC frames (DCS, byte stream) */
    YETTY_YWIRE_CHANNEL_KIND_INPUT,   /* mouse/key/resize events (OSC, messages) */
    YETTY_YWIRE_CHANNEL_KIND_RAW,     /* terminal text passthrough (verbatim) */
    YETTY_YWIRE_CHANNEL_KIND_DYNAMIC, /* opened on demand */
};

/* SSH connection-layer message set for the (deferred) dynamic-channel wire
 * handshake. The well-known channels are implicit DATA on a pre-opened id. */
enum yetty_ywire_channel_msg {
    YETTY_YWIRE_CHANNEL_MSG_OPEN = 1,
    YETTY_YWIRE_CHANNEL_MSG_DATA = 2,
    YETTY_YWIRE_CHANNEL_MSG_EOF = 3,
    YETTY_YWIRE_CHANNEL_MSG_CLOSE = 4,
};

/* Push sink for a message channel (input): one call per decoded envelope.
 * `args` may be NULL/0; `payload` is the decoded body. Valid for the call. */
typedef void (*yetty_ywire_channel_envelope_sink)(void *user, int wire_code, const uint8_t *args,
                                                  size_t args_len, const uint8_t *payload,
                                                  size_t payload_len);

/* Push sink for a byte channel (raw): runs of bytes outside any envelope. */
typedef void (*yetty_ywire_channel_raw_sink)(void *user, const uint8_t *bytes, size_t n);

/* Identity. */
uint32_t yetty_ywire_channel_id(const struct yetty_ywire_channel *channel);
enum yetty_ywire_channel_kind yetty_ywire_channel_kind_of(
    const struct yetty_ywire_channel *channel);

/* Outbound: coalesce bytes, then frame them onto the wire. */
struct yetty_ycore_size_result yetty_ywire_channel_write(struct yetty_ywire_channel *channel,
                                                         const void *bytes, size_t len);
struct yetty_ycore_void_result yetty_ywire_channel_flush(struct yetty_ywire_channel *channel);

/* Inbound (pull): copy already-demuxed bytes out. Returns 0 when empty
 * (non-blocking). */
struct yetty_ycore_size_result yetty_ywire_channel_read(struct yetty_ywire_channel *channel,
                                                        void *buf, size_t max);

/* Inbound (blocking): flush pending outbound, then pump the connection until
 * this channel has bytes (or the link dies). For the synchronous attach
 * handshake before the host loop owns the fd. Returns 0 on EOF/timeout. */
struct yetty_ycore_size_result yetty_ywire_channel_recv_blocking(
    struct yetty_ywire_channel *channel, void *buf, size_t max);

/* Inbound (push): register a sink fired during the connection's pump. */
struct yetty_ycore_void_result yetty_ywire_channel_set_envelope_sink(
    struct yetty_ywire_channel *channel, yetty_ywire_channel_envelope_sink sink, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_set_raw_sink(struct yetty_ywire_channel *channel,
                                                                yetty_ywire_channel_raw_sink sink,
                                                                void *user);

/* A yetty_yclass_transport whose send/recv/flush ride this channel — so the
 * yclass RPC session binds the channel instead of reading the fd. The returned
 * transport is heap-allocated and owned by the caller (typically handed to
 * yetty_yclass_rpc_session_create, which destroys it). It borrows the channel,
 * which must outlive it. */
struct yetty_yclass_transport_ptr_result yetty_ywire_channel_transport(
    struct yetty_ywire_channel *channel);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YWIRE_CHANNEL_H */
