// YPaint Buffer - primitive buffer for ypaint
// Pure data container, struct is public for direct field access

#pragma once

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ypaint-core/flyweight.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error codes
#define YPAINT_OK 0
#define YPAINT_ERR_NULL 1
#define YPAINT_ERR_OVERFLOW 2
#define YPAINT_ERR_ALLOC 3

// Result from adding a primitive
struct yetty_ypaint_core_id_result {
    int error;
    uint32_t id; // byte offset in prims buffer
};

struct yetty_ypaint_core_buffer;

YETTY_YRESULT_DECLARE(yetty_ypaint_core_buffer, struct yetty_ypaint_core_buffer *);

// Optional context provided at create time — known up-front by producers
// (e.g. the PDF renderer computes scene bounds in a MediaBox pre-pass before
// any primitives are emitted). Pass NULL to create with defaults.
struct yetty_ypaint_core_buffer_config {
    float scene_min_x;
    float scene_min_y;
    float scene_max_x;
    float scene_max_y;
};

// Create/destroy
struct yetty_ypaint_core_buffer_result yetty_ypaint_core_buffer_config_buffer_create(
    const struct yetty_ypaint_core_buffer_config *config);
struct yetty_ypaint_core_buffer_result yetty_ypaint_core_buffer_create_from_base64(
    const struct yetty_ycore_buffer *base64_buf);

/* Build a ypaint buffer directly from already-decoded raw bytes (bare
 * primitive stream OR magic-tagged framed payload — same shape that
 * _create_from_base64 sees after base64 decoding). Used by callers that
 * have done their own decompression / decoding (e.g. the ypaint-layer's
 * yface-driven path: base64 + LZ4F decompression happens upstream, the
 * decompressed bytes land here). */
struct yetty_ypaint_core_buffer_result yetty_ypaint_core_buffer_create_from_bytes(
    const uint8_t *data, size_t len);

/* Base64-encode the buffer's raw primitive bytes. Allocates the output —
 * caller owns result.value.data and must free() it. Symmetric inverse of
 * yetty_ypaint_core_buffer_create_from_base64. */
struct yetty_ycore_buffer_result yetty_ypaint_core_buffer_to_base64(
    const struct yetty_ypaint_core_buffer *buf);
void yetty_ypaint_core_buffer_destroy(struct yetty_ypaint_core_buffer *buf);

// Scene bounds accessors (populated from config at create time, 0s otherwise)
float yetty_ypaint_core_buffer_scene_min_x(const struct yetty_ypaint_core_buffer *buf);
float yetty_ypaint_core_buffer_scene_min_y(const struct yetty_ypaint_core_buffer *buf);
float yetty_ypaint_core_buffer_scene_max_x(const struct yetty_ypaint_core_buffer *buf);
float yetty_ypaint_core_buffer_scene_max_y(const struct yetty_ypaint_core_buffer *buf);

// Clear all data (keeps allocation)
void yetty_ypaint_core_buffer_clear(struct yetty_ypaint_core_buffer *buf);

// Read-only view into the primitives payload. Used by serializers that base64-
// encode the raw primitive bytes (the wire format accepted by
// yetty_ypaint_core_buffer_create_from_base64). NULL on invalid buf.
const struct yetty_ycore_buffer *yetty_ypaint_core_buffer_primitives(
    const struct yetty_ypaint_core_buffer *buf);

// Add raw primitive data, returns byte offset
struct yetty_ypaint_core_id_result yetty_ypaint_core_buffer_add_prim(
    struct yetty_ypaint_core_buffer *buf, const void *data, size_t size);

// Read-only access to the accumulated primitive bytes — base64-encode these
// and the receiver's yetty_ypaint_core_buffer_create_from_base64() rebuilds
// an equivalent buffer. Lifetime = until next add/clear on this buffer.
const void *yetty_ypaint_core_buffer_data(const struct yetty_ypaint_core_buffer *buf);
size_t yetty_ypaint_core_buffer_size(const struct yetty_ypaint_core_buffer *buf);

/* Serialize the whole buffer (scene_bounds + primitives + text_spans) into
 * a single binary blob, tagged with a magic header. Pass the raw bytes into
 * create_from_base64() after base64-encoding on the sender; the receiver
 * recognises the magic and restores all sections. Lifetime of *out_data =
 * until next serialize/clear/destroy. Returns byte count. */
size_t yetty_ypaint_core_buffer_serialize(struct yetty_ypaint_core_buffer *buf,
                                          const uint8_t **out_data);

// Update scene bounds on an existing buffer.
void yetty_ypaint_core_buffer_set_scene_bounds(struct yetty_ypaint_core_buffer *buf, float min_x,
                                               float min_y, float max_x, float max_y);

// Primitive iterator
struct yetty_ypaint_core_primitive_iter {
    struct yetty_ypaint_core_prim_flyweight fw;
};

YETTY_YRESULT_DECLARE(yetty_ypaint_core_primitive_iter, struct yetty_ypaint_core_primitive_iter);

struct yetty_ypaint_core_primitive_iter_result yetty_ypaint_core_buffer_prim_first(
    const struct yetty_ypaint_core_buffer *buf, const struct yetty_ypaint_core_flyweight_registry *reg);

struct yetty_ypaint_core_primitive_iter_result yetty_ypaint_core_buffer_prim_next(
    const struct yetty_ypaint_core_buffer *buf, const struct yetty_ypaint_core_flyweight_registry *reg,
    const struct yetty_ypaint_core_primitive_iter *iter);

/*=============================================================================
 * Producer convenience: pack flyweight FONT and TEXT_SPAN prims into the
 * buffer. These are thin wrappers — same path as add_prim. Readers iterate
 * via the flyweight registry; the canvas dispatches by prim type.
 *===========================================================================*/

/* Pack a FONT primitive (font-prim.h). Returns the producer-assigned
 * font_id (consecutive, starts at 0). Text spans reference fonts by this
 * id. */
struct yetty_ycore_int_result yetty_ypaint_core_buffer_add_font(
    struct yetty_ypaint_core_buffer *buf, const struct yetty_ycore_buffer *ttf_data,
    const char *name);

/* Pack a TEXT_SPAN primitive (text-span-prim.h). font_id must match a
 * previously-added FONT prim's id, or be -1 to use the canvas default. */
struct yetty_ycore_void_result yetty_ypaint_core_buffer_add_text(
    struct yetty_ypaint_core_buffer *buf, float x, float y, const struct yetty_ycore_buffer *text,
    float font_size, uint32_t color, uint32_t layer, int32_t font_id, float rotation);

/* Like _add_text plus PDF-style per-character (Tc) and per-space (Tw)
 * spacing. The canvas applies them inside expand_text_span_to_glyphs so
 * a Tj with internal spaces (the common case for body text) lays out
 * matching what a reference PDF renderer (mutool / pdf.js / Acrobat)
 * computes. Both values are in display PIXELS at the current font_size
 * — ypdf bakes the unit conversion. Producers without spacing pass the
 * one-arg `add_text` which defaults Tc/Tw to 0. */
struct yetty_ycore_void_result yetty_ypaint_core_buffer_add_text_full(
    struct yetty_ypaint_core_buffer *buf, float x, float y, const struct yetty_ycore_buffer *text,
    float font_size, uint32_t color, uint32_t layer, int32_t font_id, float rotation,
    float char_spacing, float word_spacing);

#ifdef __cplusplus
}
#endif
