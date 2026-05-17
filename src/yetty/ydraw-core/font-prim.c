/*
 * font-prim.c - flyweight FONT primitive (see font-prim.h).
 */

#include <yetty/ydraw-core/font-prim.h>
#include "font-prim-internal.h"

#include <string.h>

/* FAM header size: type(u32) + payload_size(u32). */
#define FONT_PRIM_HEADER 8u

/* Round up to a multiple of 4 so the next prim in the stream is aligned. */
static inline uint32_t align4(uint32_t n)
{
    return (n + 3u) & ~3u;
}

static uint32_t font_payload_size(uint32_t name_len, uint32_t ttf_len)
{
    /* font_id(4) + name_len(4) + name + ttf_len(4) + ttf */
    uint32_t bare = 4u + 4u + name_len + 4u + ttf_len;
    return align4(bare);
}

size_t yetty_ydraw_font_drawable_size_for(uint32_t name_len, uint32_t ttf_len)
{
    return FONT_PRIM_HEADER + font_payload_size(name_len, ttf_len);
}

void yetty_ydraw_font_drawable_write(uint8_t *out, int32_t font_id, const char *name,
                                       uint32_t name_len, const uint8_t *ttf, uint32_t ttf_len)
{
    uint32_t payload_size = font_payload_size(name_len, ttf_len);
    size_t total = FONT_PRIM_HEADER + payload_size;

    /* Zero pad slot first so trailing alignment bytes are deterministic. */
    memset(out, 0, total);

    uint32_t type = YETTY_YDRAW_TYPE_FONT;
    memcpy(out + 0, &type, 4);
    memcpy(out + 4, &payload_size, 4);

    uint8_t *p = out + FONT_PRIM_HEADER;
    memcpy(p, &font_id, 4);
    p += 4;
    memcpy(p, &name_len, 4);
    p += 4;
    if (name_len) {
        memcpy(p, name, name_len);
        p += name_len;
    }
    memcpy(p, &ttf_len, 4);
    p += 4;
    if (ttf_len) {
        memcpy(p, ttf, ttf_len);
    }
}

int yetty_ydraw_font_drawable_parse(const uint32_t *prim,
                                      struct yetty_ydraw_font_drawable_view *out)
{
    if (!prim || !out) {
        return -1;
    }

    uint32_t type, payload_size;
    memcpy(&type, prim, 4);
    memcpy(&payload_size, (const uint8_t *)prim + 4, 4);
    if (type != YETTY_YDRAW_TYPE_FONT) {
        return -1;
    }
    if (payload_size < 12) { /* font_id + name_len + ttf_len at minimum */
        return -1;
    }

    const uint8_t *p = (const uint8_t *)prim + FONT_PRIM_HEADER;
    const uint8_t *end = p + payload_size;

    memcpy(&out->font_id, p, 4);
    p += 4;
    memcpy(&out->name_len, p, 4);
    p += 4;
    if ((size_t)(end - p) < out->name_len) {
        return -1;
    }
    out->name = (const char *)p;
    p += out->name_len;
    if ((size_t)(end - p) < 4) {
        return -1;
    }
    memcpy(&out->ttf_len, p, 4);
    p += 4;
    if ((size_t)(end - p) < out->ttf_len) {
        return -1;
    }
    out->ttf = p;
    return 0;
}

/*=============================================================================
 * Flyweight base ops
 *===========================================================================*/

static struct yetty_ycore_size_result font_drawable_size(const uint32_t *prim)
{
    uint32_t payload_size;
    memcpy(&payload_size, (const uint8_t *)prim + 4, 4);
    return YETTY_OK(yetty_ycore_size, FONT_PRIM_HEADER + (size_t)payload_size);
}

/* Fonts don't render directly — return a degenerate empty rect so the
 * spatial grid never picks them up. */
static struct rectangle_result font_drawable_aabb(const uint32_t *prim)
{
    (void)prim;
    struct yetty_ycore_rectangle r = {.min = {0, 0}, .max = {0, 0}};
    return YETTY_OK(rectangle, r);
}

static const struct yetty_ydraw_drawable_base_ops g_font_drawable_base_ops = {
    .size = font_drawable_size,
    .aabb = font_drawable_aabb,
};

struct yetty_ydraw_drawable_base_ops_ptr_result yetty_ydraw_font_drawable_handler(
    uint32_t drawable_type)
{
    if (drawable_type == YETTY_YDRAW_TYPE_FONT) {
        return YETTY_OK(yetty_ydraw_drawable_base_ops_ptr, &g_font_drawable_base_ops);
    }
    return YETTY_ERR(yetty_ydraw_drawable_base_ops_ptr, "not FONT");
}
