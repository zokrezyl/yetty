/*
 * scroll-tiers.h — tiered scroll buffer internals shared between grid.c (the
 * yclass model) and scroll-tiers.c (the archive engine). Private to yvterm.
 *
 *     newest ──────────────────────────────────────────────► oldest
 *     [ HOT: line ring        ][ WARM: lz4 segments in RAM ][ COLD: spill file ]
 *
 * The hot tier is the grid's line ring (visible rows + scrollback/hot-lines).
 * A line aging out of the ring is serialized into the open segment; sealed
 * segments are lz4-compressed and held in RAM under scrollback/warm-bytes;
 * beyond that they spill to a session-scoped temp file (unbounded by default).
 * Lines are addressed by TIMELINE index: a monotonically increasing count of
 * lines pushed out of the top of the primary screen since the session began.
 * The archive owns [dropped_lines, archived_lines); the ring owns
 * [archived_lines, archived_lines + ring history + visible rows).
 */
#ifndef YETTY_YVTERM_SCROLL_TIERS_H
#define YETTY_YVTERM_SCROLL_TIERS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>

#include "rich-store.h"

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvterm_text_cell;
struct yetty_ydraw_complex;
struct yetty_yvterm_line;
struct yetty_ycore_memtag;

YETTY_YRESULT_DECLARE(yetty_yvterm_line_ptr, struct yetty_yvterm_line *);

/* One slot in the scrolling line ring. The line owns its text cells and
 * anchors zero or more rich BLOCKS by generation-checked handle — a handle
 * lives on the block's BOTTOM owner line, so a block leaves the scrollback
 * only when its last covered line is evicted. The blocks themselves (records,
 * arena bytes, figure runtimes, span) live in the grid's rich store
 * (rich-store.h). The same struct backs the archive materialization cache, so
 * tier-resolved history lines flow through the very accessors the ring lines
 * use. */
struct yetty_yvterm_line {
    struct yetty_yvterm_text_cell *text_cells;

    struct yetty_yvterm_rich_handle *rich_blocks;
    uint32_t rich_block_count;
    uint32_t rich_block_capacity;

    /* Number of live rich blocks COVERING this line (their vertical span
     * includes it — the anchoring bottom line included). The O(1) guard for
     * the text hot path: putglyph/erase consult the rich model only when
     * this is nonzero. Maintained by block create/relocate/destroy; a zero
     * counter guarantees no block covers the line (saturating decrements —
     * a line recycled out from under a deep block re-enters at zero). Ring
     * lines only; cache lines stay at 0. */
    uint32_t rich_coverage_count;

    /* Nonzero when this cache line's figure runtimes were re-created from
     * retained envelopes for a scrolled-back view; holds the resolver stamp
     * of the last window that used them, so the sweep can destroy runtimes
     * whose lines left the view. Always 0 on ring (hot) lines. */
    uint32_t view_stamp;

    int dirty;

    /* 1 when this physical line is a soft-wrap continuation of the line above
     * it (the previous line filled up and the text flowed onto this one). Driven
     * from libvterm's per-line `continuation` flag and used by resize reflow to
     * tell a single wrapped logical line from two hard-separated lines. A blank
     * or freshly-reset line is never a continuation. */
    int continuation;
};

/* One screen buffer: a rolling ring of lines plus its scroll state. */
struct yetty_yvterm_screen {
    struct yetty_yvterm_line *lines;
    uint32_t line_count;
    uint32_t base;

    /* Absolute count of rows scrolled off the top of THIS screen — its
     * rolling-row origin. The top of the screen is at absolute row
     * `total_scrolled`; the cursor's absolute row is `total_scrolled +
     * cursor_row`. Anchored rich content (the ydraw ygrid) uses this to scroll
     * in lockstep with the text. */
    uint64_t total_scrolled;
};

/*===========================================================================
 * Archive: warm segments, cold spill, materialization cache.
 *=========================================================================*/

/* One sealed segment: `line_count` serialized lines starting at timeline
 * index `first_line`. Warm: `bytes` holds the lz4 blob in RAM. Cold: bytes is
 * NULL and the blob lives at `file_offset` in the spill file. */
struct yetty_yvterm_tier_segment {
    uint64_t first_line;
    uint32_t line_count;
    uint32_t raw_size;  /* serialized payload before compression */
    uint8_t *bytes;     /* lz4 blob (NULL once spilled) */
    uint32_t byte_size; /* lz4 blob size */
    uint32_t checksum;  /* FNV-1a of the lz4 blob */
    int on_disk;
    uint64_t file_offset;
};

/* The open (unsealed) segment: serialized lines accumulate raw until the
 * segment reaches its line/byte threshold and is sealed (compressed). */
struct yetty_yvterm_tier_builder {
    uint8_t *bytes;
    size_t byte_count;
    size_t byte_capacity;
    uint32_t *line_offsets; /* per-line byte offset into bytes */
    uint32_t line_count;
    uint32_t line_capacity;
    uint64_t first_line;
};

/* One inflated segment in the materialization cache: expanded lines served
 * to the view resolver. Re-created figure runtimes live on these lines and
 * die with the entry. */
struct yetty_yvterm_tier_cache_entry {
    int valid;
    /* Deferred release: the entry was evicted/invalidated while pinned by
     * the live view window — its lines (which window_lines may still point
     * into) stay allocated until the next window resolution frees zombies. */
    int zombie;
    /* Window generation that last resolved lines out of this entry. An entry
     * pinned to the LIVE generation is never re-used as an inflate victim
     * and never freed in place — continued PTY output must not pull lines
     * out from under the window being rendered (the scroll-back-while-
     * streaming crash). */
    uint32_t pin_stamp;
    uint64_t first_line;
    uint32_t line_count;
    struct yetty_yvterm_line *lines;
    uint64_t last_used_tick;
};

enum {
    YETTY_YVTERM_TIER_SEGMENT_MAX_LINES = 512,
    YETTY_YVTERM_TIER_SEGMENT_MAX_BYTES = 2u * 1024u * 1024u,
    YETTY_YVTERM_TIER_CACHE_ENTRIES = 4,
    /* v2: per-line rich content is a list of rich BLOCKS (span + records +
     * arena each) instead of the line-wide primitive/arena/span triple.
     * v3: record descriptors carry the accepted-update journal length and
     * the journals trail the block arena, so re-materialized figures replay
     * their updates.
     * v4: record descriptors carry the record kind and the complete paint
     * key (paint_z, paint_sequence, record_ordinal). Materialization
     * reproduces the stored key exactly — sequences are never re-minted —
     * and cross-checks the serialized kind against the wire record. Keys
     * are valid within the session epoch that minted them (archives are
     * session-scoped).
     * v5: record descriptors carry the record's FROZEN accumulated group
     * offset (two f32 words), baked from the group chain at serialize time.
     * The group tree itself is not archived — sealed content is immutable,
     * so the final projection is all that must survive; materialization
     * re-attaches the offset through a synthetic cache-local group. */
    YETTY_YVTERM_TIER_FORMAT_VERSION = 5,
};

struct yetty_yvterm_tiers {
    /* Sealed segments, oldest first. `segment_head` is the first live index
     * (dropped segments advance it; the array compacts when it gets bad). */
    struct yetty_yvterm_tier_segment *segments;
    uint32_t segment_head;
    uint32_t segment_count;
    uint32_t segment_capacity;

    struct yetty_yvterm_tier_builder builder;

    /* Timeline bookkeeping. archived_lines = total lines ever serialized =
     * timeline index of the oldest line still in the ring. dropped_lines =
     * timeline index of the oldest line still reachable (the scrollback
     * floor). */
    uint64_t archived_lines;
    uint64_t dropped_lines;

    /* Budgets. warm counts sealed RAM blobs + the open builder. file budget 0
     * = unlimited. total_line_cap 0 = unlimited (scrollback/lines). */
    size_t warm_bytes_used;
    size_t warm_bytes_budget;
    uint64_t file_bytes_budget;
    uint64_t total_line_cap;

    /* Cold spill file: session-scoped anonymous temp file (tmpfile), created
     * on first spill. spill_disabled latches after a failure (logged once) —
     * the tier then degrades to dropping its oldest segments. */
    FILE *spill_file;
    uint64_t spill_file_size;
    int spill_disabled;

    /* Anchored rich content older than this timeline index was cleared by a
     * full-clear (DCS YDRAW_CLEAR / RIS): inflate suppresses their arena
     * records and spans, so cleared figures do not resurrect on scroll-back.
     * Segments themselves are never rewritten. */
    uint64_t rich_clear_watermark;

    struct yetty_yvterm_tier_cache_entry cache[YETTY_YVTERM_TIER_CACHE_ENTRIES];
    uint64_t cache_tick;

    /* Optional per-owner byte accounting for the archive's long-lived
     * allocations (builder, sealed blobs, cache lines). NULL = untagged. */
    struct yetty_ycore_memtag *memtag;

    /* The live view-window generation (set by the grid each resolution).
     * Cache entries pinned to it are protected from eviction/release. */
    uint32_t live_pin_stamp;

    /* Borrowed from the owning grid: the rich-block store every line handle
     * resolves through (serialize on push, mint cache-local blocks on
     * inflate, destroy on cache teardown). */
    struct yetty_yvterm_rich_store *rich_store;
};

/* Line helpers owned by grid.c (they route through the rich store). */
void yetty_yvterm_line_release_rich(struct yetty_yvterm_rich_store *store,
                                    struct yetty_yvterm_line *line);
void yetty_yvterm_line_evict_figures(struct yetty_yvterm_rich_store *store,
                                     struct yetty_yvterm_line *line);

void yetty_yvterm_tiers_init(struct yetty_yvterm_tiers *tiers);
void yetty_yvterm_tiers_destroy(struct yetty_yvterm_tiers *tiers);

/* Serialize one line into the open segment and advance archived_lines. Seals
 * the open segment when full and enforces the warm/file/total budgets
 * (`retained_top` is the timeline index of the live screen top — the
 * reference the total-line cap counts back from). Does NOT touch the line's
 * complexes — the caller destroys runtimes first. */
struct yetty_ycore_void_result yetty_yvterm_tiers_push_line(struct yetty_yvterm_tiers *tiers,
                                                            const struct yetty_yvterm_line *line,
                                                            uint32_t used_cols,
                                                            uint32_t original_cols,
                                                            uint64_t retained_top);

/* Resolve one archived line by timeline index into the materialization
 * cache, inflating its segment on demand (from RAM or the spill file).
 * Returns NULL value (OK result) for indices outside the archive. The
 * returned pointer stays valid until the next cache eviction. `cols` is the
 * current grid width (lines materialize clipped/padded to it). */
struct yetty_yvterm_line_ptr_result yetty_yvterm_tiers_resolve_line(
    struct yetty_yvterm_tiers *tiers, uint64_t timeline_idx, uint32_t cols, uint32_t blank_fg,
    uint32_t blank_bg);

/* Direction-aware prefetch: make sure the segment holding `timeline_idx` is
 * cached (used for the segment adjacent to the view in scroll direction).
 * Quietly does nothing for out-of-archive indices. */
void yetty_yvterm_tiers_prefetch(struct yetty_yvterm_tiers *tiers, uint64_t timeline_idx,
                                 uint32_t cols, uint32_t blank_fg, uint32_t blank_bg);

/* Walk every line currently materialized in the cache. Used by the view
 * sweep (destroy figure runtimes whose lines left the window) and by
 * full-clear. */
typedef void (*yetty_yvterm_tiers_line_visit_fn)(struct yetty_yvterm_line *line,
                                                 uint64_t timeline_idx, void *userdata);
void yetty_yvterm_tiers_visit_cache(struct yetty_yvterm_tiers *tiers,
                                    yetty_yvterm_tiers_line_visit_fn visit, void *userdata);

/* Free zombie cache entries whose pin generation is no longer live. The grid
 * calls this at the START of each window resolution, right after advancing
 * the generation. */
void yetty_yvterm_tiers_release_zombies(struct yetty_yvterm_tiers *tiers);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_SCROLL_TIERS_H */
