/*
 * ywire foreign-DCS / foreign-OSC passthrough contract test (#579).
 *
 * The wire state machine sits on the child-output stream ahead of vterm and
 * frames yetty's own OSC/DCS envelopes. Control strings it does NOT own —
 * XTGETTCAP (ESC P +q …), DECRQSS (ESC P $q …), and any standard OSC whose
 * numeric code has no registered yetty handler (OSC 0/1/2/7/8/52/133, …) —
 * must traverse the layer BYTE-IDENTICAL so the terminal (libvterm) can
 * answer or ignore them. Previously the framer swallowed the introducer and
 * leaked the tail as screen text (and flooded the log with "malformed
 * envelope" warnings).
 *
 * These tests drive the SM with a raw default sink that accumulates every
 * passthrough byte, and assert the relayed stream equals the input exactly.
 */

#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Accumulates all raw (passthrough) bytes the SM relays to the default sink. */
struct raw_acc {
    uint8_t buf[8192];
    size_t len;
    int overflow;
};

static struct yetty_ycore_void_result on_raw(void *userdata, const uint8_t *bytes, size_t n)
{
    struct raw_acc *acc = userdata;
    if (acc->len + n > sizeof(acc->buf)) {
        acc->overflow = 1;
        return YETTY_OK_VOID();
    }
    memcpy(acc->buf + acc->len, bytes, n);
    acc->len += n;
    return YETTY_OK_VOID();
}

/* Captures a delivered yetty envelope (for the "still parses after a foreign
 * DCS" cases). */
struct env_acc {
    int calls;
    int last_code;
    int payload_ok;
};

static struct yetty_ycore_void_result on_env(void *userdata, enum yetty_ywire_envelope_kind kind,
                                             int code, const uint8_t *args, size_t args_len,
                                             const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)args;
    (void)args_len;
    struct env_acc *acc = userdata;
    acc->calls++;
    acc->last_code = code;
    acc->payload_ok = payload_len == 2 && payload != NULL && memcmp(payload, "hi", 2) == 0;
    return YETTY_OK_VOID();
}

static void feed(struct ytest *test, struct yetty_ywire_wire_statemachine *sm, const void *bytes,
                 size_t len)
{
    struct yetty_ycore_void_result fed =
        yetty_ywire_wire_statemachine_feed(sm, (const char *)bytes, len);
    YTEST_REQUIRE_OK(test, fed);
    struct yetty_ycore_void_result proc = yetty_ywire_wire_statemachine_process(sm);
    YTEST_REQUIRE_OK(test, proc);
}

/* Assert the accumulated raw stream equals `expect` byte-for-byte. */
static void check_relayed(struct ytest *test, const struct raw_acc *acc, const char *expect)
{
    size_t elen = strlen(expect);
    YTEST_CHECK(test, !acc->overflow);
    YTEST_CHECK_EQ_INT(test, (int)acc->len, (int)elen);
    if (acc->len == elen) {
        YTEST_CHECK(test, memcmp(acc->buf, expect, elen) == 0);
    }
}

/*---------------------------------------------------------------------------
 * XTGETTCAP (ESC P + q <hex> ESC \) passes through byte-identical — the byte
 * after the DCS introducer is `+`, not a digit.
 *-------------------------------------------------------------------------*/
static void test_dcs_xtgettcap_passthrough(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));

    const char *xt = "\033P+q544e;524742\033\\"; /* query TN;RGB */
    feed(test, sm.value, xt, strlen(xt));
    check_relayed(test, &raw, xt);

    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * DECRQSS (ESC P $ q <Pt> ESC \) passes through byte-identical — the byte
 * after the introducer is `$`.
 *-------------------------------------------------------------------------*/
static void test_dcs_decrqss_passthrough(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));

    const char *rq = "\033P$qm\033\\"; /* request current SGR */
    feed(test, sm.value, rq, strlen(rq));
    check_relayed(test, &raw, rq);

    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * An OSC with a clean numeric code but no registered handler (OSC 52,
 * clipboard) passes through untouched instead of being drained-and-dropped.
 * The inner `;` separators are relayed as opaque data.
 *-------------------------------------------------------------------------*/
static void test_osc_unknown_numeric_passthrough(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));

    const char *clip = "\033]52;c;aGVsbG8=\033\\"; /* set clipboard "hello" */
    feed(test, sm.value, clip, strlen(clip));
    check_relayed(test, &raw, clip);

    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * BEL-terminated OSC (title set) passes through byte-identical too — BEL is a
 * valid OSC terminator and rides through the raw path untouched.
 *-------------------------------------------------------------------------*/
static void test_osc_bel_terminated_passthrough(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));

    const char *title = "\033]0;my title\007"; /* OSC 0 ; text BEL */
    feed(test, sm.value, title, strlen(title));
    check_relayed(test, &raw, title);

    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * A genuine yetty envelope immediately after a foreign DCS still parses, and
 * the foreign DCS is relayed complete (introducer + ST) to the terminal.
 *-------------------------------------------------------------------------*/
static void test_yetty_envelope_after_foreign_dcs(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct env_acc env = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));
    /* A specific (OSC, 5) handler so the following envelope resolves — but no
     * catch-all, so the foreign OSC 52/DCS keep the passthrough path. */
    YTEST_REQUIRE_OK(
        test, yetty_ywire_wire_statemachine_register_buffered(sm.value, YETTY_YWIRE_ENVELOPE_OSC, 5,
                                                              /*has_args=*/0, on_env, &env));

    const char *xt = "\033P+q544e\033\\";
    struct yetty_ycore_buffer envbuf = {0};
    YTEST_REQUIRE_OK(test, yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 5, /*has_args=*/0,
                                            /*compressed=*/0, NULL, 0, "hi", 2, &envbuf));

    uint8_t buf[256];
    size_t n = 0;
    memcpy(buf + n, xt, strlen(xt));
    n += strlen(xt);
    memcpy(buf + n, envbuf.data, envbuf.size);
    n += envbuf.size;
    feed(test, sm.value, buf, n);

    YTEST_CHECK_EQ_INT(test, env.calls, 1);
    YTEST_CHECK_EQ_INT(test, env.last_code, 5);
    YTEST_CHECK(test, env.payload_ok);
    /* The foreign DCS was relayed complete (byte-identical), and the yetty
     * envelope's own framing bytes did NOT leak into the raw stream. */
    check_relayed(test, &raw, xt);

    yetty_ycore_buffer_destroy(&envbuf);
    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * A truncated foreign DCS (no ST) does not wedge the scanner: a valid yetty
 * envelope fed afterwards is still delivered.
 *-------------------------------------------------------------------------*/
static void test_truncated_foreign_dcs_no_wedge(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct env_acc env = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));
    YTEST_REQUIRE_OK(
        test, yetty_ywire_wire_statemachine_register_buffered(sm.value, YETTY_YWIRE_ENVELOPE_OSC, 5,
                                                              /*has_args=*/0, on_env, &env));

    const char *truncated = "\033P+q544e"; /* no ST */
    feed(test, sm.value, truncated, strlen(truncated));

    struct yetty_ycore_buffer envbuf = {0};
    YTEST_REQUIRE_OK(test, yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 5, /*has_args=*/0,
                                            /*compressed=*/0, NULL, 0, "hi", 2, &envbuf));
    feed(test, sm.value, envbuf.data, envbuf.size);

    YTEST_CHECK_EQ_INT(test, env.calls, 1);
    YTEST_CHECK_EQ_INT(test, env.last_code, 5);
    YTEST_CHECK(test, env.payload_ok);

    yetty_ycore_buffer_destroy(&envbuf);
    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * A non-numeric byte directly after ESC ] (OSC) takes the same passthrough
 * path and does not desync the scanner.
 *-------------------------------------------------------------------------*/
static void test_osc_nonnumeric_passthrough(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));

    const char *seq = "\033]Iabc\033\\"; /* non-numeric OSC code byte */
    feed(test, sm.value, seq, strlen(seq));
    check_relayed(test, &raw, seq);

    yetty_ywire_wire_statemachine_destroy(sm.value);
}

/*---------------------------------------------------------------------------
 * A real yetty envelope with a registered handler is NOT relayed to the raw
 * sink — the passthrough change must not turn owned envelopes into text.
 *-------------------------------------------------------------------------*/
static void test_owned_envelope_not_relayed(struct ytest *test)
{
    struct raw_acc raw = {0};
    struct env_acc env = {0};
    struct yetty_ywire_wire_statemachine_ptr_result sm = yetty_ywire_wire_statemachine_create(NULL);
    YTEST_REQUIRE_OK(test, sm);
    YTEST_REQUIRE_OK(test,
                     yetty_ywire_wire_statemachine_set_default_buffered(sm.value, on_raw, &raw));
    YTEST_REQUIRE_OK(
        test, yetty_ywire_wire_statemachine_register_buffered(sm.value, YETTY_YWIRE_ENVELOPE_OSC, 5,
                                                              /*has_args=*/0, on_env, &env));

    struct yetty_ycore_buffer envbuf = {0};
    YTEST_REQUIRE_OK(test, yetty_ywire_emit(YETTY_YWIRE_ENVELOPE_OSC, 5, /*has_args=*/0,
                                            /*compressed=*/0, NULL, 0, "hi", 2, &envbuf));
    feed(test, sm.value, envbuf.data, envbuf.size);

    YTEST_CHECK_EQ_INT(test, env.calls, 1);
    YTEST_CHECK(test, env.payload_ok);
    YTEST_CHECK_EQ_INT(test, (int)raw.len, 0); /* nothing leaked to text */

    yetty_ycore_buffer_destroy(&envbuf);
    yetty_ywire_wire_statemachine_destroy(sm.value);
}

int main(void)
{
    struct ytest test = ytest_begin("ywire_passthrough");
    YTEST_RUN(&test, test_dcs_xtgettcap_passthrough);
    YTEST_RUN(&test, test_dcs_decrqss_passthrough);
    YTEST_RUN(&test, test_osc_unknown_numeric_passthrough);
    YTEST_RUN(&test, test_osc_bel_terminated_passthrough);
    YTEST_RUN(&test, test_yetty_envelope_after_foreign_dcs);
    YTEST_RUN(&test, test_truncated_foreign_dcs_no_wedge);
    YTEST_RUN(&test, test_osc_nonnumeric_passthrough);
    YTEST_RUN(&test, test_owned_envelope_not_relayed);
    return ytest_end(&test);
}
