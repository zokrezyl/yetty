/* Scrollbuffer — binary format for serialised canvas lines.
 *
 * A growable byte buffer that holds per-line records in a compact,
 * round-trippable binary form. Used by the canvas to retain evicted
 * scrollback without keeping its full expanded grid_line struct.
 *
 * The codec is decoupled from grid_line: callers feed it "view"
 * structs (cells and prims as arrays), and decode receives a "view"
 * back via caller-provided callbacks. This keeps the format testable
 * in isolation and frees the canvas from publishing its internal
 * types in a header.
 *
 * Per-line wire format
 * --------------------
 *
 *   HEADER (12 B):
 *     u32 line_rolling_row     ; line's storage row, sanity check
 *     u32 byte_count           ; bytes after this header; 0 = empty
 *     u32 prim_count           ; total prims (default + non-default)
 *
 *   if byte_count > 0:
 *
 *   CELL SECTION (variable, ends with col-plus-one == 0 sentinel):
 *     repeated:
 *       u32 col_plus_one       ; 0-based col + 1; sentinel 0
 *       u32 ref_count
 *       (u16 lines_ahead, u16 prim_idx) × ref_count
 *
 *   PRIM STYLE PREAMBLE (20 B):
 *     u32 z_base               ; z_order of the first default-style glyph
 *     u32 font_size_f32        ; bit-cast of f32 font_size
 *     u32 font_id              ; packed font slot (high 16) as emitted
 *     u32 y_f32                ; bit-cast of f32 baseline y
 *     u32 color                ; packed rgba8
 *
 *   PRIM STREAM (variable, one entry per prim_count):
 *     Default-style glyph (8 B; bit 31 clear):
 *       u32 word0 = glyph_idx          ; bit 31 = 0; low 16 = glyph_idx
 *       u32 word1 = bitcast<u32>(x)    ; f32 x
 *       (rolling_row inferred = line_rolling_row)
 *       (type = GLYPH; z = z_base + position-in-stream; font/y/color from
 *        the preamble)
 *
 *     Non-default record (8 + 4*N B; bit 31 set):
 *       u32 word0 = 0x80000000
 *       u32 word1 = prim_rolling_row
 *       u32 payload[N]                 ; N = type_words(payload[0])
 *
 * Endianness: all u32 / u16 fields are host-native. Scrollbuffer state
 * is process-local — never persisted across runs.
 */
#ifndef YETTY_YPAINT_SCROLLBUFFER_H
#define YETTY_YPAINT_SCROLLBUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * Storage (opaque)
 * ========================================================================= */

struct yetty_ypaint_scrollbuffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
};

void yetty_ypaint_scrollbuffer_init(struct yetty_ypaint_scrollbuffer *sb);
void yetty_ypaint_scrollbuffer_free(struct yetty_ypaint_scrollbuffer *sb);

/* ===========================================================================
 * Encoder inputs (view structs — caller assembles from its own state)
 * ========================================================================= */

/* Reference from one grid cell to a prim, as stored on the canvas today. */
struct yetty_ypaint_scrollbuffer_ref {
    uint16_t lines_ahead;
    uint16_t prim_idx;
};

/* One cell that has at least one ref. `col` is 0-based grid column. */
struct yetty_ypaint_scrollbuffer_cell {
    uint16_t col;
    uint16_t ref_count;
    const struct yetty_ypaint_scrollbuffer_ref *refs;
};

/* One primitive's payload. word_count must match the flyweight of
 * payload[0] (the type word); the decoder will trust the codec is
 * consistent with the canvas's flyweight. */
struct yetty_ypaint_scrollbuffer_prim {
    uint32_t rolling_row;
    uint32_t word_count;
    const uint32_t *payload;
};

/* Encode one line at the end of `sb`. Returns the byte offset where
 * the record starts (so the caller can record it in a per-line index).
 *
 * `cells` must be in ascending `col` order. The function silently
 * skips cells with `col >= grid_cols` so callers don't have to filter.
 *
 * The encoder scans `prims` to pick the predominant style — the
 * (z_base, font_size, font_id, y, color) tuple shared by the most
 * glyph-typed prims. Glyphs matching that tuple emit as default-style
 * 8-byte records; everything else (different style or non-glyph type)
 * emits as a non-default record carrying the full payload.
 */
YETTY_YRESULT_DECLARE(yetty_ypaint_scrollbuffer_offset, size_t);

struct yetty_ypaint_scrollbuffer_offset_result yetty_ypaint_scrollbuffer_encode_line(
    struct yetty_ypaint_scrollbuffer *sb,
    uint32_t line_rolling_row,
    uint32_t grid_cols,
    const struct yetty_ypaint_scrollbuffer_cell *cells,
    uint32_t n_cells,
    const struct yetty_ypaint_scrollbuffer_prim *prims,
    uint32_t n_prims);

/* ===========================================================================
 * Decoder
 * =========================================================================
 *
 * The decoder calls back into caller-supplied sinks as it walks the
 * record. Callbacks may abort by returning a non-OK Result. The
 * decoder will then propagate that result.
 */

struct yetty_ypaint_scrollbuffer_decode_sinks {
    void *ctx;

    /* Optional: called once per record after the header is read, with
     * the line's own rolling_row and the prim_count from the header.
     * Useful for the caller to pre-size its output arrays. */
    struct yetty_ycore_void_result (*on_header)(void *ctx,
                                                uint32_t line_rolling_row,
                                                uint32_t prim_count);

    /* Called for each non-empty cell, in ascending col order.
     * `refs` and `ref_count` describe that cell's refs. */
    struct yetty_ycore_void_result (*on_cell)(void *ctx,
                                              uint32_t col,
                                              const struct yetty_ypaint_scrollbuffer_ref *refs,
                                              uint32_t ref_count);

    /* Called for each primitive, in order. `payload` is a fully
     * reconstructed 7-word glyph (for default-style records) or the
     * exact payload of the non-default record. `rolling_row` is the
     * prim's anchor row (line_rolling_row for default-style glyphs,
     * the explicit value for non-default records). */
    struct yetty_ycore_void_result (*on_prim)(void *ctx,
                                              uint32_t rolling_row,
                                              const uint32_t *payload,
                                              uint32_t word_count);
};

/* For decoding non-default records, the codec needs to know the size
 * of each prim's payload from its type word. The canvas already owns
 * a flyweight registry that maps type → word_count; pass it via this
 * trampoline so the codec doesn't depend on the flyweight header. */
typedef uint32_t (*yetty_ypaint_scrollbuffer_word_count_fn)(uint32_t type_word, void *ctx);

/* Decode the record that starts at `offset`. Returns the byte offset
 * one past the end of the record (which equals the next record's
 * start, if any).
 *
 * On any decoder/sink error the returned offset is the input offset
 * and the result carries the error. */
struct yetty_ypaint_scrollbuffer_offset_result yetty_ypaint_scrollbuffer_decode_line(
    const struct yetty_ypaint_scrollbuffer *sb,
    size_t offset,
    yetty_ypaint_scrollbuffer_word_count_fn word_count_fn,
    void *word_count_ctx,
    const struct yetty_ypaint_scrollbuffer_decode_sinks *sinks);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPAINT_SCROLLBUFFER_H */
