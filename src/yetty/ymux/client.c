/*
 * client.c — the ymux client endpoint: class@ymux:client (#695 phase 5).
 *
 * The attach side of the socket: connects to the ymux daemon role, attaches to a pane,
 * receives WELCOME frames, hosts the vtsink terminal-byte lane (#699.2),
 * retains the rich body, acks applied generations, and sends input/resize
 * verbs. Non-blocking step() pump, same shape as the daemon.
 */
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/yplatform/ipc-socket.h>
#include <yetty/ytrace/ytrace.h>

#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/vtsink.h>

#include "proto.h"
#include "rich-format.h"

/* Hand-written vtsink helpers (outside the generated header, like the raw-sink
 * seams) — see vtsink.c. */
struct yetty_ycore_void_result yetty_ymux_register(void);
struct yetty_yclass_object_ptr_result yetty_ymux_vtsink_make(void);
struct yetty_ycore_uint64_result yetty_ymux_vtsink_applied(struct yetty_yclass_object *obj);
void yetty_ymux_vtsink_set_emit(struct yetty_yclass_object *obj,
                                void (*emit)(uint64_t generation, const uint8_t *bytes, size_t len,
                                             void *userdata),
                                void *userdata);

enum {
    YMUX_CLIENT_RX_CAP = 4u << 20,
    YMUX_CLIENT_TX_CAP = 1u << 16,
    /* Matches the projector's per-attachment delivered-hash bound so the two
     * caches stay in lockstep (both fill in the same order from the same frames;
     * neither evicts). */
    YMUX_CLIENT_RESOURCE_MAX = 64,
};

/* One cached content-addressed creation payload (a heavy rich blob the daemon
 * sent once and later references by hash). */
struct client_resource {
    uint64_t hash;
    uint32_t *words; /* owned */
    uint32_t word_count;
};

struct YETTY_ANNOTATE("class@ymux:client") yetty_ymux_client {
    yetty_ipc_socket_t socket;
    uint32_t attachment_id;
    uint32_t pane_id;
    uint32_t permissions;
    uint32_t capabilities; /* what we advertised at attach (YMUX_TERM_CAP_*) */
    uint32_t view_rows;
    uint32_t view_cols;
    int attached; /* WELCOME received */
    int pane_exited;
    uint32_t last_refuse;
    /* The most recent rich body (rich-format words, owned copy) — the
     * embedder hands it to the scene's apply_content_transaction;
     * rich_generation counts arrivals so a fresh body is cheap to detect. */
    uint32_t *rich_body;
    size_t rich_body_words;
    uint64_t rich_generation;
    /* #699.2 vtsink lane — this client HOSTS the ordered terminal-byte sink
     * (class@ymux:vtsink) and the daemon drives it with typed feed() calls
     * tunnelled as YMUX_PROTO_VTSINK_RPC frames. Opt-in before attach
     * (enable_vtsink); on WELCOME the sink is created, registered, and its
     * [handle, feed slot id] published so the daemon's session needs zero
     * admin round-trips. ACK is sent AFTER dispatch applies a feed — the
     * applied generation, not the received one. */
    int vtsink_enabled;
    /* The emit destination handed to enable_vtsink — wired into the sink at
     * creation, so the very first feed (the fresh complete redraw the daemon
     * opens the lane with) is never dropped on an unwired sink. */
    void (*vtsink_emit)(uint64_t generation, const uint8_t *bytes, size_t len, void *userdata);
    void *vtsink_emit_userdata;
    struct yetty_yclass_object *vtsink; /* owned; NULL until WELCOME in lane mode */

    /* Overlay-first input routing (#699.4, review #11): pointer events are
     * consumed by the overlay exactly where its chrome is opaque (the yetty
     * hit test already resolved geometry — a hit on the overlay figure id IS
     * the consumption signal); key/paste events are consumed only while the
     * overlay has CLAIMED input focus (chrome mode). Consumed events go to
     * the handler seat; with no handler they are counted and dropped — the
     * accounting hook until interactive chrome exists. */
    int overlay_input_active;
    /* The client terminal's identity (review #17 item 8): appended to
     * ATTACH so the daemon resolves the capability profile through the
     * tmux terminfo/features state model. Empty = not sent (bitmask-only
     * legacy attach). */
    char term_name[64];
    char term_features[128];
    void (*overlay_input_handler)(uint32_t input_class, const uint8_t *bytes, size_t len,
                                  void *userdata);
    void *overlay_input_userdata;
    /* ORDERED input queue (review #16/#17): raw keystroke chunks and
     * overlay pointer events, in ARRIVAL order — one queue, so a click and
     * a key from the same wire pump dispatch in wire order. DYNAMIC
     * (review #17): saturation grows the array instead of rejecting —
     * pointer events have no overflow store, so rejection was loss. Only
     * OOM can refuse a push. Entries own their raw payloads. */
    struct yetty_ymux_ordered_entry {
        uint8_t kind; /* 1 = raw bytes, 2 = pointer */
        uint8_t *raw_bytes;
        uint32_t raw_len;
        float pointer_x, pointer_y;
        uint32_t pointer_kind, pointer_button, pointer_mods, pointer_pressed;
    } *ordered_queue;
    uint32_t ordered_head;
    uint32_t ordered_count;
    uint32_t ordered_capacity;
    /* Bracketed-paste classifier state (review #14): PERSISTS across raw
     * flushes so a \e[200~/\e[201~ delimiter split across chunks still
     * classifies correctly. carry holds a trailing proper prefix of a
     * delimiter awaiting the next chunk. */
    int overlay_paste_open;
    uint32_t overlay_input_acked_seq;   /* highest daemon-accepted sequence */
    uint32_t overlay_input_nacked_seq;  /* last NACK-refused sequence (0 none) */
    uint32_t overlay_input_nack_reason; /* its YMUX_PROTO_REFUSE_* reason */
    uint32_t chrome_release_count;      /* CHROME_RELEASE frames observed */
    uint8_t overlay_classify_carry[8];
    uint32_t overlay_classify_carry_len;
    /* Epoch-reset notification (review #13): fired on VTSINK_RESET after the
     * lane rx clears and BEFORE the re-publish, so the embedder resets the
     * RECEIVING terminal (fresh grid/parser) before the new epoch's complete
     * redraw arrives. */
    int (*vtsink_reset_handler)(void *userdata); /* 0 = receiver reset FAILED */
    /* A failed receiver reset leaves this set: every subsequent pump retries
     * the handler until it succeeds, THEN re-publishes — a transient failure
     * (e.g. a momentarily full enqueue) has a real retry path instead of
     * waiting for the next RESET/WELCOME (review #15). */
    int vtsink_reset_retry_pending;
    void *vtsink_reset_userdata;
    uint32_t pane_modes; /* PANE_MODES push: bit0 = app mouse subscribed */
    uint64_t overlay_consumed_pointer;
    uint64_t overlay_consumed_key;
    uint64_t overlay_consumed_paste;
    uint64_t vtsink_handle;
    uint32_t vtsink_feed_rid;
    uint64_t vtsink_acked; /* last applied generation ACKed to the daemon */
    /* Deferred-ACK mode (#699.6): the embedder ACKs explicitly (vtsink_ack)
     * once its downstream write — the scene RPC to the renderer — completed,
     * instead of the demux auto-ACKing right after local dispatch. */
    int vtsink_defer_ack;
    uint8_t *vtsink_rx; /* lane frame reassembly (partial request frames) */
    size_t vtsink_rx_len;
    size_t vtsink_rx_capacity;
    /* Figure-surface RPC relay sink (#695: ygui/ygreeter proxy): invoked with a
     * proxied channel's request bytes the daemon forwarded. The embedder pipes
     * them to a matching channel on its own yetty connection. NULL = no sink. */
    void (*rpc_relay_sink)(uint32_t channel_id, const uint8_t *bytes, size_t len, void *userdata);
    void *rpc_relay_sink_userdata;
    /* Fires when the daemon reports a proxied channel closed (the pane app ended
     * its RPC): the embedder tears down its matching upstream channel. */
    void (*rpc_relay_close_sink)(uint32_t channel_id, void *userdata);
    void *rpc_relay_close_sink_userdata;
    /* Presentation effects (exactly-once, daemon-routed): counters move
     * per arrival; the embedder consumes and presents. */
    uint64_t bell_count;
    char title[256];
    uint64_t title_generation;
    /* Session-verb replies (new/list/has/kill/rename): last status +
     * text, with an arrival counter the CLI polls on. */
    uint32_t reply_status;
    char reply_text[512];
    uint64_t reply_generation;
    char *clipboard_text; /* owned; NULL until a clipboard effect */
    size_t clipboard_len;
    int clipboard_target; /* 1 clipboard, 0 primary */
    uint64_t clipboard_generation;
    uint8_t *rx;
    size_t rx_len;
    uint8_t *tx;
    size_t tx_len;
    /* Content-addressed rich payload cache (#695): the heavy creation payloads
     * the daemon sent once; a later frame references them by hash and this
     * resolves them back to the full rich body the scene consumes. */
    struct client_resource resource_cache[YMUX_CLIENT_RESOURCE_MAX];
    uint32_t resource_cache_count;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_client_class_get(void);
struct yetty_ymux_client_ptr_result yetty_ymux_client_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_client_ptr, struct yetty_ymux_client *);

/*===========================================================================
 * Frame plumbing.
 *=========================================================================*/

static struct yetty_ycore_void_result client_enqueue(struct yetty_ymux_client *client,
                                                     uint32_t type, const void *payload,
                                                     size_t payload_len)
{
    size_t frame_len = YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t) + payload_len;
    if (client->tx_len + frame_len > YMUX_CLIENT_TX_CAP) {
        return YETTY_ERR(yetty_ycore_void, "ymux client: tx overflow");
    }
    uint32_t header[YMUX_PROTO_HEADER_WORDS] = {YMUX_PROTO_MAGIC, type, (uint32_t)payload_len};
    memcpy(client->tx + client->tx_len, header, sizeof(header));
    client->tx_len += sizeof(header);
    if (payload_len) {
        memcpy(client->tx + client->tx_len, payload, payload_len);
        client->tx_len += payload_len;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result client_flush(struct yetty_ymux_client *client)
{
    if (!client->socket) {
        client->tx_len = 0; /* dead server: nothing to flush to */
        return YETTY_OK_VOID();
    }
    while (client->tx_len) {
        struct yetty_ycore_size_result send_res =
            yetty_platform_socket_send(client->socket, client->tx, client->tx_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, send_res, "ymux client: send");
        if (send_res.value == 0) {
            break; /* would block; the rest goes next step */
        }
        memmove(client->tx, client->tx + send_res.value, client->tx_len - send_res.value);
        client->tx_len -= send_res.value;
    }
    return YETTY_OK_VOID();
}

/* Bounded blocking drain of the client's queued frames. client_flush() above is
 * nonblocking — it returns as soon as the socket would block, leaving bytes in
 * client->tx. A teardown that enqueues RPC_RELAY_CLOSE / DETACH and then disposes
 * the client would drop them under backpressure. Pump the socket writable until
 * the queue empties or a 200-spin / 50 ms-poll bound (forced-close fallback), so
 * the daemon actually receives the closes before the transport goes away. */
struct yetty_ycore_void_result yetty_ymux_client_drain(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_drain: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    if (!client->socket) {
        client->tx_len = 0;
        return YETTY_OK_VOID();
    }
    int socket_fd = yetty_platform_socket_get_fd(client->socket);
    for (int spin = 0; spin < 200 && client->tx_len > 0; ++spin) {
        struct yetty_ycore_void_result flush_res = client_flush(client);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, flush_res, "ymux client_drain: flush");
        if (client->tx_len == 0) {
            break;
        }
        struct pollfd poll_fd = {.fd = socket_fd, .events = POLLOUT};
        if (poll(&poll_fd, 1, 50) <= 0) {
            break; /* not writable within the bound — forced-close fallback */
        }
    }
    return YETTY_OK_VOID();
}

static const uint32_t *client_resource_lookup(struct yetty_ymux_client *client, uint64_t hash,
                                              uint32_t *out_count)
{
    for (uint32_t index = 0; index < client->resource_cache_count; ++index) {
        if (client->resource_cache[index].hash == hash) {
            if (out_count) {
                *out_count = client->resource_cache[index].word_count;
            }
            return client->resource_cache[index].words;
        }
    }
    return NULL;
}

static void client_resource_store(struct yetty_ymux_client *client, uint64_t hash,
                                  const uint32_t *words, uint32_t word_count)
{
    if (client_resource_lookup(client, hash, NULL) ||
        client->resource_cache_count >= YMUX_CLIENT_RESOURCE_MAX) {
        return;
    }
    uint32_t *copy = malloc((size_t)word_count * sizeof(uint32_t));
    if (!copy) {
        return;
    }
    memcpy(copy, words, (size_t)word_count * sizeof(uint32_t));
    struct client_resource *slot = &client->resource_cache[client->resource_cache_count++];
    slot->hash = hash;
    slot->words = copy;
    slot->word_count = word_count;
}

static void client_resource_cache_free(struct yetty_ymux_client *client)
{
    for (uint32_t index = 0; index < client->resource_cache_count; ++index) {
        free(client->resource_cache[index].words);
    }
    client->resource_cache_count = 0;
}

/* Content-addressed rich body (records carrying YMUX_RICH_FLAG_HASHED) ->  the
 * plain rich format the scene consumes: cache each full creation payload by
 * hash, resolve each reference back to its cached bytes. Returns a new body
 * (caller frees) + its word count in *out_count, or NULL on a malformed frame or
 * a reference to a payload that was never cached (a desync — the caller resyncs).
 * A body with no HASHED records is duplicated verbatim. */
static uint32_t *client_resolve_rich(struct yetty_ymux_client *client, const uint32_t *in,
                                     size_t in_count, size_t *out_count)
{
    if (in_count < YMUX_RICH_HEADER_WORDS || in[0] != YMUX_RICH_MAGIC) {
        return NULL;
    }
    uint32_t record_count = in[2];

    /* Pass 1: validate + compute the resolved output size. creation_count sits
     * in every HASHED record (payload word 2), so sizing needs no cache. */
    size_t offset = YMUX_RICH_HEADER_WORDS;
    size_t resolved = YMUX_RICH_HEADER_WORDS;
    for (uint32_t rec = 0; rec < record_count; ++rec) {
        if (offset + YMUX_RICH_RECORD_HEADER_WORDS > in_count) {
            return NULL;
        }
        uint32_t flags = in[offset + 5];
        uint32_t payload_words = in[offset + 6];
        offset += YMUX_RICH_RECORD_HEADER_WORDS;
        if (offset + payload_words > in_count) {
            return NULL;
        }
        if (flags & YMUX_RICH_FLAG_HASHED) {
            if (payload_words < 3) {
                return NULL;
            }
            uint32_t creation_count = in[offset + 2];
            uint32_t body_words = payload_words - 3; /* after [hash_lo][hash_hi][creation_count] */
            uint32_t journal_words =
                (flags & YMUX_RICH_FLAG_RESOURCE_REF) ? body_words : (body_words - creation_count);
            if (!(flags & YMUX_RICH_FLAG_RESOURCE_REF) && creation_count > body_words) {
                return NULL;
            }
            resolved += YMUX_RICH_RECORD_HEADER_WORDS + creation_count + journal_words;
        } else {
            resolved += YMUX_RICH_RECORD_HEADER_WORDS + payload_words;
        }
        offset += payload_words;
    }

    uint32_t *out = malloc(resolved * sizeof(uint32_t));
    if (!out) {
        return NULL;
    }
    out[0] = in[0];
    out[1] = in[1];
    out[2] = in[2];
    size_t write = YMUX_RICH_HEADER_WORDS;
    offset = YMUX_RICH_HEADER_WORDS;
    for (uint32_t rec = 0; rec < record_count; ++rec) {
        uint32_t flags = in[offset + 5];
        uint32_t payload_words = in[offset + 6];
        const uint32_t *header = &in[offset];
        offset += YMUX_RICH_RECORD_HEADER_WORDS;
        const uint32_t *payload = &in[offset];
        offset += payload_words;
        if (!(flags & YMUX_RICH_FLAG_HASHED)) {
            memcpy(&out[write], header, YMUX_RICH_RECORD_HEADER_WORDS * sizeof(uint32_t));
            memcpy(&out[write + YMUX_RICH_RECORD_HEADER_WORDS], payload,
                   (size_t)payload_words * sizeof(uint32_t));
            write += YMUX_RICH_RECORD_HEADER_WORDS + payload_words;
            continue;
        }
        uint64_t hash = (uint64_t)payload[0] | ((uint64_t)payload[1] << 32);
        uint32_t creation_count = payload[2];
        const uint32_t *creation = NULL;
        const uint32_t *journals = NULL;
        uint32_t journal_words = 0;
        if (flags & YMUX_RICH_FLAG_RESOURCE_REF) {
            uint32_t cached_count = 0;
            creation = client_resource_lookup(client, hash, &cached_count);
            if (!creation || cached_count != creation_count) {
                free(out);
                return NULL; /* uncached reference -> desync; caller resyncs */
            }
            journals = payload + 3;
            journal_words = payload_words - 3;
        } else {
            creation = payload + 3;
            journals = payload + 3 + creation_count;
            journal_words = payload_words - 3 - creation_count;
            client_resource_store(client, hash, creation, creation_count);
        }
        uint32_t out_flags = flags & ~(YMUX_RICH_FLAG_HASHED | YMUX_RICH_FLAG_RESOURCE_REF);
        out[write + 0] = header[0];
        out[write + 1] = header[1];
        out[write + 2] = header[2];
        out[write + 3] = header[3];
        out[write + 4] = header[4];
        out[write + 5] = out_flags;
        out[write + 6] = creation_count + journal_words; /* plain payload_words */
        write += YMUX_RICH_RECORD_HEADER_WORDS;
        memcpy(&out[write], creation, (size_t)creation_count * sizeof(uint32_t));
        write += creation_count;
        memcpy(&out[write], journals, (size_t)journal_words * sizeof(uint32_t));
        write += journal_words;
    }
    *out_count = write;
    return out;
}

/* Host the vtsink and publish [handle, feed slot id] to the daemon (#699.2).
 * The feed slot id is resolved LOCALLY via dispatch_one(RESOLVE_SLOT), so the
 * daemon's lane session never needs a blocking admin round-trip. */
static struct yetty_ycore_void_result client_vtsink_publish(struct yetty_ymux_client *client)
{
    if (!client->vtsink) {
        /* First welcome: create + register the sink and resolve the feed slot
         * locally. Reconnects reuse all three (the handle stays valid in this
         * process's RPC table). */
        struct yetty_ycore_void_result register_res = yetty_ymux_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, register_res, "vtsink publish: ymux register");
        struct yetty_ycore_void_result init_res = yetty_yclass_rpc_init();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, init_res, "vtsink publish: rpc init");

        struct yetty_yclass_object_ptr_result sink_res = yetty_ymux_vtsink_make();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sink_res, "vtsink publish: make");
        if (client->vtsink_emit) {
            yetty_ymux_vtsink_set_emit(sink_res.value, client->vtsink_emit,
                                       client->vtsink_emit_userdata);
        }
        struct yetty_yclass_handle_result handle_res =
            yetty_yclass_rpc_register_object(sink_res.value);
        if (YETTY_IS_ERR(handle_res)) {
            struct yetty_ycore_void_result free_res = yetty_yclass_object_free(sink_res.value);
            if (YETTY_IS_ERR(free_res)) {
                yetty_ycore_error_destroy(free_res.error);
            }
            return YETTY_ERR(yetty_ycore_void, "vtsink publish: register object", handle_res);
        }

        static const char feed_slot_name[] = "yetty_ymux_feed";
        uint32_t feed_rid = YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED;
        uint32_t resolve_header = YETTY_YCLASS_RPC_HDR_MAKE(YETTY_YCLASS_RPC_OP_RESOLVE_SLOT, 0u);
        struct yetty_ycore_size_result resolve_res =
            yetty_yclass_rpc_dispatch_one(resolve_header, feed_slot_name,
                                          sizeof(feed_slot_name) - 1, &feed_rid, sizeof(feed_rid));
        if (YETTY_IS_ERR(resolve_res) || feed_rid == YETTY_YCLASS_RPC_REMOTE_ID_UNRESOLVED) {
            if (YETTY_IS_ERR(resolve_res)) {
                return YETTY_ERR(yetty_ycore_void, "vtsink publish: local feed resolve",
                                 resolve_res);
            }
            return YETTY_ERR(yetty_ycore_void, "vtsink publish: feed slot unresolved");
        }
        client->vtsink = sink_res.value;
        client->vtsink_handle = handle_res.value;
        client->vtsink_feed_rid = feed_rid;
    }

    uint8_t publish_payload[sizeof(uint64_t) + sizeof(uint32_t)];
    memcpy(publish_payload, &client->vtsink_handle, sizeof(uint64_t));
    memcpy(publish_payload + sizeof(uint64_t), &client->vtsink_feed_rid, sizeof(uint32_t));
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_VTSINK_PUBLISH, publish_payload, sizeof(publish_payload));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "vtsink publish: enqueue");
    client->vtsink_acked = 0;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result client_handle_frame(struct yetty_ymux_client *client,
                                                          uint32_t type, const uint8_t *payload,
                                                          size_t payload_len)
{
    switch (type) {
    case YMUX_PROTO_WELCOME: {
        if (payload_len < 5 * sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: short welcome");
        }
        uint32_t words[5];
        memcpy(words, payload, sizeof(words));
        client->attachment_id = words[0];
        client->pane_id = words[1];
        client->permissions = words[2];
        client->attached = 1;
        client->pane_exited = 0;
        if (client->vtsink_enabled && (client->capabilities & YMUX_TERM_CAP_VT_TEXT)) {
            /* The vtsink lane is the SOLE text path for a VT_TEXT client
             * (#699.2): a hosting/publish failure means no text would ever
             * arrive — fail the WELCOME loudly rather than sit silent. The
             * publish repeats on EVERY welcome: a reconnect is a NEW daemon
             * connection whose lane state starts empty (the sink object and
             * its handle are created once and reused). */
            struct yetty_ycore_void_result publish_res = client_vtsink_publish(client);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, publish_res, "ymux client: vtsink publish");
        }
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_TRANSACTION: {
        /* Rich-only content transaction (#699.3): the payload IS the rich body
         * (rich-format words) — the retired semantic paint half has no wire
         * representation at all. */
        size_t rich_bytes = payload_len;
        if (rich_bytes % sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: ragged transaction rich");
        }
        uint32_t *rich_copy = NULL;
        if (rich_bytes) {
            rich_copy = malloc(rich_bytes);
            if (!rich_copy) {
                return YETTY_ERR(yetty_ycore_void, "ymux client: rich copy alloc");
            }
            memcpy(rich_copy, payload, rich_bytes);
            /* Content addressing: resolve hash-referenced creation payloads back
             * to the plain rich body the scene consumes (cache by hash). */
            if (client->capabilities & YMUX_TERM_CAP_RESOURCE_REF) {
                size_t resolved_count = 0;
                uint32_t *resolved = client_resolve_rich(
                    client, rich_copy, rich_bytes / sizeof(uint32_t), &resolved_count);
                if (!resolved) {
                    free(rich_copy);
                    /* A referenced resource was never cached (local cache miss or
                     * a store alloc failure). This is RECOVERABLE, not fatal: ask
                     * the daemon for a fresh complete redraw (which re-sends the
                     * resource inline) and drop this frame, rather than failing
                     * client_step() and detaching the whole session. */
                    struct yetty_ycore_void_result resync_res =
                        client_enqueue(client, YMUX_PROTO_RESYNC, NULL, 0);
                    if (YETTY_IS_OK(resync_res)) {
                        resync_res = client_flush(client);
                    }
                    if (YETTY_IS_ERR(resync_res)) {
                        yetty_ycore_error_destroy(resync_res.error);
                    }
                    return YETTY_OK_VOID();
                }
                free(rich_copy);
                rich_copy = resolved;
                rich_bytes = resolved_count * sizeof(uint32_t);
            }
        }
        if (rich_bytes) {
            free(client->rich_body);
            client->rich_body = rich_copy;
            client->rich_body_words = rich_bytes / sizeof(uint32_t);
            client->rich_generation++;
        }
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_REFUSE: {
        if (payload_len >= sizeof(uint32_t)) {
            memcpy(&client->last_refuse, payload, sizeof(uint32_t));
        }
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_PANE_EXIT:
        client->pane_exited = 1;
        client->attached = 0;
        client->attachment_id = 0;
        return YETTY_OK_VOID();
    case YMUX_PROTO_RPC_RELAY: {
        /* Figure-surface RPC (ygui/ygreeter): request bytes on a proxied channel
         * the daemon tunnelled. Hand them to the embedder, which pipes them to a
         * matching channel on its own yetty connection. */
        if (payload_len < sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: short rpc relay");
        }
        if (client->rpc_relay_sink) {
            uint32_t channel_id;
            memcpy(&channel_id, payload, sizeof(uint32_t));
            client->rpc_relay_sink(channel_id, payload + sizeof(uint32_t),
                                   payload_len - sizeof(uint32_t), client->rpc_relay_sink_userdata);
        }
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_RPC_RELAY_CLOSE: {
        /* The pane app closed a proxied RPC channel; tear down our upstream one. */
        if (payload_len < sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: short rpc relay close");
        }
        if (client->rpc_relay_close_sink) {
            uint32_t channel_id;
            memcpy(&channel_id, payload, sizeof(uint32_t));
            client->rpc_relay_close_sink(channel_id, client->rpc_relay_close_sink_userdata);
        }
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_PANE_MODES: {
        if (payload_len >= sizeof(uint32_t)) {
            memcpy(&client->pane_modes, payload, sizeof(uint32_t));
        }
        break;
    }
    case YMUX_PROTO_VTSINK_RESET: {
        /* Epoch cancelled (review #12): the daemon discarded the queued feed
         * stream and destroyed its session. Drop any partial lane frame from
         * the dead epoch and RE-PUBLISH the sink — the daemon seeds a fresh
         * session and the projector opens it with a complete redraw. */
        client->vtsink_rx_len = 0;
        /* Receiver barrier FIRST: the embedder discards its pending stream
         * and rebuilds the receiving grid before the fresh epoch opens. A
         * FAILED reset blocks the re-publish (review #14) — the receiver
         * stays desynced rather than opening a fresh epoch onto a stale
         * parser; the next RESET/WELCOME retries. */
        int receiver_reset_ok = 1;
        if (client->vtsink_reset_handler) {
            receiver_reset_ok = client->vtsink_reset_handler(client->vtsink_reset_userdata);
        }
        if (!receiver_reset_ok) {
            yerror("ymux client: receiver reset failed — re-publish withheld, retrying");
            client->vtsink_reset_retry_pending = 1;
            break;
        }
        client->vtsink_reset_retry_pending = 0;
        if (client->vtsink_enabled) {
            struct yetty_ycore_void_result republish_res = client_vtsink_publish(client);
            if (YETTY_IS_ERR(republish_res)) {
                yerror("ymux client: vtsink re-publish after reset failed");
                yetty_ycore_error_destroy(republish_res.error);
            }
        }
        break;
    }
    case YMUX_PROTO_VTSINK_RPC: {
        /* The daemon's feed() request frames on the vtsink lane (#699.2).
         * Reassemble, dispatch each complete frame into the hosted sink, reply
         * with the response frame, and ACK the APPLIED generation afterwards —
         * the ACK now means "rendered state advanced", not "bytes received". */
        if (!client->vtsink) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: vtsink rpc without a hosted sink");
        }
        if (payload_len) {
            if (client->vtsink_rx_len + payload_len > client->vtsink_rx_capacity) {
                size_t grown_capacity =
                    client->vtsink_rx_capacity ? client->vtsink_rx_capacity : 1024;
                while (grown_capacity < client->vtsink_rx_len + payload_len) {
                    grown_capacity *= 2;
                }
                uint8_t *grown = realloc(client->vtsink_rx, grown_capacity);
                if (!grown) {
                    return YETTY_ERR(yetty_ycore_void, "ymux client: vtsink rx realloc");
                }
                client->vtsink_rx = grown;
                client->vtsink_rx_capacity = grown_capacity;
            }
            memcpy(client->vtsink_rx + client->vtsink_rx_len, payload, payload_len);
            client->vtsink_rx_len += payload_len;
        }
        size_t offset = 0;
        while (client->vtsink_rx_len - offset >= 2 * sizeof(uint32_t)) {
            uint32_t request_header = 0;
            uint32_t body_len = 0;
            memcpy(&request_header, client->vtsink_rx + offset, sizeof(uint32_t));
            memcpy(&body_len, client->vtsink_rx + offset + sizeof(uint32_t), sizeof(uint32_t));
            if (client->vtsink_rx_len - offset - 2 * sizeof(uint32_t) < body_len) {
                break; /* partial frame — wait for the next lane payload */
            }
            uint8_t response[1024 + sizeof(uint32_t)];
            struct yetty_ycore_size_result dispatch_res = yetty_yclass_rpc_dispatch_one(
                request_header, client->vtsink_rx + offset + 2 * sizeof(uint32_t), body_len,
                response + sizeof(uint32_t), sizeof(response) - sizeof(uint32_t));
            uint32_t response_len = 0;
            if (YETTY_IS_OK(dispatch_res)) {
                response_len = (uint32_t)dispatch_res.value;
            } else {
                /* Dispatch failure: reply zero-length (the daemon maps it to
                 * "remote impl returned error") and surface it here too. */
                char chain_buf[512];
                yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), dispatch_res.error);
                yerror("ymux client: vtsink dispatch failed: %s", chain_buf);
                yetty_ycore_error_destroy(dispatch_res.error);
            }
            memcpy(response, &response_len, sizeof(uint32_t));
            struct yetty_ycore_void_result reply_res = client_enqueue(
                client, YMUX_PROTO_VTSINK_RPC, response, sizeof(uint32_t) + response_len);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, reply_res, "ymux client: vtsink reply");
            offset += 2 * sizeof(uint32_t) + body_len;
        }
        if (offset) {
            memmove(client->vtsink_rx, client->vtsink_rx + offset, client->vtsink_rx_len - offset);
            client->vtsink_rx_len -= offset;
        }
        if (!client->vtsink_defer_ack) {
            struct yetty_ycore_uint64_result applied_res =
                yetty_ymux_vtsink_applied(client->vtsink);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, applied_res, "ymux client: vtsink applied");
            if (applied_res.value != client->vtsink_acked) {
                uint32_t ack[2] = {(uint32_t)(applied_res.value & 0xFFFFFFFFu),
                                   (uint32_t)(applied_res.value >> 32)};
                struct yetty_ycore_void_result ack_res =
                    client_enqueue(client, YMUX_PROTO_ACK, ack, sizeof(ack));
                YETTY_RETURN_IF_ERR(yetty_ycore_void, ack_res, "ymux client: vtsink ack");
                client->vtsink_acked = applied_res.value;
            }
        }
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_OVERLAY_INPUT_ACK: {
        if (payload_len >= 4) {
            uint32_t acked = 0;
            memcpy(&acked, payload, 4);
            if (acked > client->overlay_input_acked_seq) {
                client->overlay_input_acked_seq = acked;
            }
        }
        break;
    }
    case YMUX_PROTO_OVERLAY_INPUT_NACK: {
        /* Sequence-bearing refusal (review #19): the embedder still owns
         * the event and resends the SAME sequence immediately. */
        if (payload_len >= 8) {
            memcpy(&client->overlay_input_nacked_seq, payload, 4);
            memcpy(&client->overlay_input_nack_reason, payload + 4, 4);
        }
        break;
    }
    case YMUX_PROTO_CHROME_RELEASE: {
        /* Daemon-side chrome (copy-mode q) exited: drop the overlay input
         * claim — keys route to the application again. */
        client->overlay_input_active = 0;
        ++client->chrome_release_count;
        break;
    }
    case YMUX_PROTO_SESSION_REPLY: {
        if (payload_len < sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: short session reply");
        }
        memcpy(&client->reply_status, payload, sizeof(uint32_t));
        size_t text_len = payload_len - sizeof(uint32_t);
        if (text_len > sizeof(client->reply_text) - 1) {
            text_len = sizeof(client->reply_text) - 1;
        }
        memcpy(client->reply_text, payload + sizeof(uint32_t), text_len);
        client->reply_text[text_len] = 0;
        client->reply_generation++;
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_EFFECT_BELL:
        client->bell_count++;
        return YETTY_OK_VOID();
    case YMUX_PROTO_EFFECT_TITLE: {
        size_t copy_len =
            payload_len < sizeof(client->title) - 1 ? payload_len : sizeof(client->title) - 1;
        memcpy(client->title, payload, copy_len);
        client->title[copy_len] = 0;
        client->title_generation++;
        return YETTY_OK_VOID();
    }
    case YMUX_PROTO_EFFECT_CLIPBOARD: {
        if (payload_len < sizeof(uint32_t)) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: short clipboard effect");
        }
        uint32_t target;
        memcpy(&target, payload, sizeof(uint32_t));
        size_t text_len = payload_len - sizeof(uint32_t);
        char *copy = malloc(text_len + 1);
        if (!copy) {
            return YETTY_ERR(yetty_ycore_void, "ymux client: clipboard alloc");
        }
        memcpy(copy, payload + sizeof(uint32_t), text_len);
        copy[text_len] = 0;
        free(client->clipboard_text);
        client->clipboard_text = copy;
        client->clipboard_len = text_len;
        client->clipboard_target = (int)target;
        client->clipboard_generation++;
        return YETTY_OK_VOID();
    }
    default:
        return YETTY_OK_VOID(); /* forward-compatible: ignore unknown */
    }
    /* Cases exiting via `break` (PANE_MODES, VTSINK_RESET, OVERLAY_INPUT_ACK,
     * CHROME_RELEASE) land here — handled, OK. Falling off a non-void
     * function is UB (review #19 P0). */
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_client_make(const char *socket_path)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_client_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux client_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux client_make: alloc");
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(object_res.value);
    if (YETTY_IS_ERR(client_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux client_make: from_obj", client_res);
    }
    struct yetty_ymux_client *client = client_res.value;
    client->rx = malloc(YMUX_CLIENT_RX_CAP);
    client->tx = malloc(YMUX_CLIENT_TX_CAP);
    if (!client->rx || !client->tx) {
        free(client->rx);
        free(client->tx);
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux client_make: buffers");
    }
    struct yetty_ipc_socket_result connect_res = yetty_platform_socket_connect(socket_path);
    if (YETTY_IS_ERR(connect_res)) {
        free(client->rx);
        free(client->tx);
        client->rx = NULL;
        client->tx = NULL;
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux client_make: connect", connect_res);
    }
    client->socket = connect_res.value;
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_dispose: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    if (client->socket) {
        yetty_platform_socket_close(client->socket);
        client->socket = NULL;
    }
    free(client->rich_body);
    client->rich_body = NULL;
    free(client->clipboard_text);
    client->clipboard_text = NULL;
    for (uint32_t queue_index = 0; queue_index < client->ordered_count; ++queue_index) {
        uint32_t queue_slot = (client->ordered_head + queue_index) % client->ordered_capacity;
        free(client->ordered_queue[queue_slot].raw_bytes);
    }
    free(client->ordered_queue);
    client->ordered_queue = NULL;
    client->ordered_capacity = 0;
    client->ordered_count = 0;
    client_resource_cache_free(client);
    if (client->vtsink) {
        struct yetty_ycore_void_result sink_res = yetty_yclass_object_free(client->vtsink);
        if (YETTY_IS_ERR(sink_res)) {
            yetty_ycore_error_destroy(sink_res.error);
        }
        client->vtsink = NULL;
    }
    free(client->vtsink_rx);
    client->vtsink_rx = NULL;
    free(client->rx);
    free(client->tx);
    client->rx = NULL;
    client->tx = NULL;
    return yetty_yclass_object_free(obj);
}

/*===========================================================================
 * Verbs.
 *=========================================================================*/

/* Declare the client terminal's TERM name + features string; the next
 * attach carries them (the daemon then resolves the capability profile
 * via the terminfo/features state model). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_set_terminal(struct yetty_yclass_object *obj,
                                                              const char *term_name,
                                                              const char *features)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_set_terminal: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    snprintf(client->term_name, sizeof(client->term_name), "%s", term_name ? term_name : "");
    snprintf(client->term_features, sizeof(client->term_features), "%s", features ? features : "");
    return YETTY_OK_VOID();
}

/* Attach to `session_name` (NULL/empty = most recent — tmux attach
 * without -t). The session must exist; new-session creates them. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_attach(struct yetty_yclass_object *obj,
                                                        const char *session_name, uint32_t pane_id,
                                                        uint32_t view_rows, uint32_t view_cols,
                                                        uint32_t cell_pixel_height,
                                                        uint32_t capabilities, const char *token)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_attach: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    client->view_rows = view_rows;
    client->view_cols = view_cols;
    client->capabilities = capabilities;
    size_t name_len = session_name ? strlen(session_name) : 0;
    if (name_len > YMUX_PROTO_TOKEN_MAX) {
        name_len = YMUX_PROTO_TOKEN_MAX;
    }
    size_t token_len = token ? strlen(token) : 0;
    if (token_len > YMUX_PROTO_TOKEN_MAX) {
        token_len = YMUX_PROTO_TOKEN_MAX;
    }
    size_t term_len = strlen(client->term_name);
    size_t features_len = strlen(client->term_features);
    uint32_t words[8] = {YMUX_PROTO_VERSION,  pane_id,           view_rows,
                         view_cols,           cell_pixel_height, (uint32_t)name_len,
                         (uint32_t)token_len, capabilities};
    uint8_t payload[8 * sizeof(uint32_t) + 2 * YMUX_PROTO_TOKEN_MAX + 2 * sizeof(uint32_t) +
                    sizeof(client->term_name) + sizeof(client->term_features)];
    memcpy(payload, words, sizeof(words));
    size_t offset = sizeof(words);
    if (name_len) {
        memcpy(payload + offset, session_name, name_len);
    }
    offset += name_len;
    if (token_len) {
        memcpy(payload + offset, token, token_len);
    }
    offset += token_len;
    /* Terminal-strings tail (proto 9): [term_len][features_len][term]
     * [features]; both zero when unset (bitmask-only attach). */
    uint32_t term_len_word = (uint32_t)term_len;
    uint32_t features_len_word = (uint32_t)features_len;
    memcpy(payload + offset, &term_len_word, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(payload + offset, &features_len_word, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    if (term_len) {
        memcpy(payload + offset, client->term_name, term_len);
    }
    offset += term_len;
    if (features_len) {
        memcpy(payload + offset, client->term_features, features_len);
    }
    offset += features_len;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_ATTACH, payload, offset);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_attach: enqueue");
    return client_flush(client);
}

/* new-session: create a named session (empty name = auto-number) with
 * its initial shell pane. Reply arrives as a session reply. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_new(struct yetty_yclass_object *obj,
                                                             const char *name, uint32_t rows,
                                                             uint32_t cols)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_session_new: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    size_t name_len = name ? strlen(name) : 0;
    if (name_len > YMUX_PROTO_TOKEN_MAX) {
        name_len = YMUX_PROTO_TOKEN_MAX;
    }
    uint32_t words[3] = {rows, cols, (uint32_t)name_len};
    uint8_t payload[3 * sizeof(uint32_t) + YMUX_PROTO_TOKEN_MAX];
    memcpy(payload, words, sizeof(words));
    if (name_len) {
        memcpy(payload + sizeof(words), name, name_len);
    }
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_SESSION_NEW, payload, sizeof(words) + name_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_session_new: enqueue");
    return client_flush(client);
}

/* One name-only session verb (list = empty name). */
static struct yetty_ycore_void_result client_session_verb(struct yetty_yclass_object *obj,
                                                          uint32_t type, const char *name)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client session verb: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    size_t name_len = name ? strlen(name) : 0;
    if (name_len > YMUX_PROTO_TOKEN_MAX) {
        name_len = YMUX_PROTO_TOKEN_MAX;
    }
    struct yetty_ycore_void_result enqueue_res = client_enqueue(client, type, name, name_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client session verb: enqueue");
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_list(struct yetty_yclass_object *obj)
{
    return client_session_verb(obj, YMUX_PROTO_SESSION_LIST, NULL);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_has(struct yetty_yclass_object *obj,
                                                             const char *name)
{
    return client_session_verb(obj, YMUX_PROTO_SESSION_HAS, name);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_kill(struct yetty_yclass_object *obj,
                                                              const char *name)
{
    return client_session_verb(obj, YMUX_PROTO_SESSION_KILL, name);
}

/* detach-client -s: detach every client of the session server-side. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_detach(struct yetty_yclass_object *obj,
                                                                const char *name)
{
    return client_session_verb(obj, YMUX_PROTO_SESSION_DETACH, name);
}

/* send-keys: (kind, value) u32 pairs — kind 0 codepoint, kind 1 special
 * key — fed to the session's active pane engine, no attachment needed. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_send_keys(struct yetty_yclass_object *obj,
                                                                   const char *name,
                                                                   const uint32_t *pairs,
                                                                   uint32_t pair_count)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_session_send_keys: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    size_t name_len = name ? strlen(name) : 0;
    if (name_len > YMUX_PROTO_TOKEN_MAX) {
        name_len = YMUX_PROTO_TOKEN_MAX;
    }
    size_t pair_bytes = (size_t)pair_count * 2 * sizeof(uint32_t);
    size_t payload_len = sizeof(uint32_t) + name_len + pair_bytes;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "ymux client_session_send_keys: alloc");
    }
    uint32_t name_len_word = (uint32_t)name_len;
    memcpy(payload, &name_len_word, sizeof(uint32_t));
    if (name_len) {
        memcpy(payload + sizeof(uint32_t), name, name_len);
    }
    if (pair_bytes) {
        memcpy(payload + sizeof(uint32_t) + name_len, pairs, pair_bytes);
    }
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_SESSION_SEND_KEYS, payload, payload_len);
    free(payload);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_session_send_keys: enqueue");
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_session_rename(struct yetty_yclass_object *obj,
                                                                const char *old_name,
                                                                const char *new_name)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_session_rename: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    size_t old_len = old_name ? strlen(old_name) : 0;
    size_t new_len = new_name ? strlen(new_name) : 0;
    if (old_len > YMUX_PROTO_TOKEN_MAX) {
        old_len = YMUX_PROTO_TOKEN_MAX;
    }
    if (new_len > YMUX_PROTO_TOKEN_MAX) {
        new_len = YMUX_PROTO_TOKEN_MAX;
    }
    uint32_t old_len_word = (uint32_t)old_len;
    uint8_t payload[sizeof(uint32_t) + 2 * YMUX_PROTO_TOKEN_MAX];
    memcpy(payload, &old_len_word, sizeof(uint32_t));
    if (old_len) {
        memcpy(payload + sizeof(uint32_t), old_name, old_len);
    }
    if (new_len) {
        memcpy(payload + sizeof(uint32_t) + old_len, new_name, new_len);
    }
    struct yetty_ycore_void_result enqueue_res = client_enqueue(
        client, YMUX_PROTO_SESSION_RENAME, payload, sizeof(uint32_t) + old_len + new_len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_session_rename: enqueue");
    return client_flush(client);
}

/* The last session-verb reply (borrowed text; status via out param).
 * The returned counter moves per arrival — poll it around a verb. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_client_session_reply(struct yetty_yclass_object *obj,
                                                                 const char **out_text,
                                                                 uint32_t *out_status)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, client_res, "ymux client_session_reply: from_obj");
    if (out_text) {
        *out_text = client_res.value->reply_text;
    }
    if (out_status) {
        *out_status = client_res.value->reply_status;
    }
    return YETTY_OK(yetty_ycore_uint64, client_res.value->reply_generation);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_detach(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_detach: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res = client_enqueue(client, YMUX_PROTO_DETACH, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_detach: enqueue");
    client->attached = 0;
    client->attachment_id = 0;
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_input_char(struct yetty_yclass_object *obj,
                                                            uint32_t codepoint, int mods)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_input_char: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    uint32_t words[2] = {codepoint, (uint32_t)mods};
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_INPUT_CHAR, words, sizeof(words));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_input_char: enqueue");
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_input_key(struct yetty_yclass_object *obj, int key,
                                                           int mods)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_input_key: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    uint32_t words[2] = {(uint32_t)key, (uint32_t)mods};
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_INPUT_KEY, words, sizeof(words));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_input_key: enqueue");
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_input_mouse_move(struct yetty_yclass_object *obj,
                                                                  uint32_t row, uint32_t col,
                                                                  int mods)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_input_mouse_move: from_obj");
    uint32_t words[4] = {0u /* kind: move */, row, col, (uint32_t)mods};
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client_res.value, YMUX_PROTO_INPUT_MOUSE, words, sizeof(words));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_input_mouse_move: enqueue");
    return client_flush(client_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_input_mouse_button(struct yetty_yclass_object *obj,
                                                                    int button, int pressed,
                                                                    int mods)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_input_mouse_button: from_obj");
    uint32_t words[4] = {1u /* kind: button */, (uint32_t)button, (uint32_t)pressed,
                         (uint32_t)mods};
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client_res.value, YMUX_PROTO_INPUT_MOUSE, words, sizeof(words));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_input_mouse_button: enqueue");
    return client_flush(client_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_input_paste(struct yetty_yclass_object *obj,
                                                             const char *text, size_t len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_input_paste: from_obj");
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client_res.value, YMUX_PROTO_INPUT_PASTE, text, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_input_paste: enqueue");
    return client_flush(client_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_resize(struct yetty_yclass_object *obj,
                                                        uint32_t rows, uint32_t cols)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_resize: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    client->view_rows = rows;
    client->view_cols = cols;
    uint32_t words[2] = {rows, cols};
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_RESIZE, words, sizeof(words));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_resize: enqueue");
    return client_flush(client);
}

/* Scroll the viewport by `delta` rows (negative = into history; reaching
 * the live top resumes follow-live). The daemon answers with a FULL for
 * the new viewport. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_scroll(struct yetty_yclass_object *obj, int delta)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_scroll: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    int32_t delta_word = (int32_t)delta;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_SCROLL, &delta_word, sizeof(delta_word));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_scroll: enqueue");
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_takeover(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_takeover: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_TAKEOVER, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_takeover: enqueue");
    return client_flush(client);
}

/* Ask the daemon to resync this attachment: the client's terminal-byte stream
 * became unusable (a dropped or failed VT frame), so request a fresh COMPLETE
 * redraw rather than continue a desynced incremental stream. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_resync(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_resync: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res = client_enqueue(client, YMUX_PROTO_RESYNC, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_resync: enqueue");
    return client_flush(client);
}

/* Forward RAW terminal-response bytes from THIS attachment's renderer to
 * its daemon-side response parser (review #16). Opaque transport: the
 * bytes are preserved exactly — no decoding, no keyboard re-encoding. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_send_tty_response(struct yetty_yclass_object *obj,
                                                                   const uint8_t *bytes,
                                                                   uint32_t byte_count)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_send_tty_response: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_TTY_RESPONSE, bytes, byte_count);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_send_tty_response: enqueue");
    return client_flush(client);
}

/* Forward one CONSUMED overlay input event (drained from the overlay
 * scene's queue) to the daemon's chrome seat, tagged with an acceptance
 * SEQUENCE (review #17): the daemon ACKs it after taking ownership; the
 * caller pops its source event only at that commit point. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_send_overlay_input(struct yetty_yclass_object *obj,
                                                                    uint32_t sequence,
                                                                    uint32_t input_class,
                                                                    const uint8_t *bytes,
                                                                    uint32_t byte_count)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_send_overlay_input: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    /* LOSSLESS (review #16): the payload is heap-sized to the event — no
     * fixed truncation boundary. The tx-cap check inside client_enqueue is
     * the only limit, and a failure surfaces. */
    uint8_t *payload = malloc((size_t)byte_count + 8);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "ymux client_send_overlay_input: payload alloc");
    }
    memcpy(payload, &sequence, 4);
    memcpy(payload + 4, &input_class, 4);
    if (byte_count > 0 && bytes) {
        memcpy(payload + 8, bytes, byte_count);
    }
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_OVERLAY_INPUT, payload, (size_t)byte_count + 8);
    free(payload);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_send_overlay_input: enqueue");
    return client_flush(client);
}

/* The highest overlay-input sequence the daemon has ACCEPTED. */
YETTY_ANNOTATE("expose")
/* Last NACK-refused overlay sequence (0 = none); reads clear nothing —
 * the embedder compares against its inflight sequence. Hand-written,
 * module-internal. */
struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_nacked(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_overlay_input_nacked");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->overlay_input_nacked_seq);
}

struct yetty_ycore_uint32_result yetty_ymux_client_overlay_input_acked(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_overlay_input_acked");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->overlay_input_acked_seq);
}

/* CHROME_RELEASE frames observed (the daemon-side chrome exited). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_client_chrome_release_count(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_chrome_release_count");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->chrome_release_count);
}

/* Ops/debug: ask the daemon to force slow-client recovery (epoch reset) on
 * every attached connection — the attach-level reset-ordering probe. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_request_recover(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_request_recover: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_RECOVER, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_request_recover: enqueue");
    return client_flush(client);
}

/* Ask the daemon to paste its copy-mode buffer into the target pane
 * (tmux paste-buffer). The SESSION_REPLY ack carries the outcome. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_request_paste(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_request_paste: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_PASTE_BUFFER, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_request_paste: enqueue");
    return client_flush(client);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_client_shutdown_server(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_shutdown_server: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client, YMUX_PROTO_SHUTDOWN, NULL, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_shutdown_server: enqueue");
    return client_flush(client);
}

/*===========================================================================
 * The pump.
 *=========================================================================*/

/* Parse complete frames out of the rx buffer + flush any generated tx
 * (acks). Returns the number of frames handled. */
static struct yetty_ycore_int_result client_drain_frames(struct yetty_ymux_client *client)
{
    int handled = 0;
    for (;;) {
        if (client->rx_len < YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t)) {
            break;
        }
        uint32_t header[YMUX_PROTO_HEADER_WORDS];
        memcpy(header, client->rx, sizeof(header));
        if (header[0] != YMUX_PROTO_MAGIC || header[2] > YMUX_PROTO_MAX_PAYLOAD) {
            return YETTY_ERR(yetty_ycore_int, "ymux client: bad frame");
        }
        size_t frame_len = sizeof(header) + header[2];
        if (client->rx_len < frame_len) {
            break;
        }
        struct yetty_ycore_void_result frame_res =
            client_handle_frame(client, header[1], client->rx + sizeof(header), header[2]);
        if (YETTY_IS_ERR(frame_res)) {
            return YETTY_ERR(yetty_ycore_int, "ymux client: frame", frame_res);
        }
        memmove(client->rx, client->rx + frame_len, client->rx_len - frame_len);
        client->rx_len -= frame_len;
        ++handled;
    }
    struct yetty_ycore_void_result flush_res = client_flush(client);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, flush_res, "ymux client: flush");
    return YETTY_OK(yetty_ycore_int, handled);
}

/* One non-blocking pump: recv frames, apply paints (acking each), flush
 * pending tx. Returns the number of frames handled. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_client_step(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, client_res, "ymux client_step: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    if (!client->socket) {
        return YETTY_ERR(yetty_ycore_int, "ymux client_step: not connected");
    }
    /* Receiver-reset RETRY (review #15): a transient reset failure left the
     * receiver desynced with the re-publish withheld — retry the handler on
     * every step until it succeeds, then open the fresh epoch. */
    if (client->vtsink_reset_retry_pending && client->vtsink_reset_handler) {
        if (client->vtsink_reset_handler(client->vtsink_reset_userdata)) {
            client->vtsink_reset_retry_pending = 0;
            if (client->vtsink_enabled) {
                struct yetty_ycore_void_result republish_res = client_vtsink_publish(client);
                if (YETTY_IS_ERR(republish_res)) {
                    yerror("ymux client: vtsink re-publish after retried reset failed");
                    yetty_ycore_error_destroy(republish_res.error);
                }
            }
        }
    }
    int server_closed = 0;
    while (yetty_platform_socket_has_data(client->socket) && client->rx_len < YMUX_CLIENT_RX_CAP) {
        struct yetty_ycore_size_result recv_res = yetty_platform_socket_recv(
            client->socket, client->rx + client->rx_len, YMUX_CLIENT_RX_CAP - client->rx_len);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, recv_res, "ymux client_step: recv");
        if (recv_res.value == 0) {
            /* EOF — but frames already received (e.g. the reply to the
             * verb that made the server exit) must still be delivered:
             * drain first, error on the NEXT step. */
            yetty_platform_socket_close(client->socket);
            client->socket = NULL;
            client->attached = 0;
            server_closed = 1;
            break;
        }
        client->rx_len += recv_res.value;
    }
    struct yetty_ycore_int_result drained_res = client_drain_frames(client);
    if (server_closed && YETTY_IS_OK(drained_res) && drained_res.value == 0) {
        return YETTY_ERR(yetty_ycore_int, "ymux client_step: server closed");
    }
    /* Flush the ACKs client_drain_frames enqueued (each applied frame acks its
     * generation) back to the daemon. The daemon's projection ACK window
     * (MAX_UNACKED) only advances on ACK, so without this flush it sends the
     * first couple of frames and then stops projecting — the pane freezes on
     * whatever rendered first. */
    struct yetty_ycore_void_result flush_res = client_flush(client);
    if (YETTY_IS_ERR(flush_res)) {
        yetty_ycore_error_destroy(flush_res.error);
    }
    return drained_res;
}

/* Feed externally-read bytes into the frame parser (an embedder whose
 * event loop owns the socket reads — e.g. a uv pipe watcher — routes the
 * bytes here instead of letting step() recv). Returns frames handled. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_client_ingest(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, client_res, "ymux client_ingest: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    if (len > YMUX_CLIENT_RX_CAP - client->rx_len) {
        return YETTY_ERR(yetty_ycore_int, "ymux client_ingest: rx overflow");
    }
    if (len) {
        memcpy(client->rx + client->rx_len, bytes, len);
        client->rx_len += len;
    }
    return client_drain_frames(client);
}

/* The connection's file descriptor for event-loop registration (-1 when
 * disconnected or on platforms without one). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_client_fd(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, client_res, "ymux client_fd: from_obj");
    if (!client_res.value->socket) {
        return YETTY_OK(yetty_ycore_int, -1);
    }
    return YETTY_OK(yetty_ycore_int, yetty_platform_socket_get_fd(client_res.value->socket));
}

/*===========================================================================
 * Accessors.
 *=========================================================================*/

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_client_attached(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, client_res, "ymux client_attached: from_obj");
    return YETTY_OK(yetty_ycore_int, client_res.value->attached);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_client_attachment_id(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_attachment_id: from_obj");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->attachment_id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_client_pane_id(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_pane_id: from_obj");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->pane_id);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_client_permissions(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_permissions: from_obj");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->permissions);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint32_result yetty_ymux_client_last_refuse(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint32, client_res, "ymux client_last_refuse: from_obj");
    return YETTY_OK(yetty_ycore_uint32, client_res.value->last_refuse);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_client_pane_exited(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, client_res, "ymux client_pane_exited: from_obj");
    return YETTY_OK(yetty_ycore_int, client_res.value->pane_exited);
}

/* Borrowed — the most recent rich body (rich-format words; NULL until one
 * arrives). Valid until the next transaction or dispose. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_const_uint32_ptr_result yetty_ymux_client_rich_body(
    struct yetty_yclass_object *obj, uint32_t *out_word_count)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_const_uint32_ptr, client_res,
                        "ymux client_rich_body: from_obj");
    if (out_word_count) {
        *out_word_count = (uint32_t)client_res.value->rich_body_words;
    }
    return YETTY_OK(yetty_ycore_const_uint32_ptr, client_res.value->rich_body);
}

/* Counts rich-body arrivals — the embedder polls it to detect fresh
 * bodies without comparing content. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_client_rich_generation(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, client_res, "ymux client_rich_generation: from_obj");
    return YETTY_OK(yetty_ycore_uint64, client_res.value->rich_generation);
}

/* Opt in to hosting the vtsink lane (#699.2). Must be called BEFORE attach —
 * the sink is created and published on WELCOME when the attach advertised
 * VT_TEXT, with `emit` wired at creation so the lane's opening complete
 * redraw is never dropped on an unwired sink. Plain hand-written setter,
 * like the raw-sink seams. */
void yetty_ymux_client_enable_vtsink(struct yetty_yclass_object *obj,
                                     void (*emit)(uint64_t generation, const uint8_t *bytes,
                                                  size_t len, void *userdata),
                                     void *userdata)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->vtsink_enabled = 1;
    client_res.value->vtsink_emit = emit;
    client_res.value->vtsink_emit_userdata = userdata;
}

/* Overlay-first input routing (#699.4). Class values are the
 * YMUX_INPUT_CLASS_* enum in proto.h. route() answers whether the OVERLAY
 * consumes the event: POINTER — consumed iff the yetty hit test resolved to
 * the overlay figure (its hit_opaque already yielded empty regions to the
 * content scene, so a hit on the overlay id means opaque chrome at that
 * point); KEY/PASTE — consumed iff the overlay has claimed input focus.
 * deliver() hands a consumed event to the handler seat (or counts and drops
 * it when no chrome handler is installed). Plain hand-written seams, like
 * the raw-sink family. */
void yetty_ymux_client_set_overlay_input_active(struct yetty_yclass_object *obj, int active)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->overlay_input_active = active ? 1 : 0;
}

void yetty_ymux_client_set_overlay_input_handler(struct yetty_yclass_object *obj,
                                                 void (*handler)(uint32_t input_class,
                                                                 const uint8_t *bytes, size_t len,
                                                                 void *userdata),
                                                 void *userdata)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->overlay_input_handler = handler;
    client_res.value->overlay_input_userdata = userdata;
}

void yetty_ymux_client_set_vtsink_reset_handler(struct yetty_yclass_object *obj,
                                                int (*handler)(void *userdata), void *userdata)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->vtsink_reset_handler = handler;
    client_res.value->vtsink_reset_userdata = userdata;
}

uint32_t yetty_ymux_client_pane_modes(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    return client_res.value->pane_modes;
}

/* Classify a raw stdin chunk into KEY and PASTE runs (review #14): the
 * bracketed-paste state and any trailing partial delimiter PERSIST on the
 * client, so a \e[200~ / \e[201~ split across raw flushes still classifies
 * correctly. Runs are emitted in order via the callback; a paste span keeps
 * its delimiters. */
static uint32_t client_delimiter_prefix_len(const uint8_t *bytes, size_t len)
{
    static const uint8_t paste_open[6] = "\x1b[200~";
    static const uint8_t paste_close[6] = "\x1b[201~";
    size_t max = len < 6 ? len : 6;
    uint32_t best = 0;
    for (size_t take = 1; take <= max; ++take) {
        if (memcmp(bytes, paste_open, take) == 0 || memcmp(bytes, paste_close, take) == 0) {
            best = (uint32_t)take;
        }
    }
    return best;
}

void yetty_ymux_client_overlay_classify_input(
    struct yetty_yclass_object *obj, const uint8_t *bytes, size_t len,
    void (*emit)(uint32_t input_class, const uint8_t *bytes, size_t len, void *userdata),
    void *userdata)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    struct yetty_ymux_client *client = client_res.value;
    size_t offset = 0;
    /* Resolve a carried partial delimiter first: stitch carry + just enough
     * new bytes (a delimiter is 6 bytes total) to decide. Bounded scratch —
     * the chunk itself is never copied. */
    if (client->overlay_classify_carry_len > 0) {
        uint8_t head[8];
        uint32_t carry_len = client->overlay_classify_carry_len;
        size_t take = len < (size_t)(6 - carry_len) ? len : (size_t)(6 - carry_len);
        memcpy(head, client->overlay_classify_carry, carry_len);
        memcpy(head + carry_len, bytes, take);
        uint32_t prefix = client_delimiter_prefix_len(head, carry_len + take);
        if (prefix == carry_len + take && prefix < 6) {
            /* Still a proper prefix and the chunk is exhausted: keep carrying. */
            memcpy(client->overlay_classify_carry, head, prefix);
            client->overlay_classify_carry_len = prefix;
            return;
        }
        client->overlay_classify_carry_len = 0;
        if (prefix == 6) {
            /* The delimiter completed across the boundary. */
            int opens = head[4] == '0';
            if (opens && !client->overlay_paste_open) {
                client->overlay_paste_open = 1;
                emit(YMUX_INPUT_CLASS_PASTE, head, 6, userdata);
            } else if (!opens && client->overlay_paste_open) {
                emit(YMUX_INPUT_CLASS_PASTE, head, 6, userdata);
                client->overlay_paste_open = 0;
            } else {
                emit(client->overlay_paste_open ? YMUX_INPUT_CLASS_PASTE : YMUX_INPUT_CLASS_KEY,
                     head, 6, userdata);
            }
            offset = take;
        } else {
            /* Not a delimiter after all: the carried bytes are ordinary
             * content (they contain no later ESC — a delimiter prefix has
             * ESC only at position 0). The peeked chunk bytes were not
             * consumed and scan normally below. */
            emit(client->overlay_paste_open ? YMUX_INPUT_CLASS_PASTE : YMUX_INPUT_CLASS_KEY,
                 client->overlay_classify_carry, carry_len, userdata);
        }
    }
    size_t run_start = offset;
    while (offset < len) {
        if (bytes[offset] != 0x1b) {
            ++offset;
            continue;
        }
        uint32_t prefix = client_delimiter_prefix_len(bytes + offset, len - offset);
        if (prefix == 0) {
            ++offset;
            continue;
        }
        if (prefix < 6 && offset + prefix == len) {
            /* Chunk ends inside a delimiter candidate: hold it back. */
            if (offset > run_start) {
                emit(client->overlay_paste_open ? YMUX_INPUT_CLASS_PASTE : YMUX_INPUT_CLASS_KEY,
                     bytes + run_start, offset - run_start, userdata);
            }
            memcpy(client->overlay_classify_carry, bytes + offset, prefix);
            client->overlay_classify_carry_len = prefix;
            return;
        }
        if (prefix < 6) {
            offset += prefix; /* e.g. \e[20x — ordinary bytes */
            continue;
        }
        int opens = bytes[offset + 4] == '0';
        if (opens && !client->overlay_paste_open) {
            if (offset > run_start) {
                emit(YMUX_INPUT_CLASS_KEY, bytes + run_start, offset - run_start, userdata);
            }
            client->overlay_paste_open = 1;
            run_start = offset; /* the span includes its opening delimiter */
            offset += 6;
        } else if (!opens && client->overlay_paste_open) {
            offset += 6; /* the span includes its closing delimiter */
            emit(YMUX_INPUT_CLASS_PASTE, bytes + run_start, offset - run_start, userdata);
            client->overlay_paste_open = 0;
            run_start = offset;
        } else {
            offset += 6; /* stray delimiter — flows with the current class */
        }
    }
    if (offset > run_start) {
        emit(client->overlay_paste_open ? YMUX_INPUT_CLASS_PASTE : YMUX_INPUT_CLASS_KEY,
             bytes + run_start, offset - run_start, userdata);
    }
}

/* ORDERED input queue (review #16): one queue for raw keystroke chunks and
 * overlay pointer events, preserving wire arrival order across a single
 * pump. push_raw copies; a full queue rejects (the caller processes in
 * place — order still held because the queue is drained first). */
/* Grow the ordered ring (unwrapping it into the new array). OOM -> 0. */
static int client_ordered_reserve(struct yetty_ymux_client *client)
{
    if (client->ordered_count < client->ordered_capacity) {
        return 1;
    }
    uint32_t new_capacity = client->ordered_capacity ? client->ordered_capacity * 2 : 64;
    struct yetty_ymux_ordered_entry *grown =
        malloc((size_t)new_capacity * sizeof(struct yetty_ymux_ordered_entry));
    if (!grown) {
        return 0;
    }
    for (uint32_t index = 0; index < client->ordered_count; ++index) {
        grown[index] =
            client->ordered_queue[(client->ordered_head + index) % client->ordered_capacity];
    }
    free(client->ordered_queue);
    client->ordered_queue = grown;
    client->ordered_head = 0;
    client->ordered_capacity = new_capacity;
    return 1;
}

int yetty_ymux_client_ordered_push_raw(struct yetty_yclass_object *obj, const uint8_t *bytes,
                                       uint32_t byte_count)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (!client_ordered_reserve(client)) {
        return 0; /* OOM only */
    }
    uint8_t *copy = NULL;
    if (byte_count > 0) {
        copy = malloc(byte_count);
        if (!copy) {
            return 0;
        }
        memcpy(copy, bytes, byte_count);
    }
    uint32_t slot = (client->ordered_head + client->ordered_count) % client->ordered_capacity;
    client->ordered_queue[slot].kind = 1;
    client->ordered_queue[slot].raw_bytes = copy;
    client->ordered_queue[slot].raw_len = byte_count;
    ++client->ordered_count;
    return 1;
}

int yetty_ymux_client_ordered_push_pointer(struct yetty_yclass_object *obj, float local_x,
                                           float local_y, uint32_t kind, uint32_t button,
                                           uint32_t mods, uint32_t pressed)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (!client_ordered_reserve(client)) {
        return 0; /* OOM only */
    }
    uint32_t slot = (client->ordered_head + client->ordered_count) % client->ordered_capacity;
    client->ordered_queue[slot].kind = 2;
    client->ordered_queue[slot].raw_bytes = NULL;
    client->ordered_queue[slot].raw_len = 0;
    client->ordered_queue[slot].pointer_x = local_x;
    client->ordered_queue[slot].pointer_y = local_y;
    client->ordered_queue[slot].pointer_kind = kind;
    client->ordered_queue[slot].pointer_button = button;
    client->ordered_queue[slot].pointer_mods = mods;
    client->ordered_queue[slot].pointer_pressed = pressed;
    ++client->ordered_count;
    return 1;
}

/* Head kind: 0 = empty, 1 = raw, 2 = pointer. */
int yetty_ymux_client_ordered_head_kind(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (client->ordered_count == 0) {
        return 0;
    }
    return client->ordered_queue[client->ordered_head].kind;
}

/* Pop a RAW entry: copies up to capacity, returns the stored length; the
 * entry pops only when it fits (mirror of the scene take contract). */
int yetty_ymux_client_ordered_pop_raw(struct yetty_yclass_object *obj, uint8_t *out_bytes,
                                      uint32_t out_capacity, uint32_t *out_len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    enum { RING = 64 };
    if (client->ordered_count == 0 || client->ordered_queue[client->ordered_head].kind != 1) {
        return 0;
    }
    uint32_t slot = client->ordered_head;
    uint32_t stored = client->ordered_queue[slot].raw_len;
    if (out_len) {
        *out_len = stored;
    }
    if (stored > out_capacity) {
        return 0; /* caller retries with a bigger buffer */
    }
    if (stored > 0 && out_bytes) {
        memcpy(out_bytes, client->ordered_queue[slot].raw_bytes, stored);
    }
    free(client->ordered_queue[slot].raw_bytes);
    client->ordered_queue[slot].raw_bytes = NULL;
    client->ordered_head = (slot + 1) % client->ordered_capacity;
    --client->ordered_count;
    return 1;
}

/* PEEK the head RAW entry WITHOUT removing it: copies up to capacity and
 * returns the stored length via out_len. Returns 1 when the head is a raw
 * entry that fits (still present — commit with ordered_drop_head), 0 when the
 * head is absent / not raw / does not fit (out_len still carries the size so
 * the caller can grow its buffer). This is the read half of the peek/commit
 * protocol: the source event is not consumed until retention is guaranteed. */
int yetty_ymux_client_ordered_peek_raw(struct yetty_yclass_object *obj, uint8_t *out_bytes,
                                       uint32_t out_capacity, uint32_t *out_len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (client->ordered_count == 0 || client->ordered_queue[client->ordered_head].kind != 1) {
        return 0;
    }
    uint32_t slot = client->ordered_head;
    uint32_t stored = client->ordered_queue[slot].raw_len;
    if (out_len) {
        *out_len = stored;
    }
    if (stored > out_capacity) {
        return 0; /* caller grows its buffer and re-peeks; entry stays */
    }
    if (stored > 0 && out_bytes) {
        memcpy(out_bytes, client->ordered_queue[slot].raw_bytes, stored);
    }
    return 1;
}

/* COMMIT: drop the head RAW entry after a successful peek+retain. Returns 1
 * when a raw head was dropped, 0 otherwise. */
int yetty_ymux_client_ordered_drop_head(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (client->ordered_count == 0 || client->ordered_queue[client->ordered_head].kind != 1) {
        return 0;
    }
    uint32_t slot = client->ordered_head;
    free(client->ordered_queue[slot].raw_bytes);
    client->ordered_queue[slot].raw_bytes = NULL;
    client->ordered_head = (slot + 1) % client->ordered_capacity;
    --client->ordered_count;
    return 1;
}

/* Snapshot / restore the overlay CLASSIFIER's whole mutable state (the partial-
 * delimiter carry + the bracketed-paste-open flag). This lets the bridge run a
 * non-destructive MEASURE pass over a peeked chunk — count the exact classified
 * record bytes — then restore the classifier so the real retain pass reproduces
 * byte-identical runs and advances the state exactly once (cycle-25 P0: exact
 * reservation for the record set the classifier actually produces, not a run
 * estimate). `carry_out` must hold at least 8 bytes. */
void yetty_ymux_client_overlay_classify_save(struct yetty_yclass_object *obj, uint8_t *carry_out,
                                             uint32_t *carry_len_out, int *paste_open_out)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (carry_out) {
        memcpy(carry_out, client->overlay_classify_carry, sizeof(client->overlay_classify_carry));
    }
    if (carry_len_out) {
        *carry_len_out = client->overlay_classify_carry_len;
    }
    if (paste_open_out) {
        *paste_open_out = client->overlay_paste_open;
    }
}

void yetty_ymux_client_overlay_classify_restore(struct yetty_yclass_object *obj,
                                                const uint8_t *carry, uint32_t carry_len,
                                                int paste_open)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    struct yetty_ymux_client *client = client_res.value;
    if (carry && carry_len <= sizeof(client->overlay_classify_carry)) {
        memcpy(client->overlay_classify_carry, carry, sizeof(client->overlay_classify_carry));
        client->overlay_classify_carry_len = carry_len;
    }
    client->overlay_paste_open = paste_open;
}

int yetty_ymux_client_ordered_pop_pointer(struct yetty_yclass_object *obj, float *out_x,
                                          float *out_y, uint32_t *out_kind, uint32_t *out_button,
                                          uint32_t *out_mods, uint32_t *out_pressed)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    struct yetty_ymux_client *client = client_res.value;
    enum { RING = 64 };
    if (client->ordered_count == 0 || client->ordered_queue[client->ordered_head].kind != 2) {
        return 0;
    }
    uint32_t slot = client->ordered_head;
    *out_x = client->ordered_queue[slot].pointer_x;
    *out_y = client->ordered_queue[slot].pointer_y;
    *out_kind = client->ordered_queue[slot].pointer_kind;
    *out_button = client->ordered_queue[slot].pointer_button;
    *out_mods = client->ordered_queue[slot].pointer_mods;
    *out_pressed = client->ordered_queue[slot].pointer_pressed;
    client->ordered_head = (slot + 1) % client->ordered_capacity;
    --client->ordered_count;
    return 1;
}

int yetty_ymux_client_route_overlay_input(struct yetty_yclass_object *obj, uint32_t input_class,
                                          uint32_t figure_id, uint32_t overlay_figure_id)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    if (input_class == YMUX_INPUT_CLASS_POINTER) {
        return figure_id == overlay_figure_id ? 1 : 0;
    }
    return client_res.value->overlay_input_active ? 1 : 0;
}

void yetty_ymux_client_overlay_input_deliver(struct yetty_yclass_object *obj, uint32_t input_class,
                                             const uint8_t *bytes, size_t len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    struct yetty_ymux_client *client = client_res.value;
    switch (input_class) {
    case YMUX_INPUT_CLASS_POINTER:
        ++client->overlay_consumed_pointer;
        break;
    case YMUX_INPUT_CLASS_KEY:
        ++client->overlay_consumed_key;
        break;
    case YMUX_INPUT_CLASS_PASTE:
        ++client->overlay_consumed_paste;
        break;
    default:
        break;
    }
    if (client->overlay_input_handler) {
        client->overlay_input_handler(input_class, bytes, len, client->overlay_input_userdata);
    }
}

uint64_t yetty_ymux_client_overlay_consumed_count(struct yetty_yclass_object *obj,
                                                  uint32_t input_class)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return 0;
    }
    switch (input_class) {
    case YMUX_INPUT_CLASS_POINTER:
        return client_res.value->overlay_consumed_pointer;
    case YMUX_INPUT_CLASS_KEY:
        return client_res.value->overlay_consumed_key;
    case YMUX_INPUT_CLASS_PASTE:
        return client_res.value->overlay_consumed_paste;
    default:
        return 0;
    }
}

/* Defer the feed ACK to the embedder (#699.6): the demux stops auto-ACKing
 * after local dispatch; the embedder calls yetty_ymux_client_vtsink_ack once
 * its downstream scene write for those bytes has COMPLETED, so the daemon's
 * window tracks true end-to-end application. Call before attach, with
 * enable_vtsink. */
void yetty_ymux_client_vtsink_defer_ack(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->vtsink_defer_ack = 1;
}

/* Explicit deferred ACK: report `generation` (a value previously applied by
 * the sink) as delivered end to end. Idempotent for stale/duplicate values. */
struct yetty_ycore_void_result yetty_ymux_client_vtsink_ack(struct yetty_yclass_object *obj,
                                                            uint64_t generation)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux vtsink_ack: from_obj");
    struct yetty_ymux_client *client = client_res.value;
    if (!client->vtsink) {
        return YETTY_ERR(yetty_ycore_void, "ymux vtsink_ack: no hosted sink");
    }
    if (generation <= client->vtsink_acked) {
        return YETTY_OK_VOID(); /* stale or duplicate — already reported */
    }
    uint32_t ack[2] = {(uint32_t)(generation & 0xFFFFFFFFu), (uint32_t)(generation >> 32)};
    struct yetty_ycore_void_result ack_res =
        client_enqueue(client, YMUX_PROTO_ACK, ack, sizeof(ack));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ack_res, "ymux vtsink_ack: enqueue");
    client->vtsink_acked = generation;
    /* FLUSH NOW, like every other verb. An enqueued-but-unflushed ACK
     * deadlocks a sustained flood: the daemon stops projecting at its
     * unacked window, so it sends nothing; the embedder's loop only steps
     * the client on daemon traffic, so nothing ever flushes the queue —
     * the screen freezes until unrelated control traffic (a title change)
     * happens to wake the loop. */
    return client_flush(client);
}

/* The hosted vtsink object (NULL until WELCOME creates it in lane mode). The
 * embedder points the sink's emit at its grid via yetty_ymux_vtsink_set_emit. */
struct yetty_yclass_object *yetty_ymux_client_vtsink_object(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return NULL;
    }
    return client_res.value->vtsink;
}

/* Install the figure-surface RPC relay sink (#695: ygui/ygreeter proxy).
 * Raw function pointer — a plain hand-written setter, like set_vt_sink. */
void yetty_ymux_client_set_rpc_relay_sink(struct yetty_yclass_object *obj,
                                          void (*sink)(uint32_t channel_id, const uint8_t *bytes,
                                                       size_t len, void *userdata),
                                          void *userdata)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->rpc_relay_sink = sink;
    client_res.value->rpc_relay_sink_userdata = userdata;
}

/* Install the proxied-channel-close sink (daemon -> client). */
void yetty_ymux_client_set_rpc_relay_close_sink(struct yetty_yclass_object *obj,
                                                void (*sink)(uint32_t channel_id, void *userdata),
                                                void *userdata)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    if (YETTY_IS_ERR(client_res)) {
        yetty_ycore_error_destroy(client_res.error);
        return;
    }
    client_res.value->rpc_relay_close_sink = sink;
    client_res.value->rpc_relay_close_sink_userdata = userdata;
}

/* Send a proxied channel's RESPONSE bytes (from yetty) back to the daemon,
 * which writes them to the pane app's channel. Plain function (not exposed) —
 * the embedder calls it from its yetty-channel response sink. */
struct yetty_ycore_void_result yetty_ymux_client_rpc_relay(struct yetty_yclass_object *obj,
                                                           uint32_t channel_id,
                                                           const uint8_t *bytes, size_t len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_rpc_relay: from_obj");
    size_t payload_len = sizeof(uint32_t) + len;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "ymux client_rpc_relay: alloc");
    }
    memcpy(payload, &channel_id, sizeof(uint32_t));
    if (len > 0) {
        memcpy(payload + sizeof(uint32_t), bytes, len);
    }
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client_res.value, YMUX_PROTO_RPC_RELAY, payload, payload_len);
    free(payload);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_rpc_relay: enqueue");
    return client_flush(client_res.value);
}

/* Relay input yetty routed to a PROXIED figure back to the pane app (the daemon
 * re-emits it as an OSC envelope on the app's PTY). `wire_code` is the OSC code
 * (CLIENT_INPUT_FIGURE_MOUSE / _KEY); `bytes` is the raw client-input body. */
struct yetty_ycore_void_result yetty_ymux_client_figure_input(struct yetty_yclass_object *obj,
                                                              uint32_t wire_code,
                                                              const uint8_t *bytes, size_t len)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_figure_input: from_obj");
    size_t payload_len = sizeof(uint32_t) + len;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        return YETTY_ERR(yetty_ycore_void, "ymux client_figure_input: alloc");
    }
    memcpy(payload, &wire_code, sizeof(uint32_t));
    if (len > 0) {
        memcpy(payload + sizeof(uint32_t), bytes, len);
    }
    struct yetty_ycore_void_result enqueue_res =
        client_enqueue(client_res.value, YMUX_PROTO_FIGURE_INPUT, payload, payload_len);
    free(payload);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_figure_input: enqueue");
    return client_flush(client_res.value);
}

/* Signal that a proxied channel closed on the client's side. */
struct yetty_ycore_void_result yetty_ymux_client_rpc_relay_close(struct yetty_yclass_object *obj,
                                                                 uint32_t channel_id)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, client_res, "ymux client_rpc_relay_close: from_obj");
    struct yetty_ycore_void_result enqueue_res = client_enqueue(
        client_res.value, YMUX_PROTO_RPC_RELAY_CLOSE, &channel_id, sizeof(channel_id));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, enqueue_res, "ymux client_rpc_relay_close: enqueue");
    return client_flush(client_res.value);
}

/* Bell arrivals (exactly-once effects — never replayed on reconnect). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_client_bell_count(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, client_res, "ymux client_bell_count: from_obj");
    return YETTY_OK(yetty_ycore_uint64, client_res.value->bell_count);
}

/* The latest pane title (copy-out) + its arrival counter. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_client_title(struct yetty_yclass_object *obj, char *out,
                                                         size_t out_cap)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, client_res, "ymux client_title: from_obj");
    if (out && out_cap) {
        strncpy(out, client_res.value->title, out_cap - 1);
        out[out_cap - 1] = 0;
    }
    return YETTY_OK(yetty_ycore_uint64, client_res.value->title_generation);
}

/* The latest clipboard effect text (borrowed; NULL until one arrives).
 * `out_target`: 1 clipboard, 0 primary selection. The returned counter
 * moves per arrival — controller-only routing means non-controllers
 * never see it move. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_client_clipboard(struct yetty_yclass_object *obj,
                                                             const char **out_text, size_t *out_len,
                                                             int *out_target)
{
    struct yetty_ymux_client_ptr_result client_res = yetty_ymux_client_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, client_res, "ymux client_clipboard: from_obj");
    if (out_text) {
        *out_text = client_res.value->clipboard_text;
    }
    if (out_len) {
        *out_len = client_res.value->clipboard_len;
    }
    if (out_target) {
        *out_target = client_res.value->clipboard_target;
    }
    return YETTY_OK(yetty_ycore_uint64, client_res.value->clipboard_generation);
}

#include "yetty/gen/impl/ymux/client.c"
