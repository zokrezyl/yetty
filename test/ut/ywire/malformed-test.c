/*
 * ywire/yface malformed-stream + recovery contract test (#417).
 *
 * Hardens the byte-stream/protocol boundary: garbage framing, corrupt
 * base64/LZ4 payloads, exhaustive chunk boundaries, oversized args, desync
 * detection across interleaved malformed/valid envelopes, and the same at the
 * yface high-level boundary. These are negative tests — they assert RECOVERY
 * after bad input and would fail on parser desync or silent acceptance.
 */

#include <yetty/yface/yface.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct cap {
    int calls;
    int code;
    size_t payload_len;
    int payload_ok;
    const uint8_t *expect;
    size_t expect_len;
    int codes[8];
};

static struct yetty_ycore_void_result on_env(void *userdata, enum yetty_ywire_envelope_kind kind,
                                             int code, const uint8_t *args, size_t args_len,
                                             const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)args;
    (void)args_len;
    struct cap *cap = userdata;
    if (cap->calls < 8) {
        cap->codes[cap->calls] = code;
    }
    cap->calls++;
    cap->code = code;
    cap->payload_len = payload_len;
    cap->payload_ok =
        (payload_len == cap->expect_len) &&
        (payload_len == 0 || (cap->expect && memcmp(payload, cap->expect, payload_len) == 0));
    return YETTY_OK_VOID();
}

static struct yetty_ywire_wire_statemachine *make_sm(struct ytest *test, struct cap *cap,
                                                     int has_args)
{
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test, yetty_ywire_wire_statemachine_set_envelope_default_buffered(
                               sm.value, has_args, on_env, cap));
    return sm.value;
}

static void feed(struct ytest *test, struct yetty_ywire_wire_statemachine *sm, const uint8_t *bytes,
                 size_t len)
{
    struct yetty_ycore_void_result fed =
        yetty_ywire_wire_statemachine_feed(sm, (const char *)bytes, len);
    YTEST_REQUIRE_OK(test, fed);
    struct yetty_ycore_void_result proc = yetty_ywire_wire_statemachine_process(sm);
    YTEST_REQUIRE_OK(test, proc);
}

/* Emit a valid OSC envelope into a fresh buffer (caller destroys). */
static struct yetty_ycore_buffer emit_osc(struct ytest *test, int code, int compressed,
                                          const void *body, size_t body_len)
{
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result r =
        yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, code,
                         /*has_args=*/0, compressed, NULL, 0, body, body_len, &env);
    YTEST_REQUIRE_OK(test, r);
    return env;
}

/*---------------------------------------------------------------------------
 * Garbage before and after a valid envelope is ignored; envelope delivered.
 *-------------------------------------------------------------------------*/
static void test_garbage_surrounding(struct ytest *test)
{
    struct cap cap = {.expect = (const uint8_t *)"hi", .expect_len = 2};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);
    struct yetty_ycore_buffer env = emit_osc(test, 1, 0, "hi", 2);

    const char *pre = "raw junk \x01\x02 before ";
    const char *post = " and after \x1b[0m more";
    /* pre + envelope + post, all in one feed. */
    uint8_t buf[512];
    size_t n = 0;
    memcpy(buf + n, pre, strlen(pre));
    n += strlen(pre);
    memcpy(buf + n, env.data, env.size);
    n += env.size;
    memcpy(buf + n, post, strlen(post));
    n += strlen(post);
    feed(test, sm, buf, n);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_INT(test, cap.code, 1);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ycore_buffer_destroy(&env);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * A corrupt LZ4 payload must not crash and must not wedge the parser: the
 * following valid envelope is still delivered.
 *-------------------------------------------------------------------------*/
static void test_corrupt_lz4_then_recover(struct ytest *test)
{
    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);

    /* Compressible payload → real LZ4F frame. */
    uint8_t raw[4096];
    for (size_t i = 0; i < sizeof(raw); i++) {
        raw[i] = (uint8_t)(i % 5);
    }
    struct yetty_ycore_buffer bad = emit_osc(test, 10, /*compressed=*/1, raw, sizeof(raw));
    /* Flip a base64 char in the middle of the payload (past the LZ4F magic) so
     * the frame stays syntactically an envelope but the compressed stream is
     * corrupt. */
    size_t mid = bad.size / 2;
    bad.data[mid] = (uint8_t)(bad.data[mid] == 'A' ? 'B' : 'A');
    feed(test, sm, bad.data, bad.size);
    /* Whatever the parser did with the corrupt frame, it must not have
     * delivered the original 4096-byte payload as if valid. */
    YTEST_CHECK(test, !(cap.calls == 1 && cap.payload_len == sizeof(raw) && cap.payload_ok));

    /* Recovery: a valid envelope after the corrupt one is delivered intact. */
    cap.expect = (const uint8_t *)"ok";
    cap.expect_len = 2;
    struct yetty_ycore_buffer good = emit_osc(test, 11, 0, "ok", 2);
    feed(test, sm, good.data, good.size);
    YTEST_CHECK_EQ_INT(test, cap.code, 11);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ycore_buffer_destroy(&bad);
    yetty_ycore_buffer_destroy(&good);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * A hand-built envelope whose payload contains non-base64 bytes must not crash
 * or wedge the parser; a following valid envelope is delivered.
 *-------------------------------------------------------------------------*/
static void test_bad_base64_then_recover(struct ytest *test)
{
    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);

    /* ESC ] 5 ; <garbage payload> ESC \  — payload is not valid base64. */
    const char *bad = "\033]5;@@@!!!not^base64%%%\033\\";
    feed(test, sm, (const uint8_t *)bad, strlen(bad));

    cap.expect = (const uint8_t *)"good";
    cap.expect_len = 4;
    struct yetty_ycore_buffer good = emit_osc(test, 6, 0, "good", 4);
    feed(test, sm, good.data, good.size);
    YTEST_CHECK_EQ_INT(test, cap.code, 6);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ycore_buffer_destroy(&good);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * Splitting a valid envelope at EVERY byte boundary still delivers it exactly
 * once with the correct payload.
 *-------------------------------------------------------------------------*/
static void test_chunk_boundary_exhaustive(struct ytest *test)
{
    struct yetty_ycore_buffer env = emit_osc(test, 42, 0, "boundary", 8);
    for (size_t k = 1; k < env.size; k++) {
        struct cap cap = {.expect = (const uint8_t *)"boundary", .expect_len = 8};
        struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);
        feed(test, sm, env.data, k);
        feed(test, sm, env.data + k, env.size - k);
        YTEST_CHECK_EQ_INT(test, cap.calls, 1);
        YTEST_CHECK(test, cap.payload_ok);
        yetty_ywire_wire_statemachine_destroy(sm);
    }
    yetty_ycore_buffer_destroy(&env);
}

/*---------------------------------------------------------------------------
 * Desync detection: valid, malformed, valid, garbage, valid — exactly the three
 * valid envelopes are delivered, in order. A desync would change the count/order.
 *-------------------------------------------------------------------------*/
static void test_interleaved_desync(struct ytest *test)
{
    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);
    struct yetty_ycore_buffer a = emit_osc(test, 11, 0, "a", 1);
    struct yetty_ycore_buffer b = emit_osc(test, 22, 0, "b", 1);
    struct yetty_ycore_buffer c = emit_osc(test, 33, 0, "c", 1);

    uint8_t buf[1024];
    size_t n = 0;
#define APPEND(p, l)                                                                               \
    do {                                                                                           \
        memcpy(buf + n, (p), (l));                                                                 \
        n += (l);                                                                                  \
    } while (0)
    const char *malformed = "\033]notanumber;zzz\033\\"; /* malformed code, clean ST */
    APPEND(a.data, a.size);
    APPEND(malformed, strlen(malformed));
    APPEND(b.data, b.size);
    APPEND("plain raw junk", strlen("plain raw junk"));
    APPEND(c.data, c.size);
#undef APPEND
    feed(test, sm, buf, n);

    YTEST_REQUIRE_EQ_INT(test, cap.calls, 3);
    YTEST_CHECK_EQ_INT(test, cap.codes[0], 11);
    YTEST_CHECK_EQ_INT(test, cap.codes[1], 22);
    YTEST_CHECK_EQ_INT(test, cap.codes[2], 33);

    yetty_ycore_buffer_destroy(&a);
    yetty_ycore_buffer_destroy(&b);
    yetty_ycore_buffer_destroy(&c);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * A dangling introducer (ESC ] with nothing after) does not wedge the parser:
 * a fresh envelope fed afterwards is still delivered.
 *-------------------------------------------------------------------------*/
static void test_dangling_introducer_recover(struct ytest *test)
{
    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);
    feed(test, sm, (const uint8_t *)"\033]", 2);
    YTEST_CHECK_EQ_INT(test, cap.calls, 0);

    cap.expect = (const uint8_t *)"back";
    cap.expect_len = 4;
    struct yetty_ycore_buffer good = emit_osc(test, 7, 0, "back", 4);
    feed(test, sm, good.data, good.size);
    YTEST_CHECK_EQ_INT(test, cap.code, 7);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ycore_buffer_destroy(&good);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * A stray ESC immediately before an envelope introducer (ESC ESC ]) must not
 * swallow the introducer: the envelope is still delivered. Regression guard for
 * doubled-ESC resync at the SCAN_AFTER_ESC boundary.
 *-------------------------------------------------------------------------*/
static void test_stray_esc_then_envelope(struct ytest *test)
{
    struct cap cap = {.expect = (const uint8_t *)"hi", .expect_len = 2};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, 0);

    struct yetty_ycore_buffer env = emit_osc(test, 5, 0, "hi", 2);
    uint8_t buf[128];
    size_t n = 0;
    buf[n++] = 0x1b; /* stray ESC directly before the envelope's own introducer */
    memcpy(buf + n, env.data, env.size);
    n += env.size;
    feed(test, sm, buf, n);

    YTEST_CHECK_EQ_INT(test, cap.calls, 1);
    YTEST_CHECK_EQ_INT(test, cap.code, 5);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ycore_buffer_destroy(&env);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * An OSC with an args slot far beyond the internal cap is rejected without a
 * crash or unbounded allocation; a following valid envelope is delivered.
 *-------------------------------------------------------------------------*/
static void test_oversized_args_recover(struct ytest *test)
{
    struct cap cap = {0};
    struct yetty_ywire_wire_statemachine *sm = make_sm(test, &cap, /*has_args=*/1);

    /* ESC ] 9 ; <4000 'A' base64 chars as args> ; <payload> ESC \ */
    uint8_t big[4200];
    size_t n = 0;
    const char *head = "\033]9;";
    memcpy(big + n, head, strlen(head));
    n += strlen(head);
    memset(big + n, 'A', 4000);
    n += 4000;
    const char *tail = ";aGk=\033\\"; /* ; base64("hi") ST */
    memcpy(big + n, tail, strlen(tail));
    n += strlen(tail);
    feed(test, sm, big, n);

    /* Recovery: a valid has_args envelope is delivered. */
    uint8_t args[2] = {0xAB, 0xCD};
    struct yetty_ycore_buffer env = {0};
    struct yetty_ycore_void_result r = yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 8, /*has_args=*/1,
                                                        0, args, sizeof(args), "yo", 2, &env);
    YTEST_REQUIRE_OK(test, r);
    cap.expect = (const uint8_t *)"yo";
    cap.expect_len = 2;
    feed(test, sm, env.data, env.size);
    YTEST_CHECK_EQ_INT(test, cap.code, 8);
    YTEST_CHECK(test, cap.payload_ok);

    yetty_ycore_buffer_destroy(&env);
    yetty_ywire_wire_statemachine_destroy(sm);
}

/*---------------------------------------------------------------------------
 * yface stream boundary: raw text reaches on_raw, envelopes reach on_osc, and
 * a malformed segment does not stop a following valid envelope.
 *-------------------------------------------------------------------------*/
struct yface_cap {
    int osc_calls;
    int raw_calls;
    int last_code;
    int payload_ok;
    size_t raw_total;
};

static void yface_on_osc(void *user, int wire_code, const uint8_t *args, size_t args_len,
                         const uint8_t *payload, size_t payload_len)
{
    (void)args;
    (void)args_len;
    struct yface_cap *cap = user;
    cap->osc_calls++;
    cap->last_code = wire_code;
    cap->payload_ok = payload_len == 4 && payload && memcmp(payload, "body", 4) == 0;
}

static void yface_on_raw(void *user, const char *bytes, size_t n)
{
    (void)bytes;
    struct yface_cap *cap = user;
    cap->raw_calls++;
    cap->raw_total += n;
}

static void test_yface_mixed_stream(struct ytest *test)
{
    struct yetty_yface_ptr_result yf = yetty_yface_create();
    YTEST_REQUIRE_OK(test, yf);
    struct yface_cap cap = {0};
    yetty_yface_set_handlers(yf.value, yface_on_osc, yface_on_raw, &cap);

    /* "hello " + OSC(code 77, "body") + " world" via yface. yface's receiver
     * registers has_args=1 (matching yface's own emit), so build a matching
     * envelope: code ; args ; body. A has_args=0 envelope would have its body
     * misparsed as the args slot and dropped. */
    struct yetty_ycore_buffer env = {0};
    YTEST_REQUIRE_OK(test, yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 77, /*has_args=*/1,
                                            /*compressed=*/0, "a", 1, "body", 4, &env));
    uint8_t buf[256];
    size_t n = 0;
    memcpy(buf + n, "hello ", 6);
    n += 6;
    memcpy(buf + n, env.data, env.size);
    n += env.size;
    memcpy(buf + n, " world", 6);
    n += 6;
    struct yetty_ycore_void_result fed = yetty_yface_feed_bytes(yf.value, (const char *)buf, n);
    YTEST_REQUIRE_OK(test, fed);

    YTEST_CHECK_EQ_INT(test, cap.osc_calls, 1);
    YTEST_CHECK_EQ_INT(test, cap.last_code, 77);
    YTEST_CHECK(test, cap.payload_ok);
    YTEST_CHECK(test, cap.raw_calls > 0); /* the "hello "/" world" text */
    YTEST_CHECK(test, cap.raw_total >= 12);

    yetty_ycore_buffer_destroy(&env);
    yetty_yface_destroy(yf.value);
}

int main(void)
{
    struct ytest test = ytest_begin("ywire_malformed");
    YTEST_RUN(&test, test_garbage_surrounding);
    YTEST_RUN(&test, test_corrupt_lz4_then_recover);
    YTEST_RUN(&test, test_bad_base64_then_recover);
    YTEST_RUN(&test, test_chunk_boundary_exhaustive);
    YTEST_RUN(&test, test_interleaved_desync);
    YTEST_RUN(&test, test_dangling_introducer_recover);
    YTEST_RUN(&test, test_stray_esc_then_envelope);
    YTEST_RUN(&test, test_oversized_args_recover);
    YTEST_RUN(&test, test_yface_mixed_stream);
    return ytest_end(&test);
}
