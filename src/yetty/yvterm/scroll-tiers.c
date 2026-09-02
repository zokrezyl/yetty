/*
 * scroll-tiers.c — the archive engine of the tiered scroll buffer: line
 * serialization, lz4-compressed warm segments, the session-scoped cold spill
 * file, and the bounded materialization cache the view resolver reads from.
 *
 * Everything here is line-serial: text cells as (repeat, cell) runs, the SDF
 * primitive arena verbatim (it already holds opaque u32 wire words, including
 * the retained complex envelopes), plus continuation flags and the original
 * width for a future view-time re-wrap. Segments are self-delimiting and
 * versioned (magic + version + checksum), so the same blob layout feeds the
 * warm tier, the cold file, and a future persistent session log.
 *
 * Figure runtimes never live here — the caller destroys them before a line is
 * pushed, and the grid re-materializes them from the retained envelopes when
 * a cached line scrolls back into view.
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <lz4.h>

#include <yetty/ycore/memtag.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/cmds.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>
#include "yetty/gen/impl/yvterm/grid.h"

#include "scroll-tiers.h"

/* Complex-envelope type test — hand-declared from ydraw-list (same
 * header-clash avoidance as grid.c). */
bool yetty_ydraw_is_complex(uint32_t type);

/* Serialized line header, in u32 words (format v2: the rich content is a
 * list of BLOCKS — span + records + arena each — instead of the old
 * line-wide primitive/arena/span triple). */
enum {
    LINE_WORD_FLAGS = 0,
    LINE_WORD_STORED_COLS = 1,
    LINE_WORD_ORIGINAL_COLS = 2,
    LINE_WORD_RUN_COUNT = 3,
    LINE_WORD_RICH_BLOCK_COUNT = 4,
    /* Total u32 words of all serialized rich blocks trailing the cell runs
     * (headers + record descriptors + arenas), so the record size validates
     * before walking the variable-length blocks. */
    LINE_WORD_RICH_WORDS = 5,
    LINE_WORD_RESERVED = 6,
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
    /* Per-block sub-header words: span_rows, record_count, arena_count.
     * The block's arena words follow the descriptors verbatim. Bytes-less
     * runtime-only records serialize with word_count 0 and decode to
     * nothing renderable — their runtimes died with the hot line. */
    RICH_BLOCK_HEADER_WORDS = 3,
    /* v5 descriptor: arena_offset, word_count, journal_words, record kind,
     * paint_z (i32 bits), paint_sequence lo, paint_sequence hi,
     * record_ordinal, frozen offset_x (f32 bits), frozen offset_y (f32
     * bits). The paint key round-trips EXACTLY — materialization never
     * re-extracts or re-mints it. The offset is the record's accumulated
     * group-chain translation at serialize time: sealed content is
     * immutable, so this bake preserves the final projection without
     * archiving the group tree. */
    RICH_RECORD_WORDS = 10,
    RICH_RECORD_WORD_OFFSET_X = 8,
    RICH_RECORD_WORD_OFFSET_Y = 9,
};

/* The record's accumulated group-chain translation (cycle-guarded walk, the
 * same accumulation the renderer's leaf resolve performs). */
static void record_frozen_offset(const struct yetty_yvterm_rich_block *block,
                                 const struct yetty_yvterm_rich_record *record, float *out_x,
                                 float *out_y)
{
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    uint32_t walk = record->group_slot;
    uint32_t guard = 0;
    while (walk != 0 && walk <= block->group_count && guard++ <= block->group_count) {
        const struct yetty_yvterm_rich_group *group = &block->groups[walk - 1u];
        sum_x += group->offset_x;
        sum_y += group->offset_y;
        walk = group->parent_slot;
    }
    *out_x = sum_x;
    *out_y = sum_y;
}

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
        yetty_yvterm_line_release_rich(tiers->rich_store, &entry->lines[index]);
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

    /* Size the serialized rich blocks. Stale handles serialize as nothing;
     * live blocks contribute a sub-header + record descriptors + arena. */
    uint32_t rich_block_count = 0;
    size_t rich_words = 0;
    for (uint32_t index = 0; index < line->rich_block_count; ++index) {
        const struct yetty_yvterm_rich_block *block =
            yetty_yvterm_rich_store_resolve(tiers->rich_store, line->rich_blocks[index]);
        if (!block) {
            continue;
        }
        rich_block_count++;
        uint32_t alive_records = 0;
        uint32_t alive_words = 0;
        for (uint32_t record_index = 0; record_index < block->record_count; ++record_index) {
            if (block->records[record_index].alive) {
                alive_records++;
                alive_words += block->records[record_index].word_count;
                alive_words += block->records[record_index].journal_count;
            }
        }
        rich_words +=
            RICH_BLOCK_HEADER_WORDS + (size_t)alive_records * RICH_RECORD_WORDS + alive_words;
    }

    size_t word_count =
        LINE_HEADER_WORDS + (size_t)run_count * CELL_RUN_WORDS + (size_t)mark_words + rich_words;
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
    words[LINE_WORD_RICH_BLOCK_COUNT] = rich_block_count;
    words[LINE_WORD_RICH_WORDS] = (uint32_t)rich_words;
    words[LINE_WORD_RESERVED] = 0u;
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
    for (uint32_t index = 0; index < line->rich_block_count; ++index) {
        const struct yetty_yvterm_rich_block *block =
            yetty_yvterm_rich_store_resolve(tiers->rich_store, line->rich_blocks[index]);
        if (!block) {
            continue;
        }
        /* Alive records only — dead (deleted/replaced) records and their
         * bytes compact away here; the serialized arena is the alive records'
         * words back to back with rebased offsets. */
        uint32_t alive_records = 0;
        uint32_t alive_arena_words = 0;
        uint32_t alive_journal_words = 0;
        for (uint32_t record_index = 0; record_index < block->record_count; ++record_index) {
            if (block->records[record_index].alive) {
                alive_records++;
                alive_arena_words += block->records[record_index].word_count;
                alive_journal_words += block->records[record_index].journal_count;
            }
        }
        cursor[0] = block->span_rows;
        cursor[1] = alive_records;
        cursor[2] = alive_arena_words + alive_journal_words;
        cursor += RICH_BLOCK_HEADER_WORDS;
        uint32_t *arena_cursor = cursor + (size_t)alive_records * RICH_RECORD_WORDS;
        uint32_t *journal_cursor = arena_cursor + alive_arena_words;
        uint32_t rebased_offset = 0;
        uint32_t journal_offset = 0;
        for (uint32_t record_index = 0; record_index < block->record_count; ++record_index) {
            const struct yetty_yvterm_rich_record *record = &block->records[record_index];
            if (!record->alive) {
                continue;
            }
            cursor[0] = rebased_offset;
            cursor[1] = record->word_count;
            cursor[2] = record->journal_count;
            cursor[3] = (uint32_t)record->kind;
            memcpy(&cursor[4], &record->paint_z, sizeof(uint32_t));
            cursor[5] = (uint32_t)(record->paint_sequence & 0xFFFFFFFFu);
            cursor[6] = (uint32_t)(record->paint_sequence >> 32);
            cursor[7] = record->record_ordinal;
            float frozen_x = 0.0f;
            float frozen_y = 0.0f;
            record_frozen_offset(block, record, &frozen_x, &frozen_y);
            memcpy(&cursor[RICH_RECORD_WORD_OFFSET_X], &frozen_x, sizeof(uint32_t));
            memcpy(&cursor[RICH_RECORD_WORD_OFFSET_Y], &frozen_y, sizeof(uint32_t));
            cursor += RICH_RECORD_WORDS;
            if (record->word_count) {
                memcpy(arena_cursor + rebased_offset, block->arena + record->arena_offset,
                       (size_t)record->word_count * sizeof(uint32_t));
                rebased_offset += record->word_count;
            }
            if (record->journal_count) {
                memcpy(journal_cursor + journal_offset, record->journal,
                       (size_t)record->journal_count * sizeof(uint32_t));
                journal_offset += record->journal_count;
            }
        }
        cursor = journal_cursor + journal_offset;
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
/* Append one block handle onto a cache line (plain realloc, matching the
 * grid-side handle arrays that release_line_rich frees). */
static struct yetty_ycore_void_result line_blocks_push_cache(struct yetty_yvterm_line *line,
                                                             struct yetty_yvterm_rich_handle handle)
{
    if (line->rich_block_count == line->rich_block_capacity) {
        uint32_t new_capacity = line->rich_block_capacity ? line->rich_block_capacity * 2u : 2u;
        struct yetty_yvterm_rich_handle *grown = realloc(
            line->rich_blocks, (size_t)new_capacity * sizeof(struct yetty_yvterm_rich_handle));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: cache line handle grow");
        }
        line->rich_blocks = grown;
        line->rich_block_capacity = new_capacity;
    }
    line->rich_blocks[line->rich_block_count++] = handle;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result tier_inflate_line(struct yetty_yvterm_tiers *tiers,
                                                        const uint32_t *words, size_t word_count,
                                                        struct yetty_yvterm_line *line,
                                                        uint32_t cols, uint32_t blank_fg,
                                                        uint32_t blank_bg, int suppress_rich,
                                                        uint64_t timeline_row)
{
    if (word_count < LINE_HEADER_WORDS) {
        return YETTY_ERR(yetty_ycore_void, "scroll tiers: truncated line record");
    }
    uint32_t run_count = words[LINE_WORD_RUN_COUNT];
    uint32_t rich_block_count = words[LINE_WORD_RICH_BLOCK_COUNT];
    uint32_t rich_words = words[LINE_WORD_RICH_WORDS];
    uint32_t mark_words = words[LINE_WORD_MARK_WORDS];
    if (LINE_HEADER_WORDS + (size_t)run_count * CELL_RUN_WORDS + (size_t)mark_words + rich_words >
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

    if (suppress_rich || rich_block_count == 0) {
        return YETTY_OK_VOID();
    }
    /* Mint one sealed cache-local block per serialized block. Materialized
     * archive content is immutable and cache-local: it dies with its cache
     * entry and never re-registers producer-visible identity. Best-effort:
     * a failed block mint drops that block (text still renders). */
    const uint32_t *rich_end = words + word_count;
    for (uint32_t block_index = 0; block_index < rich_block_count; ++block_index) {
        if (cursor + RICH_BLOCK_HEADER_WORDS > rich_end) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: truncated rich block header");
        }
        uint32_t span_rows = cursor[0];
        uint32_t record_count = cursor[1];
        uint32_t body_words = cursor[2]; /* arena + journals */
        cursor += RICH_BLOCK_HEADER_WORDS;
        if (cursor + (size_t)record_count * RICH_RECORD_WORDS + body_words > rich_end) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: truncated rich block body");
        }
        const uint32_t *descriptors = cursor;
        uint32_t arena_count = 0;
        for (uint32_t record_index = 0; record_index < record_count; ++record_index) {
            arena_count += descriptors[record_index * RICH_RECORD_WORDS + 1u];
        }
        if (arena_count > body_words) {
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: rich block arena overrun");
        }
        const uint32_t *arena = cursor + (size_t)record_count * RICH_RECORD_WORDS;
        const uint32_t *journals = arena + arena_count;
        uint32_t journal_words_total = body_words - arena_count;
        cursor = arena + body_words;

        /* The serializing line was the block's bottom OWNER line, so this
         * line's timeline index restores the block's real rolling anchors
         * — plan-order rendering and view placement need them, not a zero
         * placeholder. */
        uint32_t anchor_span = span_rows ? span_rows : 1u;
        uint64_t insertion_row =
            timeline_row >= anchor_span - 1u ? timeline_row - (anchor_span - 1u) : 0u;
        struct yetty_yvterm_rich_handle_result block_res = yetty_yvterm_rich_store_block_create(
            tiers->rich_store, YETTY_YVTERM_RICH_SCREEN_CACHE, insertion_row);
        if (YETTY_IS_ERR(block_res)) {
            yetty_ycore_error_destroy(block_res.error);
            continue;
        }
        int block_ok = 1;
        uint32_t journal_offset = 0;
        /* Synthetic offset carrier: consecutive records sharing one frozen
         * offset reuse one cache-local group (the common case — records of
         * one source group serialize adjacently). */
        float carrier_x = 0.0f;
        float carrier_y = 0.0f;
        uint32_t carrier_slot = 0;
        for (uint32_t record_index = 0; record_index < record_count && block_ok; ++record_index) {
            uint32_t record_offset = descriptors[record_index * RICH_RECORD_WORDS];
            uint32_t record_words = descriptors[record_index * RICH_RECORD_WORDS + 1u];
            uint32_t record_journal_words = descriptors[record_index * RICH_RECORD_WORDS + 2u];
            uint32_t record_kind = descriptors[record_index * RICH_RECORD_WORDS + 3u];
            int32_t record_paint_z;
            memcpy(&record_paint_z, &descriptors[record_index * RICH_RECORD_WORDS + 4u],
                   sizeof(record_paint_z));
            uint64_t record_sequence =
                (uint64_t)descriptors[record_index * RICH_RECORD_WORDS + 5u] |
                ((uint64_t)descriptors[record_index * RICH_RECORD_WORDS + 6u] << 32);
            uint32_t record_ordinal = descriptors[record_index * RICH_RECORD_WORDS + 7u];
            float frozen_x;
            float frozen_y;
            memcpy(&frozen_x,
                   &descriptors[record_index * RICH_RECORD_WORDS + RICH_RECORD_WORD_OFFSET_X],
                   sizeof(frozen_x));
            memcpy(&frozen_y,
                   &descriptors[record_index * RICH_RECORD_WORDS + RICH_RECORD_WORD_OFFSET_Y],
                   sizeof(frozen_y));
            if (!isfinite(frozen_x) || !isfinite(frozen_y)) {
                frozen_x = 0.0f; /* corrupt spill data degrades to unshifted */
                frozen_y = 0.0f;
            }
            if (record_journal_words > journal_words_total ||
                journal_offset > journal_words_total - record_journal_words) {
                block_ok = 0;
                break;
            }
            if (record_words == 0) {
                journal_offset += record_journal_words;
                continue; /* bytes-less runtime-only record — nothing survives */
            }
            if (record_offset > arena_count || record_words > arena_count - record_offset) {
                block_ok = 0;
                break;
            }
            /* Serialized kind must agree with the wire record it fronts —
             * a mismatch means a corrupt or forged descriptor. */
            uint32_t wire_type = arena[record_offset];
            uint32_t wire_kind =
                yetty_ydraw_is_complex(wire_type) &&
                        yetty_ysdf_primitive_size(wire_type & ~YETTY_YDRAW_HAS_ID_FLAG) == 0u
                    ? (uint32_t)YETTY_YVTERM_RICH_RECORD_COMPLEX
                    : (uint32_t)YETTY_YVTERM_RICH_RECORD_PRIMITIVE;
            if (record_kind != wire_kind) {
                block_ok = 0;
                break;
            }
            /* Re-attach the frozen projection: records with a nonzero baked
             * offset hang under a synthetic group carrying it, so the
             * renderer's ancestor accumulation reproduces the sealed
             * appearance without the original group tree. */
            uint32_t record_group_slot = 0;
            if (frozen_x != 0.0f || frozen_y != 0.0f) {
                if (carrier_slot == 0 || frozen_x != carrier_x || frozen_y != carrier_y) {
                    struct yetty_ycore_uint32_result carrier_res =
                        yetty_yvterm_rich_store_block_group_open(tiers->rich_store, block_res.value,
                                                                 0u, false, 0u);
                    if (YETTY_IS_ERR(carrier_res)) {
                        yetty_ycore_error_destroy(carrier_res.error);
                        block_ok = 0;
                        break;
                    }
                    carrier_slot = carrier_res.value;
                    carrier_x = frozen_x;
                    carrier_y = frozen_y;
                    struct yetty_yvterm_rich_block *carrier_block =
                        yetty_yvterm_rich_store_resolve(tiers->rich_store, block_res.value);
                    if (carrier_block && carrier_slot <= carrier_block->group_count) {
                        carrier_block->groups[carrier_slot - 1u].offset_x = frozen_x;
                        carrier_block->groups[carrier_slot - 1u].offset_y = frozen_y;
                    }
                }
                record_group_slot = carrier_slot;
            }
            struct yetty_ycore_void_result append_res =
                yetty_yvterm_rich_store_block_append_record_exact(
                    tiers->rich_store, block_res.value, record_group_slot, arena + record_offset,
                    record_words, record_paint_z, record_sequence, record_ordinal);
            if (YETTY_IS_ERR(append_res)) {
                yetty_ycore_error_destroy(append_res.error);
                block_ok = 0;
                break;
            }
            if (record_journal_words) {
                /* Rebuild the record's journal so a later materialize replays
                 * the accepted updates. Direct field fill (module-internal);
                 * bytes count against the aggregate budget. */
                struct yetty_yvterm_rich_block *fresh =
                    yetty_yvterm_rich_store_resolve(tiers->rich_store, block_res.value);
                if (fresh && fresh->record_count) {
                    struct yetty_yvterm_rich_record *record =
                        &fresh->records[fresh->record_count - 1u];
                    record->journal = malloc((size_t)record_journal_words * sizeof(uint32_t));
                    if (record->journal) {
                        memcpy(record->journal, journals + journal_offset,
                               (size_t)record_journal_words * sizeof(uint32_t));
                        record->journal_count = record_journal_words;
                        record->journal_capacity = record_journal_words;
                        tiers->rich_store->journal_bytes_used +=
                            (size_t)record_journal_words * sizeof(uint32_t);
                    }
                }
            }
            journal_offset += record_journal_words;
        }
        struct yetty_yvterm_rich_block *block =
            yetty_yvterm_rich_store_resolve(tiers->rich_store, block_res.value);
        if (!block_ok || !block) {
            yetty_yvterm_rich_store_block_destroy(tiers->rich_store, block_res.value);
            continue;
        }
        block->span_rows = span_rows;
        block->bottom_owner_row = block->insertion_rolling_row + anchor_span - 1u;
        block->state = YETTY_YVTERM_RICH_BLOCK_SEALED;
        struct yetty_ycore_void_result push_res = line_blocks_push_cache(line, block_res.value);
        if (YETTY_IS_ERR(push_res)) {
            yetty_ycore_error_destroy(push_res.error);
            yetty_yvterm_rich_store_block_destroy(tiers->rich_store, block_res.value);
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
                yetty_yvterm_line_release_rich(tiers->rich_store, &lines[undo]);
                yetty_ycore_memtag_free(tiers->memtag, lines[undo].text_cells);
            }
            yetty_ycore_memtag_free(tiers->memtag, lines);
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "scroll tiers: corrupt segment directory");
        }
        int suppress_rich = segment->first_line + index < tiers->rich_clear_watermark;
        struct yetty_ycore_void_result line_res = tier_inflate_line(
            tiers, (const uint32_t *)(payload + offset), (next_offset - offset) / sizeof(uint32_t),
            &lines[index], cols, blank_fg, blank_bg, suppress_rich, segment->first_line + index);
        if (YETTY_IS_ERR(line_res)) {
            for (uint32_t undo = 0; undo < index; ++undo) {
                yetty_yvterm_line_release_rich(tiers->rich_store, &lines[undo]);
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
