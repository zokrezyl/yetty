/*
 * ymux daemon outbound frame-queue helpers — pure, dependency-free.
 *
 * The daemon holds each client's queued output as a run of ymux protocol frames
 * (header words: magic, type, len) in one contiguous buffer. `sent` is how many
 * leading bytes have already been written to the socket; a leading frame is
 * reclaimed only once fully sent, so buffer[0] is always a clean frame boundary.
 *
 * These two operations are the whole of the slow-client tx policy that is easy
 * to get wrong (partial-frame boundaries, selective compaction). They touch only
 * the byte buffer + the frame header format from proto.h — no yclass, ywire, or
 * socket state — so they live here and are unit-tested directly.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Borrowed view of a connection's tx buffer for the pure helpers below. */
struct yetty_ymux_tx_queue {
    uint8_t *buffer; /* borrowed — the connection's tx storage */
    size_t len;      /* bytes currently queued */
    size_t sent;     /* bytes of the LEADING frame already on the wire */
};

/* Reclaim every FULLY-sent leading frame: compact it out of the buffer and drop
 * `sent` by the same amount. Afterwards buffer[0] is a valid frame header and
 * `sent` is the offset into the still-partial (or fresh) leading frame. */
void yetty_ymux_tx_queue_reclaim_sent(struct yetty_ymux_tx_queue *queue);

/* Slow-client recovery compaction: drop obsolete terminal-redraw frames
 * (VT/TRANSACTION/PAINT), keeping control/effect/RPC-relay frames and —
 * unconditionally — the LEADING frame when it is mid-flight (`sent` > 0), since
 * its head is already on the wire and truncating it would corrupt the stream.
 * Stops at the first non-boundary or trailing partial frame, leaving the
 * remainder intact. Compacts in place; `sent` is unchanged (the leading frame,
 * if any, is always kept). */
void yetty_ymux_tx_queue_drop_terminal_frames(struct yetty_ymux_tx_queue *queue);

/* Slow-client recovery strike accounting. `drained_since_last` is the bytes the
 * socket drained since the previous recovery observation; `strikes` is the
 * current consecutive no-progress count. Returns the updated count — reset to 0
 * whenever the socket made ANY progress, incremented otherwise — and sets
 * *should_close when the (incremented) count reaches `strike_limit` with no
 * progress. A client steadily draining even one oversized frame therefore never
 * trips the bound; only a genuinely stalled socket does. `should_close` may be
 * NULL. */
uint32_t yetty_ymux_tx_recovery_strike(uint32_t strikes, uint64_t drained_since_last,
                                       uint32_t strike_limit, int *should_close);
