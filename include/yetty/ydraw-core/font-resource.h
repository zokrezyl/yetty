#ifndef YETTY_YDRAW_CORE_FONT_PRIM_H
#define YETTY_YDRAW_CORE_FONT_PRIM_H

/*
 * font-resource - drawable-list entry carrying TTF bytes through a ydraw buffer.
 *
 * Tier:
 *   Simple (SDF, fixed-size):    [0x00, 0xFF]
 *   Drawable-list entry (variable-size):   [0x40000000, 0x7FFFFFFF]   ← font/text-span
 *   Complex (factory + GPU):     [0x80000000, 0xFFFFFFFF]   ← yplot, yimage, …
 *
 * A font primitive is just bytes in the buffer's stream — it has no per-instance
 * GPU state. The canvas materializes it during add_buffer (writes TTF to the
 * yetty cache, generates an MSDF CDB on miss, opens it as a font).
 *
 * Wire layout (little-endian, 4-byte aligned):
 *   u32 type           (= YETTY_YDRAW_RESOURCE_FONT)
 *   u32 payload_size   (bytes of payload, padded to 4)
 *   i32 font_id        (producer-assigned id; text spans reference it)
 *   u32 name_len       (bytes; not NUL-terminated)
 *   u8  name[name_len]
 *   u32 ttf_len
 *   u8  ttf[ttf_len]
 *   u8  pad[0..3]      (so total prim size is 4-aligned)
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ydraw-core/drawable-list-registry.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YDRAW_RESOURCE_FONT 0x40000001u

struct yetty_ydraw_font_resource_view {
    int32_t font_id;
    const char *name; /* NOT NUL-terminated, len in name_len */
    uint32_t name_len;
    const uint8_t *ttf;
    uint32_t ttf_len;
};

/* Parse a FONT prim. The view points into the prim payload — lifetime is
 * tied to the underlying buffer. Returns 0 on success, -1 on malformed. */
int yetty_ydraw_font_resource_parse(const uint32_t *prim,
                                    struct yetty_ydraw_font_resource_view *out YETTY_ANNOT_OUT);

/* Drawable-list entry base ops handler. Returns ops only for type FONT. Register
 * via yetty_ydraw_drawable_list_registry_add(reg, FONT, FONT, handler). */
struct yetty_ydraw_drawable_list_entry_ops_ptr_result yetty_ydraw_font_resource_handler(
    uint32_t drawable_type);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDRAW_CORE_FONT_PRIM_H */
