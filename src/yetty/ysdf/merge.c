/*
 * merge.c — splice one drawable list into another under a 2D affine
 * transform. See merge.h for the contract and coverage notes.
 *
 * The walker mirrors the ydraw_embed widget's stream walk: SDF primitives
 * are fixed-size (plus an optional id word); every other record is
 * [type][payload_size][payload...].
 */

#include <yetty/ysdf/merge.h>

#include <yetty/ydraw-list/cmds.h>
#include <yetty/ydraw-list/complex.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ydraw-list/font-resource.h>
#include <yetty/ydraw-list/text-drawable-list.h>
#include <yetty/ysdf/types.gen.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MERGE_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

/* Size in bytes of the record at `prim`, or 0 when it cannot fit in
 * `remaining` (malformed stream). */
static size_t merge_record_size(const uint32_t *prim, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];
    uint32_t base = MERGE_TYPE_BASE(type);
    size_t sdf_bytes = yetty_ysdf_primitive_size(base);
    if (sdf_bytes > 0) {
        size_t record_bytes = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        return record_bytes <= remaining ? record_bytes : 0;
    }
    if (remaining < 2 * sizeof(uint32_t)) {
        return 0;
    }
    uint32_t payload_size = prim[1];
    size_t record_bytes = 2 * sizeof(uint32_t) + payload_size;
    return record_bytes <= remaining ? record_bytes : 0;
}

/* Transform the copied record in place. `words` is the record length in
 * 32-bit words. */
static void merge_transform_record(uint32_t *prim, size_t words, float offset_x, float offset_y,
                                   float scale_x, float scale_y, uint32_t z_order_offset)
{
    uint32_t type = prim[0];
    uint32_t base = MERGE_TYPE_BASE(type);
    float scale_mean = 0.5f * (scale_x + scale_y);
    if (yetty_ysdf_primitive_size(base) > 0) {
        /* SDF layout: [type][id?][z_order][fill][stroke][stroke_width][geom...] */
        size_t shift = (type & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
        size_t geometry_word = 5u + shift;
        if (words < geometry_word) {
            return;
        }
        float *floats = (float *)prim;
        prim[1 + shift] += z_order_offset;
        floats[4 + shift] *= scale_mean; /* stroke_width — line widths scale too */
        yetty_ysdf_geometry_transform(base, floats + geometry_word, offset_x, offset_y, scale_x,
                                      scale_y);
        return;
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST && words >= 8) {
        /* [type][payload_size][x][y][font_size][rotation][color][layer]... */
        float *floats = (float *)prim;
        floats[2] = floats[2] * scale_x + offset_x;
        floats[3] = floats[3] * scale_y + offset_y;
        floats[4] *= scale_mean;
        prim[7] += z_order_offset;
        return;
    }
    if (yetty_ydraw_is_complex(type) && words >= 2 + 4) {
        /* Complex figure record: [type][payload_size][bounds_x][bounds_y]
		 * [bounds_w][bounds_h][...]. Every complex serializer starts its
		 * payload with the four bounds floats (complex_record_aabb reads
		 * the same fixed offsets), so the embed transform maps them like
		 * any other geometry — this is what places an SVG <image> raster
		 * correctly when the scene is merged under a box transform. */
        float *bounds = (float *)(prim + 2);
        bounds[0] = bounds[0] * scale_x + offset_x;
        bounds[1] = bounds[1] * scale_y + offset_y;
        bounds[2] *= scale_x;
        bounds[3] *= scale_y;
        return;
    }
    /* FONT resources and structured commands pass through unchanged — see
	 * the coverage notes in merge.h. */
}

struct yetty_ycore_int_result yetty_ydraw_drawable_list_merge_transformed(
    struct yetty_ydraw_drawable_list *destination, const struct yetty_ydraw_drawable_list *source,
    float offset_x, float offset_y, float scale_x, float scale_y, uint32_t z_order_offset)
{
    if (!destination || !source) {
        return YETTY_ERR(yetty_ycore_int, "drawable_list_merge: null list");
    }
    const uint8_t *stream = (const uint8_t *)yetty_ydraw_drawable_list_data(source);
    size_t remaining = yetty_ydraw_drawable_list_size(source);
    if (!stream || remaining == 0) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    int merged_count = 0;
    uint8_t stack_slot[4096];
    uint8_t *heap_slot = NULL;
    size_t heap_cap = 0;
    while (remaining >= sizeof(uint32_t)) {
        size_t record_bytes = merge_record_size((const uint32_t *)stream, remaining);
        if (record_bytes == 0) {
            free(heap_slot);
            return YETTY_ERR(yetty_ycore_int,
                             "drawable_list_merge: malformed record (unknown type or "
                             "size overruns the source stream)");
        }
        /* Embedded content never clears the host canvas. */
        if (((const uint32_t *)stream)[0] == YETTY_YDRAW_CMD_ZERO) {
            stream += record_bytes;
            remaining -= record_bytes;
            continue;
        }
        uint8_t *work = stack_slot;
        if (record_bytes > sizeof(stack_slot)) {
            if (record_bytes > heap_cap) {
                uint8_t *grown = realloc(heap_slot, record_bytes);
                if (!grown) {
                    free(heap_slot);
                    return YETTY_ERR(yetty_ycore_int, "drawable_list_merge: oom");
                }
                heap_slot = grown;
                heap_cap = record_bytes;
            }
            work = heap_slot;
        }
        memcpy(work, stream, record_bytes);
        merge_transform_record((uint32_t *)work, record_bytes / sizeof(uint32_t), offset_x,
                               offset_y, scale_x, scale_y, z_order_offset);
        struct yetty_ydraw_id_result add_res =
            yetty_ydraw_drawable_list_add_prim(destination, work, record_bytes);
        if (YETTY_IS_ERR(add_res)) {
            free(heap_slot);
            return YETTY_ERR(yetty_ycore_int, "drawable_list_merge: add_prim", add_res);
        }
        merged_count++;
        stream += record_bytes;
        remaining -= record_bytes;
    }
    free(heap_slot);
    if (remaining != 0) {
        return YETTY_ERR(yetty_ycore_int,
                         "drawable_list_merge: trailing bytes shorter than a record header");
    }
    return YETTY_OK(yetty_ycore_int, merged_count);
}
