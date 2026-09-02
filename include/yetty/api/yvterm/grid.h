/* GENERATED — do not edit. */
/* Object API for regular class(es) `grid` (implementation module: yvterm).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YVTERM_GRID_H
#define YETTY_YCLASSGEN_API_YVTERM_GRID_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_buffer;
struct yetty_ycore_memtag_registry;
struct yetty_ydraw_complex;
struct yetty_yvterm_grid;
struct yetty_ywire_wire_statemachine;

typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_clear_hook_fn)(void *);
typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_materialize_fn)(
    const uint32_t *, uint32_t, const uint32_t *, uint32_t, void *, struct yetty_ydraw_complex **);
typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_reset_hook_fn)(int, void *);

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_ATTR
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_ATTR
enum yetty_yvterm_text_attr {
    YETTY_YVTERM_ATTR_BOLD = 1,
    YETTY_YVTERM_ATTR_UNDERLINE = 2,
    YETTY_YVTERM_ATTR_UNDERLINE2 = 4,
    YETTY_YVTERM_ATTR_ITALIC = 8,
    YETTY_YVTERM_ATTR_REVERSE = 16,
    YETTY_YVTERM_ATTR_BLINK = 32,
    YETTY_YVTERM_ATTR_STRIKE = 64,
    YETTY_YVTERM_ATTR_CONCEAL = 128,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_CELL_LIMITS
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_CELL_LIMITS
/* Combining marks a single cell can carry beyond its base codepoint. Matches
 * libvterm's VTERM_MAX_CHARS_PER_CELL (6) minus the base — the static assert
 * below keeps the two in lock-step. Stored inline so a cluster travels with the
 * cell through every scroll/move/blank path (all of which copy or clear the
 * whole struct) without a parallel side table to keep in sync. */
enum yetty_yvterm_cell_limits {
    YETTY_YVTERM_CELL_MAX_MARKS = 5,
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_CELL
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_CELL
/* One terminal text cell. Glyph lookup is renderer-owned; the model stores the
 * source codepoint and style. width is 1 for normal cells, 2 for a wide glyph
 * head, and 0 for the wide glyph spill cell. `codepoint` is the grapheme
 * cluster's base; `marks[0..mark_count)` are its combining continuation
 * (libvterm's chars[1..]), so no codepoint of a cluster is dropped. Exposed so
 * the renderer packs from the bulk per-line accessor without copying. */
struct yetty_yvterm_text_cell {
    uint32_t glyph_index;
    uint32_t codepoint;
    uint32_t fg;
    uint32_t bg;
    uint16_t attrs;
    uint8_t width;
    uint8_t flags;
    uint8_t mark_count;
    uint32_t marks[5];
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_CELL_CONST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_CELL_CONST_PTR_RESULT
struct yetty_yvterm_text_cell_const_ptr_result {
    int ok;
    union {
        const struct yetty_yvterm_text_cell *value;
        struct yetty_ycore_error error;
    };
};
#endif

/* The unified terminal grid — the yclass data block. */
struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_grid;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_GRID_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_GRID_PTR_RESULT
struct yetty_yvterm_grid_ptr_result {
    int ok;
    union {
        struct yetty_yvterm_grid *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yvterm_grid_to(struct yetty_yvterm_grid *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_create(struct yetty_yclass_ctx *ctx);

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_make(uint32_t cols, uint32_t rows,
                                                             uint32_t scrollback_rows,
                                                             uint32_t hot_rows);
struct yetty_ycore_void_result yetty_yvterm_grid_dispose(struct yetty_yclass_object *obj);
/* Install the terminal-host sink the grid dispatches its upcalls on (pty_write
 * / mouse_sub / clipboard_write / sixel_write). Borrowed — the host owns it. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_sink(struct yetty_yclass_object *obj,
                                                          struct yetty_yclass_object *sink);
struct yetty_ycore_void_result yetty_yvterm_grid_set_clear_hook(struct yetty_yclass_object *obj,
                                                                yetty_yvterm_grid_clear_hook_fn fn,
                                                                void *userdata);
struct yetty_ycore_void_result yetty_yvterm_grid_set_reset_hook(struct yetty_yclass_object *obj,
                                                                yetty_yvterm_grid_reset_hook_fn fn,
                                                                void *userdata);
/* Register the figure re-materialization hook. The integration layer (which
 * owns the complex factory) supplies it; the grid replays retained wire
 * envelopes through it when an evicted history line scrolls back into view. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_materialize(
    struct yetty_yclass_object *obj, yetty_yvterm_grid_materialize_fn fn, void *userdata);
struct yetty_ycore_void_result yetty_yvterm_grid_feed(struct yetty_yclass_object *obj,
                                                      const char *bytes, size_t len);
/* Feed the current text-area pixel size to libvterm so DEC mode 2048 (in-band
 * window resize) can report it, then drain any notification the size change
 * produced. Called by the terminal on every resize (and once initially). */
struct yetty_ycore_void_result yetty_yvterm_grid_set_pixel_size(struct yetty_yclass_object *obj,
                                                                uint32_t width_px,
                                                                uint32_t height_px);
/* The rich content density: producer-logical → framebuffer multiplier
 * (the committed content scale the cell metrics were derived at). Pushed
 * by the owning figure at creation; per-node retirement multiplies the
 * stored logical footprints by it before the framebuffer-cell row math.
 * Non-positive values are rejected. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_rich_density(struct yetty_yclass_object *obj,
                                                                  float density_scale);
/* The committed rich density product (1.0 while unset) — the ingest's
 * reserve/span conversions read it back so they always agree with the
 * retirement math on the same value. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_density(struct yetty_yclass_object *obj,
                                                              float *out_density);
struct yetty_ycore_void_result yetty_yvterm_grid_resize(struct yetty_yclass_object *obj,
                                                        uint32_t cols, uint32_t rows);
struct yetty_ycore_int_result yetty_yvterm_grid_is_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_grid_cursor(struct yetty_yclass_object *obj,
                                                        uint32_t *out_row, uint32_t *out_col,
                                                        uint32_t *out_visible);
/* Absolute row at the top of the ACTIVE screen (rows scrolled off so far). The
 * cursor's absolute output row is this + the visible cursor row — used to place
 * anchored rich content on the rolling-row scroll. Each screen carries its own
 * origin; the alternate one restarts at 0 on every entry. */
struct yetty_ycore_uint64_result yetty_yvterm_grid_scroll_origin(struct yetty_yclass_object *obj);
/* Open an explicit paint-z scope (CMD_PAINT_Z): every record ingested until
 * the matching pop paints at `z`, overriding any wire z — the way a complex
 * (which has no wire z) is placed at an arbitrary depth. Scopes nest. A
 * push past the bounded depth is IGNORED (the matching pop stays balanced)
 * and reported as an error, so a caller that REQUIRES its z to take effect
 * (local chrome replacement) can abort instead of stamping records with
 * whatever scope happens to be innermost. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_push_paint_z(struct yetty_yclass_object *obj,
                                                                   int32_t z);
/* Close the innermost paint-z scope (CMD_PAINT_Z_END). */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_pop_paint_z(struct yetty_yclass_object *obj);
/* Drop the whole paint-z stack — the ingest calls this at the envelope
 * boundary so an unbalanced scope cannot leak into later output. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_reset_paint_z(
    struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_grid_append_primitive(struct yetty_yclass_object *obj,
                                                                    uint32_t row,
                                                                    const uint32_t *words,
                                                                    uint32_t word_count);
/* append_primitive plus the record's content-space vertical extent (its
 * effective AABB, pre-offset pixels) — what per-node scroll retirement
 * projects into rows. The plain append leaves the extent unknown, which
 * exempts the node from retirement (conservative). */
struct yetty_ycore_uint32_result yetty_yvterm_grid_append_primitive_extent(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count,
    float content_top_px, float content_bottom_px);
struct yetty_ycore_uint32_result yetty_yvterm_grid_attach_complex(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_complex *complex);
/* Declare the open block's final row span BEFORE the reserve cursor advance.
 * Placement must be installed first because the advance itself can scroll:
 * with the span still 0 every lifecycle path treats the fresh block as
 * one-row, so a full-height reservation would seal it (primary) or recycle
 * its owner line and destroy it (alternate) DURING its own insertion.
 * Stamps span + bottom owner + max resident span (coverage follows in
 * relocate, once the covered rows exist) and returns the row count the
 * cursor may actually advance: the declared span on the primary screen, but
 * clamped on the ALTERNATE screen so the advance never scrolls — a
 * no-history screen has nowhere to move departing content except oblivion,
 * so an insertion there must not scroll away its own top. The stamped span
 * is clamped to the alternate screen's remaining rows for the same reason:
 * rows below its bottom can never exist. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_rich_span_declare(
    struct yetty_yclass_object *obj, uint32_t span_rows);
/* Abort the batch's provisional insertion: remove its handle from the
 * tracked line, destroy the block (records, figure runtimes, groups) and
 * close the batch. The recovery path when the reserve advance failed
 * partway — the declared placement can no longer be realized, and
 * finalizing the full span against a partial advance would let later text
 * start inside the claimed insertion. Safe no-op without a live batch. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_batch_abort(struct yetty_yclass_object *obj);
/* Advance the cursor for a batch's reservation — `advance_rows` newlines
 * through libvterm — while keeping the provisional block's handle alive: an
 * advance longer than the ring's history capacity would recycle the
 * insertion's top line (destroying the block, or archiving it from a line
 * the tier decoder would wrongly treat as the bottom owner) BEFORE
 * relocation runs. The feed is chunked to under one screenful with the
 * handle re-homed onto the cursor line around every chunk, so the line
 * carrying it can never age out mid-advance. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_reserve_advance(
    struct yetty_yclass_object *obj, uint32_t advance_rows);
/* Re-home a freshly-ingested rich block from its TOP line onto its BOTTOM line
 * and complete its coverage accounting, so a figure (complex or SDF block)
 * leaves the scrollback only when its LAST overlapping line is evicted, not
 * its first. Called once after the reserve cursor advance; the block's rows
 * are derived from its own timeline anchors (insertion row + bottom owner
 * stamped by rich_span_declare), NOT from the cursor — the advance may have
 * been clamped on a no-history screen, where the cursor ends ON the last
 * block row instead of below it. Callers that skipped rich_span_declare
 * (direct API use, tests) get the span stamped here from the argument, as
 * before. A span of 1 is a single-row block (top == bottom): nothing moves. */
struct yetty_ycore_void_result yetty_yvterm_grid_relocate_rich_to_bottom(
    struct yetty_yclass_object *obj, uint32_t span_rows);
/* Open (or reopen) the group node at path key `group_key` — the `insert`
 * of the contract (drawable-use-cases.md). A live GROUP binding means
 * REOPEN with EXACT-SUBTREE semantics: the group's whole old subtree dies
 * in place (descendant groups included), a fresh scope opens in the SAME
 * block (original anchor, span and paint position preserved), and the
 * binding stays — replacement content then appends into that scope. A live
 * COMPLEX binding at the key is a kind change: the old complex record dies
 * and a fresh group is created. No live binding means a new group scope in
 * the open block at cursor row `row` (nested opens hang under the current
 * scope). Returns 1 when it reopened, 0 when it created. The caller pairs
 * every open with rich_group_close. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_rich_group_open(struct yetty_yclass_object *obj,
                                                                   uint32_t row,
                                                                   uint64_t group_key);
/* Close the innermost open group scope (pairs with rich_group_open). */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_group_close(struct yetty_yclass_object *obj);
/* delete(path): remove the node at the path key and its whole subtree —
 * a GROUP node's subtree dies; a COMPLEX node's record dies. The owning
 * block, its anchor and its terminal row span remain — deleting rich
 * content does not pull later text upward. Unknown (or sealed — lazily
 * purged) paths answer with an error the caller may log (a no-op on the
 * wire). */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_group_delete(struct yetty_yclass_object *obj,
                                                                   uint64_t group_key);
/* Bind the newest alive complex record carrying a runtime in the OPEN block
 * as the COMPLEX node at path key `update_key` — this is how an id-bearing
 * complex (`Plot(id=7)`) becomes addressable. If the path is already live it
 * is a replacement: the old node (group subtree or complex record) dies
 * first, per the contract's create-or-exact-replace. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_bind(struct yetty_yclass_object *obj,
                                                                  uint64_t update_key);
/* Set the GROUP node's translation offset (pixels, absolute) at the path
 * key — the group-state write behind update(path, GROUP_FIELD_OFFSET, x, y).
 * Rendering-only state: placement bookkeeping (span, coverage, sealing)
 * never sees it. Errors when the path is not a live GROUP node, or when the
 * value is not a finite in-range coordinate — offsets come straight off the
 * wire and are later SUMMED across ancestors and floored to integers in the
 * SDF bucketing, so a NaN/infinity/overflow here would reach an invalid
 * float-to-int conversion. The bound keeps an 8-deep ancestor sum well
 * inside both float precision and int range. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_group_offset_set(
    struct yetty_yclass_object *obj, uint64_t group_key, float offset_x, float offset_y);
/* Set the GROUP node's CLIP rectangle (local content space, pre-offset) —
 * the group-state write behind update(path, GROUP_FIELD_CLIP, x,y,w,h).
 * Non-layout PROJECTION state: placement bookkeeping never sees it; a
 * zero-size rect disables clipping. Same validation stance as the offset:
 * wire-controlled floats must be finite and in range. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_group_clip_set(
    struct yetty_yclass_object *obj, uint64_t group_key, float clip_x, float clip_y, float clip_w,
    float clip_h);
/* The live figure runtime behind `update_id`, or NULL when the binding is
 * gone (never minted, replaced, deleted, or sealed away — the lazy purge in
 * the lookup). The grid hands out the pointer only; the caller (which owns
 * the complex factory layer) performs the update call. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_target(
    struct yetty_yclass_object *obj, uint64_t update_key, struct yetty_ydraw_complex **out_complex);
/* Retain one ACCEPTED update for future re-materialization (live-first
 * budget policy — see the store). Call after the runtime applied it. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_journal(
    struct yetty_yclass_object *obj, uint64_t update_key, uint32_t target_field,
    const uint32_t *payload_words, uint32_t payload_word_count);
/* Poison the journal behind `update_id`: a live update was applied whose
 * payload cannot round-trip the journal framing (ragged byte tail) — from
 * now on the record re-materializes from its creation envelope only, never
 * from a partial journal. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_journal_poison(
    struct yetty_yclass_object *obj, uint64_t update_key);
/* Refresh the retained scroll-retirement footprint of the COMPLEX record
 * bound at `update_key` from its runtime's CURRENT effective AABB — the
 * post-mutation half of a geometry/range update. Only the record's
 * content-space extent moves (retirement and the ancestor group's
 * subtree union judge what is actually drawn); the block's immutable row
 * span never changes. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_extent_refresh(
    struct yetty_yclass_object *obj, uint64_t update_key, float content_top_px,
    float content_bottom_px);
/* Read back the retained scroll-retirement footprint of the COMPLEX
 * record bound at `update_key` — the assertion mirror of extent_refresh
 * for the ingest round-trip tests. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_extent(struct yetty_yclass_object *obj,
                                                                    uint64_t update_key,
                                                                    float *out_top_px,
                                                                    float *out_bottom_px);
/* The effective paint-z of the COMPLEX record bound at `update_key` — the
 * depth the whole figure (runtime + chrome) was minted at. A receiver-
 * local chrome replacement pushes this as the ambient scope so the
 * regenerated prims keep the figure's original stacking, independent of
 * the envelope being processed. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_update_paint_z(
    struct yetty_yclass_object *obj, uint64_t update_key, int32_t *out_paint_z);
/* Pre-grow the block behind the GROUP at `group_key` so the next
 * `extra_records` appends totalling `extra_words` words cannot fail on
 * allocation, AND preflight the group's replacement-ordinal headroom —
 * together the full staging half of an atomic in-place replacement: a
 * successful reserve makes the later replace-open + appends infallible
 * (see yetty_yvterm_rich_store_block_group_reserve). */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_group_reserve(struct yetty_yclass_object *obj,
                                                                    uint64_t group_key,
                                                                    uint32_t extra_records,
                                                                    uint32_t extra_words);
/* Opaque block identity for a live group id: 0 when unbound, otherwise a
 * token equal for two ids exactly when they are bound to the same block.
 * Lets the envelope validator reject cross-block nested reopens BEFORE any
 * mutation, without exposing handles. */
struct yetty_ycore_uint64_result yetty_yvterm_grid_rich_group_token(struct yetty_yclass_object *obj,
                                                                    uint64_t group_key);
/* Live-group query for producers/validators: *out_span_rows receives the
 * bound block's reserved span (0 = single-row). Returns 1 when the id is
 * live, 0 otherwise. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_rich_group_query(struct yetty_yclass_object *obj,
                                                                    uint64_t group_key,
                                                                    uint32_t *out_span_rows);
/* Aggregate journal budget in bytes (scrollback/update-journal-bytes; 0 =
 * unlimited). */
struct yetty_ycore_void_result yetty_yvterm_grid_set_update_journal_budget(
    struct yetty_yclass_object *obj, uint64_t budget_bytes);
struct yetty_ycore_void_result yetty_yvterm_grid_clear_rich_line(struct yetty_yclass_object *obj,
                                                                 uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_grid_clear_rich_all(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_grid_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_int_result yetty_yvterm_grid_on_char(struct yetty_yclass_object *obj,
                                                        uint32_t codepoint, int mods);
struct yetty_ycore_int_result yetty_yvterm_grid_on_key(struct yetty_yclass_object *obj, int key,
                                                       int mods);
struct yetty_ycore_void_result yetty_yvterm_grid_set_selection(struct yetty_yclass_object *obj,
                                                               int active, uint32_t anchor_row,
                                                               uint32_t anchor_col,
                                                               uint32_t head_row,
                                                               uint32_t head_col);
struct yetty_ycore_void_result yetty_yvterm_grid_get_selection_text(struct yetty_yclass_object *obj,
                                                                    struct yetty_ycore_buffer *out);
/* Word boundaries around (row, col): the inclusive [start_col, end_col] run of
 * word chars covering the clicked cell. A click on a non-word cell selects just
 * that cell. Used for double-click word selection. */
struct yetty_ycore_void_result yetty_yvterm_grid_word_bounds(struct yetty_yclass_object *obj,
                                                             uint32_t row, uint32_t col,
                                                             uint32_t *out_start_col,
                                                             uint32_t *out_end_col);
struct yetty_ycore_void_result yetty_yvterm_grid_dims(struct yetty_yclass_object *obj,
                                                      uint32_t *out_cols, uint32_t *out_rows,
                                                      uint32_t *out_base);
/* The cell array for visible row `row` (length = cols). NULL value if out of
 * range. */
struct yetty_yvterm_text_cell_const_ptr_result yetty_yvterm_grid_line_cells(
    struct yetty_yclass_object *obj, uint32_t row);
struct yetty_ycore_int_result yetty_yvterm_grid_line_dirty(struct yetty_yclass_object *obj,
                                                           uint32_t row);
/* Test/introspection accessor: the plan leaf's resolved clip. */
struct yetty_ycore_void_result yetty_yvterm_grid_paint_plan_leaf_clip(
    struct yetty_yclass_object *obj, uint32_t leaf_index, int *out_valid, float *out_x,
    float *out_y, float *out_w, float *out_h);
/* Leaf count of the ACTIVE screen's plan (rebuilding it if stale). */
struct yetty_ycore_uint32_result yetty_yvterm_grid_paint_plan_leaf_count(
    struct yetty_yclass_object *obj);
/* One leaf of the ACTIVE screen's sorted plan: the block slot + record
 * index it addresses, its kind and its complete paint key, in paint
 * order. The (slot, record index) tuple is a SNAPSHOT — valid until the
 * next rich mutation (live compaction re-packs indices); re-query the
 * plan afterwards instead of retaining leaves across mutations. */
struct yetty_ycore_void_result yetty_yvterm_grid_paint_plan_leaf(
    struct yetty_yclass_object *obj, uint32_t leaf_index, uint32_t *out_block_slot,
    uint32_t *out_record_index, uint32_t *out_kind, int32_t *out_paint_z,
    uint64_t *out_paint_sequence, uint32_t *out_record_ordinal);
/* The accumulated PROJECTION offset (ancestor group offsets summed) of one
 * plan leaf — the value the renderer shifts the leaf by. Exposed so the
 * accumulation contract is testable without the render stack. */
struct yetty_ycore_void_result yetty_yvterm_grid_paint_plan_leaf_offset(
    struct yetty_yclass_object *obj, uint32_t leaf_index, float *out_offset_x, float *out_offset_y);
/* Rolling placement of one plan leaf's block — the renderer's exact
 * per-frame inputs (viewport bottom row = bottom_owner_row − view top).
 * Errors when the leaf index is out of range or its handle went stale. */
struct yetty_ycore_void_result yetty_yvterm_grid_paint_plan_leaf_anchor(
    struct yetty_yclass_object *obj, uint32_t leaf_index, uint64_t *out_bottom_owner_row,
    uint32_t *out_span_rows);
/* How many times the ACTIVE screen's plan has been rebuilt — lets tests
 * pin that scrolling/view changes reuse the cached order. */
struct yetty_ycore_uint64_result yetty_yvterm_grid_paint_plan_build_count(
    struct yetty_yclass_object *obj);
/* Number of rich blocks anchored (bottom-owned) on RAW ring slot `slot` or an
 * extended view-window id. Both render passes iterate blocks per slot — the
 * same raw-slot addressing the text pass uses — so every block scrolls in
 * lockstep with its text, live and scrolled-back alike. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_slot_rich_block_count(
    struct yetty_yclass_object *obj, uint32_t slot);
/* Geometry + record count of one block on a slot. span_rows 0 = single-row
 * anchor; otherwise the block's top row is (slot's row − (span_rows − 1)) —
 * it draws top-down from where its text sits while staying owned (and
 * evicted) by this bottom line. record_count 0 doubles as the out-of-range
 * answer. */
struct yetty_ycore_void_result yetty_yvterm_grid_slot_rich_block(struct yetty_yclass_object *obj,
                                                                 uint32_t slot,
                                                                 uint32_t block_index,
                                                                 uint32_t *out_span_rows,
                                                                 uint32_t *out_record_count);
/* Physical retention counters of one block — record slots, group slots
 * and arena words currently HELD (live plus not-yet-reclaimed). Snapshot
 * values: any rich mutation may change them (and live reclamation may
 * shrink them). The boundedness tests pin these against replacement
 * churn. */
struct yetty_ycore_void_result yetty_yvterm_grid_slot_rich_block_stats(
    struct yetty_yclass_object *obj, uint32_t slot, uint32_t block_index,
    uint32_t *out_record_count, uint32_t *out_group_count, uint32_t *out_arena_words);
/* Reuse-cache rebuild count of one block — the complexity pin for the
 * group-slot recycler: an exact-subtree replacement window pays ONE
 * reachability scan however many nodes it recreates. The churn tests
 * assert scans stay proportional to replacement WINDOWS, not to
 * recreated NODES. */
struct yetty_ycore_void_result yetty_yvterm_grid_slot_rich_reusable_scans(
    struct yetty_yclass_object *obj, uint32_t slot, uint32_t block_index, uint64_t *out_scans);
/* Monotone change stamp over ALL screens' rich content (sum of the
 * per-screen paint generations): any append, kill, replacement or
 * compaction moves it. Change-gated sweeps (the SDF layer's font
 * install pass) skip their walk while it holds still. */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_change_stamp(struct yetty_yclass_object *obj,
                                                                   uint64_t *out_stamp);
/* Debug/inspection: live entries + physical capacity of the path-binding
 * table. Snapshot values; the boundedness tests pin these against
 * record-less replacement churn (the growth-time sweep). */
struct yetty_ycore_void_result yetty_yvterm_grid_rich_binding_occupancy(
    struct yetty_yclass_object *obj, uint32_t *out_live, uint32_t *out_capacity);
/* 1 once the block's insertion row crossed into primary scrollback: the
 * content is immutable history — still rendered, no longer addressable.
 * 0 for live blocks and out-of-range slot/indices. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_slot_rich_block_sealed(
    struct yetty_yclass_object *obj, uint32_t slot, uint32_t block_index);
/* One record of one block on a slot: the verbatim wire words (aliasing the
 * block's arena — read, do not retain) and, for a complex record with a live
 * runtime, the figure instance. *out_complex may be NULL for primitive
 * records, evicted complexes, and bytes-less runtime-only records (which
 * report *out_word_count 0). Out-of-range slot/indices answer NULL/0. */
struct yetty_ycore_const_uint32_ptr_result yetty_yvterm_grid_slot_rich_block_record(
    struct yetty_yclass_object *obj, uint32_t slot, uint32_t block_index, uint32_t record_index,
    uint32_t *out_word_count, struct yetty_ydraw_complex **out_complex);
/* The record's paint key — its position in the one total paint order
 * (paint_z, paint_sequence, record_ordinal). Errors on a dead or missing
 * record: dead records have no paint position. */
struct yetty_ycore_void_result yetty_yvterm_grid_slot_rich_block_record_paint_key(
    struct yetty_yclass_object *obj, uint32_t slot, uint32_t block_index, uint32_t record_index,
    int32_t *out_paint_z, uint64_t *out_paint_sequence, uint32_t *out_record_ordinal);
/* Timeline index of the live screen top (rows above it are history). */
struct yetty_ycore_uint64_result yetty_yvterm_grid_live_anchor(struct yetty_yclass_object *obj);
/* Timeline index of the oldest line still reachable across all tiers — the
 * scrollback floor. With the cold tier unbounded this stays 0 for the whole
 * session ("effectively infinite" scrollback). */
uint64_t grid_history_floor_value(struct yetty_yvterm_grid *grid);
struct yetty_ycore_uint64_result yetty_yvterm_grid_history_floor(struct yetty_yclass_object *obj);
/* Enter/leave the scrolled-back view. `view_top_line` is the TIMELINE index
 * of the line to show at the window's top row (see grid_live_anchor /
 * grid_history_floor for the valid range). */
struct yetty_ycore_void_result yetty_yvterm_grid_set_view(struct yetty_yclass_object *obj,
                                                          int active, uint64_t view_top_line);
/* Current scrollback view state — the grid is the ONE owner; the terminal's
 * wheel driver reads this fresh on every event instead of holding its own
 * copy (which could reactivate a stale position after a resize or after
 * eviction moved the floor past it). */
struct yetty_ycore_void_result yetty_yvterm_grid_view(struct yetty_yclass_object *obj,
                                                      int *out_active, uint64_t *out_view_top);
/* Test/diagnostic seam: pre-age the timeline so 32-bit-wrap behavior is
 * testable without feeding four billion lines. Only meaningful on a fresh
 * grid (nothing archived or scrolled yet); the seeded range [base, base)
 * resolves as dropped history. */
struct yetty_ycore_void_result yetty_yvterm_grid_seed_timeline(struct yetty_yclass_object *obj,
                                                               uint64_t base);
/* Test/diagnostic seam: make the Nth upcoming ring allocation fail, so the
 * transactional-resize rollback (and any other alloc-failure branch) is
 * actually exercisable. 0 disables. */
struct yetty_ycore_void_result yetty_yvterm_grid_inject_ring_alloc_failure(
    struct yetty_yclass_object *obj, uint32_t nth_allocation);
/* Resolve the renderer's window for this frame: `row_count` rows starting at
 * the view top (live top when no view is active). Returns a grid-owned array
 * of one slot id per window row — real ring slots for hot rows (the text,
 * complex and SDF passes read them exactly as before), or extended ids
 * (>= slot_count) that the slot accessors transparently serve from the
 * archive materialization cache. Also sweeps archive figure runtimes whose
 * lines left the window, and prefetches the adjacent segment in the scroll
 * direction so figure re-decode hides behind the scroll. */
struct yetty_ycore_const_uint32_ptr_result yetty_yvterm_grid_view_window(
    struct yetty_yclass_object *obj, uint32_t row_count, uint32_t *out_row_count);
/* Warm / cold tier budgets (scrollback/warm-bytes, scrollback/file-max-bytes;
 * 0 keeps the built-in warm default / unlimited file). */
struct yetty_ycore_void_result yetty_yvterm_grid_set_tier_budgets(struct yetty_yclass_object *obj,
                                                                  uint32_t warm_bytes,
                                                                  uint32_t file_max_bytes);
/* Set one of the 16 ANSI palette entries (web-style 0xRRGGBB). Indexed
 * colours (SGR 30-37/90-97, 38;5;n for n < 16) are resolved through this
 * table as PTY data is parsed, so install the palette before feeding the
 * terminal any output. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_palette_color(struct yetty_yclass_object *obj,
                                                                   uint32_t index, uint32_t rgb);
/* Set the default foreground/background (web-style 0xRRGGBB) and re-seed
 * everything already derived from the built-in libvterm defaults: the state
 * is hard-reset so the active pen adopts the new colours, and the blank-line
 * template plus both screen rings are re-blanked. Intended to run right after
 * create, before any PTY data is fed — content already on screen is lost. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_default_colors(struct yetty_yclass_object *obj,
                                                                    uint32_t fg_rgb,
                                                                    uint32_t bg_rgb);
/* Register the grid's per-owner byte accounting (ring + archive tags) with
 * the framework's memtag registry, feeding the yctl `memtags` dump. The grid
 * unregisters itself at dispose. */
struct yetty_ycore_void_result yetty_yvterm_grid_register_memtags(
    struct yetty_yclass_object *obj, struct yetty_ycore_memtag_registry *registry);
/* Current selection rectangle (raw anchor/head; the renderer normalises to a
 * reading-order stream). active=0 → no selection. */
struct yetty_ycore_void_result yetty_yvterm_grid_selection(
    struct yetty_yclass_object *obj, int *out_active, uint32_t *out_anchor_row,
    uint32_t *out_anchor_col, uint32_t *out_head_row, uint32_t *out_head_col);
/* Renderer has consumed the model; drop every dirty flag (both screens — a
 * dormant ring's stale flags would otherwise fire a spurious full repaint on
 * the next switch, which mark_dirty_all re-arms anyway). */
struct yetty_ycore_void_result yetty_yvterm_grid_clear_dirty(struct yetty_yclass_object *obj);
/* Raw ring-slot accessors (slot in [0, slot_count)) for the text upload, which
 * is slot-indexed so the shader's root_row=base gives O(1) scroll. Distinct
 * from the visible-row accessors above (which resolve the ring). Slots address
 * the ACTIVE screen's ring; its size changes when the alternate screen toggles
 * (primary: visible + scrollback, alternate: visible only), so the renderer
 * re-queries slot_count each pass. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_slot_count(struct yetty_yclass_object *obj);
struct yetty_yvterm_text_cell_const_ptr_result yetty_yvterm_grid_slot_cells(
    struct yetty_yclass_object *obj, uint32_t slot);
struct yetty_ycore_int_result yetty_yvterm_grid_slot_dirty(struct yetty_yclass_object *obj,
                                                           uint32_t slot);
/* The line's rich coverage counter — the constant-time gate consulted by
 * putglyph/erase. Test observable: the counter must return to zero when the
 * blocks covering the line are gone, or every later write on the row pays
 * the rich invalidation walk for nothing. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_slot_rich_coverage(
    struct yetty_yclass_object *obj, uint32_t slot);

#ifdef __cplusplus
}
#endif

#endif
