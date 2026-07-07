/*
 * ywire streaming-encoder reuse contract test.
 *
 * Hot emitters hold ONE wire state machine and run start_write /
 * write / finish_write per envelope; the LZ4F compression context and
 * scratch persist across envelopes. This suite locks that contract:
 * back-to-back envelopes on a reused SM (uncompressed, compressed
 * across the LZ4F block boundary, and mixed) must all decode byte-exact
 * on a receiver; the encoder and scanner sides of ONE SM must not
 * interfere (the DCS transport shares its SM for both directions);
 * guard errors must not corrupt an in-flight envelope; and a reused
 * context's frames must decode identically to one-shot frames (byte
 * identity is NOT the contract for multi-block LZ4F frames — a reused
 * context may pick different, equally valid matches).
 */

#include <yetty/ycore/types.h>
#include <yetty/yface/yface.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Capture for the receiver SM's buffered handler. Each delivery is compared
 * against `expect`/`expect_len` immediately; `matches` counts the ones that
 * arrived byte-exact. */
struct capture {
    int calls;
    int matches;
    int last_code;
    const uint8_t *expect;
    size_t expect_len;
};

static struct yetty_ycore_void_result on_envelope(void *userdata,
                                                  enum yetty_ywire_envelope_kind kind, int code,
                                                  const uint8_t *args, size_t args_len,
                                                  const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)args;
    (void)args_len;
    struct capture *capture = userdata;
    capture->calls++;
    capture->last_code = code;
    if (payload_len == capture->expect_len &&
        (payload_len == 0 || memcmp(payload, capture->expect, payload_len) == 0)) {
        capture->matches++;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ywire_wire_statemachine *make_receiver_args(struct ytest *test,
                                                                struct capture *capture,
                                                                int has_args)
{
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm_res);
    struct yetty_ycore_void_result reg = yetty_ywire_wire_statemachine_set_envelope_default_buffered(
        sm_res.value, has_args, on_envelope, capture);
    YTEST_REQUIRE_OK(test, reg);
    return sm_res.value;
}

static struct yetty_ywire_wire_statemachine *make_receiver(struct ytest *test,
                                                           struct capture *capture)
{
    return make_receiver_args(test, capture, /*has_args=*/0);
}

static void feed_all(struct ytest *test, struct yetty_ywire_wire_statemachine *receiver,
                     const struct yetty_ycore_buffer *envelope)
{
    struct yetty_ycore_void_result fed = yetty_ywire_wire_statemachine_feed(
        receiver, (const char *)envelope->data, envelope->size);
    YTEST_REQUIRE_OK(test, fed);
    struct yetty_ycore_void_result processed = yetty_ywire_wire_statemachine_process(receiver);
    YTEST_REQUIRE_OK(test, processed);
}

/* Deterministic per-round body: alternating compressible runs and pseudo-
 * random bytes, seeded by the round so cross-frame contamination on a reused
 * LZ4F context would show up as a decode mismatch. */
static void fill_body(uint8_t *body, size_t len, unsigned round)
{
    unsigned state = 0x9e3779b9u + round * 0x85ebca6bu;
    for (size_t i = 0; i < len; i++) {
        if ((i / 512) % 2 == 0) {
            body[i] = (uint8_t)(round + 'A');
        } else {
            state = state * 1664525u + 1013904223u;
            body[i] = (uint8_t)(state >> 24);
        }
    }
}

/* Emit one envelope through the reused streaming encoder into `out`. */
static void emit_streamed(struct ytest *test, struct yetty_ywire_wire_statemachine *encoder,
                          int code, int compressed, const uint8_t *body, size_t body_len,
                          struct yetty_ycore_buffer *out)
{
    struct yetty_ycore_void_result res = yetty_ywire_wire_statemachine_start_write(
        encoder, YETTY_YWIRE_ENVELOPE_DCS, code, /*has_args=*/0, compressed, NULL, 0, out);
    YTEST_REQUIRE_OK(test, res);
    res = yetty_ywire_wire_statemachine_write(encoder, body, body_len);
    YTEST_REQUIRE_OK(test, res);
    res = yetty_ywire_wire_statemachine_finish_write(encoder);
    YTEST_REQUIRE_OK(test, res);
}

/*---------------------------------------------------------------------------
 * Tests.
 *-------------------------------------------------------------------------*/

/* Many uncompressed envelopes through ONE encoder SM. Every envelope must
 * decode byte-exact AND be byte-identical to the one-shot helper's output
 * (base64 framing is deterministic — only LZ4F may legitimately diverge). */
static void test_reuse_uncompressed(struct ytest *test)
{
    struct yetty_ywire_wire_statemachine_ptr_result encoder_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, encoder_res);
    struct yetty_ywire_wire_statemachine *encoder = encoder_res.value;

    uint8_t body[3000];
    for (unsigned round = 0; round < 8; round++) {
        size_t body_len = 1 + round * 397; /* odd sizes exercise b64 carry/padding */
        fill_body(body, body_len, round);

        struct yetty_ycore_buffer streamed = {0};
        emit_streamed(test, encoder, 600001, /*compressed=*/0, body, body_len, &streamed);

        struct yetty_ycore_buffer oneshot = {0};
        struct yetty_ycore_void_result emit_res =
            yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, 600001, /*has_args=*/0, /*compressed=*/0,
                             NULL, 0, body, body_len, &oneshot);
        YTEST_REQUIRE_OK(test, emit_res);
        YTEST_CHECK_EQ_SIZE(test, streamed.size, oneshot.size);
        YTEST_CHECK_MEM_EQ(test, streamed.data, oneshot.data, streamed.size);

        struct capture capture = {.expect = body, .expect_len = body_len};
        struct yetty_ywire_wire_statemachine *receiver = make_receiver(test, &capture);
        feed_all(test, receiver, &streamed);
        YTEST_CHECK_EQ_INT(test, capture.calls, 1);
        YTEST_CHECK_EQ_INT(test, capture.matches, 1);

        yetty_ywire_wire_statemachine_destroy(receiver);
        yetty_ycore_buffer_destroy(&streamed);
        yetty_ycore_buffer_destroy(&oneshot);
    }
    yetty_ywire_wire_statemachine_destroy(encoder);
}

/* Compressed envelopes through ONE encoder SM, with bodies growing across
 * the 64 KB LZ4F block boundary. The reused compression context must start
 * every frame clean — a stale dictionary or unreset block state shows up as
 * a decode mismatch on the receiver. The receiver SM is ALSO reused across
 * all frames, covering the decode-side context reuse in the same pass. */
static void test_reuse_compressed_across_block_boundary(struct ytest *test)
{
    struct yetty_ywire_wire_statemachine_ptr_result encoder_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, encoder_res);
    struct yetty_ywire_wire_statemachine *encoder = encoder_res.value;

    enum { ROUNDS = 6, BODY_MAX = 200 * 1024 };
    uint8_t *body = malloc(BODY_MAX);
    YTEST_REQUIRE(test, body != NULL);

    struct capture capture = {0};
    struct yetty_ywire_wire_statemachine *receiver = make_receiver(test, &capture);

    for (unsigned round = 0; round < ROUNDS; round++) {
        size_t body_len = 1000 + round * 39000; /* 1 KB … ~196 KB: 1..4 LZ4F blocks */
        fill_body(body, body_len, round);

        struct yetty_ycore_buffer streamed = {0};
        emit_streamed(test, encoder, 600001, /*compressed=*/1, body, body_len, &streamed);

        capture.expect = body;
        capture.expect_len = body_len;
        feed_all(test, receiver, &streamed);
        YTEST_CHECK_EQ_INT(test, capture.calls, (int)round + 1);
        YTEST_CHECK_EQ_INT(test, capture.matches, (int)round + 1);

        yetty_ycore_buffer_destroy(&streamed);
    }

    yetty_ywire_wire_statemachine_destroy(receiver);
    yetty_ywire_wire_statemachine_destroy(encoder);
    free(body);
}

/* Alternating compressed / uncompressed envelopes on one encoder SM: the
 * kept-alive LZ4F context must neither leak into uncompressed frames nor go
 * stale while skipped. */
static void test_reuse_mixed_compression(struct ytest *test)
{
    struct yetty_ywire_wire_statemachine_ptr_result encoder_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, encoder_res);
    struct yetty_ywire_wire_statemachine *encoder = encoder_res.value;

    uint8_t body[20000];
    struct capture capture = {0};
    struct yetty_ywire_wire_statemachine *receiver = make_receiver(test, &capture);

    for (unsigned round = 0; round < 10; round++) {
        size_t body_len = 500 + round * 1777;
        fill_body(body, body_len, round);
        int compressed = (int)(round % 2);

        struct yetty_ycore_buffer streamed = {0};
        emit_streamed(test, encoder, 600001, compressed, body, body_len, &streamed);

        capture.expect = body;
        capture.expect_len = body_len;
        feed_all(test, receiver, &streamed);
        YTEST_CHECK_EQ_INT(test, capture.matches, (int)round + 1);

        yetty_ycore_buffer_destroy(&streamed);
    }

    yetty_ywire_wire_statemachine_destroy(receiver);
    yetty_ywire_wire_statemachine_destroy(encoder);
}

/* ONE SM drives both directions at once — the DCS transport's shape. An
 * inbound envelope is fed in two halves with a full outbound emit wedged
 * between them: the encoder must not disturb the half-parsed scanner state,
 * and the scanner must not disturb the encoder's carry/LZ4F state. */
static void test_encode_decode_interleaved_on_one_sm(struct ytest *test)
{
    struct capture capture = {0};
    struct yetty_ywire_wire_statemachine *shared = make_receiver(test, &capture);

    uint8_t inbound_body[5000];
    uint8_t outbound_body[70000];

    for (unsigned round = 0; round < 4; round++) {
        fill_body(inbound_body, sizeof(inbound_body), round);
        fill_body(outbound_body, sizeof(outbound_body), round + 100);

        /* Inbound envelope, built by an independent one-shot emit. */
        struct yetty_ycore_buffer inbound = {0};
        struct yetty_ycore_void_result emit_res = yetty_ywire_emit(
            YETTY_YWIRE_ENVELOPE_DCS, 700 + (int)round, /*has_args=*/0, /*compressed=*/1, NULL, 0,
            inbound_body, sizeof(inbound_body), &inbound);
        YTEST_REQUIRE_OK(test, emit_res);

        /* First half in… */
        size_t half = inbound.size / 2;
        struct yetty_ycore_void_result fed =
            yetty_ywire_wire_statemachine_feed(shared, (const char *)inbound.data, half);
        YTEST_REQUIRE_OK(test, fed);
        struct yetty_ycore_void_result processed = yetty_ywire_wire_statemachine_process(shared);
        YTEST_REQUIRE_OK(test, processed);

        /* …a full compressed outbound emit on the SAME SM (multi-block, so
         * the encoder-side LZ4F context is genuinely engaged)… */
        capture.expect = inbound_body;
        capture.expect_len = sizeof(inbound_body);
        struct yetty_ycore_buffer outbound = {0};
        emit_streamed(test, shared, 600001, /*compressed=*/1, outbound_body,
                      sizeof(outbound_body), &outbound);

        /* …then the second inbound half completes and must decode intact. */
        fed = yetty_ywire_wire_statemachine_feed(shared, (const char *)inbound.data + half,
                                                 inbound.size - half);
        YTEST_REQUIRE_OK(test, fed);
        processed = yetty_ywire_wire_statemachine_process(shared);
        YTEST_REQUIRE_OK(test, processed);
        YTEST_CHECK_EQ_INT(test, capture.calls, (int)round + 1);
        YTEST_CHECK_EQ_INT(test, capture.matches, (int)round + 1);
        YTEST_CHECK_EQ_INT(test, capture.last_code, 700 + (int)round);

        /* The outbound envelope produced mid-parse must itself be intact —
         * verify on a fresh receiver. */
        struct capture outbound_capture = {.expect = outbound_body,
                                           .expect_len = sizeof(outbound_body)};
        struct yetty_ywire_wire_statemachine *verifier = make_receiver(test, &outbound_capture);
        feed_all(test, verifier, &outbound);
        YTEST_CHECK_EQ_INT(test, outbound_capture.matches, 1);
        yetty_ywire_wire_statemachine_destroy(verifier);

        yetty_ycore_buffer_destroy(&inbound);
        yetty_ycore_buffer_destroy(&outbound);
    }

    yetty_ywire_wire_statemachine_destroy(shared);
}

/* Guard errors (double start, write outside a frame, finish without start)
 * must fail loudly WITHOUT corrupting an in-flight envelope or wedging the
 * SM for later envelopes. */
static void test_guard_errors_leave_sm_usable(struct ytest *test)
{
    struct yetty_ywire_wire_statemachine_ptr_result encoder_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, encoder_res);
    struct yetty_ywire_wire_statemachine *encoder = encoder_res.value;

    /* Outside a frame: write and finish are rejected. */
    struct yetty_ycore_void_result res = yetty_ywire_wire_statemachine_write(encoder, "x", 1);
    YTEST_CHECK(test, YETTY_IS_ERR(res));
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    res = yetty_ywire_wire_statemachine_finish_write(encoder);
    YTEST_CHECK(test, YETTY_IS_ERR(res));
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }

    /* Open an envelope, then hit it with a second start_write: rejected, and
     * the FIRST envelope keeps going and decodes cleanly. */
    const char *payload = "guarded but intact";
    struct yetty_ycore_buffer streamed = {0};
    res = yetty_ywire_wire_statemachine_start_write(encoder, YETTY_YWIRE_ENVELOPE_DCS, 600001,
                                                    /*has_args=*/0, /*compressed=*/0, NULL, 0,
                                                    &streamed);
    YTEST_REQUIRE_OK(test, res);

    struct yetty_ycore_buffer other = {0};
    res = yetty_ywire_wire_statemachine_start_write(encoder, YETTY_YWIRE_ENVELOPE_DCS, 600002,
                                                    /*has_args=*/0, /*compressed=*/0, NULL, 0,
                                                    &other);
    YTEST_CHECK(test, YETTY_IS_ERR(res));
    if (YETTY_IS_ERR(res)) {
        yetty_ycore_error_destroy(res.error);
    }
    yetty_ycore_buffer_destroy(&other);

    res = yetty_ywire_wire_statemachine_write(encoder, payload, strlen(payload));
    YTEST_REQUIRE_OK(test, res);
    res = yetty_ywire_wire_statemachine_finish_write(encoder);
    YTEST_REQUIRE_OK(test, res);

    struct capture capture = {.expect = (const uint8_t *)payload, .expect_len = strlen(payload)};
    struct yetty_ywire_wire_statemachine *receiver = make_receiver(test, &capture);
    feed_all(test, receiver, &streamed);
    YTEST_CHECK_EQ_INT(test, capture.calls, 1);
    YTEST_CHECK_EQ_INT(test, capture.matches, 1);
    YTEST_CHECK_EQ_INT(test, capture.last_code, 600001);
    yetty_ywire_wire_statemachine_destroy(receiver);
    yetty_ycore_buffer_destroy(&streamed);

    /* And the SM is still good for a fresh envelope afterwards. */
    struct yetty_ycore_buffer second = {0};
    emit_streamed(test, encoder, 600003, /*compressed=*/1, (const uint8_t *)payload,
                  strlen(payload), &second);
    struct capture second_capture = {.expect = (const uint8_t *)payload,
                                     .expect_len = strlen(payload)};
    struct yetty_ywire_wire_statemachine *second_receiver = make_receiver(test, &second_capture);
    feed_all(test, second_receiver, &second);
    YTEST_CHECK_EQ_INT(test, second_capture.matches, 1);
    yetty_ywire_wire_statemachine_destroy(second_receiver);
    yetty_ycore_buffer_destroy(&second);

    yetty_ywire_wire_statemachine_destroy(encoder);
}

/* Destroying an SM with an unfinished compressed envelope must not leak the
 * LZ4F context or scratch (the LSAN-enabled gate catches regressions). */
static void test_destroy_with_active_write(struct ytest *test)
{
    struct yetty_ywire_wire_statemachine_ptr_result encoder_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, encoder_res);
    struct yetty_ywire_wire_statemachine *encoder = encoder_res.value;

    uint8_t body[4096];
    fill_body(body, sizeof(body), 7);
    struct yetty_ycore_buffer streamed = {0};
    struct yetty_ycore_void_result res = yetty_ywire_wire_statemachine_start_write(
        encoder, YETTY_YWIRE_ENVELOPE_DCS, 600001, /*has_args=*/0, /*compressed=*/1, NULL, 0,
        &streamed);
    YTEST_REQUIRE_OK(test, res);
    res = yetty_ywire_wire_statemachine_write(encoder, body, sizeof(body));
    YTEST_REQUIRE_OK(test, res);
    /* No finish_write — teardown mid-envelope. */
    struct yetty_ycore_void_result destroy_res = yetty_ywire_wire_statemachine_destroy(encoder);
    YTEST_CHECK(test, YETTY_IS_OK(destroy_res));
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
    yetty_ycore_buffer_destroy(&streamed);
}

/* The yface wrapper is the long-lived-emitter API tools are pointed at
 * (ydraw-bench, the ymgui frontend). Lock the documented hot-path pattern:
 * one yface, many start_write/write/finish_write cycles, out_buf handed off
 * and cleared between envelopes. yface always emits an args slot, so the
 * receiver registers has_args=1. */
static void test_yface_streaming_reuse(struct ytest *test)
{
    struct yetty_yface_ptr_result yface_res = yetty_yface_create();
    YTEST_REQUIRE_OK(test, yface_res);
    struct yetty_yface *yface = yface_res.value;
    struct yetty_ycore_buffer *out_buf = yetty_yface_out_buf(yface);

    struct capture capture = {0};
    struct yetty_ywire_wire_statemachine *receiver =
        make_receiver_args(test, &capture, /*has_args=*/1);

    uint8_t body[30000];
    for (unsigned round = 0; round < 6; round++) {
        size_t body_len = 200 + round * 5333;
        fill_body(body, body_len, round);
        int compressed = (int)(round % 2);

        struct yetty_ycore_void_result res = yetty_yface_start_write(
            yface, YETTY_YWIRE_ENVELOPE_DCS, 600001, compressed, "meta", 4);
        YTEST_REQUIRE_OK(test, res);
        res = yetty_yface_write(yface, body, body_len);
        YTEST_REQUIRE_OK(test, res);
        res = yetty_yface_finish_write(yface);
        YTEST_REQUIRE_OK(test, res);

        capture.expect = body;
        capture.expect_len = body_len;
        feed_all(test, receiver, out_buf);
        YTEST_CHECK_EQ_INT(test, capture.calls, (int)round + 1);
        YTEST_CHECK_EQ_INT(test, capture.matches, (int)round + 1);

        yetty_ycore_buffer_clear(out_buf);
    }

    yetty_ywire_wire_statemachine_destroy(receiver);
    struct yetty_ycore_void_result destroy_res = yetty_yface_destroy(yface);
    YTEST_CHECK(test, YETTY_IS_OK(destroy_res));
    if (YETTY_IS_ERR(destroy_res)) {
        yetty_ycore_error_destroy(destroy_res.error);
    }
}

int main(void)
{
    struct ytest test = ytest_begin("ywire_emit_reuse");
    YTEST_RUN(&test, test_reuse_uncompressed);
    YTEST_RUN(&test, test_reuse_compressed_across_block_boundary);
    YTEST_RUN(&test, test_reuse_mixed_compression);
    YTEST_RUN(&test, test_encode_decode_interleaved_on_one_sm);
    YTEST_RUN(&test, test_guard_errors_leave_sm_usable);
    YTEST_RUN(&test, test_destroy_with_active_write);
    YTEST_RUN(&test, test_yface_streaming_reuse);
    return ytest_end(&test);
}
