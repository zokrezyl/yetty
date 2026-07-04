/*
 * ywire host-side connection-layer contract test (#455).
 *
 * Mirrors the host terminal's shape exactly: a wire statemachine owned by the
 * "terminal" (the test), fed manually from the client's byte stream, with an
 * attach-mode yetty_ywire_connection registered on it and a writer callback
 * shipping outbound envelopes back to the client — while the client end is a
 * full owned-mode connection over a transport_pty, as shipped in tools.
 *
 * Covers: deterministic rejection when no accept callback is set (the bug
 * this layer fixes — OPEN used to be silently dropped), accept + DATA both
 * directions, host-initiated channels (odd ids), EOF/CLOSE handshake, and
 * host-side flow-control credit for a pull consumer.
 */

#include <yetty/yclass/transport-pty.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/channel.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*===========================================================================
 * Fixture: owned-mode client <-> attach-mode host over two pipes
 *=========================================================================*/

struct host_link {
    /* Client end — the shape every tool ships with. */
    struct yetty_yclass_transport_pty *client_transport;
    struct yetty_ywire_connection *client;
    /* Host end — the terminal's shape: own SM + attach-mode connection. */
    struct yetty_ywire_wire_statemachine *host_sm;
    struct yetty_ywire_connection *host;
    int client_to_host[2];
    int host_to_client[2];
};

/* The host writer — the stand-in for the terminal's PTY-master write path:
 * ships one whole envelope, blocking until written. */
static struct yetty_ycore_void_result host_writer(const uint8_t *bytes, size_t n, void *user)
{
    struct host_link *link = user;
    size_t off = 0;
    while (off < n) {
        ssize_t written = write(link->host_to_client[1], bytes + off, n - off);
        if (written < 0) {
            return YETTY_ERR(yetty_ycore_void, "host_writer: write failed");
        }
        off += (size_t)written;
    }
    return YETTY_OK_VOID();
}

static int host_link_up(struct ytest *test, struct host_link *link)
{
    memset(link, 0, sizeof(*link));
    YTEST_REQUIRE(test, pipe(link->client_to_host) == 0);
    YTEST_REQUIRE(test, pipe(link->host_to_client) == 0);
    /* The host's inbound drain below must not block when the client has
     * nothing in flight. */
    YTEST_REQUIRE(test, fcntl(link->client_to_host[0], F_SETFL, O_NONBLOCK) == 0);

    struct yetty_yclass_transport_pty_ptr_result transport_res =
        yetty_yclass_transport_pty_create(link->host_to_client[0], link->client_to_host[1]);
    YTEST_REQUIRE_OK(test, transport_res);
    link->client_transport = transport_res.value;
    struct yetty_ycore_void_result raw_res =
        yetty_yclass_transport_pty_enable_raw_mode(link->client_transport);
    YTEST_REQUIRE_OK(test, raw_res);
    struct yetty_ywire_connection_ptr_result client_res = yetty_ywire_connection_create(
        yetty_yclass_transport_pty_reactor(link->client_transport), /*compressed=*/0);
    YTEST_REQUIRE_OK(test, client_res);
    link->client = client_res.value;

    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm_res);
    link->host_sm = sm_res.value;
    struct yetty_ywire_connection_ptr_result host_res =
        yetty_ywire_connection_attach(link->host_sm, host_writer, link, /*compressed=*/0);
    YTEST_REQUIRE_OK(test, host_res);
    link->host = host_res.value;
    return 1;
}

static void host_link_down(struct host_link *link)
{
    struct yetty_ycore_void_result res;
    res = yetty_ywire_connection_destroy(link->client);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_yclass_transport_pty_destroy(link->client_transport);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    /* Terminal contract: the SM goes first, the attached connection after. */
    res = yetty_ywire_wire_statemachine_destroy(link->host_sm);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_ywire_connection_destroy(link->host);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    close(link->client_to_host[0]);
    close(link->client_to_host[1]);
    close(link->host_to_client[0]);
    close(link->host_to_client[1]);
}

/* One host inbound step — what the terminal's PTY read path does: read what
 * the client sent, feed the terminal's SM, process (fires the attach-mode
 * channel handler, whose replies go out through host_writer). Returns bytes
 * consumed. */
static size_t host_drain_once(struct ytest *test, struct host_link *link)
{
    uint8_t buf[4096];
    size_t total = 0;
    for (;;) {
        ssize_t got = read(link->client_to_host[0], buf, sizeof(buf));
        if (got <= 0) {
            break;
        }
        total += (size_t)got;
        struct yetty_ycore_void_result feed_res =
            yetty_ywire_wire_statemachine_feed(link->host_sm, (const char *)buf, (size_t)got);
        YTEST_REQUIRE_OK(test, feed_res);
        struct yetty_ycore_void_result process_res =
            yetty_ywire_wire_statemachine_process(link->host_sm);
        YTEST_REQUIRE_OK(test, process_res);
    }
    return total;
}

/* Shuttle both directions until neither side makes progress. */
static void pump(struct ytest *test, struct host_link *link)
{
    for (int spin = 0; spin < 10000; spin++) {
        size_t moved = 0;
        struct yetty_ycore_size_result res;
        res = yetty_ywire_connection_pump_writable(link->client);
        YTEST_REQUIRE_OK(test, res);
        moved += res.value;
        moved += host_drain_once(test, link);
        res = yetty_ywire_connection_pump_readable(link->client);
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
};

static int on_accept(void *user, struct yetty_ywire_channel *channel)
{
    struct accept_capture *capture = user;
    capture->calls++;
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

struct byte_capture {
    uint8_t bytes[4096];
    size_t len;
};

static void on_bytes(void *user, const uint8_t *bytes, size_t n)
{
    struct byte_capture *capture = user;
    size_t room = sizeof(capture->bytes) - capture->len;
    size_t take = n < room ? n : room;
    memcpy(capture->bytes + capture->len, bytes, take);
    capture->len += take;
}

/* Echo consumer for the host: whatever arrives goes straight back out on the
 * same channel (write + flush from inside the sink — the attach-mode ship
 * path is synchronous, so this exercises reentrancy from the demux). */
static void on_echo(void *user, const uint8_t *bytes, size_t n)
{
    struct yetty_ywire_channel *channel = user;
    struct yetty_ycore_size_result write_res = yetty_ywire_channel_write(channel, bytes, n);
    if (YETTY_IS_ERR(write_res)) {
        yetty_ycore_error_destroy(write_res.error);
        return;
    }
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(channel);
    if (YETTY_IS_ERR(flush_res)) {
        yetty_ycore_error_destroy(flush_res.error);
    }
}

struct echo_acceptor {
    struct yetty_ywire_channel *channel;
};

static int on_accept_echo(void *user, struct yetty_ywire_channel *channel)
{
    struct echo_acceptor *acceptor = user;
    acceptor->channel = channel;
    struct yetty_ycore_void_result sink_res =
        yetty_ywire_channel_set_raw_sink(channel, on_echo, channel);
    if (YETTY_IS_ERR(sink_res)) {
        yetty_ycore_error_destroy(sink_res.error);
        return 0;
    }
    return 1;
}

/*===========================================================================
 * Tests
 *=========================================================================*/

static void test_open_rejected_by_default(struct ytest *test)
{
    struct host_link link;
    if (!host_link_up(test, &link)) {
        return;
    }
    /* No accept callback on the host — the terminal's default policy. Before
     * #455 this OPEN was silently dropped and the client hung; now the host
     * answers CLOSE and the client gets a deterministic CLOSED event. */
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.client, 0);
    YTEST_REQUIRE_OK(test, open_res);
    uint32_t id = yetty_ywire_channel_id(open_res.value);
    struct event_capture events = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_channel_set_event_cb(open_res.value, on_event, &events);
    YTEST_REQUIRE_OK(test, cb_res);

    pump(test, &link);
    YTEST_CHECK_EQ_INT(test, events.closed, 1);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.client, id) == NULL);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.host, id) == NULL);
    host_link_down(&link);
}

static void test_accept_and_echo(struct ytest *test)
{
    struct host_link link;
    if (!host_link_up(test, &link)) {
        return;
    }
    struct echo_acceptor acceptor = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.host, on_accept_echo, &acceptor);
    YTEST_REQUIRE_OK(test, cb_res);

    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.client, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *client_channel = open_res.value;

    struct byte_capture echoed = {0};
    cb_res = yetty_ywire_channel_set_raw_sink(client_channel, on_bytes, &echoed);
    YTEST_REQUIRE_OK(test, cb_res);

    static const char ping[] = "ping through the host acceptor";
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(client_channel, ping, sizeof(ping));
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(client_channel);
    YTEST_REQUIRE_OK(test, flush_res);

    pump(test, &link);
    YTEST_REQUIRE(test, acceptor.channel != NULL);
    YTEST_CHECK_EQ_INT(test, (int)echoed.len, (int)sizeof(ping));
    YTEST_CHECK(test, memcmp(echoed.bytes, ping, sizeof(ping)) == 0);

    /* Close from the client; both slots release. */
    uint32_t id = yetty_ywire_channel_id(client_channel);
    struct yetty_ycore_void_result close_res = yetty_ywire_channel_close(client_channel);
    YTEST_REQUIRE_OK(test, close_res);
    pump(test, &link);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.client, id) == NULL);
    YTEST_CHECK(test, yetty_ywire_connection_channel(link.host, id) == NULL);
    host_link_down(&link);
}

static void test_host_initiated_channel(struct ytest *test)
{
    struct host_link link;
    if (!host_link_up(test, &link)) {
        return;
    }
    struct accept_capture client_accept = {0};
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.client, on_accept, &client_accept);
    YTEST_REQUIRE_OK(test, cb_res);

    /* The host (acceptor role) opens toward the client: odd id space. */
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.host, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *host_channel = open_res.value;
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_id(host_channel),
                       (int)(YETTY_YWIRE_CHANNEL_DYNAMIC_BASE + 1));

    pump(test, &link);
    YTEST_CHECK_EQ_INT(test, client_accept.calls, 1);
    YTEST_REQUIRE(test, client_accept.channel != NULL);

    static const char notice[] = "host pushes to the client";
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(host_channel, notice, sizeof(notice));
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(host_channel);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);

    char received[64] = {0};
    struct yetty_ycore_size_result read_res =
        yetty_ywire_channel_read(client_accept.channel, received, sizeof(received));
    YTEST_REQUIRE_OK(test, read_res);
    YTEST_CHECK_EQ_INT(test, (int)read_res.value, (int)sizeof(notice));
    YTEST_CHECK(test, memcmp(received, notice, sizeof(notice)) == 0);
    host_link_down(&link);
}

static void test_host_grants_credit_to_bulk_sender(struct ytest *test)
{
    struct host_link link;
    if (!host_link_up(test, &link)) {
        return;
    }
    struct accept_capture host_accept = {0};
    /* Pull consumer on the host (no sink): the window must observably run
     * dry, then refill as the host-side read() grants credit through the
     * writer path. */
    struct yetty_ycore_void_result cb_res =
        yetty_ywire_connection_set_accept_cb(link.host, on_accept, &host_accept);
    YTEST_REQUIRE_OK(test, cb_res);
    struct yetty_ywire_channel_ptr_result open_res =
        yetty_ywire_connection_open_channel(link.client, 0);
    YTEST_REQUIRE_OK(test, open_res);
    struct yetty_ywire_channel *client_channel = open_res.value;
    pump(test, &link);
    YTEST_REQUIRE(test, host_accept.channel != NULL);

    const size_t total = YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT + 30000;
    uint8_t *payload = malloc(total);
    YTEST_REQUIRE(test, payload != NULL);
    for (size_t i = 0; i < total; i++) {
        payload[i] = (uint8_t)(i * 37 + 11);
    }
    struct yetty_ycore_size_result write_res =
        yetty_ywire_channel_write(client_channel, payload, total);
    YTEST_REQUIRE_OK(test, write_res);
    struct yetty_ycore_void_result flush_res = yetty_ywire_channel_flush(client_channel);
    YTEST_REQUIRE_OK(test, flush_res);
    pump(test, &link);

    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_send_window(client_channel), 0);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_pending_out(client_channel),
                       (int)(total - YETTY_YWIRE_CHANNEL_WINDOW_DEFAULT));

    uint8_t *received = malloc(total);
    YTEST_REQUIRE(test, received != NULL);
    size_t got = 0;
    for (int spin = 0; spin < 10000 && got < total; spin++) {
        struct yetty_ycore_size_result read_res =
            yetty_ywire_channel_read(host_accept.channel, received + got, total - got);
        YTEST_REQUIRE_OK(test, read_res);
        got += read_res.value;
        pump(test, &link);
    }
    YTEST_CHECK_EQ_INT(test, (int)got, (int)total);
    YTEST_CHECK(test, memcmp(received, payload, total) == 0);
    YTEST_CHECK_EQ_INT(test, (int)yetty_ywire_channel_pending_out(client_channel), 0);

    free(payload);
    free(received);
    host_link_down(&link);
}

int main(void)
{
    struct ytest test = ytest_begin("ywire_host_connection");
    YTEST_RUN(&test, test_open_rejected_by_default);
    YTEST_RUN(&test, test_accept_and_echo);
    YTEST_RUN(&test, test_host_initiated_channel);
    YTEST_RUN(&test, test_host_grants_credit_to_bulk_sender);
    return ytest_end(&test);
}
