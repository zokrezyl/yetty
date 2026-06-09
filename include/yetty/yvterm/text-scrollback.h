#ifndef YETTY_YVTERM_TEXT_SCROLLBACK_H
#define YETTY_YVTERM_TEXT_SCROLLBACK_H

#include <stddef.h>
#include <stdint.h>
#include <vterm.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One scrollback line descriptor: byte offset into the cells ring + the
 * column count stored there. */
struct yetty_yvterm_text_sb_line_rec {
    size_t offset;
    int cols;
};

/* Scrollback arena.
 *
 * Cells payload is packed back-to-back in a byte ring; lines[] is a parallel
 * ring of (offset, cols) descriptors. Two phases:
 *
 *   Growth phase (lines_cap < max_lines): both buffers double std::vector-
 *     style when full. No eviction. Data is contiguous from offset 0, so
 *     plain realloc preserves it.
 *
 *   Steady phase (lines_cap == max_lines): both buffers fixed; push evicts
 *     oldest line(s) until the new line fits in cells[] and lines[] has a
 *     free slot. Pure circular ring.
 *
 * Logical indexing: position 0 = oldest live line, lines_count-1 = newest.
 * Stored at lines[(lines_tail + i) % lines_cap]. */
struct yetty_yvterm_text_sb_arena {
    uint8_t *cells;
    size_t cells_cap;
    size_t cells_head;
    size_t cells_tail;
    size_t cells_used;

    struct yetty_yvterm_text_sb_line_rec *lines;
    uint32_t lines_cap;
    uint32_t lines_head;
    uint32_t lines_tail;
    uint32_t lines_count;

    /* Count of lines evicted off the oldest end since init. Together with
     * lines_count this gives the ABSOLUTE index of the live-screen top in
     * libvterm's full timeline (lines_evicted + lines_count) — the same
     * absolute space the ydraw canvas's rolling_row_0 counts in, so both
     * layers share one scroll coordinate even after the cap is hit. */
    uint64_t lines_evicted;

    uint32_t max_lines;
};

void yetty_yvterm_text_sb_arena_init(struct yetty_yvterm_text_sb_arena *arena, uint32_t max_lines);
void yetty_yvterm_text_sb_arena_destroy(struct yetty_yvterm_text_sb_arena *arena);

/* Read cols cells from the ring at byte offset into dst, handling wrap. */
void yetty_yvterm_text_sb_arena_read(const struct yetty_yvterm_text_sb_arena *arena, size_t offset,
                                     int cols, VTermScreenCell *dst);

/* Resolve logical index (0=oldest) to a descriptor pointer, or NULL. */
const struct yetty_yvterm_text_sb_line_rec *yetty_yvterm_text_sb_arena_peek(
    const struct yetty_yvterm_text_sb_arena *arena, uint32_t index);

/* Push one line. Returns 1 on success, 0 on allocation failure. */
int yetty_yvterm_text_sb_arena_push(struct yetty_yvterm_text_sb_arena *arena,
                                    const VTermScreenCell *src, int cols);

/* Pop newest line. Copies up to copy_cols cells into dst. Returns the number
 * of cells copied (0 on empty). */
int yetty_yvterm_text_sb_arena_pop(struct yetty_yvterm_text_sb_arena *arena, VTermScreenCell *dst,
                                   int copy_cols);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_TEXT_SCROLLBACK_H */
