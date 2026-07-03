/*
 * yctl RPC message-layer contract test (#423) — pure, no display/socket/loop.
 *
 * The external control client (yctl.py) and the in-app server speak
 * msgpack-RPC. This pins the pure wire contract of that layer:
 *   Request:      [0, msgid, channel, method, params]
 *   Response:     [1, msgid, error, result]
 *   Notification: [2, channel, method, params]
 * Covers request/notification decode, response encode→decode round-trip,
 * malformed-frame rejection, partial-frame (streaming) handling, and multiple
 * framed messages in one buffer. No TCP socket, no event loop, no GPU.
 */

#include <yetty/yctl/rpc-message.h>

#include "ytest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* msgpack params re-packed by the parser are heap-allocated; free without a
 * const-cast warning. */
static void free_params(const uint8_t *params)
{
    free((void *)(uintptr_t)params);
}

/*---------------------------------------------------------------------------
 * A well-formed Request decodes into all its typed fields.
 *-------------------------------------------------------------------------*/
static void test_parse_request(struct ytest *test)
{
    /* [0, msgid=42, channel=0, "run", 7] */
    const uint8_t wire[] = {
        0x95,                      /* fixarray 5 */
        0x00,                      /* type = REQUEST */
        0x2a,                      /* msgid = 42 */
        0x00,                      /* channel = 0 */
        0xa3, 'r', 'u', 'n',       /* method "run" */
        0x07,                      /* params = fixint 7 */
    };
    size_t consumed = 0;
    struct yetty_rpc_message_result r = yetty_yctl_message_parse(wire, sizeof(wire), &consumed);
    YTEST_REQUIRE_OK(test, r);
    YTEST_CHECK_EQ_SIZE(test, consumed, sizeof(wire));
    YTEST_CHECK_EQ_INT(test, r.value.type, YETTY_YCTL_MSG_REQUEST);
    YTEST_CHECK_EQ_INT(test, r.value.msgid, 42);
    YTEST_CHECK_EQ_INT(test, r.value.channel, 0);
    YTEST_REQUIRE_NOT_NULL(test, r.value.method);
    YTEST_CHECK_EQ_SIZE(test, r.value.method_len, 3u);
    YTEST_CHECK(test, memcmp(r.value.method, "run", 3) == 0);
    YTEST_REQUIRE_NOT_NULL(test, r.value.params);
    YTEST_CHECK_EQ_SIZE(test, r.value.params_len, 1u);
    YTEST_CHECK_EQ_INT(test, r.value.params[0], 0x07); /* re-packed fixint 7 */
    free_params(r.value.params);
}

/*---------------------------------------------------------------------------
 * A Notification decodes (msgid is 0, channel + method present).
 *-------------------------------------------------------------------------*/
static void test_parse_notification(struct ytest *test)
{
    /* [2, channel=0, "shutdown", nil] */
    const uint8_t wire[] = {
        0x94,                                          /* fixarray 4 */
        0x02,                                          /* type = NOTIFICATION */
        0x00,                                          /* channel = 0 */
        0xa8, 's', 'h', 'u', 't', 'd', 'o', 'w', 'n',  /* method "shutdown" */
        0xc0,                                          /* params = nil */
    };
    size_t consumed = 0;
    struct yetty_rpc_message_result r = yetty_yctl_message_parse(wire, sizeof(wire), &consumed);
    YTEST_REQUIRE_OK(test, r);
    YTEST_CHECK_EQ_SIZE(test, consumed, sizeof(wire));
    YTEST_CHECK_EQ_INT(test, r.value.type, YETTY_YCTL_MSG_NOTIFICATION);
    YTEST_CHECK_EQ_INT(test, r.value.msgid, 0);
    YTEST_CHECK_EQ_SIZE(test, r.value.method_len, 8u);
    YTEST_CHECK(test, memcmp(r.value.method, "shutdown", 8) == 0);
    YTEST_REQUIRE_NOT_NULL(test, r.value.params);
    free_params(r.value.params);
}

/*---------------------------------------------------------------------------
 * Response encode → decode round-trip (ok / error / bool).
 *-------------------------------------------------------------------------*/
static void test_response_roundtrip(struct ytest *test)
{
    uint8_t storage[128];
    struct yetty_yctl_write_buffer wb;
    yetty_yctl_write_buffer_init(&wb, storage, sizeof(storage));

    /* ok response with a fixint result */
    const uint8_t result[] = {0x07};
    YTEST_REQUIRE_OK(test, yetty_yctl_write_response_ok(&wb, 99, result, sizeof(result)));
    YTEST_CHECK(test, wb.len > 0);
    YTEST_CHECK_EQ_INT(test, wb.data[0], 0x94); /* fixarray 4 */
    YTEST_CHECK_EQ_INT(test, wb.data[1], 0x01); /* type RESPONSE */

    size_t consumed = 0;
    struct yetty_rpc_message_result ok = yetty_yctl_message_parse(wb.data, wb.len, &consumed);
    YTEST_REQUIRE_OK(test, ok);
    YTEST_CHECK_EQ_INT(test, ok.value.type, YETTY_YCTL_MSG_RESPONSE);
    YTEST_CHECK_EQ_INT(test, ok.value.msgid, 99);

    /* error response */
    YTEST_REQUIRE_OK(test, yetty_yctl_write_response_error(&wb, 7, "boom"));
    struct yetty_rpc_message_result er = yetty_yctl_message_parse(wb.data, wb.len, &consumed);
    YTEST_REQUIRE_OK(test, er);
    YTEST_CHECK_EQ_INT(test, er.value.type, YETTY_YCTL_MSG_RESPONSE);
    YTEST_CHECK_EQ_INT(test, er.value.msgid, 7);

    /* bool response */
    YTEST_REQUIRE_OK(test, yetty_yctl_write_response_bool(&wb, 3, 1));
    struct yetty_rpc_message_result bl = yetty_yctl_message_parse(wb.data, wb.len, &consumed);
    YTEST_REQUIRE_OK(test, bl);
    YTEST_CHECK_EQ_INT(test, bl.value.type, YETTY_YCTL_MSG_RESPONSE);
    YTEST_CHECK_EQ_INT(test, bl.value.msgid, 3);
    /* Response params are left for the caller to parse; nothing to free above. */
}

/*---------------------------------------------------------------------------
 * Malformed frames are rejected (no crash, error Result).
 *-------------------------------------------------------------------------*/
static void test_malformed(struct ytest *test)
{
    size_t consumed = 0;

    const uint8_t not_array[] = {0x07}; /* a bare int, not an array */
    YTEST_CHECK(test, YETTY_IS_ERR(yetty_yctl_message_parse(not_array, sizeof(not_array), &consumed)));

    const uint8_t empty_array[] = {0x90}; /* fixarray 0 */
    YTEST_CHECK(test,
                YETTY_IS_ERR(yetty_yctl_message_parse(empty_array, sizeof(empty_array), &consumed)));

    const uint8_t type_not_int[] = {0x91, 0xa1, 'x'}; /* [ "x" ] */
    YTEST_CHECK(
        test, YETTY_IS_ERR(yetty_yctl_message_parse(type_not_int, sizeof(type_not_int), &consumed)));

    const uint8_t short_request[] = {0x93, 0x00, 0x2a, 0x00}; /* request needs 5 elems */
    YTEST_CHECK(test, YETTY_IS_ERR(
                          yetty_yctl_message_parse(short_request, sizeof(short_request), &consumed)));

    const uint8_t unknown_type[] = {0x91, 0x09}; /* [ 9 ] — unknown message type */
    YTEST_CHECK(test,
                YETTY_IS_ERR(yetty_yctl_message_parse(unknown_type, sizeof(unknown_type), &consumed)));

    /* [0, 42, 0, 7, nil] — method is an int, not a string */
    const uint8_t bad_method[] = {0x95, 0x00, 0x2a, 0x00, 0x07, 0xc0};
    YTEST_CHECK(test,
                YETTY_IS_ERR(yetty_yctl_message_parse(bad_method, sizeof(bad_method), &consumed)));
}

/*---------------------------------------------------------------------------
 * A partial (truncated) frame is not an error: parse returns OK with
 * consumed == 0 so the caller waits for more bytes.
 *-------------------------------------------------------------------------*/
static void test_partial_frame(struct ytest *test)
{
    /* fixarray 5 declared, only the type element present. */
    const uint8_t partial[] = {0x95, 0x00};
    size_t consumed = 123;
    struct yetty_rpc_message_result r = yetty_yctl_message_parse(partial, sizeof(partial), &consumed);
    YTEST_REQUIRE_OK(test, r);
    YTEST_CHECK_EQ_SIZE(test, consumed, 0u); /* incomplete → wait for more */
}

/*---------------------------------------------------------------------------
 * Two framed messages in one buffer are consumed one at a time via consumed.
 *-------------------------------------------------------------------------*/
static void test_streaming(struct ytest *test)
{
    const uint8_t stream[] = {
        /* [2, 0, "a", nil] */
        0x94, 0x02, 0x00, 0xa1, 'a', 0xc0,
        /* [2, 0, "bb", nil] */
        0x94, 0x02, 0x00, 0xa2, 'b', 'b', 0xc0,
    };
    size_t off = 0;

    size_t c1 = 0;
    struct yetty_rpc_message_result r1 =
        yetty_yctl_message_parse(stream + off, sizeof(stream) - off, &c1);
    YTEST_REQUIRE_OK(test, r1);
    YTEST_CHECK(test, c1 > 0);
    YTEST_CHECK_EQ_SIZE(test, r1.value.method_len, 1u);
    YTEST_CHECK(test, memcmp(r1.value.method, "a", 1) == 0);
    free_params(r1.value.params);
    off += c1;

    size_t c2 = 0;
    struct yetty_rpc_message_result r2 =
        yetty_yctl_message_parse(stream + off, sizeof(stream) - off, &c2);
    YTEST_REQUIRE_OK(test, r2);
    YTEST_CHECK(test, c2 > 0);
    YTEST_CHECK_EQ_SIZE(test, r2.value.method_len, 2u);
    YTEST_CHECK(test, memcmp(r2.value.method, "bb", 2) == 0);
    free_params(r2.value.params);
    off += c2;

    YTEST_CHECK_EQ_SIZE(test, off, sizeof(stream)); /* whole buffer consumed */
}

int main(void)
{
    struct ytest test = ytest_begin("yctl_message");
    YTEST_RUN(&test, test_parse_request);
    YTEST_RUN(&test, test_parse_notification);
    YTEST_RUN(&test, test_response_roundtrip);
    YTEST_RUN(&test, test_malformed);
    YTEST_RUN(&test, test_partial_frame);
    YTEST_RUN(&test, test_streaming);
    return ytest_end(&test);
}
