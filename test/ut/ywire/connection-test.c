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
#include <yetty/yterminal/client-input.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
        struct yetty_ywire_channel *reader = (round % 2 == 0) ? b_rpc : accept.channel;
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

/* Stand-in for a sink whose owner (the GUI framework) teardown has already
 * destroyed: any call is the use-after-free the drain must make impossible. */
static void canary_raw_sink(void *user, const uint8_t *bytes, size_t n)
{
    (void)bytes;
    (void)n;
    ++*(int *)user;
}

/* The COMPLETION-AWARE teardown drain contract (the exit-hygiene fix in the
 * yguiapp host + ygreeter): pending_close_count is the predicate the exit
 * drain pumps on — 0 before a close, 1 from the local close until the peer's
 * FRAMED CLOSE echo is parsed, back to 0 on the echo — and the drain STOPS
 * READING at that point. Per the review: the final CLOSE echo, an in-flight
 * DATA frame, and a raw user key are ALL queued in the same inbound
 * availability BEFORE one production drain call; the key must remain unread
 * and no sink callback may touch the destroyed owner. */
static void test_close_drain_preserves_user_key(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    /* Nothing closed: nothing pending. */
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_pending_close_count(link.a.connection), 0);

    /* Local close: pending flips to 1 (awaiting the peer's echo). */
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_close(open_res.value));
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_pending_close_count(link.a.connection), 1);

    /* Dead-host boundedness: with nothing inbound the predicate holds at 1 —
     * the production loop's spin deadline is what ends the wait, never a
     * false completion. */
    struct yetty_ycore_size_result io_res = yetty_ywire_connection_pump_readable(link.a.connection);
    YTEST_REQUIRE_OK(test, io_res);
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_pending_close_count(link.a.connection), 1);

    /* Interleaved inbound, all queued into A's pipe BEFORE one drain call —
     * the same kernel-readable availability:
     *   [channel DATA "x"] [B's framed CLOSE echo] [raw user key 'k']
     * B writes the DATA before it has seen A's CLOSE (in flight), then
     * processes the CLOSE and echoes; the raw key follows the echo. */
    io_res = yetty_ywire_connection_pump_writable(link.a.connection); /* ship A's CLOSE */
    YTEST_REQUIRE_OK(test, io_res);
    io_res = yetty_ywire_channel_write(accept.channel, "x", 1); /* B: in-flight DATA */
    YTEST_REQUIRE_OK(test, io_res);
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_flush(accept.channel));
    io_res = yetty_ywire_connection_pump_writable(link.b.connection);
    YTEST_REQUIRE_OK(test, io_res);
    io_res = yetty_ywire_connection_pump_readable(link.b.connection); /* B sees CLOSE */
    YTEST_REQUIRE_OK(test, io_res);
    io_res = yetty_ywire_connection_pump_writable(link.b.connection); /* B ships echo */
    YTEST_REQUIRE_OK(test, io_res);
    YTEST_CHECK(test, write(link.b_to_a[1], "k", 1) == 1); /* the user key */
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_pending_close_count(link.a.connection), 1);

    /* Production teardown order: the sink's owner (the framework) is being
     * destroyed, so the sink is DETACHED first — anything the drain still
     * parses must never reach the dead owner (the in-flight DATA lands in
     * the channel inbuf instead). The canary registered at open stands in
     * for the destroyed framework: any call is the use-after-free. */
    int canary_calls = 0;
    YTEST_REQUIRE_OK(
        test, yetty_ywire_channel_set_raw_sink(open_res.value, canary_raw_sink, &canary_calls));
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_set_raw_sink(open_res.value, NULL, NULL));

    /* ONE production drain call over the whole queued availability: consumes
     * the DATA frame and the echo, reaches completion, and stops EXACTLY at
     * the framed boundary. */
    int still_pending = yetty_ywire_connection_drain_closes(link.a.connection, 500);
    YTEST_CHECK_EQ_INT(test, still_pending, 0);
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_pending_close_count(link.a.connection), 0);
    YTEST_CHECK_EQ_INT(test, canary_calls, 0); /* nothing touched the dead owner */

    /* The USER KEY queued in the SAME availability right after the echo
     * SURVIVES unread for whoever reads the terminal next (the shell). */
    char key = 0;
    YTEST_CHECK(test, read(link.b_to_a[0], &key, 1) == 1);
    YTEST_CHECK_EQ_INT(test, key, 'k');

    link_down(&link);
}

/* The wall-clock bound (review at fd37ea71): a peer that keeps the fd
 * readable WITHOUT ever completing — key-repeat flood, a babbling host —
 * must not hold teardown open. The drain must return within its
 * CLOCK_MONOTONIC deadline even though every poll is instantly readable. */
static void test_close_drain_wall_clock_bounded(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_close(open_res.value));

    /* No echo will ever come; instead the peer babbles raw traffic. Keep the
     * pipe re-filled from a background writer? Not needed: the deadline must
     * hold even with a large pre-queued flood plus poll-timeout tail. */
    char flood[4096];
    memset(flood, 'x', sizeof(flood));
    for (int chunk = 0; chunk < 8; ++chunk) {
        YTEST_CHECK(test, write(link.b_to_a[1], flood, sizeof(flood)) == (ssize_t)sizeof(flood));
    }

    struct timespec before, after;
    YTEST_REQUIRE(test, clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    int still_pending = yetty_ywire_connection_drain_closes(link.a.connection, 150);
    YTEST_REQUIRE(test, clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    long long elapsed_ms = (long long)(after.tv_sec - before.tv_sec) * 1000 +
                           (after.tv_nsec - before.tv_nsec) / 1000000;
    /* Never completed (no echo), and returned within the wall-clock bound —
     * generous 10x margin for a loaded CI box, far below "indefinitely". */
    YTEST_CHECK_EQ_INT(test, still_pending, 1);
    YTEST_CHECK(test, elapsed_ms < 1500);
    link_down(&link);
}

/* The host-owned input barrier (the SAFE replacement for the deleted 610014
 * child-supplied handback). On the host side of a pane connection it holds the
 * host's OWN user keystrokes while the client tears down and releases them once
 * the client is gone — exactly once. The security invariant: the release
 * carries ONLY bytes the host explicitly held via _hold(); nothing the peer
 * (the pane child) SENDS can ever become released PTY input. */
static void test_input_barrier_holds_and_releases_own_input(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    /* B is the HOST. The client's INPUT_HOLD arms the barrier at teardown start
     * (before it closes the channel), so keystrokes typed anywhere in the
     * window are held — even ones the host writes before the client's CLOSE. */
    yetty_ywire_connection_input_barrier_arm(link.b.connection, 5000);
    /* Capture into a variable first: YTEST_CHECK_EQ_INT evaluates its argument
     * twice, and _hold() has a side effect (it appends) — inlining it would
     * hold each run twice. */
    int held_ab = yetty_ywire_connection_input_barrier_hold(link.b.connection, "ab", 2);
    YTEST_CHECK_EQ_INT(test, held_ab, 1);
    int held_c = yetty_ywire_connection_input_barrier_hold(link.b.connection, "c", 1);
    YTEST_CHECK_EQ_INT(test, held_c, 1);

    /* NOTHING releases while the client still has a channel open (a running
     * client never gets its own input replayed early). */
    struct yetty_ycore_buffer early = {0};
    YTEST_CHECK_EQ_INT(test,
                       yetty_ywire_connection_input_barrier_release(link.b.connection, &early), 0);
    YTEST_CHECK_EQ_SIZE(test, early.size, 0);
    yetty_ycore_buffer_destroy(&early);

    /* The client tears down: close its channel and pump the handshake so B
     * releases the channel (client gone). */
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_close(open_res.value));
    pump(test, &link);

    /* Client gone → release EXACTLY the held bytes. */
    struct yetty_ycore_buffer out = {0};
    int released = yetty_ywire_connection_input_barrier_release(link.b.connection, &out);
    YTEST_CHECK_EQ_INT(test, released, 3);
    YTEST_CHECK_EQ_SIZE(test, out.size, 3);
    if (out.size == 3) {
        YTEST_CHECK(test, memcmp(out.data, "abc", 3) == 0);
    }
    /* Delivered EXACTLY ONCE: a second release yields nothing. */
    struct yetty_ycore_buffer again = {0};
    YTEST_CHECK_EQ_INT(test,
                       yetty_ywire_connection_input_barrier_release(link.b.connection, &again), 0);
    yetty_ycore_buffer_destroy(&out);
    yetty_ycore_buffer_destroy(&again);
    link_down(&link);
}

/* Child output cannot synthesize released input. The peer (pane child) floods
 * the connection with raw bytes AND framed channel DATA; the host holds NONE of
 * its own input. On release the barrier yields ZERO bytes — nothing the child
 * sent can become PTY input. Only host-held keystrokes ever release; _hold() is
 * a separate API the terminal calls solely from its keystroke sink. */
static void test_input_barrier_no_child_injection(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    yetty_ywire_connection_input_barrier_arm(link.b.connection, 5000);

    /* The child sends channel DATA that LOOKS like a shell command. It reaches
     * B's raw/channel inbuf via the normal inbound path — never the barrier. */
    static const char evil[] = "rm -rf ~\n";
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_write(open_res.value, evil, sizeof(evil) - 1));
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_flush(open_res.value));
    pump(test, &link);

    /* The host held nothing of its own. */
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_close(open_res.value));
    pump(test, &link);

    struct yetty_ycore_buffer out = {0};
    int released = yetty_ywire_connection_input_barrier_release(link.b.connection, &out);
    YTEST_CHECK_EQ_INT(test, released, 0); /* the child's bytes NEVER become input */
    YTEST_CHECK_EQ_SIZE(test, out.size, 0);
    yetty_ycore_buffer_destroy(&out);
    link_down(&link);
}

/* The arm/teardown-race fix: the barrier only holds once ARMED. Before the arm
 * (the pre-ACK window) a hold is DECLINED (returns 0) so the caller forwards the
 * key to the pane, where the still-live client handles it — nothing is held and
 * nothing is lost. This is the invariant the HOLD-ACK handshake relies on: the
 * client keeps its sinks alive until the ACK precisely because pre-arm keys go
 * through this decline-and-forward path. */
static void test_input_barrier_unarmed_declines(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    /* Not armed: hold declines. */
    int held = yetty_ywire_connection_input_barrier_hold(link.b.connection, "k", 1);
    YTEST_CHECK_EQ_INT(test, held, 0);
    /* And an unarmed release yields nothing. */
    struct yetty_ycore_buffer out = {0};
    int released = yetty_ywire_connection_input_barrier_release(link.b.connection, &out);
    YTEST_CHECK_EQ_INT(test, released, 0);
    YTEST_CHECK_EQ_SIZE(test, out.size, 0);
    yetty_ycore_buffer_destroy(&out);
    link_down(&link);
}

/* The framed HOLD-ACK handshake, client side. drain_until_hold_ack must:
 *   - return 0 within its wall-clock bound when no ACK arrives (dead host), and
 *   - return 1 once the host's framed INPUT_HOLD_ACK envelope is parsed,
 * reading the connection's inbound fd byte-wise the same way the close drain
 * does. This is what the client waits on before detaching its sinks. */
static void test_input_barrier_ack_detect(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }

    /* No ACK on the wire: the wait times out to 0, bounded by its deadline. */
    struct timespec before, after;
    YTEST_REQUIRE(test, clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    int seen = yetty_ywire_connection_drain_until_hold_ack(link.a.connection, 100);
    YTEST_REQUIRE(test, clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    long long elapsed_ms = (long long)(after.tv_sec - before.tv_sec) * 1000 +
                           (after.tv_nsec - before.tv_nsec) / 1000000;
    YTEST_CHECK_EQ_INT(test, seen, 0);
    YTEST_CHECK(test, elapsed_ms < 1000);

    /* The host emits the framed HOLD-ACK inline on A's inbound (exactly how the
     * terminal ships it: DCS INPUT_HOLD_ACK, has_args=1, no body). */
    struct yetty_ycore_buffer ack_env = {0};
    YTEST_REQUIRE_OK(test, yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS,
                                            YETTY_OSC_CS_CLIENT_INPUT_HOLD_ACK, /*has_args=*/1,
                                            /*compressed=*/0, NULL, 0, NULL, 0, &ack_env));
    YTEST_CHECK(test, write(link.b_to_a[1], ack_env.data, ack_env.size) == (ssize_t)ack_env.size);
    yetty_ycore_buffer_destroy(&ack_env);

    /* Now the wait sees the ACK. */
    seen = yetty_ywire_connection_drain_until_hold_ack(link.a.connection, 500);
    YTEST_CHECK_EQ_INT(test, seen, 1);
    /* Idempotent: once parsed it stays seen without touching the fd again. */
    seen = yetty_ywire_connection_drain_until_hold_ack(link.a.connection, 500);
    YTEST_CHECK_EQ_INT(test, seen, 1);

    link_down(&link);
}

/* A paste far larger than the old 8 KiB cap must be HELD in full, never bypassed
 * into the drainable stream (which was the reintroduced loss). While armed the
 * hold always takes the bytes; the whole paste releases to the resumed shell
 * exactly once. */
static void test_input_barrier_over_cap_paste(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    yetty_ywire_connection_input_barrier_arm(link.b.connection, 5000);

    enum { PASTE_LEN = 16384 }; /* 2x the retired cap */
    char *paste = malloc(PASTE_LEN);
    YTEST_REQUIRE(test, paste != NULL);
    memset(paste, 'z', PASTE_LEN);
    /* Capture first: YTEST_CHECK_EQ_INT double-evaluates and _hold() appends. */
    int held = yetty_ywire_connection_input_barrier_hold(link.b.connection, paste, PASTE_LEN);
    YTEST_CHECK_EQ_INT(test, held, 1); /* held in full, NOT declined/bypassed */

    /* Client gone → the entire paste releases, exactly once. */
    YTEST_REQUIRE_OK(test, yetty_ywire_channel_close(open_res.value));
    pump(test, &link);
    struct yetty_ycore_buffer out = {0};
    int released = yetty_ywire_connection_input_barrier_release(link.b.connection, &out);
    YTEST_CHECK_EQ_INT(test, released, PASTE_LEN);
    YTEST_CHECK_EQ_SIZE(test, out.size, (size_t)PASTE_LEN);
    if (out.size == (size_t)PASTE_LEN) {
        YTEST_CHECK(test, memcmp(out.data, paste, PASTE_LEN) == 0);
    }
    yetty_ycore_buffer_destroy(&out);
    free(paste);
    link_down(&link);
}

/* The ACK-gate fallback: when no HOLD-ACK arrives (an older/dead host, or a
 * failed ACK write), drain_until_hold_ack returns 0 and the client MUST skip the
 * inbound close drain — otherwise an unarmed host's forwarded key, interleaved
 * with the CLOSE echoes, is consumed sink-detached (the original regression). In
 * the real sequence nothing is on the wire during the ACK wait (the client has
 * not sent its CLOSEs yet), so the wait just times out; the key + echo arrive
 * AFTER, and because the drain is skipped they stay queued for the shell. */
static void test_input_barrier_no_ack_gate(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    /* No ACK on the (empty) wire: the wait times out to 0, consuming nothing. */
    struct timespec before, after;
    YTEST_REQUIRE(test, clock_gettime(CLOCK_MONOTONIC, &before) == 0);
    int ack = yetty_ywire_connection_drain_until_hold_ack(link.a.connection, 80);
    YTEST_REQUIRE(test, clock_gettime(CLOCK_MONOTONIC, &after) == 0);
    long long elapsed_ms = (long long)(after.tv_sec - before.tv_sec) * 1000 +
                           (after.tv_nsec - before.tv_nsec) / 1000000;
    YTEST_CHECK_EQ_INT(test, ack, 0);
    YTEST_CHECK(test, elapsed_ms < 800);

    /* ack==0 → the client SKIPS drain_closes. Model the post-teardown wire (the
     * host forwards a user key) and prove that without the drain it survives. */
    YTEST_CHECK(test, write(link.b_to_a[1], "k", 1) == 1);
    char key = 0;
    YTEST_CHECK(test, read(link.b_to_a[0], &key, 1) == 1);
    YTEST_CHECK_EQ_INT(test, key, 'k');

    link_down(&link);
}

/* Bounded retention (P0#2): an armed barrier has an ENFORCED host-side deadline.
 * A client that arms and never closes its channel while input keeps arriving
 * must NOT make the host accumulate keystrokes without limit — past the deadline
 * the hold refuses and the backlog is recoverable even with the channel open. */
static void test_input_barrier_deadline_bounds_retention(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    /* Arm with a SHORT deadline; the client channel stays OPEN the whole test. */
    yetty_ywire_connection_input_barrier_arm(link.b.connection, 40);
    int held_before = yetty_ywire_connection_input_barrier_hold(link.b.connection, "ab", 2);
    YTEST_CHECK_EQ_INT(test, held_before, 1);

    /* Cross the deadline. */
    struct timespec nap = {.tv_sec = 0, .tv_nsec = 90 * 1000000L};
    nanosleep(&nap, NULL);

    /* Past the deadline: further input is NOT accumulated — retention bounded. */
    int held_after = yetty_ywire_connection_input_barrier_hold(link.b.connection, "cd", 2);
    YTEST_CHECK_EQ_INT(test, held_after, 0);

    /* The backlog is recoverable via the deadline path even though the client's
     * channel is STILL open, and exactly once. */
    struct yetty_ycore_buffer out = {0};
    int released = yetty_ywire_connection_input_barrier_release(link.b.connection, &out);
    YTEST_CHECK_EQ_INT(test, released, 2);
    YTEST_CHECK_EQ_SIZE(test, out.size, 2);
    if (out.size == 2) {
        YTEST_CHECK(test, memcmp(out.data, "ab", 2) == 0);
    }
    struct yetty_ycore_buffer again = {0};
    YTEST_CHECK_EQ_INT(test,
                       yetty_ywire_connection_input_barrier_release(link.b.connection, &again), 0);
    YTEST_REQUIRE(test, accept.channel != NULL); /* owning channel never closed */
    yetty_ycore_buffer_destroy(&out);
    yetty_ycore_buffer_destroy(&again);
    link_down(&link);
}

/* Forced release (P0#2): on PTY EOF / owner death the host recovers the held
 * bytes unconditionally — normal release still refuses (channel open, deadline
 * not passed), forced release hands them back exactly once. */
static void test_input_barrier_forced_release(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    struct accept_capture accept = {0};
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_connection_set_accept_cb(link.b.connection, on_accept, &accept));
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.a.connection, 0);
    YTEST_REQUIRE_OK(test, open_res);
    pump(test, &link);
    YTEST_REQUIRE(test, accept.channel != NULL);

    yetty_ywire_connection_input_barrier_arm(link.b.connection, 5000);
    int held = yetty_ywire_connection_input_barrier_hold(link.b.connection, "xyz", 3);
    YTEST_CHECK_EQ_INT(test, held, 1);

    /* Normal release refuses: channel open AND deadline not passed. */
    struct yetty_ycore_buffer early = {0};
    YTEST_CHECK_EQ_INT(test,
                       yetty_ywire_connection_input_barrier_release(link.b.connection, &early), 0);
    yetty_ycore_buffer_destroy(&early);

    /* Forced release recovers the bytes regardless (owner death / PTY EOF). */
    struct yetty_ycore_buffer out = {0};
    int forced = yetty_ywire_connection_input_barrier_release_forced(link.b.connection, &out);
    YTEST_CHECK_EQ_INT(test, forced, 3);
    YTEST_CHECK_EQ_SIZE(test, out.size, 3);
    if (out.size == 3) {
        YTEST_CHECK(test, memcmp(out.data, "xyz", 3) == 0);
    }
    /* Exactly once: a second forced release yields nothing. */
    struct yetty_ycore_buffer again = {0};
    YTEST_CHECK_EQ_INT(
        test, yetty_ywire_connection_input_barrier_release_forced(link.b.connection, &again), 0);
    yetty_ycore_buffer_destroy(&out);
    yetty_ycore_buffer_destroy(&again);
    link_down(&link);
}

/* The ACK is a bounded LEASE, not a permanent grant (P0#1). The host's barrier
 * expires on its own deadline and resumes forwarding; a client that stalls past
 * the lease before draining must treat the ACK as stale and SKIP the drain,
 * else it could consume a newly-forwarded key sink-detached. hold_ack_lease_valid
 * is the freshness gate the teardown checks: valid only while the ACK is recent. */
static void test_input_barrier_ack_lease(struct ytest *test)
{
    struct link link;
    if (!link_up(test, &link, /*compressed=*/0)) {
        return;
    }
    /* No ACK yet → the lease is invalid (the client must not drain). */
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_hold_ack_lease_valid(link.a.connection, 5000),
                       0);

    /* Deliver a real HOLD_ACK and observe it. */
    struct yetty_ycore_buffer ack_env = {0};
    YTEST_REQUIRE_OK(test, yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS,
                                            YETTY_OSC_CS_CLIENT_INPUT_HOLD_ACK, /*has_args=*/1,
                                            /*compressed=*/0, NULL, 0, NULL, 0, &ack_env));
    YTEST_CHECK(test, write(link.b_to_a[1], ack_env.data, ack_env.size) == (ssize_t)ack_env.size);
    yetty_ycore_buffer_destroy(&ack_env);
    int seen = yetty_ywire_connection_drain_until_hold_ack(link.a.connection, 500);
    YTEST_CHECK_EQ_INT(test, seen, 1);

    /* Fresh: a generous lease is valid — the client would drain. */
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_hold_ack_lease_valid(link.a.connection, 5000),
                       1);

    /* Stall past a short lease: now stale — the client SKIPS the drain (the host
     * may already have expired its barrier and resumed forwarding). */
    struct timespec nap = {.tv_sec = 0, .tv_nsec = 60 * 1000000L};
    nanosleep(&nap, NULL);
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_hold_ack_lease_valid(link.a.connection, 30), 0);
    /* Still valid under a lease that covers the elapsed time. */
    YTEST_CHECK_EQ_INT(test, yetty_ywire_connection_hold_ack_lease_valid(link.a.connection, 5000),
                       1);
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
    YTEST_RUN(&test, test_close_drain_preserves_user_key);
    YTEST_RUN(&test, test_close_drain_wall_clock_bounded);
    YTEST_RUN(&test, test_input_barrier_holds_and_releases_own_input);
    YTEST_RUN(&test, test_input_barrier_no_child_injection);
    YTEST_RUN(&test, test_input_barrier_unarmed_declines);
    YTEST_RUN(&test, test_input_barrier_ack_detect);
    YTEST_RUN(&test, test_input_barrier_over_cap_paste);
    YTEST_RUN(&test, test_input_barrier_no_ack_gate);
    YTEST_RUN(&test, test_input_barrier_deadline_bounds_retention);
    YTEST_RUN(&test, test_input_barrier_forced_release);
    YTEST_RUN(&test, test_input_barrier_ack_lease);
    return ytest_end(&test);
}
