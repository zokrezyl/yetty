/*
 * channel.h — one logical lane of a yetty_ywire_connection.
 *
 * A channel is an independent, ordered, framed byte sub-stream multiplexed over
 * the single terminal byte stream — SSH's "channel". One connection owns many
 * channels. There is ONE channel type; the kinds (rpc / input / raw) are just
 * well-known channel ids opened at startup, mapped onto the existing yetty wire
 * codes (rpc = DCS YETTY_DCS_YCLASS_RPC, input = OSC YETTY_OSC_SC_CLIENT_INPUT_*,
 * raw = the wire-statemachine default sink). Everything else is a dynamic
 * channel opened on demand via yetty_ywire_connection_open_channel(): its
 * traffic rides DCS YETTY_DCS_YWIRE_CHANNEL envelopes carrying the
 * OPEN/DATA/EOF/CLOSE(/WINDOW_ADJUST) message set plus a channel id.
 *
 * Dynamic-channel semantics (the deltas from SSH):
 *   - OPEN is optimistic — there is no OPEN_CONFIRMATION. The opener may write
 *     immediately; a peer that cannot or will not accept answers CLOSE, which
 *     surfaces as a CLOSED event on the opener's channel.
 *   - Flow control: each direction has a byte window. A sender consumes window
 *     with every DATA byte and stalls (buffers locally) at zero; the receiver
 *     grants credit back with WINDOW_ADJUST as the consumer drains. Windows are
 *     enforced on DYNAMIC channels only — the well-known lanes speak to a host
 *     that predates the channel protocol and would never grant credit back.
 *   - Chunking: DATA is emitted in chunks of at most
 *     YETTY_YWIRE_CHANNEL_CHUNK_MAX decoded bytes, fair-interleaved across
 *     channels by the connection's outbound scheduler, and reassembled in
 *     order per channel on the receive side (a channel is a byte stream, so
 *     reassembly is plain in-order append). One large write can therefore not
 *     head-of-line-block the rpc/input lanes.
 *   - EOF is a half-close of the local→remote direction; the reverse direction
 *     stays usable until CLOSE. CLOSE is answered with CLOSE and releases the
 *     channel slot on both sides (ids are never reused).
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
YETTY_YRESULT_DECLARE(yetty_ywire_channel_ptr, struct yetty_ywire_channel *);

/* Well-known channel ids, opened at connection create. Dynamic channels start
 * at _DYNAMIC_BASE: the connection's initiator side allocates even offsets
 * (16, 18, …), the acceptor side odd (17, 19, …) — see
 * yetty_ywire_connection_set_role() — so both peers can open without an id
 * exchange. Ids are never reused within a connection. */
#define YETTY_YWIRE_CHANNEL_RPC 1u
#define YETTY_YWIRE_CHANNEL_INPUT 2u
#define YETTY_YWIRE_CHANNEL_RAW 3u
#define YETTY_YWIRE_CHANNEL_DYNAMIC_BASE 16u

/* Default per-direction flow-control window for a dynamic channel, in decoded
 * body bytes. OPEN may override the opener's receive window. */
#define YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT (256u * 1024u)

/* Ceiling on decoded body bytes per DATA envelope. Larger writes are split and
 * fair-interleaved with other channels' traffic by the outbound scheduler. */
#define YETTY_YWIRE_CHANNEL_CHUNK_MAX (16u * 1024u)

enum yetty_ywire_channel_kind {
    YETTY_YWIRE_CHANNEL_KIND_RPC = 0, /* yclass RPC frames (DCS, byte stream) */
    YETTY_YWIRE_CHANNEL_KIND_INPUT,   /* mouse/key/resize events (OSC, messages) */
    YETTY_YWIRE_CHANNEL_KIND_RAW,     /* terminal text passthrough (verbatim) */
    YETTY_YWIRE_CHANNEL_KIND_DYNAMIC, /* opened on demand */
};

/* SSH connection-layer message set carried on DCS YETTY_DCS_YWIRE_CHANNEL for
 * dynamic channels. The well-known channels are implicit DATA on a pre-opened
 * id and never appear on this code. */
enum yetty_ywire_channel_msg {
    YETTY_YWIRE_CHANNEL_MSG_OPEN = 1,  /* window = opener's recv window (0 = default) */
    YETTY_YWIRE_CHANNEL_MSG_DATA = 2,  /* payload = one chunk of channel bytes */
    YETTY_YWIRE_CHANNEL_MSG_EOF = 3,   /* half-close, sender will write no more */
    YETTY_YWIRE_CHANNEL_MSG_CLOSE = 4, /* teardown (or OPEN rejection); answered with CLOSE */
    YETTY_YWIRE_CHANNEL_MSG_WINDOW_ADJUST = 5, /* window = credit delta granted to the peer */
};

/* Lifecycle notifications for a dynamic channel, fired during the connection's
 * pump. After CLOSED the channel pointer is dead — the slot is released (any
 * still-buffered inbound bytes remain readable until drained). */
enum yetty_ywire_channel_event {
    YETTY_YWIRE_CHANNEL_EVENT_REMOTE_EOF = 1, /* peer half-closed; reads will drain then stop */
    YETTY_YWIRE_CHANNEL_EVENT_CLOSED = 2,     /* close handshake done (or OPEN was rejected) */
};

typedef void (*yetty_ywire_channel_event_cb)(void *user, struct yetty_ywire_channel *channel,
                                             enum yetty_ywire_channel_event event);

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

/* Inbound (push): register a sink fired during the connection's pump. Dynamic
 * channels deliver DATA through the raw sink (a channel is a byte stream). */
struct yetty_ycore_void_result yetty_ywire_channel_set_envelope_sink(
    struct yetty_ywire_channel *channel, yetty_ywire_channel_envelope_sink sink, void *user);
struct yetty_ycore_void_result yetty_ywire_channel_set_raw_sink(struct yetty_ywire_channel *channel,
                                                                yetty_ywire_channel_raw_sink sink,
                                                                void *user);

/*===========================================================================
 * Dynamic-channel lifecycle (no-ops / errors on the well-known lanes)
 *=========================================================================*/

/* Lifecycle notifications (REMOTE_EOF / CLOSED). */
struct yetty_ycore_void_result yetty_ywire_channel_set_event_cb(struct yetty_ywire_channel *channel,
                                                                yetty_ywire_channel_event_cb cb,
                                                                void *user);

/* Half-close the local→remote direction: flush pending outbound, then emit
 * EOF. Further writes fail; the remote→local direction stays open. */
struct yetty_ycore_void_result yetty_ywire_channel_send_eof(struct yetty_ywire_channel *channel);

/* Tear the channel down: flush pending outbound, emit CLOSE. The slot is
 * released once the peer's CLOSE comes back (CLOSED event fires). */
struct yetty_ycore_void_result yetty_ywire_channel_close(struct yetty_ywire_channel *channel);

/* Bytes the peer currently allows us to send (flow-control credit). Unlimited
 * lanes (rpc / input / raw) report a negative value. */
int64_t yetty_ywire_channel_send_window(const struct yetty_ywire_channel *channel);

/* Non-zero once the peer half-closed (EOF received). Buffered bytes may still
 * be pending in read(). */
int yetty_ywire_channel_remote_eof(const struct yetty_ywire_channel *channel);

/* Outbound bytes accepted by write()/flush() but not yet framed onto the wire
 * (window-blocked or awaiting the fair scheduler). */
size_t yetty_ywire_channel_pending_out(const struct yetty_ywire_channel *channel);

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
