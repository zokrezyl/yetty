/*
 * ymux daemon tx-queue helpers (#695 slow-client TX policy) — headless, no GPU,
 * no socket. Exercises the pure frame-buffer operations that back the daemon's
 * slow-client recovery and leading-frame accounting:
 *
 *   - reclaim of FULLY-sent leading frames (partial leading frame retained);
 *   - selective compaction that drops obsolete terminal redraw (VT/TRANSACTION/
 *     PAINT) while keeping control/effect/RPC-relay frames AND a mid-flight
 *     leading frame;
 *   - the strike predicate that must NOT count a draining (progressing) socket
 *     as hopeless even when a single oversized frame keeps the buffer large.
 *
 * These are the fifth-cycle-review cases (partially-sent leading frame kept;
 * high-water recovery while the socket advances) tested directly on the logic.
 */
#include <stdint.h>
#include <string.h>

#include <yetty/ycore/types.h>

#include "ytest.h"

#include "../../../src/yetty/ymux/proto.h"
#include "../../../src/yetty/ymux/tx-queue.h"

enum { TX_HEADER_BYTES = YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t) };

/* Append one ymux frame (header + `payload_len` filler bytes) at *off. */
static void append_frame(uint8_t *buffer, size_t *off, uint32_t type, uint32_t payload_len,
                         uint8_t fill)
{
    uint32_t header[YMUX_PROTO_HEADER_WORDS] = {YMUX_PROTO_MAGIC, type, payload_len};
    memcpy(buffer + *off, header, sizeof(header));
    *off += sizeof(header);
    memset(buffer + *off, fill, payload_len);
    *off += payload_len;
}

/* Read the type word of the frame at `off`. */
static uint32_t frame_type_at(const uint8_t *buffer, size_t off)
{
    uint32_t header[YMUX_PROTO_HEADER_WORDS];
    memcpy(header, buffer + off, sizeof(header));
    return header[1];
}

/* reclaim_sent drops only FULLY-sent leading frames; a partial leading frame is
 * retained and `sent` becomes the offset into it. */
static void test_reclaim_sent(struct ytest *test)
{
    uint8_t buffer[256];
    size_t len = 0;
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 4, 0xA1); /* frame A: 16 bytes */
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 4, 0xB2); /* frame B: 16 bytes */
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 4, 0xC3); /* frame C: 16 bytes */
    size_t frame_bytes = TX_HEADER_BYTES + 4;
    YTEST_CHECK_EQ_SIZE(test, len, 3 * frame_bytes);

    /* sent == 0: nothing reclaimed. */
    struct yetty_ymux_tx_queue q0 = {.buffer = buffer, .len = len, .sent = 0};
    yetty_ymux_tx_queue_reclaim_sent(&q0);
    YTEST_CHECK_EQ_SIZE(test, q0.len, len);
    YTEST_CHECK_EQ_SIZE(test, q0.sent, 0);

    /* sent == A + 10 bytes into B: reclaim A only; B becomes leading, sent=10.
     * Uses a fresh fixture so the exact-boundary case below is independent. */
    uint8_t buffer_partial[256];
    size_t len_partial = 0;
    append_frame(buffer_partial, &len_partial, YMUX_PROTO_TRANSACTION, 4, 0xA1);
    append_frame(buffer_partial, &len_partial, YMUX_PROTO_TRANSACTION, 4, 0xB2);
    append_frame(buffer_partial, &len_partial, YMUX_PROTO_TRANSACTION, 4, 0xC3);
    struct yetty_ymux_tx_queue q1 = {
        .buffer = buffer_partial, .len = len_partial, .sent = frame_bytes + 10};
    yetty_ymux_tx_queue_reclaim_sent(&q1);
    YTEST_CHECK_EQ_SIZE(test, q1.len, 2 * frame_bytes);
    YTEST_CHECK_EQ_SIZE(test, q1.sent, 10);
    YTEST_CHECK(test, q1.buffer[TX_HEADER_BYTES] == 0xB2); /* B is now leading */

    /* sent == A + B exactly: reclaim both; C leading at a clean boundary. Built
     * independently (NOT copied from a mutated buffer) so this is a true test. */
    uint8_t buffer2[256];
    size_t len2 = 0;
    append_frame(buffer2, &len2, YMUX_PROTO_TRANSACTION, 4, 0xA1);
    append_frame(buffer2, &len2, YMUX_PROTO_TRANSACTION, 4, 0xB2);
    append_frame(buffer2, &len2, YMUX_PROTO_TRANSACTION, 4, 0xC3);
    struct yetty_ymux_tx_queue q2 = {.buffer = buffer2, .len = len2, .sent = 2 * frame_bytes};
    yetty_ymux_tx_queue_reclaim_sent(&q2);
    YTEST_CHECK_EQ_SIZE(test, q2.len, frame_bytes);
    YTEST_CHECK_EQ_SIZE(test, q2.sent, 0);
    YTEST_CHECK(test, q2.buffer[TX_HEADER_BYTES] == 0xC3); /* C is now leading */
}

/* drop_terminal_frames removes VT/TRANSACTION/PAINT but keeps RPC-relay and
 * effect frames, preserving order, when nothing is mid-flight (sent == 0). */
static void test_drop_terminal_selective(struct ytest *test)
{
    uint8_t buffer[512];
    size_t len = 0;
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 8, 0x01); /* drop */
    append_frame(buffer, &len, YMUX_PROTO_RPC_RELAY, 8, 0x02);   /* keep */
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 8, 0x03); /* drop */
    append_frame(buffer, &len, YMUX_PROTO_EFFECT_BELL, 0, 0x00); /* keep */
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 8, 0x04); /* drop */

    struct yetty_ymux_tx_queue queue = {.buffer = buffer, .len = len, .sent = 0};
    yetty_ymux_tx_queue_drop_terminal_frames(&queue);

    /* Expected survivors, in order: RPC_RELAY (8), EFFECT_BELL (0). */
    size_t expected = (TX_HEADER_BYTES + 8) + (TX_HEADER_BYTES + 0);
    YTEST_CHECK_EQ_SIZE(test, queue.len, expected);
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, 0), (int)YMUX_PROTO_RPC_RELAY);
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, TX_HEADER_BYTES + 8),
                       (int)YMUX_PROTO_EFFECT_BELL);
}

/* Review #12 (epoch recovery): queued VTSINK_RPC feed frames are terminal
 * stream too — the epoch reset tears the session down, so dropping them
 * cannot desync a completion FIFO. Control/effect frames still survive. */
static void test_drop_discards_vtsink_rpc(struct ytest *test)
{
    uint8_t buffer[512];
    size_t len = 0;
    append_frame(buffer, &len, YMUX_PROTO_VTSINK_RPC, 16, 0x11); /* drop */
    append_frame(buffer, &len, YMUX_PROTO_RPC_RELAY, 8, 0x02);   /* keep */
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 8, 0x03); /* drop */
    append_frame(buffer, &len, YMUX_PROTO_VTSINK_RPC, 24, 0x12); /* drop */
    append_frame(buffer, &len, YMUX_PROTO_EFFECT_BELL, 0, 0x00); /* keep */

    struct yetty_ymux_tx_queue queue = {.buffer = buffer, .len = len, .sent = 0};
    yetty_ymux_tx_queue_drop_terminal_frames(&queue);

    size_t expected = (TX_HEADER_BYTES + 8) + (TX_HEADER_BYTES + 0);
    YTEST_CHECK_EQ_SIZE(test, queue.len, expected);
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, 0), (int)YMUX_PROTO_RPC_RELAY);
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, TX_HEADER_BYTES + 8),
                       (int)YMUX_PROTO_EFFECT_BELL);
}

/* A mid-flight leading frame (sent > 0) is kept even when it is a terminal
 * frame that would otherwise be dropped — truncating an in-flight frame would
 * corrupt the stream. Later terminal frames are still dropped. */
static void test_drop_keeps_partial_leading(struct ytest *test)
{
    uint8_t buffer[512];
    size_t len = 0;
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 16, 0x11); /* leading, mid-flight: KEEP */
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 16, 0x22); /* later VT: drop */
    append_frame(buffer, &len, YMUX_PROTO_RPC_RELAY, 8, 0x33);    /* keep */

    /* 5 bytes of the leading frame already on the wire. */
    struct yetty_ymux_tx_queue queue = {.buffer = buffer, .len = len, .sent = 5};
    yetty_ymux_tx_queue_drop_terminal_frames(&queue);

    size_t expected = (TX_HEADER_BYTES + 16) + (TX_HEADER_BYTES + 8);
    YTEST_CHECK_EQ_SIZE(test, queue.len, expected);
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, 0), (int)YMUX_PROTO_TRANSACTION);
    YTEST_CHECK(test, queue.buffer[TX_HEADER_BYTES] == 0x11); /* the mid-flight one */
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, TX_HEADER_BYTES + 16),
                       (int)YMUX_PROTO_RPC_RELAY);
    YTEST_CHECK_EQ_SIZE(test, queue.sent, 5); /* sent unchanged */
}

/* A trailing partial frame (header present, payload short) is left intact — the
 * compactor stops at it rather than mis-parsing beyond the boundary. */
static void test_drop_stops_at_trailing_partial(struct ytest *test)
{
    uint8_t buffer[512];
    size_t len = 0;
    append_frame(buffer, &len, YMUX_PROTO_TRANSACTION, 8, 0x01); /* drop */
    append_frame(buffer, &len, YMUX_PROTO_RPC_RELAY, 8, 0x02);   /* keep */
    /* Trailing partial: header claims 100 payload bytes, only 20 present. */
    uint32_t header[YMUX_PROTO_HEADER_WORDS] = {YMUX_PROTO_MAGIC, YMUX_PROTO_TRANSACTION, 100};
    memcpy(buffer + len, header, sizeof(header));
    len += sizeof(header);
    memset(buffer + len, 0x77, 20);
    len += 20;

    struct yetty_ymux_tx_queue queue = {.buffer = buffer, .len = len, .sent = 0};
    yetty_ymux_tx_queue_drop_terminal_frames(&queue);

    /* Survivors: RPC_RELAY (16) + the intact trailing partial (12 + 20). */
    size_t expected = (TX_HEADER_BYTES + 8) + (TX_HEADER_BYTES + 20);
    YTEST_CHECK_EQ_SIZE(test, queue.len, expected);
    YTEST_CHECK_EQ_INT(test, (int)frame_type_at(queue.buffer, 0), (int)YMUX_PROTO_RPC_RELAY);
}

/* The strike predicate: a socket draining ANY bytes resets the strike count and
 * never closes; a fully-stalled socket increments each step and closes exactly
 * at the bound. This is the fifth-cycle "recovery must not disconnect a client
 * whose tx_sent is advancing on a huge frame" case. */
static void test_recovery_strike(struct ytest *test)
{
    const uint32_t limit = 4;
    int should_close = 99;

    /* Progress every step: count stays 0, never closes — even repeated. */
    uint32_t count = 3;
    count = yetty_ymux_tx_recovery_strike(count, /*drained=*/1, limit, &should_close);
    YTEST_CHECK_EQ_INT(test, (int)count, 0);
    YTEST_CHECK_EQ_INT(test, should_close, 0);
    count = yetty_ymux_tx_recovery_strike(count, /*drained=*/4096, limit, &should_close);
    YTEST_CHECK_EQ_INT(test, (int)count, 0);
    YTEST_CHECK_EQ_INT(test, should_close, 0);

    /* Fully stalled: increments each step, closes exactly at the limit. */
    count = 0;
    should_close = 99;
    count = yetty_ymux_tx_recovery_strike(count, 0, limit, &should_close); /* 1 */
    YTEST_CHECK_EQ_INT(test, (int)count, 1);
    YTEST_CHECK_EQ_INT(test, should_close, 0);
    count = yetty_ymux_tx_recovery_strike(count, 0, limit, &should_close); /* 2 */
    YTEST_CHECK_EQ_INT(test, (int)count, 2);
    YTEST_CHECK_EQ_INT(test, should_close, 0);
    count = yetty_ymux_tx_recovery_strike(count, 0, limit, &should_close); /* 3 */
    YTEST_CHECK_EQ_INT(test, (int)count, 3);
    YTEST_CHECK_EQ_INT(test, should_close, 0);
    count = yetty_ymux_tx_recovery_strike(count, 0, limit, &should_close); /* 4 == limit */
    YTEST_CHECK_EQ_INT(test, (int)count, 4);
    YTEST_CHECK_EQ_INT(test, should_close, 1);

    /* One byte of progress AFTER strikes clears the count (no premature close). */
    should_close = 99;
    count = yetty_ymux_tx_recovery_strike(3, /*drained=*/1, limit, &should_close);
    YTEST_CHECK_EQ_INT(test, (int)count, 0);
    YTEST_CHECK_EQ_INT(test, should_close, 0);
}

int main(void)
{
    struct ytest test = ytest_begin("ymux_tx_queue");
    YTEST_RUN(&test, test_reclaim_sent);
    YTEST_RUN(&test, test_drop_terminal_selective);
    YTEST_RUN(&test, test_drop_discards_vtsink_rpc);
    YTEST_RUN(&test, test_drop_keeps_partial_leading);
    YTEST_RUN(&test, test_drop_stops_at_trailing_partial);
    YTEST_RUN(&test, test_recovery_strike);
    return ytest_end(&test);
}
