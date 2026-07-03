/*
 * ywire OSC/DCS protocol contract test.
 *
 * Builds envelopes with yetty_ywire_emit(), feeds the resulting bytes through a
 * fresh wire state machine (no PTY), and asserts on the decoded envelopes a
 * catch-all buffered handler receives. Covers the framing contract every rich
 * figure depends on: uncompressed + args + DCS round-trips, binary base64,
 * LZ4F payloads, partial frames, multiple frames per buffer, malformed-input
 * recovery, truncated frames, and large chunked payloads.
 */

#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ycore/types.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One capture shared by all handlers. Single-frame tests read the last-call
 * snapshot + payload_ok; the multi-frame test reads the per-call code log. */
struct cap {
    int calls;
    enum yetty_ywire_envelope_kind kind;
    int code;
    size_t payload_len;
    int payload_ok;
    uint8_t args[64];
    size_t args_len;
    const uint8_t *expect; /* expected payload for payload_ok */
    size_t expect_len;
    int codes[8];
    size_t plens[8];
};

static struct yetty_ycore_void_result on_env(void *userdata, enum yetty_ywire_envelope_kind kind,
                                             int code, const uint8_t *args, size_t args_len,
                                             const uint8_t *payload, size_t payload_len)
{
    struct cap *cap = userdata;
    if (cap->calls < 8) {
        cap->codes[cap->calls] = code;
        cap->plens[cap->calls] = payload_len;
    }
    cap->calls++;
    cap->kind = kind;
    cap->code = code;
    cap->payload_len = payload_len;
    cap->args_len = args_len < sizeof(cap->args) ? args_len : sizeof(cap->args);
    if (args && cap->args_len) {
        memcpy(cap->args, args, cap->args_len);
    }
    cap->payload_ok =
        (payload_len == cap->expect_len) &&
        (payload_len == 0 || (cap->expect && memcmp(payload, cap->expect, payload_len) == 0));
    return YETTY_OK_VOID();
}

/* Fresh SM with a catch-all buffered handler bound to `cap`. */
static struct yetty_ywire_wire_statemachine *make_sm(struct ytest *test, struct cap *cap,
                                                     int has_args)
{
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm_res);
    struct yetty_ywire_wire_statemachine *sm = sm_res.value;
    struct yetty_ycore_void_result reg =
        yetty_ywire_wire_statemachine_set_envelope_default_buffered(sm, has_args, on_env, cap);
    YTEST_REQUIRE_OK(test, reg);
    return sm;
}

static void feed_chunk(struct ytest *test, struct yetty_ywire_wire_statemachine *sm,
                       const uint8_t *bytes, size_t len)
{
    struct yetty_ycore_void_result fed =
        yetty_ywire_wire_statemachine_feed(sm, (const char *)bytes, len);
    YTEST_REQUIRE_OK(test, fed);
    struct yetty_ycore_void_result proc = yetty_ywire_wire_statemachine_process(sm);
    YTEST_REQUIRE_OK(test, proc);
}

/*---------------------------------------------------------------------------
 * Tests.
 *-------------------------------------------------------------------------*/
static void test_roundtrip_uncompressed(struct ytest *test)
{
    const char *payload = "hello world";
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 12345, /*has_args=*/0, /*compressed=*/0, NULL, 0,
                         payload, strlen(payload), &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = (const uint8_t *)payload, .expect_len = strlen(payload)};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    feed_chunk(test, sm, env.data, env.size);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_INT(test, cap.kind, YETTY_YWIRE_ENVELOPE_OSC);
    YTEST_CHECK_EQ_INT(test, cap.code, 12345);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_roundtrip_with_args(struct ytest *test)
{
    const uint8_t args[] = {0xDE, 0xAD, 0xBE, 0xEF};
    const char *payload = "the body";
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 7, /*has_args=*/1, /*compressed=*/0, args,
                         sizeof(args), payload, strlen(payload), &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = (const uint8_t *)payload, .expect_len = strlen(payload)};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/1);
    feed_chunk(test, sm, env.data, env.size);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_SIZE(test, cap.args_len, sizeof(args));
    YTEST_CHECK_MEM_EQ(test, cap.args, args, sizeof(args));
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_dcs_envelope(struct ytest *test)
{
    const char *payload = "dcs payload";
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, 54321, /*has_args=*/0, /*compressed=*/0, NULL, 0,
                         payload, strlen(payload), &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = (const uint8_t *)payload, .expect_len = strlen(payload)};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    feed_chunk(test, sm, env.data, env.size);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_INT(test, cap.kind, YETTY_YWIRE_ENVELOPE_DCS);
    YTEST_CHECK_EQ_INT(test, cap.code, 54321);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_binary_base64_payload(struct ytest *test)
{
    /* All 256 byte values exercise the base64 codec's full alphabet + padding
     * and prove the payload survives the wire byte-exact. */
    uint8_t payload[256];
    for (int i = 0; i < 256; i++) {
        payload[i] = (uint8_t)i;
    }
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 99, /*has_args=*/0, /*compressed=*/0, NULL, 0,
                         payload, sizeof(payload), &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = payload, .expect_len = sizeof(payload)};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    feed_chunk(test, sm, env.data, env.size);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_SIZE(test, cap.payload_len, sizeof(payload));
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_lz4_payload(struct ytest *test)
{
    /* A highly compressible payload flows through the LZ4F compress→base64
     * (emit) and base64→LZ4F-decompress (parse) path and comes back exact. */
    size_t raw_len = 8192;
    uint8_t *raw = malloc(raw_len);
    YTEST_REQUIRE_NOT_NULL(test, raw);
    for (size_t i = 0; i < raw_len; i++) {
        raw[i] = (uint8_t)(i % 7);
    }
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, 200, /*has_args=*/0, /*compressed=*/1, NULL, 0,
                         raw, raw_len, &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = raw, .expect_len = raw_len};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    feed_chunk(test, sm, env.data, env.size);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_SIZE(test, cap.payload_len, raw_len);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
    free(raw);
}

static void test_partial_frames(struct ytest *test)
{
    /* One envelope fed one byte at a time: the scanner holds position across
     * feeds and delivers exactly once when the terminator finally arrives. */
    const char *payload = "reassembled";
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 321, /*has_args=*/0, /*compressed=*/0, NULL, 0,
                         payload, strlen(payload), &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = (const uint8_t *)payload, .expect_len = strlen(payload)};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    for (size_t i = 0; i < env.size; i++) {
        feed_chunk(test, sm, env.data + i, 1);
    }
    /* Reassembled from single-byte feeds: delivered exactly once, intact. */
    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_multiple_frames_one_buffer(struct ytest *test)
{
    /* Three envelopes concatenated into one buffer are delivered in order. */
    struct yetty_ycore_buffer env = {0};
    const char *bodies[3] = {"one", "two", "three"};
    int codes[3] = {11, 22, 33};
    for (int i = 0; i < 3; i++) {
        struct yetty_ycore_void_result emit =
            yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, codes[i], /*has_args=*/0, /*compressed=*/0,
                             NULL, 0, bodies[i], strlen(bodies[i]), &env);
        YTEST_REQUIRE_OK(test, emit);
    }

    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    feed_chunk(test, sm, env.data, env.size);

    YTEST_REQUIRE_EQ_INT(test, cap.calls, 3);
    for (int i = 0; i < 3; i++) {
        YTEST_CHECK_EQ_INT(test, cap.codes[i], codes[i]);
        YTEST_CHECK_EQ_SIZE(test, cap.plens[i], strlen(bodies[i]));
    }

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_malformed_then_recover(struct ytest *test)
{
    /* Garbage — including a broken envelope introducer — must not crash and
     * must not stop a following valid envelope from being delivered (the
     * scanner resyncs on the next ESC). */
    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);

    const char junk[] = "plain text, no escapes\033]notanumber;@@@not-base64@@@ more junk";
    feed_chunk(test, sm, (const uint8_t *)junk, sizeof(junk) - 1);

    const char *payload = "recovered";
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 42, /*has_args=*/0, /*compressed=*/0, NULL, 0,
                         payload, strlen(payload), &env);
    YTEST_REQUIRE_OK(test, emit);
    cap.expect = (const uint8_t *)payload;
    cap.expect_len = strlen(payload);
    feed_chunk(test, sm, env.data, env.size);

    /* The valid envelope after the garbage is delivered intact. */
    YTEST_CHECK_EQ_INT(test, cap.code, 42);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_truncated_no_delivery(struct ytest *test)
{
    /* An envelope cut off mid-payload (base64 not yet complete) is never
     * delivered — and tearing the SM down mid-frame does not crash or leak. */
    const char *payload = "this payload never completes on the wire";
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 5, /*has_args=*/0, /*compressed=*/0, NULL, 0,
                         payload, strlen(payload), &env);
    YTEST_REQUIRE_OK(test, emit);
    YTEST_REQUIRE(test, env.size > 8);

    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    /* Stop halfway through — well inside the base64 body, before any
     * terminator. Nothing is delivered. */
    feed_chunk(test, sm, env.data, env.size / 2);
    YTEST_CHECK_EQ_INT(test, cap.calls, 0);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
}

static void test_large_payload_chunked(struct ytest *test)
{
    /* A 256 KiB payload, compressed, fed in 4 KiB chunks: reassembled and
     * decompressed byte-exact with bounded per-feed memory. */
    size_t raw_len = 256u * 1024u;
    uint8_t *raw = malloc(raw_len);
    YTEST_REQUIRE_NOT_NULL(test, raw);
    for (size_t i = 0; i < raw_len; i++) {
        raw[i] = (uint8_t)((i * 131u) ^ (i >> 3));
    }
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result emit =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_DCS, 900, /*has_args=*/0, /*compressed=*/1, NULL, 0,
                         raw, raw_len, &env);
    YTEST_REQUIRE_OK(test, emit);

    struct cap cap = {.expect = raw, .expect_len = raw_len};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/0);
    const size_t chunk = 4096;
    for (size_t off = 0; off < env.size; off += chunk) {
        size_t n = env.size - off < chunk ? env.size - off : chunk;
        feed_chunk(test, sm, env.data + off, n);
    }

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_SIZE(test, cap.payload_len, raw_len);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ywire_wire_statemachine_destroy(sm);
    yetty_ycore_buffer_destroy(&env);
    free(raw);
}

int main(void)
{
    struct ytest test = ytest_begin("ywire_protocol");
    YTEST_RUN(&test, test_roundtrip_uncompressed);
    YTEST_RUN(&test, test_roundtrip_with_args);
    YTEST_RUN(&test, test_dcs_envelope);
    YTEST_RUN(&test, test_binary_base64_payload);
    YTEST_RUN(&test, test_lz4_payload);
    YTEST_RUN(&test, test_partial_frames);
    YTEST_RUN(&test, test_multiple_frames_one_buffer);
    YTEST_RUN(&test, test_malformed_then_recover);
    YTEST_RUN(&test, test_truncated_no_delivery);
    YTEST_RUN(&test, test_large_payload_chunked);
    return ytest_end(&test);
}
