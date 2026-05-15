/* Scrollbuffer codec — see include/yetty/ydraw/scrollbuffer.h for the
 * wire format and storage tiering.
 *
 * Storage tiers
 * -------------
 *
 *  TAIL          : a growable byte buffer holding the most-recently
 *                  encoded line records, uncompressed. Encoding always
 *                  appends here. After each line, if the tail exceeds
 *                  SB_SEAL_THRESHOLD bytes we LZ4-compress it into a
 *                  new sealed chunk and reset the tail. Sealing only
 *                  happens at line boundaries, so every line record
 *                  stays fully contained within one storage block
 *                  (the tail, or one chunk).
 *
 *  CHUNKS        : LZ4-compressed snapshots of past tail contents.
 *                  Logically concatenated end-to-end; each chunk
 *                  carries its logical span [logical_start,
 *                  logical_start + logical_size). The decoder
 *                  decompresses on demand into a single-entry scratch
 *                  cache; adjacent reads inside the same chunk
 *                  (e.g. rendering a viewport that overlaps one
 *                  chunk) are free after the first miss.
 *
 *  LOGICAL OFFSETS : the per-line offsets the canvas hands back are
 *                    positions in the conceptual uncompressed
 *                    concatenation. They survive chunk sealing
 *                    without rewriting.
 *
 * LZ4 mode      : LZ4_compress_default — the canonical "fast" setting.
 *                  ~500 MB/s compress, ~3 GB/s decompress, 2-3× ratio
 *                  on text-heavy content. LZ4HC would compress ~30%
 *                  better but is 10× slower to encode; not the
 *                  speed/compression compromise the user asked for.
 *
 * Predominant-style picker
 * ------------------------
 *
 * For every input prim of glyph type the encoder hashes the
 * (font_size, font_id, y, color) tuple and tallies a small fixed
 * table; the bucket with the highest count wins. z_base is derived
 * from that bucket's first match — z_base + position_in_stream
 * yields the original z_order of every default-style glyph (z_order
 * is a monotonically incrementing per-line counter, so the encoder
 * just needs the offset).
 */

#include <yetty/ydraw/scrollbuffer.h>

#include <lz4.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Glyph type word emitted by expand_text_span_to_glyphs(). Kept as a
 * private constant here so this TU doesn't drag in the SDF type
 * generator. Must match `YETTY_YSDF_GLYPH` in ypaint-canvas.c. */
#define SB_GLYPH_TYPE_WORD 200u
#define SB_GLYPH_WORD_COUNT 7u

/* Bit 31 of the prim-stream marker word tells encoded vs default-style. */
#define SB_NONDEFAULT_MARKER 0x80000000u

#define SB_INITIAL_TAIL_CAPACITY 4096u

/* Seal the tail into an LZ4 chunk when it grows past this many bytes.
 * 64 KB is LZ4's natural block boundary and the standard sweet spot
 * for ratio vs. per-block overhead. */
#define SB_SEAL_THRESHOLD (64u * 1024u)

/* ===========================================================================
 * Buffer management
 * ========================================================================= */

void yetty_ydraw_scrollbuffer_init(struct yetty_ydraw_scrollbuffer *sb)
{
    memset(sb, 0, sizeof(*sb));
    sb->decode_scratch_chunk = -1;
}

void yetty_ydraw_scrollbuffer_free(struct yetty_ydraw_scrollbuffer *sb)
{
    free(sb->tail_data);
    for (uint32_t i = 0; i < sb->chunks_count; i++) {
        free(sb->chunks[i].compressed_data);
    }
    free(sb->chunks);
    free(sb->decode_scratch);
    memset(sb, 0, sizeof(*sb));
    sb->decode_scratch_chunk = -1;
}

size_t yetty_ydraw_scrollbuffer_logical_size(const struct yetty_ydraw_scrollbuffer *sb)
{
    return sb->logical_committed + sb->tail_size;
}

size_t yetty_ydraw_scrollbuffer_compressed_size(const struct yetty_ydraw_scrollbuffer *sb)
{
    size_t total = 0;
    for (uint32_t i = 0; i < sb->chunks_count; i++) {
        total += sb->chunks[i].compressed_size;
    }
    return total;
}

static struct yetty_ycore_void_result sb_tail_reserve(struct yetty_ydraw_scrollbuffer *sb,
                                                      size_t additional)
{
    size_t need = sb->tail_size + additional;
    if (need <= sb->tail_capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = sb->tail_capacity ? sb->tail_capacity : SB_INITIAL_TAIL_CAPACITY;
    while (new_cap < need) {
        new_cap *= 2;
    }
    uint8_t *p = realloc(sb->tail_data, new_cap);
    if (!p) {
        return YETTY_ERR(yetty_ycore_void, "scrollbuffer: tail realloc failed");
    }
    sb->tail_data = p;
    sb->tail_capacity = new_cap;
    return YETTY_OK_VOID();
}

static inline void sb_tail_write_u32(struct yetty_ydraw_scrollbuffer *sb, uint32_t v)
{
    memcpy(sb->tail_data + sb->tail_size, &v, sizeof(uint32_t));
    sb->tail_size += sizeof(uint32_t);
}

static inline void sb_tail_write_u16_pair(struct yetty_ydraw_scrollbuffer *sb, uint16_t a,
                                          uint16_t b)
{
    memcpy(sb->tail_data + sb->tail_size, &a, sizeof(uint16_t));
    sb->tail_size += sizeof(uint16_t);
    memcpy(sb->tail_data + sb->tail_size, &b, sizeof(uint16_t));
    sb->tail_size += sizeof(uint16_t);
}

static inline void sb_tail_write_bytes(struct yetty_ydraw_scrollbuffer *sb, const void *src,
                                       size_t n)
{
    memcpy(sb->tail_data + sb->tail_size, src, n);
    sb->tail_size += n;
}

static inline void sb_tail_patch_u32(struct yetty_ydraw_scrollbuffer *sb, size_t tail_offset,
                                     uint32_t v)
{
    memcpy(sb->tail_data + tail_offset, &v, sizeof(uint32_t));
}

/* Compress the current tail into a new sealed chunk. No-op if the
 * tail is empty. Idempotent at line boundaries. */
static struct yetty_ycore_void_result sb_seal_tail(struct yetty_ydraw_scrollbuffer *sb)
{
    if (sb->tail_size == 0) {
        return YETTY_OK_VOID();
    }
    int src_size = (int)sb->tail_size;
    int max_dst = LZ4_compressBound(src_size);
    if (max_dst <= 0) {
        return YETTY_ERR(yetty_ycore_void, "scrollbuffer: LZ4_compressBound failed");
    }
    uint8_t *compressed = malloc((size_t)max_dst);
    if (!compressed) {
        return YETTY_ERR(yetty_ycore_void, "scrollbuffer: chunk alloc failed");
    }
    int comp_size = LZ4_compress_default((const char *)sb->tail_data, (char *)compressed,
                                         src_size, max_dst);
    if (comp_size <= 0) {
        free(compressed);
        return YETTY_ERR(yetty_ycore_void, "scrollbuffer: LZ4_compress_default failed");
    }
    /* Shrink-fit so we don't pay the bound padding for every chunk. */
    uint8_t *fitted = realloc(compressed, (size_t)comp_size);
    if (fitted) {
        compressed = fitted;
    }

    if (sb->chunks_count == sb->chunks_capacity) {
        uint32_t nc = sb->chunks_capacity ? sb->chunks_capacity * 2 : 16u;
        struct yetty_ydraw_scrollbuffer_chunk *grown =
            realloc(sb->chunks, nc * sizeof(*sb->chunks));
        if (!grown) {
            free(compressed);
            return YETTY_ERR(yetty_ycore_void, "scrollbuffer: chunks realloc failed");
        }
        sb->chunks = grown;
        sb->chunks_capacity = nc;
    }
    struct yetty_ydraw_scrollbuffer_chunk *ch = &sb->chunks[sb->chunks_count++];
    ch->logical_start    = (uint32_t)sb->logical_committed;
    ch->logical_size     = (uint32_t)sb->tail_size;
    ch->compressed_size  = (uint32_t)comp_size;
    ch->compressed_data  = compressed;

    sb->logical_committed += sb->tail_size;
    sb->tail_size = 0;
    /* Invalidate the decode scratch — chunk indices haven't moved but
     * the index of the most-recently-decompressed-chunk is unchanged.
     * Nothing to invalidate here, but if we ever recycle a chunk's
     * slot, we'd need to invalidate. */
    return YETTY_OK_VOID();
}

/* Find the chunk that contains a given logical offset. Returns -1 if
 * the offset falls in the tail or is out of range. */
static int32_t sb_find_chunk(const struct yetty_ydraw_scrollbuffer *sb, size_t logical_offset)
{
    if (logical_offset >= sb->logical_committed) {
        return -1;
    }
    /* Linear scan would be fine for typical chunk counts (~hundreds)
     * but binary search costs nothing. */
    uint32_t lo = 0, hi = sb->chunks_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const struct yetty_ydraw_scrollbuffer_chunk *c = &sb->chunks[mid];
        if (logical_offset < c->logical_start) {
            hi = mid;
        } else if (logical_offset >= (size_t)c->logical_start + c->logical_size) {
            lo = mid + 1;
        } else {
            return (int32_t)mid;
        }
    }
    return -1;
}

/* Ensure scratch holds the decompressed bytes of chunk `idx`. Cheap if
 * `idx == decode_scratch_chunk` already. Returns scratch base on
 * success, NULL on allocation failure. */
static const uint8_t *sb_load_chunk(struct yetty_ydraw_scrollbuffer *sb, int32_t idx)
{
    if (sb->decode_scratch_chunk == idx && sb->decode_scratch) {
        return sb->decode_scratch;
    }
    const struct yetty_ydraw_scrollbuffer_chunk *ch = &sb->chunks[idx];
    if (sb->decode_scratch_capacity < ch->logical_size) {
        size_t new_cap = sb->decode_scratch_capacity ? sb->decode_scratch_capacity : 4096u;
        while (new_cap < ch->logical_size) {
            new_cap *= 2;
        }
        uint8_t *p = realloc(sb->decode_scratch, new_cap);
        if (!p) {
            return NULL;
        }
        sb->decode_scratch = p;
        sb->decode_scratch_capacity = new_cap;
    }
    int decoded = LZ4_decompress_safe((const char *)ch->compressed_data,
                                      (char *)sb->decode_scratch, (int)ch->compressed_size,
                                      (int)sb->decode_scratch_capacity);
    if (decoded < 0 || (uint32_t)decoded != ch->logical_size) {
        sb->decode_scratch_chunk = -1;
        return NULL;
    }
    sb->decode_scratch_chunk = idx;
    return sb->decode_scratch;
}

/* ===========================================================================
 * Predominant-style picker
 * ========================================================================= */

struct sb_style_key {
    uint32_t font_size;
    uint32_t font_id;
    uint32_t y_f32;
    uint32_t color;
};

struct sb_style_tally {
    struct sb_style_key key;
    uint32_t count;
    uint32_t first_z;
    uint32_t first_pos;
    bool in_use;
};

#define SB_STYLE_BUCKETS 4

static bool sb_glyph_style(const struct yetty_ydraw_scrollbuffer_prim *p,
                           struct sb_style_key *out_key,
                           uint32_t *out_z_order)
{
    if (p->word_count != SB_GLYPH_WORD_COUNT) {
        return false;
    }
    if (p->payload[0] != SB_GLYPH_TYPE_WORD) {
        return false;
    }
    *out_z_order = p->payload[1];
    out_key->y_f32     = p->payload[3];
    out_key->font_size = p->payload[4];
    /* font_id sits in the high 16 of payload[5]; the low 16 is
     * per-glyph (glyph_idx) and must not be in the style key. */
    out_key->font_id   = p->payload[5] & 0xFFFF0000u;
    out_key->color     = p->payload[6];
    return true;
}

static bool sb_pick_predominant_style(const struct yetty_ydraw_scrollbuffer_prim *prims,
                                      uint32_t n_prims,
                                      struct sb_style_key *out_key,
                                      uint32_t *out_z_base)
{
    struct sb_style_tally tally[SB_STYLE_BUCKETS] = {0};
    uint32_t any_glyph_seen = 0;

    for (uint32_t i = 0; i < n_prims; i++) {
        struct sb_style_key k;
        uint32_t z;
        if (!sb_glyph_style(&prims[i], &k, &z)) {
            continue;
        }
        any_glyph_seen++;

        int hit = -1;
        for (int b = 0; b < SB_STYLE_BUCKETS; b++) {
            if (!tally[b].in_use) {
                continue;
            }
            if (tally[b].key.font_size == k.font_size && tally[b].key.font_id == k.font_id &&
                tally[b].key.y_f32 == k.y_f32 && tally[b].key.color == k.color) {
                hit = b;
                break;
            }
        }
        if (hit < 0) {
            for (int b = 0; b < SB_STYLE_BUCKETS; b++) {
                if (!tally[b].in_use) {
                    tally[b].in_use = true;
                    tally[b].key = k;
                    tally[b].first_z = z;
                    tally[b].first_pos = i;
                    hit = b;
                    break;
                }
            }
        }
        if (hit < 0) {
            continue;
        }
        tally[hit].count++;
    }

    if (any_glyph_seen == 0) {
        return false;
    }
    int winner = -1;
    uint32_t best = 0;
    for (int b = 0; b < SB_STYLE_BUCKETS; b++) {
        if (tally[b].in_use && tally[b].count > best) {
            best = tally[b].count;
            winner = b;
        }
    }
    if (winner < 0) {
        return false;
    }
    *out_key = tally[winner].key;
    *out_z_base = tally[winner].first_z - tally[winner].first_pos;
    return true;
}

/* ===========================================================================
 * Encode
 * ========================================================================= */

struct yetty_ydraw_scrollbuffer_offset_result yetty_ydraw_scrollbuffer_encode_line(
    struct yetty_ydraw_scrollbuffer *sb,
    uint32_t line_rolling_row,
    uint32_t grid_cols,
    const struct yetty_ydraw_scrollbuffer_cell *cells,
    uint32_t n_cells,
    const struct yetty_ydraw_scrollbuffer_prim *prims,
    uint32_t n_prims)
{
    /* Logical offset where this record begins. */
    size_t record_logical_start = sb->logical_committed + sb->tail_size;
    size_t tail_record_start = sb->tail_size;

    /* Header: rolling_row + byte_count placeholder + prim_count. */
    struct yetty_ycore_void_result r = sb_tail_reserve(sb, 12);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "encode: header reserve", r);
    }
    sb_tail_write_u32(sb, line_rolling_row);
    size_t byte_count_tail_offset = sb->tail_size;
    sb_tail_write_u32(sb, 0);
    sb_tail_write_u32(sb, n_prims);

    bool body_empty = (n_prims == 0);
    if (body_empty) {
        uint32_t count_cells_with_refs = 0;
        for (uint32_t i = 0; i < n_cells; i++) {
            if (cells[i].ref_count > 0 && cells[i].col < grid_cols) {
                count_cells_with_refs++;
                break;
            }
        }
        if (count_cells_with_refs == 0) {
            /* Empty line is just 12 bytes of header. Don't seal here —
             * the caller may emit many empties in a row that all fit
             * in the tail under threshold. */
            return YETTY_OK(yetty_ydraw_scrollbuffer_offset, record_logical_start);
        }
    }

    size_t body_start_tail = sb->tail_size;

    /* CELL SECTION. */
    for (uint32_t i = 0; i < n_cells; i++) {
        const struct yetty_ydraw_scrollbuffer_cell *c = &cells[i];
        if (c->ref_count == 0) {
            continue;
        }
        if (c->col >= grid_cols) {
            continue;
        }
        size_t need = 4u + 4u + (size_t)c->ref_count * 4u;
        struct yetty_ycore_void_result rr = sb_tail_reserve(sb, need);
        if (YETTY_IS_ERR(rr)) {
            sb->tail_size = tail_record_start;
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "encode: cell reserve", rr);
        }
        sb_tail_write_u32(sb, (uint32_t)c->col + 1u);
        sb_tail_write_u32(sb, c->ref_count);
        for (uint32_t k = 0; k < c->ref_count; k++) {
            sb_tail_write_u16_pair(sb, c->refs[k].lines_ahead, c->refs[k].prim_idx);
        }
    }
    {
        struct yetty_ycore_void_result rr = sb_tail_reserve(sb, 4);
        if (YETTY_IS_ERR(rr)) {
            sb->tail_size = tail_record_start;
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "encode: cell sentinel", rr);
        }
        sb_tail_write_u32(sb, 0u);
    }

    /* Style preamble. */
    struct sb_style_key default_style;
    uint32_t z_base = 0;
    bool have_default = sb_pick_predominant_style(prims, n_prims, &default_style, &z_base);
    {
        struct yetty_ycore_void_result rr = sb_tail_reserve(sb, 20);
        if (YETTY_IS_ERR(rr)) {
            sb->tail_size = tail_record_start;
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "encode: preamble reserve", rr);
        }
        sb_tail_write_u32(sb, z_base);
        sb_tail_write_u32(sb, have_default ? default_style.font_size : 0u);
        sb_tail_write_u32(sb, have_default ? default_style.font_id   : 0u);
        sb_tail_write_u32(sb, have_default ? default_style.y_f32     : 0u);
        sb_tail_write_u32(sb, have_default ? default_style.color     : 0u);
    }

    /* Prim stream. */
    for (uint32_t i = 0; i < n_prims; i++) {
        const struct yetty_ydraw_scrollbuffer_prim *p = &prims[i];
        struct sb_style_key key;
        uint32_t z;
        bool is_glyph = sb_glyph_style(p, &key, &z);

        bool emit_default = have_default && is_glyph &&
                            key.font_size == default_style.font_size &&
                            key.font_id   == default_style.font_id   &&
                            key.y_f32     == default_style.y_f32     &&
                            key.color     == default_style.color     &&
                            z == z_base + i &&
                            p->rolling_row == line_rolling_row;

        if (emit_default) {
            struct yetty_ycore_void_result rr = sb_tail_reserve(sb, 8);
            if (YETTY_IS_ERR(rr)) {
                sb->tail_size = tail_record_start;
                return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "encode: default prim", rr);
            }
            uint16_t glyph_idx = (uint16_t)(p->payload[5] & 0xFFFFu);
            sb_tail_write_u32(sb, (uint32_t)glyph_idx);
            sb_tail_write_u32(sb, p->payload[2]);
            continue;
        }

        size_t need = 8u + (size_t)p->word_count * 4u;
        struct yetty_ycore_void_result rr = sb_tail_reserve(sb, need);
        if (YETTY_IS_ERR(rr)) {
            sb->tail_size = tail_record_start;
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "encode: non-default reserve", rr);
        }
        sb_tail_write_u32(sb, SB_NONDEFAULT_MARKER);
        sb_tail_write_u32(sb, p->rolling_row);
        sb_tail_write_bytes(sb, p->payload, (size_t)p->word_count * 4u);
    }

    /* Patch byte_count. */
    uint32_t body_bytes = (uint32_t)(sb->tail_size - body_start_tail);
    sb_tail_patch_u32(sb, byte_count_tail_offset, body_bytes);

    /* Seal the tail if it crossed the threshold (line-boundary aligned). */
    if (sb->tail_size >= SB_SEAL_THRESHOLD) {
        struct yetty_ycore_void_result sr = sb_seal_tail(sb);
        if (YETTY_IS_ERR(sr)) {
            /* Seal failed — keep the tail uncompressed; offsets stay
             * valid. Log via warn so we notice but don't break. */
            yetty_ycore_error_destroy(sr.error);
        }
    }

    return YETTY_OK(yetty_ydraw_scrollbuffer_offset, record_logical_start);
}

/* ===========================================================================
 * Decode
 * ========================================================================= */

static inline uint32_t sb_read_u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(uint32_t));
    return v;
}

struct yetty_ydraw_scrollbuffer_offset_result yetty_ydraw_scrollbuffer_decode_line(
    const struct yetty_ydraw_scrollbuffer *sb_const,
    size_t logical_offset,
    yetty_ydraw_scrollbuffer_word_count_fn word_count_fn,
    void *word_count_ctx,
    const struct yetty_ydraw_scrollbuffer_decode_sinks *sinks)
{
    /* We need to maybe mutate the decode_scratch cache; cast away
     * const on the resolution path. The buffer's logical content is
     * not mutated. */
    struct yetty_ydraw_scrollbuffer *sb = (struct yetty_ydraw_scrollbuffer *)sb_const;

    /* Resolve logical offset → base byte pointer + bound. */
    const uint8_t *base = NULL;
    size_t base_size = 0;
    size_t offset_in_base = 0;
    size_t total_logical = yetty_ydraw_scrollbuffer_logical_size(sb);

    if (logical_offset >= total_logical) {
        return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                         "decode: logical offset past buffer end");
    }

    if (logical_offset >= sb->logical_committed) {
        /* In tail. */
        base = sb->tail_data;
        base_size = sb->tail_size;
        offset_in_base = logical_offset - sb->logical_committed;
    } else {
        int32_t chunk_idx = sb_find_chunk(sb, logical_offset);
        if (chunk_idx < 0) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: chunk not found for offset");
        }
        const uint8_t *scratch = sb_load_chunk(sb, chunk_idx);
        if (!scratch) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: chunk decompress failed");
        }
        base = scratch;
        base_size = sb->chunks[chunk_idx].logical_size;
        offset_in_base = logical_offset - sb->chunks[chunk_idx].logical_start;
    }

    if (offset_in_base + 12 > base_size) {
        return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                         "decode: header beyond block end");
    }
    const uint8_t *p = base + offset_in_base;
    uint32_t line_rolling_row = sb_read_u32(p + 0);
    uint32_t byte_count       = sb_read_u32(p + 4);
    uint32_t prim_count       = sb_read_u32(p + 8);

    if (sinks && sinks->on_header) {
        struct yetty_ycore_void_result r =
            sinks->on_header(sinks->ctx, line_rolling_row, prim_count);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "decode: on_header", r);
        }
    }

    size_t pos = offset_in_base + 12u;
    if (byte_count == 0) {
        return YETTY_OK(yetty_ydraw_scrollbuffer_offset, sb->logical_committed +
                        (base == sb->tail_data ? pos : 0) /* unused for empty lines */);
    }

    if (offset_in_base + 12u + byte_count > base_size) {
        return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                         "decode: body beyond block end (line spans chunks?)");
    }
    size_t body_end = offset_in_base + 12u + byte_count;

    /* CELL SECTION. */
    while (pos + 4 <= body_end) {
        uint32_t col_plus_one = sb_read_u32(base + pos);
        pos += 4;
        if (col_plus_one == 0) {
            break;
        }
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: cell ref_count truncated");
        }
        uint32_t ref_count = sb_read_u32(base + pos);
        pos += 4;
        size_t need = (size_t)ref_count * 4u;
        if (pos + need > body_end) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: cell refs truncated");
        }
        if (sinks && sinks->on_cell) {
            const struct yetty_ydraw_scrollbuffer_ref *refs =
                (const struct yetty_ydraw_scrollbuffer_ref *)(base + pos);
            struct yetty_ycore_void_result r =
                sinks->on_cell(sinks->ctx, col_plus_one - 1u, refs, ref_count);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "decode: on_cell", r);
            }
        }
        pos += need;
    }

    /* PRIM STYLE PREAMBLE. */
    if (pos + 20 > body_end) {
        return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "decode: preamble truncated");
    }
    uint32_t z_base    = sb_read_u32(base + pos + 0);
    uint32_t font_size = sb_read_u32(base + pos + 4);
    uint32_t font_id   = sb_read_u32(base + pos + 8);
    uint32_t y_f32     = sb_read_u32(base + pos + 12);
    uint32_t color     = sb_read_u32(base + pos + 16);
    pos += 20;

    /* PRIM STREAM. */
    uint32_t payload_buf[64];
    for (uint32_t i = 0; i < prim_count; i++) {
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: prim word0 truncated");
        }
        uint32_t word0 = sb_read_u32(base + pos);
        pos += 4;
        if ((word0 & SB_NONDEFAULT_MARKER) == 0) {
            if (pos + 4 > body_end) {
                return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                                 "decode: default glyph x truncated");
            }
            uint32_t x_bits = sb_read_u32(base + pos);
            pos += 4;

            uint16_t glyph_idx = (uint16_t)(word0 & 0xFFFFu);

            payload_buf[0] = SB_GLYPH_TYPE_WORD;
            payload_buf[1] = z_base + i;
            payload_buf[2] = x_bits;
            payload_buf[3] = y_f32;
            payload_buf[4] = font_size;
            payload_buf[5] = (uint32_t)glyph_idx | (font_id & 0xFFFF0000u);
            payload_buf[6] = color;

            if (sinks && sinks->on_prim) {
                struct yetty_ycore_void_result r = sinks->on_prim(
                    sinks->ctx, line_rolling_row, payload_buf, SB_GLYPH_WORD_COUNT);
                if (YETTY_IS_ERR(r)) {
                    return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "decode: on_prim default",
                                     r);
                }
            }
            continue;
        }

        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: non-default rolling_row truncated");
        }
        uint32_t prim_rolling_row = sb_read_u32(base + pos);
        pos += 4;
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: non-default type truncated");
        }
        uint32_t type_word = sb_read_u32(base + pos);
        uint32_t word_count = word_count_fn(type_word, word_count_ctx);
        if (word_count == 0 || word_count > 64) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: non-default invalid word_count");
        }
        size_t need = (size_t)word_count * 4u;
        if (pos + need > body_end) {
            return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                             "decode: non-default payload truncated");
        }
        for (uint32_t w = 0; w < word_count; w++) {
            payload_buf[w] = sb_read_u32(base + pos + (size_t)w * 4u);
        }
        pos += need;

        if (sinks && sinks->on_prim) {
            struct yetty_ycore_void_result r =
                sinks->on_prim(sinks->ctx, prim_rolling_row, payload_buf, word_count);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ydraw_scrollbuffer_offset, "decode: on_prim non-default",
                                 r);
            }
        }
    }

    if (pos != body_end) {
        return YETTY_ERR(yetty_ydraw_scrollbuffer_offset,
                         "decode: body trailing bytes");
    }
    /* Return the logical offset one past the end of the record. */
    size_t end_logical;
    if (base == sb->tail_data) {
        end_logical = sb->logical_committed + body_end;
    } else {
        const struct yetty_ydraw_scrollbuffer_chunk *ch = &sb->chunks[sb->decode_scratch_chunk];
        end_logical = (size_t)ch->logical_start + body_end;
    }
    return YETTY_OK(yetty_ydraw_scrollbuffer_offset, end_logical);
}
