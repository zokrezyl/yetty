#include "tx-queue.h"

#include <string.h>

#include "proto.h"

void yetty_ymux_tx_queue_reclaim_sent(struct yetty_ymux_tx_queue *queue)
{
    if (!queue || !queue->buffer) {
        return;
    }
    const size_t header_bytes = YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t);
    size_t drop = 0;
    while (queue->len - drop >= header_bytes) {
        uint32_t header[YMUX_PROTO_HEADER_WORDS];
        memcpy(header, queue->buffer + drop, header_bytes);
        if (header[0] != YMUX_PROTO_MAGIC) {
            break; /* not a clean boundary — stop */
        }
        size_t frame_len = header_bytes + header[2];
        if (drop + frame_len > queue->sent) {
            break; /* leading frame not fully sent yet */
        }
        drop += frame_len;
    }
    if (drop > 0) {
        memmove(queue->buffer, queue->buffer + drop, queue->len - drop);
        queue->len -= drop;
        queue->sent -= drop;
    }
}

void yetty_ymux_tx_queue_drop_terminal_frames(struct yetty_ymux_tx_queue *queue)
{
    if (!queue || !queue->buffer) {
        return;
    }
    const size_t header_bytes = YMUX_PROTO_HEADER_WORDS * sizeof(uint32_t);
    size_t read_off = 0, write_off = 0;
    int first = 1;
    while (queue->len - read_off >= header_bytes) {
        uint32_t header[YMUX_PROTO_HEADER_WORDS];
        memcpy(header, queue->buffer + read_off, header_bytes);
        if (header[0] != YMUX_PROTO_MAGIC) {
            break; /* not a clean frame boundary — leave the remainder intact */
        }
        size_t frame_len = header_bytes + header[2];
        if (queue->len - read_off < frame_len) {
            break; /* trailing partial frame — keep it */
        }
        uint32_t type = header[1];
        /* The leading frame is already partway on the wire when sent>0 — never
         * discard it (that would truncate an in-flight frame). */
        int partially_sent = (first && queue->sent > 0);
        /* Epoch recovery (review #12): the terminal stream now rides
         * VTSINK_RPC feed requests — recovery tears the whole vtsink session
         * down (VTSINK_RESET + re-publish), so dropping these frames cannot
         * desync a completion FIFO that is itself being destroyed. */
        int discard =
            !partially_sent && (type == YMUX_PROTO_TRANSACTION || type == YMUX_PROTO_VTSINK_RPC);
        first = 0;
        if (!discard) {
            if (write_off != read_off) {
                memmove(queue->buffer + write_off, queue->buffer + read_off, frame_len);
            }
            write_off += frame_len;
        }
        read_off += frame_len;
    }
    if (read_off < queue->len) {
        size_t tail = queue->len - read_off;
        if (write_off != read_off) {
            memmove(queue->buffer + write_off, queue->buffer + read_off, tail);
        }
        write_off += tail;
    }
    queue->len = write_off;
}

uint32_t yetty_ymux_tx_recovery_strike(uint32_t strikes, uint64_t drained_since_last,
                                       uint32_t strike_limit, int *should_close)
{
    if (drained_since_last > 0) {
        if (should_close) {
            *should_close = 0;
        }
        return 0; /* socket is progressing — not hopeless */
    }
    uint32_t next = strikes + 1;
    if (should_close) {
        *should_close = (next >= strike_limit);
    }
    return next;
}
