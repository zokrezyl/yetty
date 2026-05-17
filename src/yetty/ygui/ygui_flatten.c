/*
 * ygui_flatten.c — see ygui_flatten.h.
 *
 * Recognises four record shapes:
 *
 *   CMD_ZERO       u32 type, u32 payload_size=0                       (8 B)
 *   CMD_DELETE     u32 type|HAS_ID, u32 id, u32 payload_size=0       (12 B)
 *   CMD_GROUP      u32 type|HAS_ID, u32 id, u32 payload_size, u8[ps] (12+ps B)
 *   paint prim     SDF / TEXT_SPAN / FONT / complex                  sized
 *                  via the rich-widget walker convention
 *
 * Paint sizing reuses the same convention as ygui_rich.c (and
 * yetty_ydraw_drawable_command_parse for non-flyweight ADD records):
 *   - SDF prims      : yetty_ysdf_primitive_size(type)
 *   - everything else: FAM stride = 8 + payload_size (prim[1])
 *
 * The CMD_GROUP body is the payload bytes themselves — they may contain
 * nested groups or paint prims. We recurse so nested-group producers
 * (yjungle's depth>0 segments) collapse to a flat paint list.
 */

#include "ygui_flatten.h"

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ysdf/types.gen.h>

#include <stddef.h>
#include <stdint.h>

static size_t paint_prim_size(const uint8_t *p, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = *(const uint32_t *)p;

    /* SDF tier — primitive_size returns 0 for any non-SDF type, so it
     * also serves as the type-class probe. Strip the HAS_ID flag before
     * the lookup, and add 4 bytes for the trailing id slot when set. */
    uint32_t base = type & ~YETTY_YDRAW_HAS_ID_FLAG;
    size_t sdf_bytes = yetty_ysdf_primitive_size(base);
    if (sdf_bytes > 0) {
        size_t s = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        return (s <= remaining) ? s : 0;
    }

    if (remaining < 2 * sizeof(uint32_t)) {
        return 0;
    }
    uint32_t payload_size = ((const uint32_t *)p)[1];
    size_t s = 2 * sizeof(uint32_t) + payload_size;
    return (s <= remaining) ? s : 0;
}

static struct yetty_ycore_void_result flatten_bytes(
    struct yetty_ydraw_draw_list *dst, const uint8_t *src, size_t len)
{
    size_t off = 0;
    while (off + sizeof(uint32_t) <= len) {
        uint32_t type = *(const uint32_t *)(src + off);

        if (type == YETTY_YDRAW_CMD_ZERO) {
            /* FAM with payload_size=0 → 8 bytes. */
            if (off + 8 > len) {
                break;
            }
            off += 8;
            continue;
        }

        if (type == YETTY_YDRAW_CMD_DELETE) {
            /* type | id | payload_size=0 → 12 bytes. */
            if (off + 12 > len) {
                break;
            }
            off += 12;
            continue;
        }

        if (type == YETTY_YDRAW_CMD_GROUP) {
            if (off + 12 > len) {
                break;
            }
            uint32_t payload_size = ((const uint32_t *)(src + off))[2];
            size_t total = (size_t)12 + payload_size;
            if (off + total > len) {
                break;
            }
            struct yetty_ycore_void_result r =
                flatten_bytes(dst, src + off + 12, payload_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "flatten: nested group");
            off += total;
            continue;
        }

        /* Any other type is a paint primitive. */
        size_t s = paint_prim_size(src + off, len - off);
        if (s == 0 || off + s > len) {
            return YETTY_ERR(yetty_ycore_void, "flatten: malformed paint prim");
        }
        struct yetty_ydraw_id_result ar =
            yetty_ydraw_draw_list_add_prim(dst, src + off, s);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "flatten: add_prim failed");
        off += s;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_flatten_draw_list(
    struct yetty_ydraw_draw_list *dst,
    const struct yetty_ydraw_draw_list *src)
{
    if (!dst || !src) {
        return YETTY_ERR(yetty_ycore_void, "flatten: NULL arg");
    }
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(src);
    size_t size = yetty_ydraw_draw_list_size(src);
    if (!bytes || size == 0) {
        return YETTY_OK_VOID();
    }
    return flatten_bytes(dst, bytes, size);
}
