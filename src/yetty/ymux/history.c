/*
 * history.c — the ymux tiered scrollback store: class@ymux:history (#695
 * phase 2).
 *
 * GPU-free retention of primary-screen rows that scrolled off the top of a
 * pane's engine, addressed by a monotonic 64-bit TIMELINE index (count of
 * rows ever pushed). Three tiers:
 *
 *     newest ─────────────────────────────────────────► oldest
 *     [ HOT: row ring ][ WARM: lz4 segments in RAM ][ COLD: spill file ]
 *
 * A row aging out of the hot ring serializes into the open segment; sealed
 * segments compress (lz4) and stay in RAM under the warm budget; beyond it
 * they spill to a session-scoped temp file. A total row cap advances the
 * FLOOR by dropping whole oldest segments. Serialized rows carry the #695
 * identity fields from day one: logical_line_id, logical_cell_start,
 * continuation. No renderer state, no glyph indices, no attachment view
 * state lives here.
 *
 * resolve(timeline_idx) serves a row back — from the hot ring directly, or
 * by inflating its segment into a small materialization cache whose entries
 * stay valid until the next resolve that evicts them.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lz4.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/api/ymux/engine.h>

/* One retained row as the store keeps it (hot) or serves it (inflated). */
struct YETTY_ANNOTATE("expose") yetty_ymux_history_row {
    const struct yetty_ymux_cell *cells;
    uint32_t cols;
    uint64_t logical_line_id;
    uint32_t logical_cell_start;
    int continuation;
};

/* Serialized-row header words (u32), followed by RLE cell runs. */
enum {
    YMUX_HISTORY_ROW_WORD_FLAGS = 0, /* bit0 continuation */
    YMUX_HISTORY_ROW_WORD_COLS = 1,
    YMUX_HISTORY_ROW_WORD_RUN_COUNT = 2,
    YMUX_HISTORY_ROW_WORD_MARK_WORDS = 3,
    YMUX_HISTORY_ROW_WORD_LOGICAL_ID_LO = 4,
    YMUX_HISTORY_ROW_WORD_LOGICAL_ID_HI = 5,
    YMUX_HISTORY_ROW_WORD_LOGICAL_START = 6,
    YMUX_HISTORY_ROW_HEADER_WORDS = 7,
    /* Run: repeat|mark_count<<24, codepoint, fg, bg, attrs|width<<16 —
     * then mark_count mark words. */
    YMUX_HISTORY_CELL_RUN_WORDS = 5,
    YMUX_HISTORY_RUN_REPEAT_MASK = 0x00FFFFFF,
    YMUX_HISTORY_RUN_MARK_SHIFT = 24,
};

enum {
    YMUX_HISTORY_SEGMENT_MAX_ROWS = 512,
    YMUX_HISTORY_SEGMENT_MAX_BYTES = 2u * 1024u * 1024u,
    YMUX_HISTORY_CACHE_ENTRIES = 4,
    YMUX_HISTORY_FORMAT_VERSION = 1,
    YMUX_HISTORY_DEFAULT_HOT_ROWS = 2000,
    YMUX_HISTORY_DEFAULT_WARM_BYTES = 64 * 1024 * 1024,
};

/* One hot-ring slot: an owned cell array + identity. */
struct history_hot_row {
    struct yetty_ymux_cell *cells;
    uint32_t cols;
    uint64_t logical_line_id;
    uint32_t logical_cell_start;
    int continuation;
};

/* One sealed segment: rows [first_row, first_row + row_count) serialized and
 * lz4-compressed. Warm: bytes in RAM. Cold: bytes NULL, blob at file_offset
 * in the spill file. */
struct history_segment {
    uint64_t first_row;
    uint32_t row_count;
    uint32_t raw_size;
    uint8_t *bytes;
    uint32_t byte_size;
    int on_disk;
    uint64_t file_offset;
};

/* The open (unsealed) segment accumulating serialized rows. */
struct history_builder {
    uint8_t *bytes;
    size_t byte_count;
    size_t byte_capacity;
    uint32_t *row_offsets;
    uint32_t row_count;
    uint32_t row_capacity;
    uint64_t first_row;
};

/* One inflated segment served to resolvers. */
struct history_cache_entry {
    int valid;
    uint64_t first_row;
    uint32_t row_count;
    /* Parallel arrays per row: decoded cells + identity. */
    struct yetty_ymux_cell **row_cells;
    uint32_t *row_cols;
    uint64_t *row_logical_ids;
    uint32_t *row_logical_starts;
    uint8_t *row_continuations;
    uint64_t last_used_tick;
};

/* The tiered store — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:history") yetty_ymux_history {
    /* Hot ring. */
    struct history_hot_row *hot_rows;
    uint32_t hot_capacity;
    uint32_t hot_count; /* filled slots (grows until capacity, then rolls) */
    uint32_t hot_head;  /* index of the OLDEST hot row */

    /* Timeline: total rows ever pushed = index of the next push.
     * archived = rows serialized out of the hot ring (oldest hot row's
     * index); floor = oldest row still reachable. */
    uint64_t pushed_rows;
    uint64_t archived_rows;
    uint64_t dropped_rows;

    /* Sealed segments, oldest first; head advances on drops. */
    struct history_segment *segments;
    uint32_t segment_head;
    uint32_t segment_count;
    uint32_t segment_capacity;

    struct history_builder builder;

    /* Budgets: warm RAM bytes (sealed blobs + builder), total row cap
     * (0 = unlimited), spill file size cap (0 = unlimited). */
    size_t warm_bytes_used;
    size_t warm_bytes_budget;
    uint64_t total_row_cap;
    uint64_t file_bytes_budget;

    FILE *spill_file;
    uint64_t spill_file_size;
    int spill_disabled;

    struct history_cache_entry cache[YMUX_HISTORY_CACHE_ENTRIES];
    uint64_t cache_tick;
};

YETTY_YRESULT_DECLARE(yetty_ymux_history_row, struct yetty_ymux_history_row);

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_history_class_get(void);
struct yetty_ymux_history_ptr_result yetty_ymux_history_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_history_ptr, struct yetty_ymux_history *);

/*===========================================================================
 * Serialization.
 *=========================================================================*/

static int history_cells_equal(const struct yetty_ymux_cell *left,
                               const struct yetty_ymux_cell *right)
{
    if (left->codepoint != right->codepoint || left->fg != right->fg || left->bg != right->bg ||
        left->attrs != right->attrs || left->width != right->width ||
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

static struct yetty_ycore_void_result builder_reserve(struct yetty_ymux_history *history,
                                                      size_t extra_bytes)
{
    struct history_builder *builder = &history->builder;
    if (builder->byte_count + extra_bytes <= builder->byte_capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_capacity = builder->byte_capacity ? builder->byte_capacity * 2 : 16384;
    while (new_capacity < builder->byte_count + extra_bytes) {
        new_capacity *= 2;
    }
    uint8_t *grown = realloc(builder->bytes, new_capacity);
    if (!grown) {
        return YETTY_ERR(yetty_ycore_void, "ymux history: builder grow");
    }
    history->warm_bytes_used += new_capacity - builder->byte_capacity;
    builder->bytes = grown;
    builder->byte_capacity = new_capacity;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result builder_push_row(struct yetty_ymux_history *history,
                                                       const struct yetty_ymux_cell *cells,
                                                       uint32_t cols, uint64_t logical_line_id,
                                                       uint32_t logical_cell_start,
                                                       int continuation)
{
    struct history_builder *builder = &history->builder;

    /* Trim trailing fully-blank cells? NO: default colors are engine state
     * this store does not know — retain the row verbatim at `cols`. Runs
     * compress the tail anyway. */
    uint32_t run_count = 0;
    uint32_t mark_words = 0;
    for (uint32_t col = 0; col < cols;) {
        uint32_t run_end = col + 1;
        while (run_end < cols && history_cells_equal(&cells[col], &cells[run_end])) {
            ++run_end;
        }
        ++run_count;
        mark_words += cells[col].mark_count;
        col = run_end;
    }
    size_t word_count = YMUX_HISTORY_ROW_HEADER_WORDS +
                        (size_t)run_count * YMUX_HISTORY_CELL_RUN_WORDS + mark_words;
    struct yetty_ycore_void_result reserve_res =
        builder_reserve(history, word_count * sizeof(uint32_t));
    YETTY_RETURN_IF_ERR(yetty_ycore_void, reserve_res, "ymux history: row reserve");

    if (builder->row_count == builder->row_capacity) {
        uint32_t new_capacity = builder->row_capacity ? builder->row_capacity * 2 : 64;
        uint32_t *grown = realloc(builder->row_offsets, new_capacity * sizeof(uint32_t));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux history: row offsets grow");
        }
        builder->row_offsets = grown;
        builder->row_capacity = new_capacity;
    }
    builder->row_offsets[builder->row_count++] = (uint32_t)builder->byte_count;

    uint32_t *words = (uint32_t *)(builder->bytes + builder->byte_count);
    builder->byte_count += word_count * sizeof(uint32_t);
    words[YMUX_HISTORY_ROW_WORD_FLAGS] = continuation ? 1u : 0u;
    words[YMUX_HISTORY_ROW_WORD_COLS] = cols;
    words[YMUX_HISTORY_ROW_WORD_RUN_COUNT] = run_count;
    words[YMUX_HISTORY_ROW_WORD_MARK_WORDS] = mark_words;
    words[YMUX_HISTORY_ROW_WORD_LOGICAL_ID_LO] = (uint32_t)(logical_line_id & 0xFFFFFFFFu);
    words[YMUX_HISTORY_ROW_WORD_LOGICAL_ID_HI] = (uint32_t)(logical_line_id >> 32);
    words[YMUX_HISTORY_ROW_WORD_LOGICAL_START] = logical_cell_start;

    uint32_t *cursor = words + YMUX_HISTORY_ROW_HEADER_WORDS;
    for (uint32_t col = 0; col < cols;) {
        uint32_t run_end = col + 1;
        while (run_end < cols && history_cells_equal(&cells[col], &cells[run_end])) {
            ++run_end;
        }
        const struct yetty_ymux_cell *cell = &cells[col];
        cursor[0] = (run_end - col) | ((uint32_t)cell->mark_count << YMUX_HISTORY_RUN_MARK_SHIFT);
        cursor[1] = cell->codepoint;
        cursor[2] = cell->fg;
        cursor[3] = cell->bg;
        cursor[4] = (uint32_t)cell->attrs | ((uint32_t)cell->width << 16);
        cursor += YMUX_HISTORY_CELL_RUN_WORDS;
        for (uint8_t mark = 0; mark < cell->mark_count; ++mark) {
            *cursor++ = cell->marks[mark];
        }
        col = run_end;
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Sealing, spilling, dropping.
 *=========================================================================*/

static struct yetty_ycore_void_result history_append_segment(struct yetty_ymux_history *history,
                                                             struct history_segment segment)
{
    if (history->segment_head > 64 && history->segment_head * 2 > history->segment_count) {
        memmove(history->segments, history->segments + history->segment_head,
                (size_t)(history->segment_count - history->segment_head) *
                    sizeof(struct history_segment));
        history->segment_count -= history->segment_head;
        history->segment_head = 0;
    }
    if (history->segment_count == history->segment_capacity) {
        uint32_t new_capacity = history->segment_capacity ? history->segment_capacity * 2 : 16;
        struct history_segment *grown =
            realloc(history->segments, (size_t)new_capacity * sizeof(struct history_segment));
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "ymux history: segment table grow");
        }
        history->segments = grown;
        history->segment_capacity = new_capacity;
    }
    history->segments[history->segment_count++] = segment;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result history_seal_builder(struct yetty_ymux_history *history)
{
    struct history_builder *builder = &history->builder;
    if (builder->row_count == 0) {
        return YETTY_OK_VOID();
    }
    size_t directory_size = (size_t)builder->row_count * sizeof(uint32_t);
    size_t raw_size = directory_size + builder->byte_count;
    uint8_t *raw = malloc(raw_size);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "ymux history: seal raw alloc");
    }
    memcpy(raw, builder->row_offsets, directory_size);
    memcpy(raw + directory_size, builder->bytes, builder->byte_count);

    int compressed_capacity = LZ4_compressBound((int)raw_size);
    uint8_t *compressed = malloc((size_t)compressed_capacity);
    if (!compressed) {
        free(raw);
        return YETTY_ERR(yetty_ycore_void, "ymux history: seal lz4 alloc");
    }
    int compressed_size = LZ4_compress_default((const char *)raw, (char *)compressed, (int)raw_size,
                                               compressed_capacity);
    free(raw);
    if (compressed_size <= 0) {
        free(compressed);
        return YETTY_ERR(yetty_ycore_void, "ymux history: lz4 compress");
    }

    struct history_segment segment = {
        .first_row = builder->first_row,
        .row_count = builder->row_count,
        .raw_size = (uint32_t)raw_size,
        .bytes = compressed,
        .byte_size = (uint32_t)compressed_size,
    };
    struct yetty_ycore_void_result append_res = history_append_segment(history, segment);
    if (YETTY_IS_ERR(append_res)) {
        free(compressed);
        return YETTY_ERR(yetty_ycore_void, "ymux history: append segment", append_res);
    }
    history->warm_bytes_used += (size_t)compressed_size;
    builder->first_row += builder->row_count;
    builder->row_count = 0;
    builder->byte_count = 0;
    return YETTY_OK_VOID();
}

/* Move the oldest warm segments to the spill file until warm usage fits the
 * budget. A spill failure latches spill_disabled: the store degrades to
 * dropping oldest segments (floor advances) instead of failing pushes. */
static void history_enforce_warm_budget(struct yetty_ymux_history *history)
{
    for (uint32_t index = history->segment_head;
         index < history->segment_count && history->warm_bytes_used > history->warm_bytes_budget;
         ++index) {
        struct history_segment *segment = &history->segments[index];
        if (!segment->bytes || segment->on_disk) {
            continue;
        }
        if (history->spill_disabled) {
            break;
        }
        if (!history->spill_file) {
            history->spill_file = tmpfile();
            if (!history->spill_file) {
                history->spill_disabled = 1;
                break;
            }
        }
        if (history->file_bytes_budget &&
            history->spill_file_size + segment->byte_size > history->file_bytes_budget) {
            break; /* file cap: keep in RAM; the row cap will drop oldest */
        }
        if (fseek(history->spill_file, (long)history->spill_file_size, SEEK_SET) != 0 ||
            fwrite(segment->bytes, 1, segment->byte_size, history->spill_file) !=
                segment->byte_size) {
            history->spill_disabled = 1;
            break;
        }
        segment->file_offset = history->spill_file_size;
        history->spill_file_size += segment->byte_size;
        segment->on_disk = 1;
        history->warm_bytes_used -= segment->byte_size;
        free(segment->bytes);
        segment->bytes = NULL;
    }
}

static void history_cache_invalidate_range(struct yetty_ymux_history *history, uint64_t first_row,
                                           uint32_t row_count);

/* Enforce the total row cap by dropping whole oldest segments. */
static void history_enforce_row_cap(struct yetty_ymux_history *history)
{
    if (history->total_row_cap == 0) {
        return;
    }
    uint64_t retained_top = history->pushed_rows;
    while (history->segment_head < history->segment_count) {
        struct history_segment *segment = &history->segments[history->segment_head];
        uint64_t segment_end = segment->first_row + segment->row_count;
        if (retained_top - segment_end >= history->total_row_cap) {
            history_cache_invalidate_range(history, segment->first_row, segment->row_count);
            if (segment->bytes) {
                history->warm_bytes_used -= segment->byte_size;
                free(segment->bytes);
            }
            history->dropped_rows = segment_end;
            ++history->segment_head;
        } else {
            break;
        }
    }
}

/*===========================================================================
 * Materialization cache.
 *=========================================================================*/

static void cache_entry_free(struct history_cache_entry *entry)
{
    if (!entry->valid) {
        return;
    }
    for (uint32_t row = 0; row < entry->row_count; ++row) {
        free(entry->row_cells[row]);
    }
    free(entry->row_cells);
    free(entry->row_cols);
    free(entry->row_logical_ids);
    free(entry->row_logical_starts);
    free(entry->row_continuations);
    memset(entry, 0, sizeof(*entry));
}

static void history_cache_invalidate_range(struct yetty_ymux_history *history, uint64_t first_row,
                                           uint32_t row_count)
{
    for (uint32_t index = 0; index < YMUX_HISTORY_CACHE_ENTRIES; ++index) {
        struct history_cache_entry *entry = &history->cache[index];
        if (entry->valid && entry->first_row < first_row + row_count &&
            first_row < entry->first_row + entry->row_count) {
            cache_entry_free(entry);
        }
    }
}

static struct yetty_ycore_void_result cache_inflate_segment(struct yetty_ymux_history *history,
                                                            const struct history_segment *segment,
                                                            struct history_cache_entry *entry)
{
    uint8_t *raw = malloc(segment->raw_size);
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "ymux history: inflate raw alloc");
    }
    const uint8_t *compressed = segment->bytes;
    uint8_t *read_buffer = NULL;
    if (!compressed) {
        if (!history->spill_file) {
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "ymux history: segment lost");
        }
        read_buffer = malloc(segment->byte_size);
        if (!read_buffer) {
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "ymux history: spill read alloc");
        }
        if (fseek(history->spill_file, (long)segment->file_offset, SEEK_SET) != 0 ||
            fread(read_buffer, 1, segment->byte_size, history->spill_file) != segment->byte_size) {
            free(read_buffer);
            free(raw);
            return YETTY_ERR(yetty_ycore_void, "ymux history: spill read");
        }
        compressed = read_buffer;
    }
    int inflated = LZ4_decompress_safe((const char *)compressed, (char *)raw,
                                       (int)segment->byte_size, (int)segment->raw_size);
    free(read_buffer);
    if (inflated != (int)segment->raw_size) {
        free(raw);
        return YETTY_ERR(yetty_ycore_void, "ymux history: lz4 inflate");
    }

    const uint32_t *directory = (const uint32_t *)raw;
    const uint8_t *payload = raw + (size_t)segment->row_count * sizeof(uint32_t);
    size_t payload_size = segment->raw_size - (size_t)segment->row_count * sizeof(uint32_t);

    entry->row_cells = calloc(segment->row_count, sizeof(struct yetty_ymux_cell *));
    entry->row_cols = calloc(segment->row_count, sizeof(uint32_t));
    entry->row_logical_ids = calloc(segment->row_count, sizeof(uint64_t));
    entry->row_logical_starts = calloc(segment->row_count, sizeof(uint32_t));
    entry->row_continuations = calloc(segment->row_count, 1);
    if (!entry->row_cells || !entry->row_cols || !entry->row_logical_ids ||
        !entry->row_logical_starts || !entry->row_continuations) {
        free(raw);
        entry->valid = 1; /* let cache_entry_free release the partial arrays */
        entry->row_count = 0;
        cache_entry_free(entry);
        return YETTY_ERR(yetty_ycore_void, "ymux history: cache arrays alloc");
    }

    for (uint32_t row = 0; row < segment->row_count; ++row) {
        size_t offset = directory[row];
        if (offset + YMUX_HISTORY_ROW_HEADER_WORDS * sizeof(uint32_t) > payload_size) {
            break;
        }
        const uint32_t *words = (const uint32_t *)(payload + offset);
        uint32_t cols = words[YMUX_HISTORY_ROW_WORD_COLS];
        uint32_t run_count = words[YMUX_HISTORY_ROW_WORD_RUN_COUNT];
        entry->row_cols[row] = cols;
        entry->row_logical_ids[row] = (uint64_t)words[YMUX_HISTORY_ROW_WORD_LOGICAL_ID_LO] |
                                      ((uint64_t)words[YMUX_HISTORY_ROW_WORD_LOGICAL_ID_HI] << 32);
        entry->row_logical_starts[row] = words[YMUX_HISTORY_ROW_WORD_LOGICAL_START];
        entry->row_continuations[row] = (uint8_t)(words[YMUX_HISTORY_ROW_WORD_FLAGS] & 1u);
        entry->row_cells[row] = calloc(cols ? cols : 1, sizeof(struct yetty_ymux_cell));
        if (!entry->row_cells[row]) {
            break;
        }
        const uint32_t *cursor = words + YMUX_HISTORY_ROW_HEADER_WORDS;
        uint32_t col = 0;
        for (uint32_t run = 0; run < run_count && col < cols; ++run) {
            uint32_t repeat = cursor[0] & YMUX_HISTORY_RUN_REPEAT_MASK;
            uint8_t mark_count = (uint8_t)((cursor[0] >> YMUX_HISTORY_RUN_MARK_SHIFT) & 0xFFu);
            if (mark_count > YETTY_YMUX_CELL_MAX_MARKS) {
                mark_count = YETTY_YMUX_CELL_MAX_MARKS;
            }
            const uint32_t *marks = cursor + YMUX_HISTORY_CELL_RUN_WORDS;
            for (uint32_t step = 0; step < repeat && col < cols; ++step, ++col) {
                struct yetty_ymux_cell *cell = &entry->row_cells[row][col];
                cell->codepoint = cursor[1];
                cell->fg = cursor[2];
                cell->bg = cursor[3];
                cell->attrs = (uint16_t)(cursor[4] & 0xFFFFu);
                cell->width = (uint8_t)((cursor[4] >> 16) & 0xFFu);
                cell->mark_count = mark_count;
                for (uint8_t mark = 0; mark < mark_count; ++mark) {
                    cell->marks[mark] = marks[mark];
                }
            }
            cursor += YMUX_HISTORY_CELL_RUN_WORDS + mark_count;
        }
    }
    free(raw);
    entry->first_row = segment->first_row;
    entry->row_count = segment->row_count;
    entry->valid = 1;
    entry->last_used_tick = ++history->cache_tick;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Lifecycle + methods.
 *=========================================================================*/

static void history_teardown(struct yetty_ymux_history *history)
{
    for (uint32_t index = 0; index < history->hot_capacity && history->hot_rows; ++index) {
        free(history->hot_rows[index].cells);
    }
    free(history->hot_rows);
    history->hot_rows = NULL;
    for (uint32_t index = history->segment_head; index < history->segment_count; ++index) {
        free(history->segments[index].bytes);
    }
    free(history->segments);
    history->segments = NULL;
    free(history->builder.bytes);
    free(history->builder.row_offsets);
    memset(&history->builder, 0, sizeof(history->builder));
    for (uint32_t index = 0; index < YMUX_HISTORY_CACHE_ENTRIES; ++index) {
        cache_entry_free(&history->cache[index]);
    }
    if (history->spill_file) {
        fclose(history->spill_file);
        history->spill_file = NULL;
    }
}

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_history_make(uint32_t hot_rows,
                                                              uint64_t total_row_cap)
{
    struct yetty_yclass_ptr_result class_res = yetty_ymux_history_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux history_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux history_make: alloc");
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(object_res.value);
    if (YETTY_IS_ERR(history_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux history_make: from_obj", history_res);
    }
    struct yetty_ymux_history *history = history_res.value;
    history->hot_capacity = hot_rows ? hot_rows : YMUX_HISTORY_DEFAULT_HOT_ROWS;
    history->hot_rows = calloc(history->hot_capacity, sizeof(struct history_hot_row));
    if (!history->hot_rows) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux history_make: hot ring alloc");
    }
    history->warm_bytes_budget = YMUX_HISTORY_DEFAULT_WARM_BYTES;
    history->total_row_cap = total_row_cap;
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_history_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, history_res, "ymux history_dispose: from_obj");
    history_teardown(history_res.value);
    return yetty_yclass_object_free(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_history_set_budgets(struct yetty_yclass_object *obj,
                                                              uint64_t warm_bytes,
                                                              uint64_t file_max_bytes)
{
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, history_res, "ymux history_set_budgets: from_obj");
    history_res.value->warm_bytes_budget =
        warm_bytes ? (size_t)warm_bytes : (size_t)YMUX_HISTORY_DEFAULT_WARM_BYTES;
    history_res.value->file_bytes_budget = file_max_bytes;
    return YETTY_OK_VOID();
}

/* Push one row scrolling off a pane's screen top. Signature matches the
 * engine's scroll_out host callback so a pane can wire it directly. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_history_push(struct yetty_yclass_object *obj,
                                                       const struct yetty_ymux_cell *cells,
                                                       uint32_t cols, uint64_t logical_line_id,
                                                       uint32_t logical_cell_start,
                                                       int continuation)
{
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, history_res, "ymux history_push: from_obj");
    struct yetty_ymux_history *history = history_res.value;
    if (!cells || cols == 0) {
        return YETTY_ERR(yetty_ycore_void, "ymux history_push: invalid row");
    }

    /* Ring full: serialize the oldest hot row into the archive first. */
    if (history->hot_count == history->hot_capacity) {
        struct history_hot_row *oldest = &history->hot_rows[history->hot_head];
        if (history->builder.row_count == 0 && history->builder.byte_count == 0 &&
            history->builder.first_row == 0 && history->archived_rows != 0) {
            history->builder.first_row = history->archived_rows;
        }
        struct yetty_ycore_void_result serialize_res =
            builder_push_row(history, oldest->cells, oldest->cols, oldest->logical_line_id,
                             oldest->logical_cell_start, oldest->continuation);
        if (YETTY_IS_ERR(serialize_res)) {
            /* Degrade to dropping the row (floor moves past it later). */
            yetty_ycore_error_destroy(serialize_res.error);
        }
        ++history->archived_rows;
        free(oldest->cells);
        oldest->cells = NULL;
        history->hot_head = (history->hot_head + 1) % history->hot_capacity;
        --history->hot_count;

        if (history->builder.row_count >= YMUX_HISTORY_SEGMENT_MAX_ROWS ||
            history->builder.byte_count >= YMUX_HISTORY_SEGMENT_MAX_BYTES) {
            struct yetty_ycore_void_result seal_res = history_seal_builder(history);
            if (YETTY_IS_ERR(seal_res)) {
                yetty_ycore_error_destroy(seal_res.error);
            }
            history_enforce_warm_budget(history);
            history_enforce_row_cap(history);
        }
    }

    uint32_t slot = (history->hot_head + history->hot_count) % history->hot_capacity;
    struct history_hot_row *hot_row = &history->hot_rows[slot];
    hot_row->cells = malloc((size_t)cols * sizeof(struct yetty_ymux_cell));
    if (!hot_row->cells) {
        return YETTY_ERR(yetty_ycore_void, "ymux history_push: hot cells alloc");
    }
    memcpy(hot_row->cells, cells, (size_t)cols * sizeof(struct yetty_ymux_cell));
    hot_row->cols = cols;
    hot_row->logical_line_id = logical_line_id;
    hot_row->logical_cell_start = logical_cell_start;
    hot_row->continuation = continuation;
    ++history->hot_count;
    ++history->pushed_rows;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_history_pushed_rows(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, history_res, "ymux history_pushed_rows: from_obj");
    return YETTY_OK(yetty_ycore_uint64, history_res.value->pushed_rows);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_history_floor(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, history_res, "ymux history_floor: from_obj");
    return YETTY_OK(yetty_ycore_uint64, history_res.value->dropped_rows);
}

/* Resolve one retained row by timeline index. The returned pointers stay
 * valid until the next resolve/push that recycles their backing (hot ring
 * slot or cache entry) — callers copy what they keep. A dropped or
 * out-of-range index yields an error. */
YETTY_ANNOTATE("expose")
struct yetty_ymux_history_row_result yetty_ymux_history_resolve(struct yetty_yclass_object *obj,
                                                                uint64_t timeline_idx)
{
    struct yetty_ymux_history_ptr_result history_res = yetty_ymux_history_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ymux_history_row, history_res, "ymux history_resolve: from_obj");
    struct yetty_ymux_history *history = history_res.value;
    if (timeline_idx >= history->pushed_rows || timeline_idx < history->dropped_rows) {
        return YETTY_ERR(yetty_ymux_history_row, "ymux history_resolve: out of range");
    }

    if (timeline_idx >= history->archived_rows) {
        uint32_t hot_index = (uint32_t)(timeline_idx - history->archived_rows);
        uint32_t slot = (history->hot_head + hot_index) % history->hot_capacity;
        const struct history_hot_row *hot_row = &history->hot_rows[slot];
        struct yetty_ymux_history_row row = {
            .cells = hot_row->cells,
            .cols = hot_row->cols,
            .logical_line_id = hot_row->logical_line_id,
            .logical_cell_start = hot_row->logical_cell_start,
            .continuation = hot_row->continuation,
        };
        return YETTY_OK(yetty_ymux_history_row, row);
    }

    /* Archived: the open builder's rows are not yet sealed — seal on demand
     * so every archived row is resolvable. */
    if (history->builder.row_count && timeline_idx >= history->builder.first_row) {
        struct yetty_ycore_void_result seal_res = history_seal_builder(history);
        YETTY_RETURN_IF_ERR(yetty_ymux_history_row, seal_res, "ymux history_resolve: seal");
        history_enforce_warm_budget(history);
        history_enforce_row_cap(history);
        if (timeline_idx < history->dropped_rows) {
            return YETTY_ERR(yetty_ymux_history_row, "ymux history_resolve: dropped");
        }
    }

    /* Cached? */
    struct history_cache_entry *entry = NULL;
    for (uint32_t index = 0; index < YMUX_HISTORY_CACHE_ENTRIES; ++index) {
        struct history_cache_entry *candidate = &history->cache[index];
        if (candidate->valid && timeline_idx >= candidate->first_row &&
            timeline_idx < candidate->first_row + candidate->row_count) {
            entry = candidate;
            break;
        }
    }
    if (!entry) {
        /* Find the owning segment. */
        const struct history_segment *owner = NULL;
        for (uint32_t index = history->segment_head; index < history->segment_count; ++index) {
            const struct history_segment *segment = &history->segments[index];
            if (timeline_idx >= segment->first_row &&
                timeline_idx < segment->first_row + segment->row_count) {
                owner = segment;
                break;
            }
        }
        if (!owner) {
            return YETTY_ERR(yetty_ymux_history_row, "ymux history_resolve: segment not found");
        }
        /* Evict the least-recently-used entry. */
        struct history_cache_entry *victim = &history->cache[0];
        for (uint32_t index = 1; index < YMUX_HISTORY_CACHE_ENTRIES; ++index) {
            if (!history->cache[index].valid) {
                victim = &history->cache[index];
                break;
            }
            if (history->cache[index].last_used_tick < victim->last_used_tick) {
                victim = &history->cache[index];
            }
        }
        cache_entry_free(victim);
        struct yetty_ycore_void_result inflate_res = cache_inflate_segment(history, owner, victim);
        YETTY_RETURN_IF_ERR(yetty_ymux_history_row, inflate_res, "ymux history_resolve: inflate");
        entry = victim;
    }
    entry->last_used_tick = ++history->cache_tick;
    uint32_t row_index = (uint32_t)(timeline_idx - entry->first_row);
    if (!entry->row_cells[row_index]) {
        return YETTY_ERR(yetty_ymux_history_row, "ymux history_resolve: corrupt row");
    }
    struct yetty_ymux_history_row row = {
        .cells = entry->row_cells[row_index],
        .cols = entry->row_cols[row_index],
        .logical_line_id = entry->row_logical_ids[row_index],
        .logical_cell_start = entry->row_logical_starts[row_index],
        .continuation = entry->row_continuations[row_index],
    };
    return YETTY_OK(yetty_ymux_history_row, row);
}

#include "yetty/gen/impl/ymux/history.c"
