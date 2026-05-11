/* Scrollbuffer codec — see include/yetty/ypaint/scrollbuffer.h for the
 * wire format. This file implements only the encode/decode of one
 * line record at a time; the canvas owns the buffer lifecycle and
 * stitches per-line offsets into a scrollback index.
 *
 * Predominant-style picker
 * ------------------------
 *
 * For every input prim of glyph type the encoder hashes the
 * (z_base, font_size, font_id, y, color) tuple and tallies a small
 * fixed table; the bucket with the highest count wins. z_base is
 * derived from the FIRST glyph's z_order (since glyph z_order is a
 * monotonically incrementing per-line counter, the first glyph's z
 * is the line's z_base for the default-style stream). Glyphs sharing
 * the predominant (font_size, font_id, y, color) tuple AND whose
 * z_order matches `z_base + position_in_stream` emit as 8-byte
 * default records; everything else escalates to a non-default record
 * carrying its full payload.
 *
 * Buckets are kept tiny (4 entries). In practice a PDF text line has
 * one dominant style; a line that exceeds 4 distinct styles falls
 * back to "no glyphs were predominant", and every glyph emits as
 * non-default. The codec still round-trips correctly in that case —
 * just at the price of 36 B/glyph instead of 8.
 */

#include <yetty/ypaint/scrollbuffer.h>

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

#define SB_INITIAL_CAPACITY 4096u

/* ===========================================================================
 * Buffer management
 * ========================================================================= */

void yetty_ypaint_scrollbuffer_init(struct yetty_ypaint_scrollbuffer *sb)
{
    sb->data = NULL;
    sb->size = 0;
    sb->capacity = 0;
}

void yetty_ypaint_scrollbuffer_free(struct yetty_ypaint_scrollbuffer *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->size = 0;
    sb->capacity = 0;
}

static struct yetty_ycore_void_result sb_reserve(struct yetty_ypaint_scrollbuffer *sb,
                                                 size_t additional)
{
    size_t need = sb->size + additional;
    if (need <= sb->capacity) {
        return YETTY_OK_VOID();
    }
    size_t new_cap = sb->capacity ? sb->capacity : SB_INITIAL_CAPACITY;
    while (new_cap < need) {
        new_cap *= 2;
    }
    uint8_t *p = realloc(sb->data, new_cap);
    if (!p) {
        return YETTY_ERR(yetty_ycore_void, "scrollbuffer: realloc failed");
    }
    sb->data = p;
    sb->capacity = new_cap;
    return YETTY_OK_VOID();
}

static inline void sb_write_u32(struct yetty_ypaint_scrollbuffer *sb, uint32_t v)
{
    /* Caller must have ensured capacity via sb_reserve. */
    memcpy(sb->data + sb->size, &v, sizeof(uint32_t));
    sb->size += sizeof(uint32_t);
}

static inline void sb_write_u16_pair(struct yetty_ypaint_scrollbuffer *sb, uint16_t a, uint16_t b)
{
    memcpy(sb->data + sb->size, &a, sizeof(uint16_t));
    sb->size += sizeof(uint16_t);
    memcpy(sb->data + sb->size, &b, sizeof(uint16_t));
    sb->size += sizeof(uint16_t);
}

static inline void sb_write_bytes(struct yetty_ypaint_scrollbuffer *sb, const void *src, size_t n)
{
    memcpy(sb->data + sb->size, src, n);
    sb->size += n;
}

/* In-place u32 patch at an earlier offset (used to back-fill the
 * byte_count header field once the record's size is known). */
static inline void sb_patch_u32(struct yetty_ypaint_scrollbuffer *sb, size_t offset, uint32_t v)
{
    memcpy(sb->data + offset, &v, sizeof(uint32_t));
}

/* ===========================================================================
 * Predominant-style picker
 * ========================================================================= */

struct sb_style_key {
    uint32_t font_size;  /* bit-cast of f32 */
    uint32_t font_id;    /* the slot-packed word as emitted */
    uint32_t y_f32;      /* bit-cast of f32 */
    uint32_t color;      /* packed rgba8 */
};

struct sb_style_tally {
    struct sb_style_key key;
    uint32_t count;
    uint32_t first_z;    /* z_order of the first glyph matching this style */
    uint32_t first_pos;  /* prim-stream position of that first glyph */
    bool in_use;
};

#define SB_STYLE_BUCKETS 4

/* Extract style from a glyph payload. Returns false for non-glyph prims.
 * `font_id` is masked to the high 16 bits (the slot+1 value encoded by
 * expand_text_span_to_glyphs); the low 16 bits hold the per-glyph
 * `glyph_idx`, which is NOT a style attribute and must not be in the
 * key. */
static bool sb_glyph_style(const struct yetty_ypaint_scrollbuffer_prim *p,
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
    out_key->font_id   = p->payload[5] & 0xFFFF0000u;
    out_key->color     = p->payload[6];
    return true;
}

/* Walk the prims twice — first to tally styles, second to emit. The
 * first pass is needed before we know which style wins. Result fits in
 * a small fixed table: if a line has more than SB_STYLE_BUCKETS distinct
 * styles we drop styles past the cap and any glyph not in the picked
 * winner falls through to the non-default path. */
static bool sb_pick_predominant_style(const struct yetty_ypaint_scrollbuffer_prim *prims,
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

        /* Find existing bucket or claim a new one. */
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
            /* Too many distinct styles; bucket overflow. The lines we
             * care about (PDF body text) almost never hit this. */
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
    /* z_base is the z_order of the first glyph that matches the
     * winning style minus its position in the prim stream — so
     * z_base + pos == that glyph's z_order. For a line consisting of
     * a single span this comes out identical to the very first
     * glyph's z_order. */
    *out_z_base = tally[winner].first_z - tally[winner].first_pos;
    return true;
}

/* ===========================================================================
 * Encode
 * ========================================================================= */

struct yetty_ypaint_scrollbuffer_offset_result yetty_ypaint_scrollbuffer_encode_line(
    struct yetty_ypaint_scrollbuffer *sb,
    uint32_t line_rolling_row,
    uint32_t grid_cols,
    const struct yetty_ypaint_scrollbuffer_cell *cells,
    uint32_t n_cells,
    const struct yetty_ypaint_scrollbuffer_prim *prims,
    uint32_t n_prims)
{
    size_t record_start = sb->size;

    /* Header: rolling_row + byte_count placeholder + prim_count.
     * We'll back-fill byte_count after we know the final record size. */
    struct yetty_ycore_void_result r = sb_reserve(sb, 12);
    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "encode: header reserve", r);
    }
    sb_write_u32(sb, line_rolling_row);
    size_t byte_count_offset = sb->size;
    sb_write_u32(sb, 0);            /* byte_count, patched at the end */
    sb_write_u32(sb, n_prims);

    /* Skip the body for truly empty lines: no cells with refs AND no
     * prims. byte_count stays 0; reader bails after the header. */
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
            return YETTY_OK(yetty_ypaint_scrollbuffer_offset, record_start);
        }
    }

    size_t body_start = sb->size;

    /* CELL SECTION. Cells must be in ascending col order — caller's
     * responsibility; we just emit them and append the 0 sentinel. */
    for (uint32_t i = 0; i < n_cells; i++) {
        const struct yetty_ypaint_scrollbuffer_cell *c = &cells[i];
        if (c->ref_count == 0) {
            continue;
        }
        if (c->col >= grid_cols) {
            continue;   /* off-grid tail; not part of the wire record */
        }
        size_t need = 4u + 4u + (size_t)c->ref_count * 4u;
        struct yetty_ycore_void_result rr = sb_reserve(sb, need);
        if (YETTY_IS_ERR(rr)) {
            sb->size = record_start;
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "encode: cell reserve", rr);
        }
        sb_write_u32(sb, (uint32_t)c->col + 1u);
        sb_write_u32(sb, c->ref_count);
        for (uint32_t k = 0; k < c->ref_count; k++) {
            sb_write_u16_pair(sb, c->refs[k].lines_ahead, c->refs[k].prim_idx);
        }
    }
    /* Cell-section sentinel. */
    {
        struct yetty_ycore_void_result rr = sb_reserve(sb, 4);
        if (YETTY_IS_ERR(rr)) {
            sb->size = record_start;
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "encode: cell sentinel", rr);
        }
        sb_write_u32(sb, 0u);
    }

    /* Decide predominant style once. */
    struct sb_style_key default_style;
    uint32_t z_base = 0;
    bool have_default = sb_pick_predominant_style(prims, n_prims, &default_style, &z_base);

    /* PRIM STYLE PREAMBLE — always 20 B even if no default style;
     * matches the format spec and keeps offsets predictable. Empty
     * preamble (all zeros) is harmless because the decoder only reads
     * it when a default-style glyph is present, which by definition
     * implies a winning style was picked. */
    {
        struct yetty_ycore_void_result rr = sb_reserve(sb, 20);
        if (YETTY_IS_ERR(rr)) {
            sb->size = record_start;
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "encode: preamble reserve", rr);
        }
        sb_write_u32(sb, z_base);
        sb_write_u32(sb, have_default ? default_style.font_size : 0u);
        sb_write_u32(sb, have_default ? default_style.font_id   : 0u);
        sb_write_u32(sb, have_default ? default_style.y_f32     : 0u);
        sb_write_u32(sb, have_default ? default_style.color     : 0u);
    }

    /* PRIM STREAM — one record per prim, in input order. */
    for (uint32_t i = 0; i < n_prims; i++) {
        const struct yetty_ypaint_scrollbuffer_prim *p = &prims[i];
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
            struct yetty_ycore_void_result rr = sb_reserve(sb, 8);
            if (YETTY_IS_ERR(rr)) {
                sb->size = record_start;
                return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "encode: default prim", rr);
            }
            /* glyph_idx lives in low 16 of payload[5]; we re-extract it
             * straight from the payload so the encoder doesn't have to
             * know about the slot-packed encoding. */
            uint16_t glyph_idx = (uint16_t)(p->payload[5] & 0xFFFFu);
            sb_write_u32(sb, (uint32_t)glyph_idx);
            sb_write_u32(sb, p->payload[2]);   /* f32 x (raw bits) */
            continue;
        }

        /* Non-default record: marker + rolling_row + full payload. */
        size_t need = 8u + (size_t)p->word_count * 4u;
        struct yetty_ycore_void_result rr = sb_reserve(sb, need);
        if (YETTY_IS_ERR(rr)) {
            sb->size = record_start;
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "encode: non-default reserve", rr);
        }
        sb_write_u32(sb, SB_NONDEFAULT_MARKER);
        sb_write_u32(sb, p->rolling_row);
        sb_write_bytes(sb, p->payload, (size_t)p->word_count * 4u);
    }

    /* Patch byte_count. */
    uint32_t body_bytes = (uint32_t)(sb->size - body_start);
    sb_patch_u32(sb, byte_count_offset, body_bytes);

    return YETTY_OK(yetty_ypaint_scrollbuffer_offset, record_start);
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

static inline uint16_t sb_read_u16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(uint16_t));
    return v;
}

struct yetty_ypaint_scrollbuffer_offset_result yetty_ypaint_scrollbuffer_decode_line(
    const struct yetty_ypaint_scrollbuffer *sb,
    size_t offset,
    yetty_ypaint_scrollbuffer_word_count_fn word_count_fn,
    void *word_count_ctx,
    const struct yetty_ypaint_scrollbuffer_decode_sinks *sinks)
{
    if (offset + 12 > sb->size) {
        return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                         "decode: header beyond buffer end");
    }
    const uint8_t *p = sb->data + offset;
    uint32_t line_rolling_row = sb_read_u32(p + 0);
    uint32_t byte_count       = sb_read_u32(p + 4);
    uint32_t prim_count       = sb_read_u32(p + 8);

    if (sinks && sinks->on_header) {
        struct yetty_ycore_void_result r =
            sinks->on_header(sinks->ctx, line_rolling_row, prim_count);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "decode: on_header", r);
        }
    }

    size_t pos = offset + 12u;
    if (byte_count == 0) {
        /* Empty line — record ends here. */
        return YETTY_OK(yetty_ypaint_scrollbuffer_offset, pos);
    }

    if (offset + 12u + byte_count > sb->size) {
        return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                         "decode: body beyond buffer end");
    }
    size_t body_end = offset + 12u + byte_count;

    /* CELL SECTION. */
    while (pos + 4 <= body_end) {
        uint32_t col_plus_one = sb_read_u32(sb->data + pos);
        pos += 4;
        if (col_plus_one == 0) {
            break;
        }
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: cell ref_count truncated");
        }
        uint32_t ref_count = sb_read_u32(sb->data + pos);
        pos += 4;
        size_t need = (size_t)ref_count * 4u;
        if (pos + need > body_end) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: cell refs truncated");
        }
        if (sinks && sinks->on_cell) {
            /* Hand off a typed view of the refs without copying. */
            const struct yetty_ypaint_scrollbuffer_ref *refs =
                (const struct yetty_ypaint_scrollbuffer_ref *)(sb->data + pos);
            struct yetty_ycore_void_result r =
                sinks->on_cell(sinks->ctx, col_plus_one - 1u, refs, ref_count);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "decode: on_cell", r);
            }
        }
        pos += need;
    }

    /* PRIM STYLE PREAMBLE — 20 B. */
    if (pos + 20 > body_end) {
        return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "decode: preamble truncated");
    }
    uint32_t z_base    = sb_read_u32(sb->data + pos + 0);
    uint32_t font_size = sb_read_u32(sb->data + pos + 4);
    uint32_t font_id   = sb_read_u32(sb->data + pos + 8);
    uint32_t y_f32     = sb_read_u32(sb->data + pos + 12);
    uint32_t color     = sb_read_u32(sb->data + pos + 16);
    pos += 20;

    /* PRIM STREAM. */
    uint32_t payload_buf[64]; /* room for any reasonable prim payload */
    for (uint32_t i = 0; i < prim_count; i++) {
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: prim word0 truncated");
        }
        uint32_t word0 = sb_read_u32(sb->data + pos);
        pos += 4;
        if ((word0 & SB_NONDEFAULT_MARKER) == 0) {
            /* Default-style glyph: word1 = x bits; reconstruct payload. */
            if (pos + 4 > body_end) {
                return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                                 "decode: default glyph x truncated");
            }
            uint32_t x_bits = sb_read_u32(sb->data + pos);
            pos += 4;

            uint16_t glyph_idx = (uint16_t)(word0 & 0xFFFFu);

            payload_buf[0] = SB_GLYPH_TYPE_WORD;
            payload_buf[1] = z_base + i;
            payload_buf[2] = x_bits;
            payload_buf[3] = y_f32;
            payload_buf[4] = font_size;
            /* Rebuild slot-packed glyph_idx + font_id. The decoder
             * extracted plain glyph_idx; font_id (already slot+1 in
             * the high 16) comes verbatim from the preamble. */
            payload_buf[5] = (uint32_t)glyph_idx | (font_id & 0xFFFF0000u);
            payload_buf[6] = color;

            if (sinks && sinks->on_prim) {
                struct yetty_ycore_void_result r = sinks->on_prim(
                    sinks->ctx, line_rolling_row, payload_buf, SB_GLYPH_WORD_COUNT);
                if (YETTY_IS_ERR(r)) {
                    return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "decode: on_prim default",
                                     r);
                }
            }
            continue;
        }

        /* Non-default record: rolling_row + payload[N]. */
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: non-default rolling_row truncated");
        }
        uint32_t prim_rolling_row = sb_read_u32(sb->data + pos);
        pos += 4;
        /* Read the type word to learn the payload size, then the rest. */
        if (pos + 4 > body_end) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: non-default type truncated");
        }
        uint32_t type_word = sb_read_u32(sb->data + pos);
        uint32_t word_count = word_count_fn(type_word, word_count_ctx);
        if (word_count == 0 || word_count > 64) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: non-default invalid word_count");
        }
        size_t need = (size_t)word_count * 4u;
        if (pos + need > body_end) {
            return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                             "decode: non-default payload truncated");
        }
        for (uint32_t w = 0; w < word_count; w++) {
            payload_buf[w] = sb_read_u32(sb->data + pos + (size_t)w * 4u);
        }
        pos += need;

        if (sinks && sinks->on_prim) {
            struct yetty_ycore_void_result r =
                sinks->on_prim(sinks->ctx, prim_rolling_row, payload_buf, word_count);
            if (YETTY_IS_ERR(r)) {
                return YETTY_ERR(yetty_ypaint_scrollbuffer_offset, "decode: on_prim non-default",
                                 r);
            }
        }
    }

    if (pos != body_end) {
        /* Trailing bytes inside the body — the producer wrote more
         * than it accounted for. Surface as an error so the test
         * suite catches encoder bugs. */
        return YETTY_ERR(yetty_ypaint_scrollbuffer_offset,
                         "decode: body trailing bytes");
    }
    return YETTY_OK(yetty_ypaint_scrollbuffer_offset, body_end);
}
