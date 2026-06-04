// YDraw Buffer - primitive buffer for ydraw
// Pure data container, struct is public for direct field access

#pragma once

#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list-registry.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

/* Result from adding a primitive: value is the byte offset in the prims buffer. */
YETTY_YRESULT_DECLARE(yetty_ydraw_id, uint32_t);

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_list, struct yetty_ydraw_drawable_list *);

// Optional context provided at create time — known up-front by producers
// (e.g. the PDF renderer computes scene bounds in a MediaBox pre-pass before
// any primitives are emitted). Pass NULL to create with defaults.
struct yetty_ydraw_drawable_list_config {
    float scene_min_x;
    float scene_min_y;
    float scene_max_x;
    float scene_max_y;
};

// Create/destroy
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_drawable_list_result yetty_ydraw_drawable_list_config_buffer_create(
    const struct yetty_ydraw_drawable_list_config *config YETTY_ANNOT_NULLABLE);

/* Build a ydraw buffer directly from already-decoded raw bytes (bare
 * primitive stream OR magic-tagged framed payload). Used by callers that
 * have done their own decompression / decoding (e.g. the ydraw-layer's
 * yface-driven path: base64 + LZ4F decompression happens upstream, the
 * decompressed bytes land here). */
YETTY_ANNOT_CALLER_OWNED
struct yetty_ydraw_drawable_list_result yetty_ydraw_drawable_list_create_from_bytes(
    const uint8_t *data YETTY_ANNOT_ARRAY(len), size_t len);

void yetty_ydraw_drawable_list_destroy(struct yetty_ydraw_drawable_list *buf YETTY_ANNOT_CALLEE_OWNED);

// Scene bounds accessors (populated from config at create time, 0s otherwise)
float yetty_ydraw_drawable_list_scene_min_x(const struct yetty_ydraw_drawable_list *buf);
float yetty_ydraw_drawable_list_scene_min_y(const struct yetty_ydraw_drawable_list *buf);
float yetty_ydraw_drawable_list_scene_max_x(const struct yetty_ydraw_drawable_list *buf);
float yetty_ydraw_drawable_list_scene_max_y(const struct yetty_ydraw_drawable_list *buf);

// Clear all data (keeps allocation)
void yetty_ydraw_drawable_list_clear(struct yetty_ydraw_drawable_list *buf);

// Add raw primitive data, returns byte offset
struct yetty_ydraw_id_result yetty_ydraw_drawable_list_add_prim(
    struct yetty_ydraw_drawable_list *buf, const void *data YETTY_ANNOT_ARRAY(size), size_t size);

/* GROUP / DELETE — entity-scoped wire commands for incremental updates.
 *
 * `begin_group` writes a 12-byte GROUP header (type | id | payload_size=0)
 * at the current write head and returns the byte offset of that header.
 * Drawables added between begin and end land inside the group's payload.
 * `end_group` back-patches the GROUP's payload_size by computing
 * `current_write_head - marker - 12`. Nested groups work because each
 * marker addresses its own size slot.
 *
 * `add_cmd_delete` writes a fully-formed DELETE record (12 bytes:
 * type | id | payload_size=0) at the current write head. Used by
 * producers shipping incremental updates to scene-canvas: emit
 * DELETE(id) before a fresh GROUP(id, …) when a widget's drawables
 * have changed since the last frame.
 */
struct yetty_ydraw_id_result yetty_ydraw_drawable_list_begin_group(struct yetty_ydraw_drawable_list *buf,
                                                               uint32_t group_id);

/* New-format CMD_GROUP that carries the group's screen rect inline so
 * the ycompositor receiver can place a yfigure_group + ygrid figure at
 * `(x, y, w, h)` without inferring bounds from children. Wire layout:
 *
 *   u32 type = CMD_GROUP
 *   u32 id   = group_id
 *   u32 payload_size                  (patched by _end_group)
 *   u32 z_order        = 0            ← style header, kept at canonical
 *   u32 fill_color     = 0              drawable position so geometry
 *   u32 stroke_color   = 0              sits at the same offset as any
 *   u32 stroke_width   = 0              SDF prim's geometry
 *   f32 x
 *   f32 y
 *   f32 w
 *   f32 h
 *   bytes body[]                      (prim records with coords
 *                                      LOCAL to (x, y))
 *
 * Records inside the body whose coords are widget-local (not absolute
 * screen) flow into the receiver's per-group ygrid figure. Same
 * _end_group call patches payload_size — the marker semantics are
 * unchanged. */
struct yetty_ydraw_id_result yetty_ydraw_drawable_list_begin_group_with_rect(
    struct yetty_ydraw_drawable_list *buf, uint32_t group_id, float x, float y, float w, float h);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_end_group(struct yetty_ydraw_drawable_list *buf,
                                                               uint32_t group_marker_offset);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_cmd_delete(
    struct yetty_ydraw_drawable_list *buf, uint32_t group_id);

/* Append a CMD_GROUP_REF (kind=REF / bits 31:30 = 10) at the current
 * write head. Wire layout: 2 u32 words = [CMD_GROUP_REF][target_id].
 *
 * Use inside a group's body to express "the group identified by
 * `target_id` is one of my children" without re-declaring the target's
 * geometry / payload. The target is expected to be declared elsewhere
 * in the same envelope via a CMD_GROUP (kind=DECL / 01); forward
 * references are allowed because the receiver resolves child links
 * only after the envelope is fully walked. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_cmd_group_ref(
    struct yetty_ydraw_drawable_list *buf, uint32_t target_id);

/* CMD_UPDATE: hand `payload` (size = `payload_size` bytes) to the prim
 * addressed by `target_id`. Wire layout written:
 *   [CMD_UPDATE u32][target_id u32][payload_size u32][payload bytes]
 *
 * The bytes are opaque at the wire / canvas level — the receiving canvas
 * resolves target_id to a figure_instance and forwards them to the
 * primitive's factory `update_instance` op, which interprets per the
 * primitive's own schema. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_cmd_update(
    struct yetty_ydraw_drawable_list *buf, uint32_t target_id, const void *payload,
    size_t payload_size);

/*===========================================================================
 * Figure-tree wire records (new envelope format).
 *
 * Each record on the wire: `{length: u32, id: u32, payload[length]}`.
 * length-first so a pipeline tool can walk and validate without knowing
 * ids; id=0 means admin (the receiving container consumes the payload
 * itself, payload starts with a u32 admin_op from
 * <yetty/yfigure/wire.h>); id!=0 routes to the child figure of the
 * receiving container.
 *=========================================================================*/

/* Append a routed record `{length=payload_size, id, payload}`. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_record(struct yetty_ydraw_drawable_list *buf,
                                                                uint32_t id, const void *payload,
                                                                size_t payload_size);

/* Begin a routed record `{length=<filled>, id}` and return a marker.
 * The caller emits the body via add_prim / add_record / etc., then
 * calls end_record(buf, marker) to backfill the length. */
struct yetty_ydraw_id_result yetty_ydraw_drawable_list_begin_record(struct yetty_ydraw_drawable_list *buf,
                                                                uint32_t id);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_end_record(struct yetty_ydraw_drawable_list *buf,
                                                                uint32_t record_marker_offset);

/* Admin record emitters. id=0 admin records the receiving container
 * consumes itself; the sub-cmd byte is encoded in the payload. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_admin_clear_all(
    struct yetty_ydraw_drawable_list *buf);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_admin_delete_child(
    struct yetty_ydraw_drawable_list *buf, uint32_t child_id);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_admin_set_rect(
    struct yetty_ydraw_drawable_list *buf, float min_x, float min_y, float max_x, float max_y);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_admin_set_child_rect(
    struct yetty_ydraw_drawable_list *buf, uint32_t child_id, float min_x, float min_y, float max_x,
    float max_y);

/* CREATE_CHILD admin. The init_payload is forwarded to the freshly-
 * minted child's process_input(sm, init_payload_size). Pass init=NULL,
 * init_size=0 to create an empty figure. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_admin_create_child(
    struct yetty_ydraw_drawable_list *buf, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y, const void *init_payload, size_t init_payload_size);

/* Begin an admin CREATE_CHILD record. The caller emits the init payload
 * inline (e.g. via add_prim, or further admin records for sub-containers),
 * then calls end_admin_create_child to backfill both `length` and
 * `init_payload_bytes`. Returns marker (the offset of the outer record's
 * length field). */
struct yetty_ydraw_id_result yetty_ydraw_drawable_list_begin_admin_create_child(
    struct yetty_ydraw_drawable_list *buf, uint32_t child_id, uint32_t kind, float min_x, float min_y,
    float max_x, float max_y);

struct yetty_ycore_void_result yetty_ydraw_drawable_list_end_admin_create_child(
    struct yetty_ydraw_drawable_list *buf, uint32_t record_marker_offset);

// Read-only view of the raw primitive byte stream (no scene-bounds framing,
// just the concatenated FAM/SDF prim bytes). Used by producers that need to
// walk their own buffer (e.g. ygui's RICH widget translating prims into the
// engine's frame buffer). Lifetime = until the next mutation of buf.
const void *yetty_ydraw_drawable_list_data(const struct yetty_ydraw_drawable_list *buf);
size_t yetty_ydraw_drawable_list_size(const struct yetty_ydraw_drawable_list *buf);

/* Truncate the byte stream back to `size` bytes. Used to discard a
 * partially-written record (e.g. begin_admin_create_child emitted but
 * the body's render_fn failed) so the receiver never sees a dangling
 * begin without its matching end. Capacity is untouched. Errors:
 * NULL buf, or size > current buffer size (caller bug — would extend
 * into uninitialized memory). */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_truncate(struct yetty_ydraw_drawable_list *buf,
                                                              size_t size);

/* Serialize the whole buffer (scene_bounds + primitives + text_spans) into
 * a single binary blob, tagged with a magic header. The receiver passes the
 * raw bytes into create_from_bytes() and recognises the magic to restore all
 * sections. Lifetime of *out_data = until next serialize/clear/destroy.
 * Returns byte count. */
size_t yetty_ydraw_drawable_list_serialize(struct yetty_ydraw_drawable_list *buf,
                                       const uint8_t **out_data YETTY_ANNOT_OUT);

// Update scene bounds on an existing buffer.
void yetty_ydraw_drawable_list_set_scene_bounds(struct yetty_ydraw_drawable_list *buf, float min_x,
                                            float min_y, float max_x, float max_y);

// Primitive iterator
struct yetty_ydraw_drawable_iter {
    struct yetty_ydraw_drawable_list_entry fw;
};

YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_iter, struct yetty_ydraw_drawable_iter);

struct yetty_ydraw_drawable_iter_result yetty_ydraw_drawable_list_drawable_first(
    const struct yetty_ydraw_drawable_list *buf, const struct yetty_ydraw_drawable_list_registry *reg);

struct yetty_ydraw_drawable_iter_result yetty_ydraw_drawable_list_drawable_next(
    const struct yetty_ydraw_drawable_list *buf, const struct yetty_ydraw_drawable_list_registry *reg,
    const struct yetty_ydraw_drawable_iter *iter);

/*=============================================================================
 * Producer convenience: pack drawable-list entry FONT and TEXT_DRAWABLE_LIST prims into the
 * buffer. These are thin wrappers — same path as add_prim. Readers iterate
 * via the drawable-list registry; the canvas dispatches by prim type.
 *===========================================================================*/

/* Pack a FONT primitive (font-resource.h). Returns the producer-assigned
 * font_id (consecutive, starts at 0). Text spans reference fonts by this
 * id. */
struct yetty_ycore_int_result yetty_ydraw_drawable_list_add_font(
    struct yetty_ydraw_drawable_list *buf, const struct yetty_ycore_buffer *ttf_data,
    const char *name YETTY_ANNOT_CSTRING);

/* Pack a FONT primitive that references a font by content hash instead of
 * shipping its TTF bytes. Used in multi-envelope producers (ypdf
 * page-per-envelope) where the receiver's on-disk MSDF cache is keyed by
 * FNV1a64(TTF) — once the bytes have been sent in a prior envelope, later
 * envelopes only carry the 16-char hex hash.
 *
 * Wire shape: FONT prim with ttf_len = 0 and name = 16-char hex hash. The
 * receiver recognises ttf_len == 0 as "this name *is* the hex hash; look
 * up cache by hash without re-hashing bytes".
 *
 * Returns the producer-assigned envelope-local font_id, same numbering
 * convention as yetty_ydraw_drawable_list_add_font. */
struct yetty_ycore_int_result yetty_ydraw_drawable_list_add_font_ref(
    struct yetty_ydraw_drawable_list *buf, const char *hex16 YETTY_ANNOT_CSTRING);

/* Pack a TEXT_DRAWABLE_LIST primitive (text-drawable-list.h). font_id must match a
 * previously-added FONT prim's id, or be -1 to use the canvas default. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_text(
    struct yetty_ydraw_drawable_list *buf, float x, float y, const struct yetty_ycore_buffer *text,
    float font_size, uint32_t color, uint32_t layer, int32_t font_id, float rotation);

/* Like _add_text plus PDF-style per-character (Tc) and per-space (Tw)
 * spacing. The canvas applies them inside expand_text_drawable_list_to_glyphs so
 * a Tj with internal spaces (the common case for body text) lays out
 * matching what a reference PDF renderer (mutool / pdf.js / Acrobat)
 * computes. Both values are in display PIXELS at the current font_size
 * — ypdf bakes the unit conversion. Producers without spacing pass the
 * one-arg `add_text` which defaults Tc/Tw to 0. */
struct yetty_ycore_void_result yetty_ydraw_drawable_list_add_text_full(
    struct yetty_ydraw_drawable_list *buf, float x, float y, const struct yetty_ycore_buffer *text,
    float font_size, uint32_t color, uint32_t layer, int32_t font_id, float rotation,
    float char_spacing, float word_spacing);

#ifdef __cplusplus
}
#endif
