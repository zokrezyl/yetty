/*
 * text-span-prim.c - flyweight TEXT_SPAN primitive (see text-span-prim.h).
 */

#include <yetty/ypaint-core/text-span-prim.h>
#include "text-span-prim-internal.h"

#include <math.h>
#include <string.h>

#define TEXT_SPAN_PRIM_HEADER 8u
/* Fixed payload prefix:
 *   v1 (32 bytes): x,y,font_size,rotation,color,layer,font_id,text_len
 *   v2 (40 bytes): + char_spacing, word_spacing
 * Parsers accept either size — the v2 trailing fields default to 0 when
 * the prim was emitted by an older producer. */
#define TEXT_SPAN_FIXED_BYTES_V1 32u
#define TEXT_SPAN_FIXED_BYTES 40u

static inline uint32_t align4(uint32_t n)
{
    return (n + 3u) & ~3u;
}

static uint32_t text_span_payload_size(uint32_t text_len)
{
    return align4(TEXT_SPAN_FIXED_BYTES + text_len);
}

size_t yetty_ypaint_core_text_span_prim_size_for(uint32_t text_len)
{
    return TEXT_SPAN_PRIM_HEADER + text_span_payload_size(text_len);
}

void yetty_ypaint_core_text_span_prim_write_full(uint8_t *out, float x, float y, float font_size,
                                                 float rotation, uint32_t color, uint32_t layer,
                                                 int32_t font_id, const char *text,
                                                 uint32_t text_len, float char_spacing,
                                                 float word_spacing)
{
    uint32_t payload_size = text_span_payload_size(text_len);
    size_t total = TEXT_SPAN_PRIM_HEADER + payload_size;
    memset(out, 0, total);

    uint32_t type = YETTY_YPAINT_TYPE_TEXT_SPAN;
    memcpy(out + 0, &type, 4);
    memcpy(out + 4, &payload_size, 4);

    uint8_t *p = out + TEXT_SPAN_PRIM_HEADER;
    memcpy(p, &x, 4);
    p += 4;
    memcpy(p, &y, 4);
    p += 4;
    memcpy(p, &font_size, 4);
    p += 4;
    memcpy(p, &rotation, 4);
    p += 4;
    memcpy(p, &color, 4);
    p += 4;
    memcpy(p, &layer, 4);
    p += 4;
    memcpy(p, &font_id, 4);
    p += 4;
    memcpy(p, &text_len, 4);
    p += 4;
    memcpy(p, &char_spacing, 4);
    p += 4;
    memcpy(p, &word_spacing, 4);
    p += 4;
    if (text_len) {
        memcpy(p, text, text_len);
    }
}

void yetty_ypaint_core_text_span_prim_write(uint8_t *out, float x, float y, float font_size,
                                            float rotation, uint32_t color, uint32_t layer,
                                            int32_t font_id, const char *text, uint32_t text_len)
{
    yetty_ypaint_core_text_span_prim_write_full(out, x, y, font_size, rotation, color, layer,
                                                font_id, text, text_len, 0.0f, 0.0f);
}

int yetty_ypaint_core_text_span_prim_parse(const uint32_t *prim,
                                           struct yetty_ypaint_core_text_span_prim_view *out)
{
    if (!prim || !out) {
        return -1;
    }

    uint32_t type, payload_size;
    memcpy(&type, prim, 4);
    memcpy(&payload_size, (const uint8_t *)prim + 4, 4);
    if (type != YETTY_YPAINT_TYPE_TEXT_SPAN) {
        return -1;
    }
    /* Accept either v1 (32-byte fixed prefix) or v2 (40-byte fixed prefix
     * with char_spacing + word_spacing trailing). */
    int has_spacing = payload_size >= TEXT_SPAN_FIXED_BYTES;
    if (payload_size < TEXT_SPAN_FIXED_BYTES_V1) {
        return -1;
    }

    const uint8_t *p = (const uint8_t *)prim + TEXT_SPAN_PRIM_HEADER;
    const uint8_t *end = p + payload_size;

    memcpy(&out->x, p, 4);
    p += 4;
    memcpy(&out->y, p, 4);
    p += 4;
    memcpy(&out->font_size, p, 4);
    p += 4;
    memcpy(&out->rotation, p, 4);
    p += 4;
    memcpy(&out->color, p, 4);
    p += 4;
    memcpy(&out->layer, p, 4);
    p += 4;
    memcpy(&out->font_id, p, 4);
    p += 4;
    memcpy(&out->text_len, p, 4);
    p += 4;
    if (has_spacing) {
        memcpy(&out->char_spacing, p, 4);
        p += 4;
        memcpy(&out->word_spacing, p, 4);
        p += 4;
    } else {
        out->char_spacing = 0.0f;
        out->word_spacing = 0.0f;
    }
    if ((size_t)(end - p) < out->text_len) {
        return -1;
    }
    out->text = (const char *)p;
    return 0;
}

/*=============================================================================
 * Flyweight base ops
 *===========================================================================*/

static struct yetty_ycore_size_result text_span_prim_size(const uint32_t *prim)
{
    uint32_t payload_size;
    memcpy(&payload_size, (const uint8_t *)prim + 4, 4);
    return YETTY_OK(yetty_ycore_size, TEXT_SPAN_PRIM_HEADER + (size_t)payload_size);
}

/* Coarse AABB — the canvas decomposes a TEXT_SPAN into per-glyph SDF
 * prims (each with an exact AABB) before any spatial-grid placement,
 * so this is only used for the PASS-1 max-row computation. Width is
 * estimated at 0.6 * font_size per UTF-8 byte (over-estimate for
 * multi-byte sequences but never under-estimates the row span). */
static struct rectangle_result text_span_prim_aabb(const uint32_t *prim)
{
    struct yetty_ypaint_core_text_span_prim_view v;
    if (yetty_ypaint_core_text_span_prim_parse(prim, &v) < 0) {
        return YETTY_ERR(rectangle, "malformed TEXT_SPAN");
    }

    float w_est = 0.6f * v.font_size * (float)v.text_len;
    float h_est = v.font_size;
    struct yetty_ycore_rectangle r = {
        .min = {.x = v.x, .y = v.y - h_est},
        .max = {.x = v.x + w_est, .y = v.y},
    };
    (void)fabsf;
    return YETTY_OK(rectangle, r);
}

static const struct yetty_ypaint_core_prim_base_ops g_text_span_prim_base_ops = {
    .size = text_span_prim_size,
    .aabb = text_span_prim_aabb,
};

struct yetty_ypaint_core_prim_base_ops_ptr_result yetty_ypaint_core_text_span_prim_handler(
    uint32_t prim_type)
{
    if (prim_type == YETTY_YPAINT_TYPE_TEXT_SPAN) {
        return YETTY_OK(yetty_ypaint_core_prim_base_ops_ptr, &g_text_span_prim_base_ops);
    }
    return YETTY_ERR(yetty_ypaint_core_prim_base_ops_ptr, "not TEXT_SPAN");
}
