#include <yetty/yvterm/text-scrollback.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Scrollback arena
 *
 * See struct yetty_yvterm_text_sb_arena for the design. All operations are
 * O(1) amortised; eviction is only entered after lines_cap hits max_lines.
 *===========================================================================*/

void yetty_yvterm_text_sb_arena_init(struct yetty_yvterm_text_sb_arena *arena, uint32_t max_lines)
{
    memset(arena, 0, sizeof(*arena));
    arena->max_lines = max_lines ? max_lines : 1;
}

void yetty_yvterm_text_sb_arena_destroy(struct yetty_yvterm_text_sb_arena *arena)
{
    free(arena->cells);
    free(arena->lines);
    memset(arena, 0, sizeof(*arena));
}

/* Drop the oldest line, freeing its bytes from the cells ring. */
static void sb_arena_drop_oldest(struct yetty_yvterm_text_sb_arena *arena)
{
    if (arena->lines_count == 0) {
        return;
    }
    size_t bytes = (size_t)arena->lines[arena->lines_tail].cols * sizeof(VTermScreenCell);
    arena->cells_tail = (arena->cells_tail + bytes) % arena->cells_cap;
    arena->cells_used -= bytes;
    arena->lines_tail = (arena->lines_tail + 1) % arena->lines_cap;
    arena->lines_count--;
}

/* Drop the newest line. Used by pop. */
static void sb_arena_drop_newest(struct yetty_yvterm_text_sb_arena *arena)
{
    if (arena->lines_count == 0) {
        return;
    }
    arena->lines_head = (arena->lines_head + arena->lines_cap - 1) % arena->lines_cap;
    size_t bytes = (size_t)arena->lines[arena->lines_head].cols * sizeof(VTermScreenCell);
    arena->cells_head = (arena->cells_head + arena->cells_cap - bytes) % arena->cells_cap;
    arena->cells_used -= bytes;
    arena->lines_count--;
}

/* Read cols cells from the ring at byte offset into dst, handling wrap.
 * Defensive: garbage line records (uninit/stale) have shown up under heavy
 * ydraw scrollback. Bail with a loud yerror rather than memcpy from a
 * bogus pointer — the caller has already filled dst with blank if needed. */
void yetty_yvterm_text_sb_arena_read(const struct yetty_yvterm_text_sb_arena *arena, size_t offset,
                                    int cols, VTermScreenCell *dst)
{
    if (cols <= 0) {
        return;
    }
    size_t bytes = (size_t)cols * sizeof(VTermScreenCell);
    if (offset >= arena->cells_cap || bytes > arena->cells_cap) {
        yerror("sb_arena_read: invalid args offset=%zu cols=%d bytes=%zu "
               "cells_cap=%zu cells_head=%zu cells_tail=%zu cells_used=%zu "
               "lines_cap=%u lines_head=%u lines_tail=%u lines_count=%u",
               offset, cols, bytes, arena->cells_cap, arena->cells_head, arena->cells_tail,
               arena->cells_used, arena->lines_cap, arena->lines_head, arena->lines_tail,
               arena->lines_count);
        return;
    }
    if (offset + bytes <= arena->cells_cap) {
        memcpy(dst, arena->cells + offset, bytes);
    } else {
        size_t first = arena->cells_cap - offset;
        memcpy(dst, arena->cells + offset, first);
        memcpy((uint8_t *)dst + first, arena->cells, bytes - first);
    }
}

/* Resolve logical index (0=oldest) to a descriptor pointer. */
const struct yetty_yvterm_text_sb_line_rec *yetty_yvterm_text_sb_arena_peek(
    const struct yetty_yvterm_text_sb_arena *arena, uint32_t index)
{
    if (index >= arena->lines_count) {
        return NULL;
    }
    return &arena->lines[(arena->lines_tail + index) % arena->lines_cap];
}

/* Push one line. Returns 1 on success. */
int yetty_yvterm_text_sb_arena_push(struct yetty_yvterm_text_sb_arena *arena,
                                   const VTermScreenCell *src, int cols)
{
    if (cols <= 0) {
        return 1;
    }
    size_t bytes = (size_t)cols * sizeof(VTermScreenCell);

    /* Growth phase: extend lines[] up to max_lines. The live range is
     * always linear in this phase (lines_tail = 0), so realloc preserves it.
     * After grow, force lines_head = lines_count: the prior push's modulus
     * may have wrapped lines_head to 0 at the old cap boundary, which would
     * cause this push to overwrite slot 0 instead of appending. */
    if (arena->lines_count >= arena->lines_cap && arena->lines_cap < arena->max_lines) {
        uint32_t new_cap = arena->lines_cap == 0 ? 256 : arena->lines_cap * 2;
        if (new_cap > arena->max_lines) {
            new_cap = arena->max_lines;
        }
        struct yetty_yvterm_text_sb_line_rec *grown =
            realloc(arena->lines, (size_t)new_cap * sizeof(*grown));
        if (!grown) {
            yerror("sb_arena: lines realloc failed (new_cap=%u)", new_cap);
            return 0;
        }
        arena->lines = grown;
        arena->lines_cap = new_cap;
        arena->lines_head = arena->lines_count;
    }

    /* Growth phase: extend cells[] until the new line fits. Same linearity
     * argument — cells_tail = 0 so realloc keeps the range valid. Force
     * cells_head = cells_used for the same reason as lines_head above. */
    while (arena->cells_used + bytes > arena->cells_cap && arena->lines_cap < arena->max_lines) {
        size_t new_cap = arena->cells_cap == 0 ? 64 * 1024 : arena->cells_cap * 2;
        while (new_cap < arena->cells_used + bytes) {
            new_cap *= 2;
        }
        uint8_t *grown = realloc(arena->cells, new_cap);
        if (!grown) {
            yerror("sb_arena: cells realloc failed (%zu)", new_cap);
            return 0;
        }
        arena->cells = grown;
        arena->cells_cap = new_cap;
        arena->cells_head = arena->cells_used;
    }

    /* Steady phase: evict oldest until lines[] has a free slot and cells[]
     * has room. If a single line exceeds the entire cells_cap (extreme
     * resize), one-time grow rather than dropping data silently. */
    while (arena->lines_count >= arena->lines_cap || arena->cells_used + bytes > arena->cells_cap) {
        if (arena->lines_count == 0) {
            uint8_t *grown = realloc(arena->cells, bytes);
            if (!grown) {
                yerror("sb_arena: emergency cells realloc (%zu) failed", bytes);
                return 0;
            }
            arena->cells = grown;
            arena->cells_cap = bytes;
            break;
        }
        sb_arena_drop_oldest(arena);
    }

    /* Write payload, handling ring wrap. */
    if (arena->cells_head + bytes <= arena->cells_cap) {
        memcpy(arena->cells + arena->cells_head, src, bytes);
    } else {
        size_t first = arena->cells_cap - arena->cells_head;
        memcpy(arena->cells + arena->cells_head, src, first);
        memcpy(arena->cells, (const uint8_t *)src + first, bytes - first);
    }

    /* Record line descriptor. */
    arena->lines[arena->lines_head].offset = arena->cells_head;
    arena->lines[arena->lines_head].cols = cols;
    arena->lines_head = (arena->lines_head + 1) % arena->lines_cap;
    arena->cells_head = (arena->cells_head + bytes) % arena->cells_cap;
    arena->cells_used += bytes;
    arena->lines_count++;
    return 1;
}

/* Pop newest line. Copies up to copy_cols cells into dst. Returns the number
 * of cells copied (0 on empty). */
int yetty_yvterm_text_sb_arena_pop(struct yetty_yvterm_text_sb_arena *arena, VTermScreenCell *dst,
                                  int copy_cols)
{
    if (arena->lines_count == 0 || !dst || copy_cols <= 0) {
        return 0;
    }
    uint32_t newest = (arena->lines_head + arena->lines_cap - 1) % arena->lines_cap;
    int line_cols = arena->lines[newest].cols;
    int n = line_cols < copy_cols ? line_cols : copy_cols;
    yetty_yvterm_text_sb_arena_read(arena, arena->lines[newest].offset, n, dst);
    sb_arena_drop_newest(arena);
    return n;
}
