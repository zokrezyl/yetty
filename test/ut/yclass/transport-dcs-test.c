/*
 * DCS-over-PTY transport contract test (#30 hardening).
 *
 * The transport shares ONE wire state machine for both directions: the
 * scanner assembles inbound envelopes from read_fd while the encoder frames
 * outbound flushes into a reusable buffer (no transient SM / LZ4F context
 * per flush). Wired over two pipes with the test playing the host: it reads
 * the transport's request envelopes off one pipe (verifying them with an
 * independent SM) and pre-writes reply envelopes into the other. Covers
 * sequential flush reuse (compressed and not), duplex rounds where encode
 * and decode alternate on the shared SM, and the empty-outbuf no-op flush.
 */

#include <yetty/yclass/transport-dcs.h>
#include <yetty/yclass/transport.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { TEST_DCS_CODE = 600042 };

/*===========================================================================
 * Fixture: transport over two pipes + a host-side verify SM
 *=========================================================================*/

struct capture {
    int calls;
    int matches;
    const uint8_t *expect;
    size_t expect_len;
};

static struct yetty_ycore_void_result on_envelope(void *userdata,
                                                  enum yetty_ywire_envelope_kind kind, int code,
                                                  const uint8_t *args, size_t args_len,
                                                  const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)code;
    (void)args;
    (void)args_len;
    struct capture *capture = userdata;
    capture->calls++;
    if (payload_len == capture->expect_len &&
        (payload_len == 0 || memcmp(payload, capture->expect, payload_len) == 0)) {
        capture->matches++;
    }
    return YETTY_OK_VOID();
}

struct fixture {
    int request_pipe[2]; /* transport writes envelopes; test reads [0] */
    int reply_pipe[2];   /* test writes envelopes [1]; transport reads */
    struct yetty_yclass_transport *transport;
    struct yetty_ywire_wire_statemachine *host_sm; /* verifies request envelopes */
    struct capture capture;
};

static int fixture_up(struct ytest *test, struct fixture *fixture, int compressed)
{
    memset(fixture, 0, sizeof(*fixture));
    YTEST_REQUIRE(test, pipe(fixture->request_pipe) == 0);
    YTEST_REQUIRE(test, pipe(fixture->reply_pipe) == 0);
    /* Non-blocking host-side read end so the drain loop can poll. */
    YTEST_REQUIRE(test,
                  fcntl(fixture->request_pipe[0], F_SETFL, O_NONBLOCK) == 0);

    struct yetty_yclass_transport_ptr_result transport_res = yetty_yclass_transport_dcs_create(
        fixture->reply_pipe[0], fixture->request_pipe[1], TEST_DCS_CODE, compressed);
    YTEST_REQUIRE_OK(test, transport_res);
    fixture->transport = transport_res.value;

    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm_res);
    fixture->host_sm = sm_res.value;
    struct yetty_ycore_void_result reg = yetty_ywire_wire_statemachine_register_buffered(
        fixture->host_sm, YETTY_YWIRE_ENVELOPE_DCS, TEST_DCS_CODE, /*has_args=*/0, on_envelope,
        &fixture->capture);
    YTEST_REQUIRE_OK(test, reg);
    return 1;
}

static void fixture_down(struct fixture *fixture)
{
    struct yetty_ycore_void_result res = fixture->transport->ops->destroy(fixture->transport);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_ywire_wire_statemachine_destroy(fixture->host_sm);
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    close(fixture->request_pipe[0]);
    close(fixture->request_pipe[1]);
    close(fixture->reply_pipe[0]);
    close(fixture->reply_pipe[1]);
}

/* Drain the request pipe into the host SM until `capture.calls` reaches
 * `expected_calls` (the flush write is synchronous, so the bytes are already
 * in the pipe — the loop guard only bounds a regression, not a race). */
static void drain_requests(struct ytest *test, struct fixture *fixture, int expected_calls)
{
    for (int spin = 0; spin < 1000 && fixture->capture.calls < expected_calls; spin++) {
        char raw[4096];
        ptrdiff_t n = read(fixture->request_pipe[0], raw, sizeof(raw));
        if (n <= 0) {
            break; /* EAGAIN — everything the transport wrote is consumed */
        }
        struct yetty_ycore_void_result fed =
            yetty_ywire_wire_statemachine_feed(fixture->host_sm, raw, (size_t)n);
        YTEST_REQUIRE_OK(test, fed);
        struct yetty_ycore_void_result processed =
            yetty_ywire_wire_statemachine_process(fixture->host_sm);
        YTEST_REQUIRE_OK(test, processed);
    }
    YTEST_CHECK_EQ_INT(test, fixture->capture.calls, expected_calls);
}

/*---------------------------------------------------------------------------
 * Tests.
 *-------------------------------------------------------------------------*/

/* N sequential send+flush cycles: every flush frames through the transport's
 * long-lived SM into the reusable emit buffer, so each envelope after the
 * first proves the encoder (and, when compressed, the kept LZ4F context)
 * came out of the previous flush clean. */
static void run_sequential_flush(struct ytest *test, int compressed)
{
    struct fixture fixture;
    if (!fixture_up(test, &fixture, compressed)) {
        return;
    }
    enum { ROUNDS = 16, BODY_MAX = 6000 };
    uint8_t body[BODY_MAX];
    for (unsigned round = 0; round < ROUNDS; round++) {
        size_t body_len = 1 + (round * 401) % BODY_MAX;
        for (size_t i = 0; i < body_len; i++) {
            body[i] = (uint8_t)(round * 37u + i * 11u);
        }
        struct yetty_ycore_size_result send_res =
            fixture.transport->ops->send(fixture.transport, body, body_len);
        YTEST_REQUIRE_OK(test, send_res);
        YTEST_CHECK_EQ_SIZE(test, send_res.value, body_len);

        fixture.capture.expect = body;
        fixture.capture.expect_len = body_len;
        struct yetty_ycore_void_result flush_res =
            fixture.transport->ops->flush(fixture.transport);
        YTEST_REQUIRE_OK(test, flush_res);

        drain_requests(test, &fixture, (int)round + 1);
        YTEST_CHECK_EQ_INT(test, fixture.capture.matches, (int)round + 1);
    }
    fixture_down(&fixture);
}

static void test_sequential_flush_uncompressed(struct ytest *test)
{
    run_sequential_flush(test, /*compressed=*/0);
}

static void test_sequential_flush_compressed(struct ytest *test)
{
    run_sequential_flush(test, /*compressed=*/1);
}

/* Flushing with nothing buffered must be a no-op — no envelope, no error. */
static void test_empty_flush_is_noop(struct ytest *test)
{
    struct fixture fixture;
    if (!fixture_up(test, &fixture, /*compressed=*/1)) {
        return;
    }
    struct yetty_ycore_void_result flush_res = fixture.transport->ops->flush(fixture.transport);
    YTEST_REQUIRE_OK(test, flush_res);
    char raw[16];
    YTEST_CHECK(test, read(fixture.request_pipe[0], raw, sizeof(raw)) < 0); /* EAGAIN */
    YTEST_CHECK_EQ_INT(test, fixture.capture.calls, 0);
    fixture_down(&fixture);
}

/* Duplex rounds on the SHARED SM: each round pre-writes a reply envelope
 * into the transport's read pipe, then send+recv. recv flushes the request
 * (encoder side) and scans the reply (scanner side) on the same SM — round
 * after round, neither direction may corrupt the other. */
static void test_duplex_shared_sm(struct ytest *test)
{
    struct fixture fixture;
    if (!fixture_up(test, &fixture, /*compressed=*/1)) {
        return;
    }
    enum { ROUNDS = 8, BODY_MAX = 4000 };
    uint8_t request[BODY_MAX];
    uint8_t reply[BODY_MAX];
    uint8_t received[BODY_MAX];

    for (unsigned round = 0; round < ROUNDS; round++) {
        size_t request_len = 100 + round * 331;
        size_t reply_len = 50 + round * 449;
        for (size_t i = 0; i < request_len; i++) {
            request[i] = (uint8_t)(round * 41u + i * 13u);
        }
        for (size_t i = 0; i < reply_len; i++) {
            reply[i] = (uint8_t)(round * 43u + i * 17u);
        }

        /* Host reply goes into the pipe first so recv() finds it. */
        struct yetty_ycore_buffer reply_envelope = {0};
        struct yetty_ycore_void_result emit_res =
            yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, TEST_DCS_CODE, /*has_args=*/0,
                             /*compressed=*/1, NULL, 0, reply, reply_len, &reply_envelope);
        YTEST_REQUIRE_OK(test, emit_res);
        size_t off = 0;
        while (off < reply_envelope.size) {
            ptrdiff_t written = write(fixture.reply_pipe[1], reply_envelope.data + off,
                                      reply_envelope.size - off);
            YTEST_REQUIRE(test, written > 0);
            off += (size_t)written;
        }
        yetty_ycore_buffer_destroy(&reply_envelope);

        struct yetty_ycore_size_result send_res =
            fixture.transport->ops->send(fixture.transport, request, request_len);
        YTEST_REQUIRE_OK(test, send_res);

        /* recv: flushes the buffered request, then decodes the reply. Loop —
         * the contract allows short reads. */
        size_t got = 0;
        while (got < reply_len) {
            struct yetty_ycore_size_result recv_res =
                fixture.transport->ops->recv(fixture.transport, received + got, reply_len - got);
            YTEST_REQUIRE_OK(test, recv_res);
            YTEST_REQUIRE(test, recv_res.value > 0);
            got += recv_res.value;
        }
        YTEST_CHECK_EQ_SIZE(test, got, reply_len);
        YTEST_CHECK_MEM_EQ(test, received, reply, reply_len);

        /* And the request envelope that recv() flushed must be intact. */
        fixture.capture.expect = request;
        fixture.capture.expect_len = request_len;
        drain_requests(test, &fixture, (int)round + 1);
        YTEST_CHECK_EQ_INT(test, fixture.capture.matches, (int)round + 1);
    }
    fixture_down(&fixture);
}

int main(void)
{
    struct ytest test = ytest_begin("yclass_transport_dcs");
    YTEST_RUN(&test, test_sequential_flush_uncompressed);
    YTEST_RUN(&test, test_sequential_flush_compressed);
    YTEST_RUN(&test, test_empty_flush_is_noop);
    YTEST_RUN(&test, test_duplex_shared_sm);
    return ytest_end(&test);
}
