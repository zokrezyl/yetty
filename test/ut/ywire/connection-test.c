/*
 * ywire connection/channel contract test — the SSH-style connection layer.
 *
 * Two yetty_ywire_connections are wired back-to-back over two pipes (each end
 * a yetty_yclass_transport_pty on the pipe fds — non-tty, so no termios raw
 * mode, only O_NONBLOCK), and pumped manually. Covers: the dynamic-channel
 * OPEN/DATA/EOF/CLOSE handshake with accept/reject, per-channel flow-control
 * windows with WINDOW_ADJUST credit, DATA chunking + fair interleaving across
 * channels, slot exhaustion, and a regression pass over the well-known
 * rpc/raw lanes.
 */

#include <yetty/yclass/transport-pty.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*===========================================================================
 * Fixture: two connections joined by two pipes
 *=========================================================================*/

struct link_end {
    struct yetty_yclass_transport_pty *transport;
    struct yetty_ywire_connection *connection;
};

struct link {
    struct link_end a; /* initiator */
    struct link_end b; /* acceptor */
    int a_to_b[2];
    int b_to_a[2];
};

static int link_up(struct ytest *test, struct link *link, int compressed)
{
    memset(link, 0, sizeof(*link));
    YTEST_REQUIRE(test, pipe(link->a_to_b) == 0);
    YTEST_REQUIRE(test, pipe(link->b_to_a) == 0);

    struct yetty_yclass_transport_pty_ptr_result transport_a_res =
        yetty_yclass_transport_pty_create(link->b_to_a[0], link->a_to_b[1]);
    YTEST_REQUIRE_OK(test, transport_a_res);
    link->a.transport = transport_a_res.value;
    struct yetty_yclass_transport_pty_ptr_result transport_b_res =
        yetty_yclass_transport_pty_create(link->a_to_b[0], link->b_to_a[1]);
    YTEST_REQUIRE_OK(test, transport_b_res);
    link->b.transport = transport_b_res.value;

    struct yetty_ycore_void_result raw_a =
        yetty_yclass_transport_pty_enable_raw_mode(link->a.transport);
    YTEST_REQUIRE_OK(test, raw_a);
    struct yetty_ycore_void_result raw_b =
        yetty_yclass_transport_pty_enable_raw_mode(link->b.transport);
    YTEST_REQUIRE_OK(test, raw_b);

    struct yetty_ywire_connection_ptr_result connection_a_res = yetty_ywire_connection_create(
        yetty_yclass_transport_pty_reactor(link->a.transport), compressed);
    YTEST_REQUIRE_OK(test, connection_a_res);
    link->a.connection = connection_a_res.value;
    struct yetty_ywire_connection_ptr_result connection_b_res = yetty_ywire_connection_create(
        yetty_yclass_transport_pty_reactor(link->b.transport), compressed);
    YTEST_REQUIRE_OK(test, connection_b_res);
    link->b.connection = connection_b_res.value;

    struct yetty_ycore_void_result role_res =
        yetty_ywire_connection_set_role(link->b.connection, 1);
    YTEST_REQUIRE_OK(test, role_res);
    return 1;
}

static void link_down(struct link *link)
{
    struct yetty_ycore_void_result res;
    res = yetty_ywire_connection_destroy(link->a.connection);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_ywire_connection_destroy(link->b.connection);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_yclass_transport_pty_destroy(link->a.transport);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_yclass_transport_pty_destroy(link->b.transport);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    close(link->a_to_b[0]);
    close(link->a_to_b[1]);
    close(link->b_to_a[0]);
    close(link->b_to_a[1]);
}

/* Shuttle bytes both ways until neither side makes progress. The pipes have
 * finite capacity, so writable and readable pumping must interleave. */
static void pump(struct ytest *test, struct link *link)
{
    for (int spin = 0; spin < 10000; spin++) {
        size_t moved = 0;
        struct yetty_ycore_size_result res;
        res = yetty_ywire_connection_pump_writable(link->a.connection);
        YTEST_REQUIRE_OK(test, res);
        moved += res.value;
        res = yetty_ywire_connection_pump_readable(link->b.connection);
        YTEST_REQUIRE_OK(test, res);
        moved += res.value;
        res = yetty_ywire_connection_pump_writable(link->b.connection);
        YTEST_REQUIRE_OK(test, res);
        moved += res.value;
        res = yetty_ywire_connection_pump_readable(link->a.connection);
        YTEST_REQUIRE_OK(test, res);
        moved += res.value;
        if (moved == 0) {
            return;
        }
    }
    YTEST_REQUIRE(test, 0 && "pump did not settle");
}

/*===========================================================================
 * Capture helpers
 *=========================================================================*/

struct accept_capture {
    struct yetty_ywire_channel *channel;
    int calls;
    int refuse;
};

static int on_accept(void *user, struct yetty_ywire_channel *channel)
{
    struct accept_capture *capture = user;
    capture->calls++;
    if (capture->refuse) {
        return 0;
    }
    capture->channel = channel;
    return 1;
}

struct event_capture {
    int remote_eof;
    int closed;
};

static void on_event(void *user, struct yetty_ywire_channel *channel,
                     enum yetty_ywire_channel_event event)
{
    (void)channel;
    struct event_capture *capture = user;
    if (event == YETTY_YWIRE_CHANNEL_EVENT_REMOTE_EOF) {
        capture->remote_eof++;
    }
    if (event == YETTY_YWIRE_CHANNEL_EVENT_CLOSED) {
        capture->closed++;
    }
}

/* Arrival log for the fairness test: one entry per DATA delivery. */
struct arrival_log {
    uint32_t channel_ids[512];
    size_t lens[512];
    size_t count;
    size_t total;
};

struct arrival_sink {
    struct arrival_log *log;
    uint32_t channel_id;
};

static void on_arrival(void *user, const uint8_t *bytes, size_t n)
{
    (void)bytes;
    struct arrival_sink *sink = user;
    if (sink->log->count < 512) {
        sink->log->channel_ids[sink->log->count] = sink->channel_id;
        sink->log->lens[sink->log->count] = n;
        sink->log->count++;
    }
    sink->log->total += n;
}

/*===========================================================================
 * Tests
 *=========================================================================*/

static void test_open_accept_data_roundtrip(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept);
    YTEST_REQUIRE_OK(test, cb_res);

    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *a_channel = open_res.value;
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_id(a_channel),
                       (int)YETTY_YWIRE_CHANNEL_DYNAMIC_BASE);
    YTEST_CHECK(test, yetty_ywire_channel_kind_of(a_channel) == YETTY_YWIRE_CHANNEL_KIND_DYNAMIC);

    pump(test, &link);
    YTEST_CHECK_EQ_INT(test, accept.calls, 1);
    YTEST_REQUIRE(test, accept.channel != NULL);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_id(accept.channel),
                       (int)yetty_ywire_channel_id(a_channel));

    /* A → B */
    static const char hello[] = "hello over a dynamic channel";
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(a_channel, hello, sizeof(hello));
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(a_channel);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);

    char received[64] = {0};
    struct yetty_ycore_size_result read_res =
        yetty_ywire_channel_read(accept.channel, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)sizeof(hello));
    YTEST_CHECK(test, memcmp(received, hello, sizeof(hello)) == 0);

    /* B → A (channels are bidirectional) */
    static const char reply[] = "reply";
    write_res = yetty_ywire_channel_write(accept.channel, reply, sizeof(reply));
    YTEST_REQUIRE_OK(test, write_res);
    flush_res = yetty_ywire_channel_flush(accept.channel);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);

    memset(received, 0, sizeof(received));
    read_res = yetty_ywire_channel_read(a_channel, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)sizeof(reply));
    YTEST_CHECK(test, memcmp(received, reply, sizeof(reply)) == 0);

    link_down(&link);
}

static void test_open_rejected(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    /* No accept callback on B — every OPEN must be refused with CLOSE. */
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *a_channel = open_res.value;
    uint32_t id = yetty_ywire_channel_id(a_channel);

    struct event_capture events = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_channel_set_event_cb(a_channel, on_event, &events);
    YTEST_REQUIRE_OK(test, cb_res);

    pump(test, &link);
    YTEST_CHECK_EQ_INT(test, events.closed, 1);
    /* The opener's slot is released once the rejection round-trips. */
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.a.connection, id) == NULL);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.b.connection, id) == NULL);

    link_down(&link);
}

static void test_eof_half_close(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept);
    YTEST_REQUIRE_OK(test, cb_res);
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *a_channel = open_res.value;
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    struct event_capture b_events = {0};
    cb_res = yetty_ywire_channel_set_event_cb(accept.channel, on_event, &b_events);
    YTEST_REQUIRE_OK(test, cb_res);

    /* Data written before EOF must arrive; the EOF trails it. */
    static const char last_words[] = "last data before eof";
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(a_channel, last_words, sizeof(last_words));
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result eof_res = yetty_ywire_channel_send_eof(a_channel);
    YTEST_REQUIRE_OK(test, eof_res);
    /* Writes after EOF fail. */
    write_res = yetty_ywire_channel_write(a_channel, "x", 1);
    YTEST_CHECK(test, YETTY_IS_ERR(write_res));
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
    }
    pump(test, &link);

    YTEST_CHECK_EQ_INT(test, b_events.remote_eof, 1);
    YTEST_CHECK(test, yetty_ywire_channel_remote_eof(accept.channel));
    char received[64] = {0};
    struct yetty_ycore_size_result read_res =
        yetty_ywire_channel_read(accept.channel, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)sizeof(last_words));

    /* The reverse direction survives the half-close. */
    static const char still_open[] = "b to a still flows";
    write_res = yetty_ywire_channel_write(accept.channel, still_open, sizeof(still_open));
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(accept.channel);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);
    memset(received, 0, sizeof(received));
    read_res = yetty_ywire_channel_read(a_channel, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)sizeof(still_open));

    link_down(&link);
}

static void test_close_handshake_releases_both_slots(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept);
    YTEST_REQUIRE_OK(test, cb_res);
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *a_channel = open_res.value;
    uint32_t id = yetty_ywire_channel_id(a_channel);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    struct event_capture b_events = {0};
    cb_res = yetty_ywire_channel_set_event_cb(accept.channel, on_event, &b_events);
    YTEST_REQUIRE_OK(test, cb_res);

    struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(a_channel);
    YTEST_REQUIRE_OK(test, close_res);
    pump(test, &link);

    YTEST_CHECK_EQ_INT(test, b_events.closed, 1);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.a.connection, id) == NULL);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.b.connection, id) == NULL);

    /* Ids are never reused: the next open gets a fresh id on a fresh slot. */
    open_res = yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_id(open_res.value),
                       (int)(YETTY_YWIRE_CHANNEL_DYNAMIC_BASE + 2));

    link_down(&link);
}

static void test_flow_control_window(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/1)) {
        return;
    }
    struct accept_capture accept = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept);
    YTEST_REQUIRE_OK(test, cb_res);
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *a_channel = open_res.value;
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    /* Write one and a bit windows' worth: the tail must stay pending until the
     * consumer drains and credit flows back. */
    const size_t total = YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT + 40000;
    uint8_t *payload = malloc(total);
    YTEST_REQUIRE(test, payload != NULL);
    for (size_t i = 0; i < total; i++) {
        payload[i] = (uint8_t)(i * 131 + 7);
    }
    struct yetty_ycore_size_result write_res = yetty_ywire_channel_write(a_channel, payload, total);
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(a_channel);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);

    /* Exactly one window's worth crossed; the sender is now credit-blocked. */
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_send_window(a_channel), 0);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_pending_out(a_channel),
                       (int)(total - YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT));

    /* Drain on B, verifying content; the drain grants credit, which unblocks
     * the tail. Keep pumping between reads (pull consumers drain in chunks). */
    uint8_t *received = malloc(total);
    YTEST_REQUIRE(test, received != NULL);
    size_t got = 0;
    for (int spin = 0; spin < 10000 && got < total; spin++) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_channel_read(accept.channel, received + got, total - got);
        YTEST_REQUIRE_OK(test, read_res);
        got += read_res.value;
        pump(test, &link);
    }
    YTEST_CHECK_EQ_INT(test, (int)got, (int)total);
    YTEST_CHECK(test, memcmp(received, payload, total) == 0);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_pending_out(a_channel), 0);

    free(payload);
    free(received);
    link_down(&link);
}

static void test_chunking_and_fair_interleaving(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept_first = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept_first);
    YTEST_REQUIRE_OK(test, cb_res);

    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *bulk_channel = open_res.value;
    pump(test, &link);
    YTEST_REQUIRE(test, accept_first.channel != NULL);
    struct yetty_ywire_channel *bulk_receiver = accept_first.channel;

    struct accept_capture accept_second = {0};
    cb_res = yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept_second);
    YTEST_REQUIRE_OK(test, cb_res);
    open_res = yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *ping_channel = open_res.value;
    pump(test, &link);
    YTEST_REQUIRE(test, accept_second.channel != NULL);
    struct yetty_ywire_channel *ping_receiver = accept_second.channel;

    struct arrival_log log = {0};
    struct arrival_sink bulk_sink = {.log = &log,
                                     .channel_id = yetty_ywire_channel_id(bulk_channel)};
    struct arrival_sink ping_sink = {.log = &log,
                                     .channel_id = yetty_ywire_channel_id(ping_channel)};
    cb_res = yetty_ywire_channel_set_raw_sink(bulk_receiver, on_arrival, &bulk_sink);
    YTEST_REQUIRE_OK(test, cb_res);
    cb_res = yetty_ywire_channel_set_raw_sink(ping_receiver, on_arrival, &ping_sink);
    YTEST_REQUIRE_OK(test, cb_res);

    /* 100 KiB of bulk first, then a tiny message on the other channel. Without
     * chunking + fair interleaving the ping would arrive only after the whole
     * bulk payload. */
    const size_t bulk_len = 100 * 1024;
    uint8_t *bulk = malloc(bulk_len);
    YTEST_REQUIRE(test, bulk != NULL);
    memset(bulk, 0xAB, bulk_len);
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(bulk_channel, bulk, bulk_len);
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(bulk_channel);
    YTEST_REQUIRE_OK(test, flush_res);

    static const char ping[] = "ping";
    write_res = yetty_ywire_channel_write(ping_channel, ping, sizeof(ping));
    YTEST_REQUIRE_OK(test, write_res);
    flush_res = yetty_ywire_channel_flush(ping_channel);
    YTEST_REQUIRE_OK(test, flush_res);

    pump(test, &link);

    YTEST_CHECK_EQ_INT(test, (int)log.total, (int)(bulk_len + sizeof(ping)));
    /* Every DATA delivery respects the chunk ceiling. */
    size_t ping_index = (size_t)-1;
    size_t last_bulk_index = 0;
    for (size_t i = 0; i < log.count; i++) {
        YTEST_CHECK(test, log.lens[i] <= YETTY_YWIRE_CHANNEL_CHUNK_MAX);
        if (log.channel_ids[i] == ping_sink.channel_id) {
            ping_index = i;
        } else {
            last_bulk_index = i;
        }
    }
    /* The ping did not wait for the bulk transfer to finish. */
    YTEST_REQUIRE(test, ping_index != (size_t)-1);
    YTEST_CHECK(test, ping_index < last_bulk_index);

    free(bulk);
    link_down(&link);
}

static void test_slot_exhaustion(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    /* 3 well-known slots + 13 dynamic: the 14th open must fail cleanly. */
    for (int i = 0; i < 13; i++) {
        struct yetty_ywire_channel_ptr_result open_res =
            yetty_ywire_connection_open_channel(link.a.connection, 0);
        YTEST_REQUIRE_OK(test, open_res);
    }
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_CHECK(test, YETTY_IS_ERR(open_res));
    if (YETTY_IS_ERR(open_res)) {
        yetty_ycore_error_destroy(open_res.error);
    }
    link_down(&link);
}

static void test_well_known_lanes_regression(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    /* rpc lane: framed envelope out, byte-stream reassembly in. */
    struct yetty_ywire_channel *a_rpc =
        yetty_ywire_connection_channel(link.a.connection, YETTY_YWIRE_CHANNEL_RPC);
    struct yetty_ywire_channel *b_rpc =
        yetty_ywire_connection_channel(link.b.connection, YETTY_YWIRE_CHANNEL_RPC);
    YTEST_REQUIRE(test, a_rpc && b_rpc);
    static const char rpc_frame[] = "\x01\x02\x03\x04rpc-bytes";
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(a_rpc, rpc_frame, sizeof(rpc_frame));
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(a_rpc);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);
    char received[64] = {0};
    struct yetty_ycore_size_result read_res =
        yetty_ywire_channel_read(b_rpc, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)sizeof(rpc_frame));
    YTEST_CHECK(test, memcmp(received, rpc_frame, sizeof(rpc_frame)) == 0);

    /* Unlimited lanes report a negative window and are never credit-blocked. */
    YTEST_CHECK(test, yetty_ywire_channel_send_window(a_rpc) < 0);

    /* raw lane: verbatim passthrough (no envelope). */
    struct yetty_ywire_channel *a_raw =
        yetty_ywire_connection_channel(link.a.connection, YETTY_YWIRE_CHANNEL_RAW);
    struct yetty_ywire_channel *b_raw =
        yetty_ywire_connection_channel(link.b.connection, YETTY_YWIRE_CHANNEL_RAW);
    YTEST_REQUIRE(test, a_raw && b_raw);
    static const char keys[] = "plain keystrokes";
    write_res = yetty_ywire_channel_write(a_raw, keys, sizeof(keys) - 1);
    YTEST_REQUIRE_OK(test, write_res);
    flush_res = yetty_ywire_channel_flush(a_raw);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);
    memset(received, 0, sizeof(received));
    read_res = yetty_ywire_channel_read(b_raw, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)(sizeof(keys) - 1));
    YTEST_CHECK(test, memcmp(received, keys, sizeof(keys) - 1) == 0);

    link_down(&link);
}

/* Many sequential rpc-lane flushes on one connection (#30): every flush is
 * framed through the connection's long-lived emit SM, so this loop proves
 * the reused encoder produces clean frames envelope after envelope — on a
 * compressed link (LZ4F context reuse) and interleaved with dynamic-channel
 * DATA traffic that shares the same emit SM. */
static void test_rpc_lane_sequential_flush_reuse(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/1)) {
        return;
    }
    struct yetty_ywire_channel *a_rpc =
        yetty_ywire_connection_channel(link.a.connection, YETTY_YWIRE_CHANNEL_RPC);
    struct yetty_ywire_channel *b_rpc =
        yetty_ywire_connection_channel(link.b.connection, YETTY_YWIRE_CHANNEL_RPC);
    YTEST_REQUIRE(test, a_rpc && b_rpc);

    enum { ROUNDS = 32, FRAME_MAX = 6000 };
    uint8_t *frame = malloc(FRAME_MAX);
    uint8_t *received = malloc(FRAME_MAX);
    YTEST_REQUIRE(test, frame && received);

    for (unsigned round = 0; round < ROUNDS; round++) {
        size_t frame_len = 1 + (round * 191) % FRAME_MAX;
        for (size_t i = 0; i < frame_len; i++) {
            frame[i] = (uint8_t)(round * 31u + i * 7u);
        }
        struct yetty_ycore_size_result write_res =
            yetty_ywire_channel_write(a_rpc, frame, frame_len);
        YTEST_REQUIRE_OK(test, write_res);
        struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(a_rpc);
        YTEST_REQUIRE_OK(test, flush_res);
        pump(test, &link);

        struct yetty_ycore_size_result read_res =
            yetty_ywire_channel_read(b_rpc, received, FRAME_MAX);
        YTEST_REQUIRE_OK(test, read_res);
        YTEST_CHECK_EQ_SIZE(test, read_res.value, frame_len);
        YTEST_CHECK_MEM_EQ(test, received, frame, frame_len);
    }

    /* Interleave: a dynamic channel's DATA frames ride the SAME emit SM as
     * the rpc lane. Alternate the two for a few rounds. */
    struct accept_capture accept = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept);
    YTEST_REQUIRE_OK(test, cb_res);
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *a_dynamic = open_res.value;
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    for (unsigned round = 0; round < 8; round++) {
        size_t frame_len = 100 + round * 517;
        for (size_t i = 0; i < frame_len; i++) {
            frame[i] = (uint8_t)(round * 13u + i * 3u);
        }
        struct yetty_ywire_channel *sender = (round % 2 == 0) ? a_rpc : a_dynamic;
        struct yetty_ywire_channel *reader =
            (round % 2 == 0) ? b_rpc : accept.channel;
        struct yetty_ycore_size_result write_res =
            yetty_ywire_channel_write(sender, frame, frame_len);
        YTEST_REQUIRE_OK(test, write_res);
        struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(sender);
        YTEST_REQUIRE_OK(test, flush_res);
        pump(test, &link);

        struct yetty_ycore_size_result read_res =
            yetty_ywire_channel_read(reader, received, FRAME_MAX);
        YTEST_REQUIRE_OK(test, read_res);
        YTEST_CHECK_EQ_SIZE(test, read_res.value, frame_len);
        YTEST_CHECK_MEM_EQ(test, received, frame, frame_len);
    }

    free(frame);
    free(received);
    link_down(&link);
}

int main(void)
{
    struct ytest test = ytest_begin("ywire_connection");
    YTEST_RUN(&test, test_open_accept_data_roundtrip);
    YTEST_RUN(&test, test_open_rejected);
    YTEST_RUN(&test, test_eof_half_close);
    YTEST_RUN(&test, test_close_handshake_releases_both_slots);
    YTEST_RUN(&test, test_flow_control_window);
    YTEST_RUN(&test, test_chunking_and_fair_interleaving);
    YTEST_RUN(&test, test_slot_exhaustion);
    YTEST_RUN(&test, test_well_known_lanes_regression);
    YTEST_RUN(&test, test_rpc_lane_sequential_flush_reuse);
    return ytest_end(&test);
}
