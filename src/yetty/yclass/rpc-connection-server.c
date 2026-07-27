/*
 * rpc-connection-server.c — serve yclass RPC over a MULTIPLEXED ywire
 * connection (the SSH model).
 *
 * A host (a terminal) calls yetty_yclass_rpc_serve_connection() once on its
 * acceptor connection. From then on every DYNAMIC channel a client opens
 * (SSH CHANNEL_OPEN) is accepted and served as its OWN independent RPC
 * session: the channel's byte stream is reassembled into request frames,
 * each dispatched via yetty_yclass_rpc_dispatch_one, and the response
 * written back on the same channel.
 *
 * This is what lets several clients — in one process or many — share one
 * PTY without tearing each other's frames: each rides a separate channel.
 * The connection layer fires our accept callback (connection.c, during its
 * pump); the host never writes a callback itself.
 *
 * Wire framing on a channel (same as the fd/DCS transports):
 *   request:  u32 header | u32 body_len | u8 body[body_len]
 *   response: u32 resp_len | u8 resp[resp_len]   (omitted for one-way ops)
 */
#include <yetty/yclass/rpc.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Response scratch for one dispatch. Bounded — RPC responses are small (a
 * handle, a status byte, or a serialized error chain of a few KB); large
 * payloads travel as CALL bodies the other direction, never as responses. */
#define RPC_CHANNEL_RESP_MAX 65536u

/* Hard cap on a single reassembled request frame (header + body). A peer
 * declares body_len as an untrusted 32-bit field; without a cap it could force
 * the reassembly buffer to grow toward 4 GiB. Match the ordinary RPC path's
 * BUF_MAX (64 MiB) — any client frame above it is a protocol violation, not a
 * legitimate call. */
#define RPC_CHANNEL_FRAME_MAX (64u * 1024u * 1024u)

/* Per-accepted-channel server state: the request-frame reassembly buffer for
 * one client's channel. Freed when the channel closes. */
struct rpc_channel_server {
    struct yetty_ywire_channel *channel;
    uint8_t *inbuf;
    size_t inlen;
    size_t incap;
};

/* Peel and dispatch every complete request frame currently buffered. */
static void rpc_channel_drain(struct rpc_channel_server *server)
{
    size_t consumed = 0;
    while (server->inlen - consumed >= 8) {
        uint32_t header;
        uint32_t body_len;
        memcpy(&header, server->inbuf + consumed, 4);
        memcpy(&body_len, server->inbuf + consumed + 4, 4);
        if (body_len > RPC_CHANNEL_FRAME_MAX) {
            /* Untrusted length past the protocol cap — the stream is corrupt or
             * hostile and can never be reassembled (raw_sink refuses to grow
             * past the cap). Drop everything buffered and stop; the channel
             * layer surfaces the desync as its own error. */
            ywarn("rpc-conn: request body_len %u exceeds cap %u — dropping channel buffer",
                  body_len, RPC_CHANNEL_FRAME_MAX);
            server->inlen = 0;
            return;
        }
        if (server->inlen - consumed - 8 < body_len) {
            break; /* incomplete frame — wait for more bytes */
        }
        const uint8_t *body = server->inbuf + consumed + 8;

        uint8_t resp[RPC_CHANNEL_RESP_MAX];
        struct yetty_ycore_size_result dispatch_res =
            yetty_yclass_rpc_dispatch_one(header, body, body_len, resp, sizeof(resp));
        size_t resp_len = 0;
        if (YETTY_IS_OK(dispatch_res)) {
            resp_len = dispatch_res.value;
        } else {
            /* Logged server-side and answered with a zero-length response so
             * the client maps it to "remote impl returned error" and the
             * channel stays up for the next frame — same contract as the
             * fd/DCS server loops. */
            yetty_ycore_error_print(stderr, "[rpc-conn] dispatch", dispatch_res.error);
            yetty_ycore_error_destroy(dispatch_res.error);
        }
        consumed += 8 + body_len;

        /* One-way ops get NO response — the client never reads one. */
        if (YETTY_YCLASS_RPC_HDR_OP(header) == YETTY_YCLASS_RPC_OP_CALL_ONEWAY) {
            continue;
        }

        uint32_t resp_len_wire = (uint32_t)resp_len;
        struct yetty_ycore_size_result write_len_res =
            yetty_ywire_channel_write(server->channel, &resp_len_wire, 4);
        if (YETTY_IS_ERR(write_len_res)) {
            yetty_ycore_error_print(stderr, "[rpc-conn] write resp_len", write_len_res.error);
            yetty_ycore_error_destroy(write_len_res.error);
            break;
        }
        if (resp_len) {
            struct yetty_ycore_size_result write_body_res =
                yetty_ywire_channel_write(server->channel, resp, resp_len);
            if (YETTY_IS_ERR(write_body_res)) {
                yetty_ycore_error_print(stderr, "[rpc-conn] write resp", write_body_res.error);
                yetty_ycore_error_destroy(write_body_res.error);
                break;
            }
        }
        struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(server->channel);
        if (YETTY_IS_ERR(flush_res)) {
            yetty_ycore_error_print(stderr, "[rpc-conn] flush", flush_res.error);
            yetty_ycore_error_destroy(flush_res.error);
            break;
        }
    }
    if (consumed) {
        memmove(server->inbuf, server->inbuf + consumed, server->inlen - consumed);
        server->inlen -= consumed;
    }
}

/* Channel raw sink — one run of the client's request bytes, fired during the
 * connection's pump. Signature dictated by yetty_ywire_channel_raw_sink. */
YETTY_EXTERNAL_CALLBACK
static void rpc_channel_raw_sink(void *user, const uint8_t *bytes, size_t n)
{
    struct rpc_channel_server *server = user;
    if (!server || !bytes || n == 0) {
        return;
    }
    if (server->incap - server->inlen < n) {
        /* Overflow-guard the size arithmetic, then cap the buffer: a client
         * cannot force the reassembly allocation past one protocol frame. */
        if (n > SIZE_MAX - server->inlen) {
            ywarn("rpc-conn: reassembly size overflow — dropping channel buffer");
            server->inlen = 0;
            return;
        }
        size_t want = server->inlen + n;
        if (want > RPC_CHANNEL_FRAME_MAX) {
            ywarn("rpc-conn: reassembly want %zu exceeds cap %u — dropping channel buffer", want,
                  RPC_CHANNEL_FRAME_MAX);
            server->inlen = 0;
            return;
        }
        size_t grown = server->incap ? server->incap : 8192;
        while (grown < want) {
            grown *= 2;
            if (grown > RPC_CHANNEL_FRAME_MAX) {
                grown = RPC_CHANNEL_FRAME_MAX; /* clamp; want <= cap guaranteed above */
                break;
            }
        }
        uint8_t *bigger = realloc(server->inbuf, grown);
        if (!bigger) {
            /* Drop the whole partial frame — keeping it would append the next
             * chunk into a corrupted, mis-aligned buffer. */
            ywarn("rpc-conn: reassembly grow failed (%zu bytes) — dropping channel buffer", grown);
            server->inlen = 0;
            return;
        }
        server->inbuf = bigger;
        server->incap = grown;
    }
    memcpy(server->inbuf + server->inlen, bytes, n);
    server->inlen += n;
    rpc_channel_drain(server);
}

/* Channel lifecycle — free the per-channel server on CLOSED. Signature
 * dictated by yetty_ywire_channel_event_cb. */
YETTY_EXTERNAL_CALLBACK
static void rpc_channel_event(void *user, struct yetty_ywire_channel *channel,
                              enum yetty_ywire_channel_event event)
{
    (void)channel;
    struct rpc_channel_server *server = user;
    if (event == YETTY_YWIRE_CHANNEL_EVENT_CLOSED && server) {
        free(server->inbuf);
        free(server);
    }
}

/* Accept callback — a client opened a channel. Stand up a per-channel RPC
 * server and wire it to the channel's byte stream. Return 1 to accept.
 * Signature dictated by yetty_ywire_accept_cb; a failure refuses the
 * channel (returns 0), which the connection answers with CLOSE. */
YETTY_EXTERNAL_CALLBACK
static int rpc_connection_accept(void *user, struct yetty_ywire_channel *channel)
{
    (void)user;
    if (!channel) {
        return 0;
    }
    struct rpc_channel_server *server = calloc(1, sizeof(*server));
    if (!server) {
        ywarn("rpc-conn: accept alloc failed — refusing channel");
        return 0;
    }
    server->channel = channel;

    struct yetty_ycore_void_result sink_res =
        yetty_ywire_channel_set_raw_sink(channel, rpc_channel_raw_sink, server);
    if (YETTY_IS_ERR(sink_res)) {
        yetty_ycore_error_print(stderr, "rpc-conn: set_raw_sink", sink_res.error);
        yetty_ycore_error_destroy(sink_res.error);
        free(server);
        return 0;
    }
    struct yetty_ycore_void_result event_res =
        yetty_ywire_channel_set_event_cb(channel, rpc_channel_event, server);
    if (YETTY_IS_ERR(event_res)) {
        /* The channel is already accepted for byte delivery; without the
         * event cb we leak the state on close but service is correct. Log. */
        yetty_ycore_error_print(stderr, "rpc-conn: set_event_cb (state will leak on close)",
                                event_res.error);
        yetty_ycore_error_destroy(event_res.error);
    }
    ydebug("rpc-conn: serving channel id=%u", yetty_ywire_channel_id(channel));
    return 1;
}

struct yetty_ycore_void_result yetty_yclass_rpc_serve_connection(
    struct yetty_ywire_connection *connection)
{
    if (!connection) {
        return YETTY_ERR(yetty_ycore_void, "rpc_serve_connection: NULL connection");
    }
    /* One call: every dynamic channel a client opens from here on is accepted
     * and served as its own RPC session. The accept callback lives here, not
     * in the host — the host just "serves RPC on this connection". */
    return yetty_ywire_connection_set_accept_cb(connection, rpc_connection_accept, NULL);
}
