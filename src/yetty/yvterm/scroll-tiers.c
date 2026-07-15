/*
 * scroll-tiers.c — the archive engine of the tiered scroll buffer: line
 * serialization, lz4-compressed warm segments, the session-scoped cold spill
 * file, and the bounded materialization cache the view resolver reads from.
 *
 * Everything here is line-serial: text cells as (repeat, cell) runs, the SDF
 * primitive arena verbatim (it already holds opaque u32 wire words, including
 * the retained composite envelopes), plus continuation flags and the original
 * width for a future view-time re-wrap. Segments are self-delimiting and
 * versioned (magic + version + checksum), so the same blob layout feeds the
 * warm tier, the cold file, and a future persistent session log.
 *
 * Figure runtimes never live here — the caller destroys them before a line is
 * pushed, and the grid re-materializes them from the retained envelopes when
 * a cached line scrolls back into view.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <lz4.h>

#include <yetty/ycore/memtag.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvterm/grid.h>

#include "scroll-tiers.h"

/* Composite-envelope type test — hand-declared from ydraw-core (same
 * header-clash avoidance as grid.c). */
bool yetty_ydraw_is_composite(uint32_t type);

/* Serialized line header, in u32 words. */
enum {
    LINE_WORD_FLAGS = 0,
    LINE_WORD_STORED_COLS = 1,
    LINE_WORD_ORIGINAL_COLS = 2,
    LINE_WORD_RUN_COUNT = 3,
    LINE_WORD_PRIMITIVE_COUNT = 4,
    LINE_WORD_ARENA_COUNT = 5,
    LINE_WORD_RICH_SPAN = 6,
    /* Total combining-mark words trailing the cell runs (one block per run
     * whose representative cell carries marks). Kept in the header so a line
     * record's size can be validated before walking the variable-length
     * runs. */
    LINE_WORD_MARK_WORDS = 7,
    LINE_HEADER_WORDS = 8,
    LINE_FLAG_CONTINUATION = 1u << 0,
    CELL_RUN_WORDS = 5,
    /* A run's cell mark_count is packed into the high byte of its repeat
     * word; the low 24 bits hold the repeat count (always ≥ terminal cols,
     * far under 2^24). The marks follow the run's CELL_RUN_WORDS. */
    CELL_RUN_REPEAT_MASK = 0x00FFFFFFu,
    CELL_RUN_MARK_SHIFT = 24,
};

/* Spill-file record header (self-delimiting, one per sealed segment). */
struct tier_file_record_header {
    uint32_t magic;
    uint32_t version;
    uint64_t first_line;
    uint32_t line_count;
    uint32_t raw_size;
    uint32_t compressed_size;
    uint32_t checksum;
};

#define TIER_FILE_MAGIC 0x31425359u /* "YSB1" little-endian */

static uint32_t tier_checksum(const uint8_t *bytes, size_t length)
{
    uint32_t hash = 2166136261u; /* FNV-1a 32 */
    for (size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 16777619u;
    }
    return hash;
}

void yetty_yvterm_tiers_init(struct yetty_yvterm_tiers *tiers)
{
    memset(tiers, 0, sizeof(*tiers));
}

static void tier_cache_entry_free_lines(struct yetty_yvterm_tiers *tiers,
                                        struct yetty_yvterm_tier_cache_entry *entry)
{
    for (uint32_t index = 0; index < entry->line_count; ++index) {
        yetty_yvterm_line_release_rich(&entry->lines[index]);
        yetty_ycore_memtag_free(tiers->memtag, entry->lines[index].text_cells);
    }
    yetty_ycore_memtag_free(tiers->memtag, entry->lines);
    entry->lines = NULL;
    entry->line_count = 0;
    entry->valid = 0;
    entry->zombie = 0;
}

static void tier_cache_entry_release(struct yetty_yvterm_tiers *tiers,
                                     struct yetty_yvterm_tier_cache_entry *entry)
{
    if (!entry->valid) {
        return;
    }
    if (entry->pin_stamp && entry->pin_stamp == tiers->live_pin_stamp) {
        /* The live window resolved lines out of this entry — freeing them now
         * would leave the renderer's window_lines dangling (the scroll-back-
         * while-streaming crash). Hide the entry from lookups and defer the
         * free to the next window resolution. */
        entry->valid = 0;
        entry->zombie = 1;
        return;
    }
    tier_cache_entry_free_lines(tiers, entry);
}

/* Free zombies from earlier window generations (grid calls this right after
 * advancing the generation, so nothing pinned-live is ever touched). */
void yetty_yvterm_tiers_release_zombies(struct yetty_yvterm_tiers *tiers)
{
    for (uint32_t index = 0; index < YETTY_YVTERM_TIER_CACHE_ENTRIES; ++index) {
        struct yetty_yvterm_tier_cache_entry *entry = &tiers->cache[index];
        if (entry->zombie && entry->pin_stamp != tiers->live_pin_stamp) {
            tier_cache_entry_free_lines(tiers, entry);
        }
    }
}

void yetty_yvterm_tiers_destroy(struct yetty_yvterm_tiers *tiers)
{
    tiers->live_pin_stamp = 0; /* nothing is live at teardown */
    for (uint32_t index = 0; index < YETTY_YVTERM_TIER_CACHE_ENTRIES; ++index) {
        if (tiers->cache[index].zombie) {
            tier_cache_entry_free_lines(tiers, &tiers->cache[index]);
        } else {
            tier_cache_entry_release(tiers, &tiers->cache[index]);
        }
    }
    for (uint32_t index = tiers->segment_head; index < tiers->segment_count; ++index) {
        yetty_ycore_memtag_free(tiers->memtag, tiers->segments[index].bytes);
    }
    yetty_ycore_memtag_free(tiers->memtag, tiers->segments);
    yetty_ycore_memtag_free(tiers->memtag, tiers->builder.bytes);
    yetty_ycore_memtag_free(tiers->memtag, tiers->builder.line_offsets);
    if (tiers->spill_file) {
        fclose(tiers->spill_file); /* tmpfile — vanishes with the close */
    }
    memset(tiers, 0, sizeof(*tiers));
}

/*===========================================================================
 * Open-segment builder.
 *=========================================================================*/

static struct yetty_ycore_void_result builder_reserve(struct yetty_yvterm_tiers *tiers,
                                                      size_t need_bytes)
{
    struct yetty_yvterm_tier_builder *builder = &tiers->builder;
    if (builder->byte_count + need_bytes <= builder->byte_capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_capacity = builder->byte_capacity ? builder->byte_capacity : 16384u;
    while (new_capacity < builder->byte_count + need_bytes) {
        new_capacity *= 2u;
    }
    uint8_t *grown = yetty_ycore_memtag_realloc(tiers->memtag, builder->bytes, new_capacity);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: builder grow failed");
    }
    builder->bytes = grown;
    builder->byte_capacity = new_capacity;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result builder_reserve_line_slot(struct yetty_yvterm_tiers *tiers)
{
    struct yetty_yvterm_tier_builder *builder = &tiers->builder;
    if (builder->line_count < builder->line_capacity) {
        return YETTY_OK_VOID();
    }
    uint32_t new_capacity = builder->line_capacity ? builder->line_capacity * 2u : 64u;
    uint32_t *grown = yetty_ycore_memtag_realloc(tiers->memtag, builder->line_offsets,
                                                 (size_t)new_capacity * sizeof(uint32_t));
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: builder line table grow failed");
    }
    builder->line_offsets = grown;
    builder->line_capacity = new_capacity;
    return YETTY_OK_VOID();
}

/* Cells equal for run-length purposes (glyph_index is renderer-derived and
 * deliberately not stored). */
static int cells_equal(const struct yetty_yvterm_text_cell *left,
                       const struct yetty_yvterm_text_cell *right)
{
    if (left->codepoint != right->codepoint || left->fg != right->fg || left->bg != right->bg ||
        left->attrs != right->attrs || left->width != right->width || left->flags != right->flags ||
        left->mark_count != right->mark_count) {
        return 0;
    }
    for (uint8_t mark = 0; mark < left->mark_count; ++mark) {
        if (left->marks[mark] != right->marks[mark]) {
            return 0;
        }
    }
    return 1;
}

static struct yetty_ycore_void_result builder_push_line(struct yetty_yvterm_tiers *tiers,
                                                        const struct yetty_yvterm_line *line,
                                                        uint32_t used_cols, uint32_t original_cols)
{
    struct yetty_yvterm_tier_builder *builder = &tiers->builder;
    /* Count cell runs (and the combining-mark words they trail) first so the
     * record size is exact. */
    uint32_t run_count = 0;
    uint32_t mark_words = 0;
    for (uint32_t col = 0; col < used_cols;) {
        uint32_t run_end = col + 1u;
        while (run_end < used_cols &&
               cells_equal(&line->text_cells[col], &line->text_cells[run_end])) {
            run_end++;
        }
        run_count++;
        mark_words += line->text_cells[col].mark_count;
        col = run_end;
    }

    size_t word_count = LINE_HEADER_WORDS + (size_t)run_count * CELL_RUN_WORDS +
                        (size_t)mark_words + (size_t)line->primitive_count * 2u + line->arena_count;
    struct yetty_ycore_void_result reserve_res =
        builder_reserve(tiers, word_count * sizeof(uint32_t));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "scroll tiers: push_line reserve");
    struct yetty_ycore_void_result slot_res = builder_reserve_line_slot(tiers);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_res, "scroll tiers: push_line slot");

    builder->line_offsets[builder->line_count++] = (uint32_t)builder->byte_count;
    uint32_t *words = (uint32_t *)(builder->bytes + builder->byte_count);
    builder->byte_count += word_count * sizeof(uint32_t);

    words[LINE_WORD_FLAGS] = line->continuation ? LINE_FLAG_CONTINUATION : 0u;
    words[LINE_WORD_STORED_COLS] = used_cols;
    words[LINE_WORD_ORIGINAL_COLS] = original_cols;
    words[LINE_WORD_RUN_COUNT] = run_count;
    words[LINE_WORD_PRIMITIVE_COUNT] = line->primitive_count;
    words[LINE_WORD_ARENA_COUNT] = line->arena_count;
    words[LINE_WORD_RICH_SPAN] = line->rich_span_rows;
    words[LINE_WORD_MARK_WORDS] = mark_words;
    uint32_t *cursor = words + LINE_HEADER_WORDS;

    for (uint32_t col = 0; col < used_cols;) {
        uint32_t run_end = col + 1u;
        while (run_end < used_cols &&
               cells_equal(&line->text_cells[col], &line->text_cells[run_end])) {
            run_end++;
        }
        const struct yetty_yvterm_text_cell *cell = &line->text_cells[col];
        cursor[0] = (run_end - col) | ((uint32_t)cell->mark_count << CELL_RUN_MARK_SHIFT);
        cursor[1] = cell->codepoint;
        cursor[2] = cell->fg;
        cursor[3] = cell->bg;
        cursor[4] =
            (uint32_t)cell->attrs | ((uint32_t)cell->width << 16) | ((uint32_t)cell->flags << 24);
        cursor += CELL_RUN_WORDS;
        for (uint8_t mark = 0; mark < cell->mark_count; ++mark) {
            *cursor++ = cell->marks[mark];
        }
        col = run_end;
    }
    for (uint32_t index = 0; index < line->primitive_count; ++index) {
        cursor[0] = line->primitives[index].arena_offset;
        cursor[1] = line->primitives[index].word_count;
        cursor += 2;
    }
    if (line->arena_count) {
        memcpy(cursor, line->arena, (size_t)line->arena_count * sizeof(uint32_t));
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Sealing, budgets, spill.
 *=========================================================================*/

static struct yetty_ycore_void_result tiers_append_segment(struct yetty_yvterm_tiers *tiers,
                                                           struct yetty_yvterm_tier_segment segment)
{
    /* Compact away the dropped head when it dominates the array. */
    if (tiers->segment_head > 64u && tiers->segment_head * 2u > tiers->segment_count) {
        memmove(tiers->segments, tiers->segments + tiers->segment_head,
                (size_t)(tiers->segment_count - tiers->segment_head) *
                    sizeof(struct yetty_yvterm_tier_segment));
        tiers->segment_count -= tiers->segment_head;
        tiers->segment_head = 0;
    }
    if (tiers->segment_count == tiers->segment_capacity) {
        uint32_t new_capacity = tiers->segment_capacity ? tiers->segment_capacity * 2u : 16u;
        struct yetty_yvterm_tier_segment *grown = yetty_ycore_memtag_realloc(
            tiers->memtag, tiers->segments,
            (size_t)new_capacity * sizeof(struct yetty_yvterm_tier_segment));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: segment table grow failed");
        }
        tiers->segments = grown;
        tiers->segment_capacity = new_capacity;
    }
    tiers->segments[tiers->segment_count++] = segment;
    return YETTY_OK_VOID();
}

/* Compress and append the open segment to the warm list. */
static struct yetty_ycore_void_result tiers_seal_open_segment(struct yetty_yvterm_tiers *tiers)
{
    struct yetty_yvterm_tier_builder *builder = &tiers->builder;
    if (builder->line_count == 0) {
        return YETTY_OK_VOID();
    }

    /* Raw layout: [directory: line_count u32 offsets][payload bytes]. */
    size_t directory_size = (size_t)builder->line_count * sizeof(uint32_t);
    size_t raw_size = directory_size + builder->byte_count;
    uint8_t *raw = malloc(raw_size);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: seal raw alloc failed");
    }
    memcpy(raw, builder->line_offsets, directory_size);
    memcpy(raw + directory_size, builder->bytes, builder->byte_count);

    int compress_bound = LZ4_compressBound((int)raw_size);
    uint8_t *compressed = yetty_ycore_memtag_alloc(tiers->memtag, (size_t)compress_bound);
    if (!compressed) {
        free(raw);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: seal lz4 alloc failed");
    }
    int compressed_size =
        LZ4_compress_default((const char *)raw, (char *)compressed, (int)raw_size, compress_bound);
    free(raw);
    if (compressed_size <= 0) {
        yetty_ycore_memtag_free(tiers->memtag, compressed);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: lz4 compression failed");
    }
    /* Give back the compression slack. */
    uint8_t *shrunk =
        yetty_ycore_memtag_realloc(tiers->memtag, compressed, (size_t)compressed_size);
    if (shrunk) {
        compressed = shrunk;
    }

    struct yetty_yvterm_tier_segment segment = {
        .first_line = builder->first_line,
        .line_count = builder->line_count,
        .raw_size = (uint32_t)raw_size,
        .bytes = compressed,
        .byte_size = (uint32_t)compressed_size,
        .checksum = tier_checksum(compressed, (size_t)compressed_size),
        .on_disk = 0,
        .file_offset = 0,
    };
    struct yetty_ycore_void_result append_res = tiers_append_segment(tiers, segment);
    if (YETTY_IS_ERR(append_res)) {
        yetty_ycore_memtag_free(tiers->memtag, compressed);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: seal append", append_res);
    }
    tiers->warm_bytes_used += (size_t)compressed_size;

    ydebug("scroll tiers: sealed segment [%" PRIu64 " +%u) raw=%zu lz4=%d warm_used=%zu",
           builder->first_line, builder->line_count, raw_size, compressed_size,
           tiers->warm_bytes_used);

    builder->byte_count = 0;
    builder->line_count = 0;
    builder->first_line = tiers->archived_lines;
    return YETTY_OK_VOID();
}

/* Move one warm segment's blob into the spill file (synchronous append; the
 * write is a page-cached fwrite of an lz4 blob every few hundred lines). On
 * any failure the spill latches disabled and the tier degrades to dropping
 * its oldest segments — today's semantics, logged once. */
static int tiers_spill_segment(struct yetty_yvterm_tiers *tiers,
                               struct yetty_yvterm_tier_segment *segment)
{
    if (tiers->spill_disabled) {
        return 0;
    }
    if (!tiers->spill_file) {
        tiers->spill_file = tmpfile();
        if (!tiers->spill_file) {
            tiers->spill_disabled = 1;
            ywarn("scroll tiers: spill file creation failed — cold tier disabled, oldest "
                  "history will be dropped at the warm budget");
            return 0;
        }
    }
    if (tiers->file_bytes_budget &&
        tiers->spill_file_size + sizeof(struct tier_file_record_header) + segment->byte_size >
            tiers->file_bytes_budget) {
        return 0; /* disk cap reached — caller drops instead */
    }
    struct tier_file_record_header header = {
        .magic = TIER_FILE_MAGIC,
        .version = YETTY_YVTERM_TIER_FORMAT_VERSION,
        .first_line = segment->first_line,
        .line_count = segment->line_count,
        .raw_size = segment->raw_size,
        .compressed_size = segment->byte_size,
        .checksum = segment->checksum,
    };
    if (fseek(tiers->spill_file, (long)tiers->spill_file_size, SEEK_SET) != 0 ||
        fwrite(&header, sizeof(header), 1, tiers->spill_file) != 1 ||
        fwrite(segment->bytes, 1, segment->byte_size, tiers->spill_file) != segment->byte_size) {
        tiers->spill_disabled = 1;
        ywarn("scroll tiers: spill write failed — cold tier disabled, oldest history will be "
              "dropped at the warm budget");
        return 0;
    }
    segment->on_disk = 1;
    segment->file_offset = tiers->spill_file_size + sizeof(header);
    tiers->spill_file_size += sizeof(header) + segment->byte_size;
    tiers->warm_bytes_used -= segment->byte_size;
    yetty_ycore_memtag_free(tiers->memtag, segment->bytes);
    segment->bytes = NULL;
    ydebug("scroll tiers: spilled segment [%" PRIu64 " +%u) to disk at %" PRIu64 " (file=%" PRIu64
           "B warm_used=%zu)",
           segment->first_line, segment->line_count, segment->file_offset, tiers->spill_file_size,
           tiers->warm_bytes_used);
    return 1;
}

/* Drop the oldest segment entirely (advances the scrollback floor). */
static void tiers_drop_oldest_segment(struct yetty_yvterm_tiers *tiers)
{
    if (tiers->segment_head >= tiers->segment_count) {
        return;
    }
    struct yetty_yvterm_tier_segment *segment = &tiers->segments[tiers->segment_head];
    if (!segment->on_disk && segment->bytes) {
        tiers->warm_bytes_used -= segment->byte_size;
        yetty_ycore_memtag_free(tiers->memtag, segment->bytes);
        segment->bytes = NULL;
    }
    tiers->dropped_lines = segment->first_line + segment->line_count;
    tiers->segment_head++;
    /* Invalidate any cache entry inflated from the dropped segment. */
    for (uint32_t index = 0; index < YETTY_YVTERM_TIER_CACHE_ENTRIES; ++index) {
        struct yetty_yvterm_tier_cache_entry *entry = &tiers->cache[index];
        if (entry->valid && entry->first_line < tiers->dropped_lines) {
            tier_cache_entry_release(tiers, entry);
        }
    }
}

static void tiers_enforce_budgets(struct yetty_yvterm_tiers *tiers, uint64_t live_top)
{
    /* Total retention cap (scrollback/lines, 0 = unlimited): drop whole
     * oldest segments once every one of their lines is beyond the cap. */
    if (tiers->total_line_cap) {
        while (tiers->segment_head < tiers->segment_count) {
            struct yetty_yvterm_tier_segment *oldest = &tiers->segments[tiers->segment_head];
            uint64_t segment_end = oldest->first_line + oldest->line_count;
            if (live_top - segment_end < tiers->total_line_cap) {
                break;
            }
            tiers_drop_oldest_segment(tiers);
        }
    }

    /* Warm byte budget: move the oldest RAM blobs to disk; drop when the
     * cold tier is unavailable or capped. */
    while (tiers->warm_bytes_used > tiers->warm_bytes_budget) {
        uint32_t oldest_warm = tiers->segment_head;
        while (oldest_warm < tiers->segment_count && tiers->segments[oldest_warm].on_disk) {
            oldest_warm++;
        }
        if (oldest_warm >= tiers->segment_count) {
            break; /* only the open builder is left over budget — let it seal */
        }
        if (!tiers_spill_segment(tiers, &tiers->segments[oldest_warm])) {
            /* Degrade path: no disk — drop the oldest history instead. Only
             * whole segments drop, oldest first; anything newer stays warm. */
            if (oldest_warm == tiers->segment_head) {
                tiers_drop_oldest_segment(tiers);
            } else {
                break; /* oldest RAM blob is not the oldest segment — stop */
            }
        }
    }
}

struct yetty_ycore_void_result yetty_yvterm_tiers_push_line(struct yetty_yvterm_tiers *tiers,
                                                            const struct yetty_yvterm_line *line,
                                                            uint32_t used_cols,
                                                            uint32_t original_cols,
                                                            uint64_t retained_top)
{
    if (tiers->builder.line_count == 0 && tiers->builder.byte_count == 0) {
        tiers->builder.first_line = tiers->archived_lines;
    }
    size_t builder_bytes_before = tiers->builder.byte_count;
    struct yetty_ycore_void_result push_res =
        builder_push_line(tiers, line, used_cols, original_cols);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, push_res, "scroll tiers: push line");
    tiers->warm_bytes_used += tiers->builder.byte_count - builder_bytes_before;
    tiers->archived_lines++;

    if (tiers->builder.line_count >= YETTY_YVTERM_TIER_SEGMENT_MAX_LINES ||
        tiers->builder.byte_count >= YETTY_YVTERM_TIER_SEGMENT_MAX_BYTES) {
        size_t open_bytes = tiers->builder.byte_count;
        struct yetty_ycore_void_result seal_res = tiers_seal_open_segment(tiers);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, seal_res, "scroll tiers: seal");
        tiers->warm_bytes_used -= open_bytes; /* raw open bytes replaced by the lz4 blob */
    }
    /* Budgets are cheap no-ops when nothing is over; enforcing on every push
     * keeps the total-line cap exact even when it is smaller than a segment. */
    tiers_enforce_budgets(tiers, retained_top);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Materialization cache + resolver.
 *=========================================================================*/

static struct yetty_yvterm_tier_segment *tiers_find_segment(struct yetty_yvterm_tiers *tiers,
                                                            uint64_t timeline_idx)
{
    uint32_t low = tiers->segment_head;
    uint32_t high = tiers->segment_count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        struct yetty_yvterm_tier_segment *segment = &tiers->segments[middle];
        if (timeline_idx < segment->first_line) {
            high = middle;
        } else if (timeline_idx >= segment->first_line + segment->line_count) {
            low = middle + 1u;
        } else {
            return segment;
        }
    }
    return NULL;
}

/* Materialize one serialized line into an expanded cache line at the current
 * width. Longer stored lines clip (the archive is never rewritten; view-time
 * re-wrap is a future refinement — the continuation flag and original width
 * are already stored for it). */
static struct yetty_ycore_void_result tier_inflate_line(struct yetty_yvterm_tiers *tiers,
                                                        const uint32_t *words, size_t word_count,
                                                        struct yetty_yvterm_line *line,
                                                        uint32_t cols, uint32_t blank_fg,
                                                        uint32_t blank_bg, int suppress_rich)
{
    if (word_count < LINE_HEADER_WORDS) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: truncated line record");
    }
    uint32_t run_count = words[LINE_WORD_RUN_COUNT];
    uint32_t primitive_count = words[LINE_WORD_PRIMITIVE_COUNT];
    uint32_t arena_count = words[LINE_WORD_ARENA_COUNT];
    uint32_t mark_words = words[LINE_WORD_MARK_WORDS];
    if (LINE_HEADER_WORDS + (size_t)run_count * CELL_RUN_WORDS + (size_t)mark_words +
            (size_t)primitive_count * 2u + arena_count >
        word_count) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: corrupt line record");
    }

    line->text_cells = yetty_ycore_memtag_calloc(tiers->memtag, cols ? cols : 1u,
                                                 sizeof(struct yetty_yvterm_text_cell));
    if (!line->text_cells) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: cache line cells alloc");
    }
    for (uint32_t col = 0; col < cols; ++col) {
        line->text_cells[col].fg = blank_fg;
        line->text_cells[col].bg = blank_bg;
        line->text_cells[col].width = 1;
    }
    line->continuation = (words[LINE_WORD_FLAGS] & LINE_FLAG_CONTINUATION) ? 1 : 0;
    line->dirty = 1;

    const uint32_t *cursor = words + LINE_HEADER_WORDS;
    uint32_t col = 0;
    for (uint32_t run = 0; run < run_count; ++run) {
        uint32_t repeat = cursor[0] & CELL_RUN_REPEAT_MASK;
        uint8_t mark_count = (uint8_t)((cursor[0] >> CELL_RUN_MARK_SHIFT) & 0xFFu);
        if (mark_count > YETTY_YVTERM_CELL_MAX_MARKS) {
            mark_count = YETTY_YVTERM_CELL_MAX_MARKS; /* defend against a corrupt byte */
        }
        const uint32_t *run_marks = cursor + CELL_RUN_WORDS;
        for (uint32_t step = 0; step < repeat; ++step, ++col) {
            if (col >= cols) {
                break; /* narrower view — clip */
            }
            struct yetty_yvterm_text_cell *cell = &line->text_cells[col];
            cell->glyph_index = 0;
            cell->codepoint = cursor[1];
            cell->fg = cursor[2];
            cell->bg = cursor[3];
            cell->attrs = (uint16_t)(cursor[4] & 0xFFFFu);
            cell->width = (uint8_t)((cursor[4] >> 16) & 0xFFu);
            cell->flags = (uint8_t)((cursor[4] >> 24) & 0xFFu);
            cell->mark_count = mark_count;
            for (uint8_t mark = 0; mark < mark_count; ++mark) {
                cell->marks[mark] = run_marks[mark];
            }
        }
        cursor += CELL_RUN_WORDS + mark_count;
    }

    if (suppress_rich || (primitive_count == 0 && arena_count == 0)) {
        return YETTY_OK_VOID();
    }
    line->rich_span_rows = words[LINE_WORD_RICH_SPAN];
    line->primitives =
        calloc(primitive_count ? primitive_count : 1u, sizeof(struct yetty_yvterm_primitive));
    line->arena = arena_count ? malloc((size_t)arena_count * sizeof(uint32_t)) : NULL;
    if (!line->primitives || (arena_count && !line->arena)) {
        free(line->primitives);
        free(line->arena);
        line->primitives = NULL;
        line->arena = NULL;
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: cache line rich alloc");
    }
    line->primitive_capacity = primitive_count;
    line->arena_capacity = arena_count;
    for (uint32_t index = 0; index < primitive_count; ++index) {
        line->primitives[index].arena_offset = cursor[0];
        line->primitives[index].word_count = cursor[1];
        cursor += 2;
    }
    if (arena_count) {
        memcpy(line->arena, cursor, (size_t)arena_count * sizeof(uint32_t));
        line->arena_count = arena_count;
    }
    line->primitive_count = primitive_count;
    for (uint32_t index = 0; index < primitive_count; ++index) {
        const struct yetty_yvterm_primitive *record = &line->primitives[index];
        if (record->word_count && record->arena_offset < line->arena_count &&
            yetty_ydraw_is_composite(line->arena[record->arena_offset])) {
            line->envelope_count++;
        }
    }
    return YETTY_OK_VOID();
}

/* Fetch a segment's raw (decompressed) bytes: from the RAM blob, or from the
 * spill file. Caller frees. */
static struct yetty_ycore_void_result tier_segment_raw(
    struct yetty_yvterm_tiers *tiers, const struct yetty_yvterm_tier_segment *segment,
    uint8_t **out_raw)
{
    uint8_t *compressed = segment->bytes;
    uint8_t *read_buffer = NULL;
    if (!compressed) {
        if (!tiers->spill_file) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: segment lost (no spill file)");
        }
        read_buffer = malloc(segment->byte_size);
        if (!read_buffer) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: spill read alloc");
        }
        if (fseek(tiers->spill_file, (long)segment->file_offset, SEEK_SET) != 0 ||
            fread(read_buffer, 1, segment->byte_size, tiers->spill_file) != segment->byte_size) {
            free(read_buffer);
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: spill read failed");
        }
        compressed = read_buffer;
    }
    if (tier_checksum(compressed, segment->byte_size) != segment->checksum) {
        free(read_buffer);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: segment checksum mismatch");
    }
    uint8_t *raw = malloc(segment->raw_size);
    if (!raw) {
        free(read_buffer);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: inflate alloc");
    }
    int inflated = LZ4_decompress_safe((const char *)compressed, (char *)raw,
                                       (int)segment->byte_size, (int)segment->raw_size);
    free(read_buffer);
    if (inflated != (int)segment->raw_size) {
        free(raw);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: lz4 inflate failed");
    }
    *out_raw = raw;
    return YETTY_OK_VOID();
}

/* Inflate a sealed segment into a cache entry (evicting the LRU one). */
static struct yetty_ycore_void_result tier_cache_inflate(
    struct yetty_yvterm_tiers *tiers, struct yetty_yvterm_tier_segment *segment, uint32_t cols,
    uint32_t blank_fg, uint32_t blank_bg, struct yetty_yvterm_tier_cache_entry **out_entry)
{
    struct yetty_yvterm_tier_cache_entry *victim = NULL;
    for (uint32_t index = 0; index < YETTY_YVTERM_TIER_CACHE_ENTRIES; ++index) {
        struct yetty_yvterm_tier_cache_entry *entry = &tiers->cache[index];
        if (entry->zombie) {
            continue; /* still owed to a previous window's pointers */
        }
        if (!entry->valid) {
            victim = entry;
            break;
        }
        if (entry->pin_stamp && entry->pin_stamp == tiers->live_pin_stamp) {
            continue; /* pinned by the window being resolved right now */
        }
        if (!victim || entry->last_used_tick < victim->last_used_tick) {
            victim = entry;
        }
    }
    if (!victim) {
        /* Every slot is pinned or zombie — do not pull lines out from under
         * the live window; the caller renders this row blank instead. */
        *out_entry = NULL;
        return YETTY_OK_VOID();
    }
    tier_cache_entry_release(tiers, victim);
    if (victim->zombie) {
        /* Release deferred it (pinned) — cannot re-use this slot either. */
        *out_entry = NULL;
        return YETTY_OK_VOID();
    }

    uint8_t *raw = NULL;
    struct yetty_ycore_void_result raw_res = tier_segment_raw(tiers, segment, &raw);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, raw_res, "scroll tiers: segment raw");

    const uint32_t *directory = (const uint32_t *)raw;
    const uint8_t *payload = raw + (size_t)segment->line_count * sizeof(uint32_t);
    size_t payload_size = segment->raw_size - (size_t)segment->line_count * sizeof(uint32_t);

    struct yetty_yvterm_line *lines =
        yetty_ycore_memtag_calloc(tiers->memtag, segment->line_count, sizeof(*lines));
    if (!lines) {
        free(raw);
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: cache lines alloc");
    }
    for (uint32_t index = 0; index < segment->line_count; ++index) {
        size_t offset = directory[index];
        size_t next_offset =
            index + 1u < segment->line_count ? directory[index + 1u] : payload_size;
        if (offset > next_offset || next_offset > payload_size) {
            for (uint32_t undo = 0; undo < index; ++undo) {
                yetty_yvterm_line_release_rich(&lines[undo]);
                yetty_ycore_memtag_free(tiers->memtag, lines[undo].text_cells);
            }
            yetty_ycore_memtag_free(tiers->memtag, lines);
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: corrupt segment directory");
        }
        int suppress_rich = segment->first_line + index < tiers->rich_clear_watermark;
        struct yetty_ycore_void_result line_res = tier_inflate_line(
            tiers, (const uint32_t *)(payload + offset), (next_offset - offset) / sizeof(uint32_t),
            &lines[index], cols, blank_fg, blank_bg, suppress_rich);
        if (YETTY_IS_ERR(line_res)) {
            for (uint32_t undo = 0; undo < index; ++undo) {
                yetty_yvterm_line_release_rich(&lines[undo]);
                yetty_ycore_memtag_free(tiers->memtag, lines[undo].text_cells);
            }
            yetty_ycore_memtag_free(tiers->memtag, lines);
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: inflate line", line_res);
        }
    }
    free(raw);

    victim->valid = 1;
    victim->zombie = 0;
    victim->pin_stamp = tiers->live_pin_stamp;
    victim->first_line = segment->first_line;
    victim->line_count = segment->line_count;
    victim->lines = lines;
    victim->last_used_tick = ++tiers->cache_tick;
    ydebug("scroll tiers: inflated segment [%" PRIu64 " +%u) into the view cache%s",
           segment->first_line, segment->line_count, segment->on_disk ? " (from disk)" : "");
    *out_entry = victim;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tier_cache_lookup(
    struct yetty_yvterm_tiers *tiers, uint64_t timeline_idx, uint32_t cols, uint32_t blank_fg,
    uint32_t blank_bg, struct yetty_yvterm_tier_cache_entry **out_entry)
{
    *out_entry = NULL;
    for (uint32_t index = 0; index < YETTY_YVTERM_TIER_CACHE_ENTRIES; ++index) {
        struct yetty_yvterm_tier_cache_entry *entry = &tiers->cache[index];
        if (entry->valid && timeline_idx >= entry->first_line &&
            timeline_idx < entry->first_line + entry->line_count) {
            entry->last_used_tick = ++tiers->cache_tick;
            entry->pin_stamp = tiers->live_pin_stamp;
            *out_entry = entry;
            return YETTY_OK_VOID();
        }
    }
    struct yetty_yvterm_tier_segment *segment = tiers_find_segment(tiers, timeline_idx);
    if (!segment) {
        return YETTY_OK_VOID(); /* open segment / dropped / out of range */
    }
    return tier_cache_inflate(tiers, segment, cols, blank_fg, blank_bg, out_entry);
}

/* The open (unsealed) segment is directly addressable too: seal-on-demand
 * would churn, so its lines materialize straight from the raw builder bytes
 * into a transient cache entry keyed like a segment. Simplest correct
 * approach: seal it — the builder is only ever a fraction of a segment and
 * sealing keeps one code path. */
static struct yetty_ycore_void_result tiers_seal_for_view(struct yetty_yvterm_tiers *tiers)
{
    size_t open_bytes = tiers->builder.byte_count;
    if (tiers->builder.line_count == 0) {
        return YETTY_OK_VOID();
    }
    struct yetty_ycore_void_result seal_res = tiers_seal_open_segment(tiers);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, seal_res, "scroll tiers: seal for view");
    tiers->warm_bytes_used -= open_bytes;
    tiers_enforce_budgets(tiers, tiers->archived_lines);
    return YETTY_OK_VOID();
}

struct yetty_yvterm_line_ptr_result yetty_yvterm_tiers_resolve_line(
    struct yetty_yvterm_tiers *tiers, uint64_t timeline_idx, uint32_t cols, uint32_t blank_fg,
    uint32_t blank_bg)
{
    if (timeline_idx < tiers->dropped_lines || timeline_idx >= tiers->archived_lines) {
        return YETTY_OK(yetty_yvterm_line_ptr, NULL);
    }
    /* A view can land inside the open segment (fresh lines not yet sealed). */
    if (tiers->builder.line_count &&
        timeline_idx >= tiers->archived_lines - tiers->builder.line_count) {
        struct yetty_ycore_void_result seal_res = tiers_seal_for_view(tiers);
        YETTY_RETURN_IF_ERR(yetty_yvterm_line_ptr, seal_res, "scroll tiers: resolve seal");
    }
    struct yetty_yvterm_tier_cache_entry *entry = NULL;
    struct yetty_ycore_void_result lookup_res =
        tier_cache_lookup(tiers, timeline_idx, cols, blank_fg, blank_bg, &entry);
    YETTY_RETURN_IF_ERR(yetty_yvterm_line_ptr, lookup_res, "scroll tiers: resolve lookup");
    if (!entry) {
        return YETTY_OK(yetty_yvterm_line_ptr, NULL);
    }
    return YETTY_OK(yetty_yvterm_line_ptr, &entry->lines[timeline_idx - entry->first_line]);
}

void yetty_yvterm_tiers_prefetch(struct yetty_yvterm_tiers *tiers, uint64_t timeline_idx,
                                 uint32_t cols, uint32_t blank_fg, uint32_t blank_bg)
{
    if (timeline_idx < tiers->dropped_lines || timeline_idx >= tiers->archived_lines) {
        return;
    }
    struct yetty_yvterm_tier_cache_entry *entry = NULL;
    struct yetty_ycore_void_result lookup_res =
        tier_cache_lookup(tiers, timeline_idx, cols, blank_fg, blank_bg, &entry);
    if (YETTY_IS_ERR(lookup_res)) {
        yetty_ycore_error_destroy(lookup_res.error); /* prefetch is best-effort */
    }
}

void yetty_yvterm_tiers_visit_cache(struct yetty_yvterm_tiers *tiers,
                                    yetty_yvterm_tiers_line_visit_fn visit, void *userdata)
{
    for (uint32_t index = 0; index < YETTY_YVTERM_TIER_CACHE_ENTRIES; ++index) {
        struct yetty_yvterm_tier_cache_entry *entry = &tiers->cache[index];
        if (!entry->valid) {
            continue;
        }
        for (uint32_t line_index = 0; line_index < entry->line_count; ++line_index) {
            visit(&entry->lines[line_index], entry->first_line + line_index, userdata);
        }
    }
}
