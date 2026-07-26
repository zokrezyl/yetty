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
struct yetty_ydraw_composite;
struct yetty_yvterm_grid;
struct yetty_ywire_wire_statemachine;

typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_clear_hook_fn)(void *);
typedef struct yetty_ycore_void_result (*yetty_yvterm_grid_materialize_fn)(const uint32_t *, uint32_t, void *, struct yetty_ydraw_composite **);

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
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAW_COMPOSITE_CONST_PTR_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAW_COMPOSITE_CONST_PTR_PTR_RESULT
struct yetty_ydraw_composite_const_ptr_ptr_result {
    int ok;
    union {
        struct yetty_ydraw_composite *const *value;
        struct yetty_ycore_error error;
    } ;
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_CELL_CONST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_TEXT_CELL_CONST_PTR_RESULT
struct yetty_yvterm_text_cell_const_ptr_result {
    int ok;
    union {
        const struct yetty_yvterm_text_cell *value;
        struct yetty_ycore_error error;
    } ;
};
#endif



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



struct yetty_yclass_object_ptr_result yetty_yvterm_grid_make(uint32_t cols, uint32_t rows, uint32_t scrollback_rows, uint32_t hot_rows);
struct yetty_ycore_void_result yetty_yvterm_grid_dispose(struct yetty_yclass_object *obj);
/* Install the terminal-host sink the grid dispatches its upcalls on (pty_write
 * / mouse_sub / clipboard_write / sixel_write). Borrowed — the host owns it. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_sink(struct yetty_yclass_object *obj, struct yetty_yclass_object *sink);
struct yetty_ycore_void_result yetty_yvterm_grid_set_clear_hook(struct yetty_yclass_object *obj, yetty_yvterm_grid_clear_hook_fn fn, void *userdata);
/* Register the figure re-materialization hook. The integration layer (which
 * owns the composite factory) supplies it; the grid replays retained wire
 * envelopes through it when an evicted history line scrolls back into view. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_materialize(struct yetty_yclass_object *obj, yetty_yvterm_grid_materialize_fn fn, void *userdata);
struct yetty_ycore_void_result yetty_yvterm_grid_feed(struct yetty_yclass_object *obj, const char *bytes, size_t len);
/* Feed the current text-area pixel size to libvterm so DEC mode 2048 (in-band
 * window resize) can report it, then drain any notification the size change
 * produced. Called by the terminal on every resize (and once initially). */
struct yetty_ycore_void_result yetty_yvterm_grid_set_pixel_size(struct yetty_yclass_object *obj, uint32_t width_px, uint32_t height_px);
struct yetty_ycore_void_result yetty_yvterm_grid_resize(struct yetty_yclass_object *obj, uint32_t cols, uint32_t rows);
struct yetty_ycore_int_result yetty_yvterm_grid_is_dirty(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_grid_cursor(struct yetty_yclass_object *obj, uint32_t *out_row, uint32_t *out_col, uint32_t *out_visible);
/* Absolute row at the top of the ACTIVE screen (rows scrolled off so far). The
 * cursor's absolute output row is this + the visible cursor row — used to place
 * anchored rich content on the rolling-row scroll. Each screen carries its own
 * origin; the alternate one restarts at 0 on every entry. */
struct yetty_ycore_uint64_result yetty_yvterm_grid_scroll_origin(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_grid_append_primitive(struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count);
struct yetty_ycore_uint32_result yetty_yvterm_grid_attach_composite(struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_composite *composite);
/* Re-home a freshly-ingested rich block from its TOP line onto its BOTTOM line
 * and stamp the block's row span there, so a figure (composite or SDF block)
 * leaves the scrollback only when its LAST overlapping line is evicted, not its
 * first. Called once after the reserve newlines that allocated the block's rows:
 * the cursor then sits on the line just BELOW the block, so the bottom line is
 * cursor_row − 1 and the top is cursor_row − span_rows. span_rows is the row
 * height the block reserved. A span of 1 is a single-row block (top == bottom):
 * only the span is recorded, nothing moves. The content was attached on the top
 * line via attach_composite / append_primitive during ingestion; those rows are
 * never recycled during their own reserve (the scroll blanks the rows below the
 * block), so the top line still holds the content here. */
struct yetty_ycore_void_result yetty_yvterm_grid_relocate_rich_to_bottom(struct yetty_yclass_object *obj, uint32_t span_rows);
struct yetty_ycore_void_result yetty_yvterm_grid_clear_rich_line(struct yetty_yclass_object *obj, uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_grid_clear_rich_all(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_grid_register_wire(struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_int_result yetty_yvterm_grid_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods);
struct yetty_ycore_int_result yetty_yvterm_grid_on_key(struct yetty_yclass_object *obj, int key, int mods);
struct yetty_ycore_void_result yetty_yvterm_grid_set_selection(struct yetty_yclass_object *obj, int active, uint32_t anchor_row, uint32_t anchor_col, uint32_t head_row, uint32_t head_col);
struct yetty_ycore_void_result yetty_yvterm_grid_get_selection_text(struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out);
/* Word boundaries around (row, col): the inclusive [start_col, end_col] run of
 * word chars covering the clicked cell. A click on a non-word cell selects just
 * that cell. Used for double-click word selection. */
struct yetty_ycore_void_result yetty_yvterm_grid_word_bounds(struct yetty_yclass_object *obj, uint32_t row, uint32_t col, uint32_t *out_start_col, uint32_t *out_end_col);
struct yetty_ycore_void_result yetty_yvterm_grid_dims(struct yetty_yclass_object *obj, uint32_t *out_cols, uint32_t *out_rows, uint32_t *out_base);
/* The cell array for visible row `row` (length = cols). NULL value if out of
 * range. */
struct yetty_yvterm_text_cell_const_ptr_result yetty_yvterm_grid_line_cells(struct yetty_yclass_object *obj, uint32_t row);
struct yetty_ycore_int_result yetty_yvterm_grid_line_dirty(struct yetty_yclass_object *obj, uint32_t row);
/* Composite array anchored on visible row `row`. Sets *out_count; value may be
 * NULL. */
struct yetty_ydraw_composite_const_ptr_ptr_result yetty_yvterm_grid_line_composites(struct yetty_yclass_object *obj, uint32_t row, uint32_t *out_count);
/* Composite array on RAW ring slot `slot` (0..slot_count) or an extended
 * view-window id. Distinct from the visible-row accessor above: the text
 * upload + shader address the ring by raw slot via the resolved window, so
 * the composite pass must read by the SAME slot to scroll in lockstep (live
 * AND scrolled-back). Sets *out_count; may be NULL. */
struct yetty_ydraw_composite_const_ptr_ptr_result yetty_yvterm_grid_slot_composites(struct yetty_yclass_object *obj, uint32_t slot, uint32_t *out_count);
/* Number of raw drawable records (SDF / glyph / TEXT_DRAWABLE_LIST / FONT) stored
 * on RAW ring slot `slot`. The SDF render pass walks these by slot — same raw-slot
 * addressing the composite + text passes use — so figures and text scroll together. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_slot_primitive_count(struct yetty_yclass_object *obj, uint32_t slot);
/* Row-span of the rich-content block whose BOTTOM line is RAW ring slot `slot`.
 * The renderer recovers the block's top row as (slot's row − (span − 1)) so the
 * figure draws top-down from where its text sits, while the block is owned (and
 * evicted) by its bottom line. 0 means the slot carries no relocated block —
 * the renderer then treats it as a single-row anchor. */
struct yetty_ycore_uint32_result yetty_yvterm_grid_slot_span(struct yetty_yclass_object *obj, uint32_t slot);
/* Words of primitive `index` on RAW ring slot `slot`. *out_word_count is set to the
 * record's u32 length. Returns NULL (and *out_word_count 0) if slot/index are out of
 * range. The returned span aliases the line's arena — read it, do not retain it. */
struct yetty_ycore_const_uint32_ptr_result yetty_yvterm_grid_slot_primitive_words(struct yetty_yclass_object *obj, uint32_t slot, uint32_t index, uint32_t *out_word_count);
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
struct yetty_ycore_void_result yetty_yvterm_grid_set_view(struct yetty_yclass_object *obj, int active, uint64_t view_top_line);
/* Current scrollback view state — the grid is the ONE owner; the terminal's
 * wheel driver reads this fresh on every event instead of holding its own
 * copy (which could reactivate a stale position after a resize or after
 * eviction moved the floor past it). */
struct yetty_ycore_void_result yetty_yvterm_grid_view(struct yetty_yclass_object *obj, int *out_active, uint64_t *out_view_top);
/* Test/diagnostic seam: pre-age the timeline so 32-bit-wrap behavior is
 * testable without feeding four billion lines. Only meaningful on a fresh
 * grid (nothing archived or scrolled yet); the seeded range [base, base)
 * resolves as dropped history. */
struct yetty_ycore_void_result yetty_yvterm_grid_seed_timeline(struct yetty_yclass_object *obj, uint64_t base);
/* Test/diagnostic seam: make the Nth upcoming ring allocation fail, so the
 * transactional-resize rollback (and any other alloc-failure branch) is
 * actually exercisable. 0 disables. */
struct yetty_ycore_void_result yetty_yvterm_grid_inject_ring_alloc_failure(struct yetty_yclass_object *obj, uint32_t nth_allocation);
/* Resolve the renderer's window for this frame: `row_count` rows starting at
 * the view top (live top when no view is active). Returns a grid-owned array
 * of one slot id per window row — real ring slots for hot rows (the text,
 * composite and SDF passes read them exactly as before), or extended ids
 * (>= slot_count) that the slot accessors transparently serve from the
 * archive materialization cache. Also sweeps archive figure runtimes whose
 * lines left the window, and prefetches the adjacent segment in the scroll
 * direction so figure re-decode hides behind the scroll. */
struct yetty_ycore_const_uint32_ptr_result yetty_yvterm_grid_view_window(struct yetty_yclass_object *obj, uint32_t row_count, uint32_t *out_row_count);
/* Warm / cold tier budgets (scrollback/warm-bytes, scrollback/file-max-bytes;
 * 0 keeps the built-in warm default / unlimited file). */
struct yetty_ycore_void_result yetty_yvterm_grid_set_tier_budgets(struct yetty_yclass_object *obj, uint32_t warm_bytes, uint32_t file_max_bytes);
/* Set one of the 16 ANSI palette entries (web-style 0xRRGGBB). Indexed
 * colours (SGR 30-37/90-97, 38;5;n for n < 16) are resolved through this
 * table as PTY data is parsed, so install the palette before feeding the
 * terminal any output. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_palette_color(struct yetty_yclass_object *obj, uint32_t index, uint32_t rgb);
/* Set the default foreground/background (web-style 0xRRGGBB) and re-seed
 * everything already derived from the built-in libvterm defaults: the state
 * is hard-reset so the active pen adopts the new colours, and the blank-line
 * template plus both screen rings are re-blanked. Intended to run right after
 * create, before any PTY data is fed — content already on screen is lost. */
struct yetty_ycore_void_result yetty_yvterm_grid_set_default_colors(struct yetty_yclass_object *obj, uint32_t fg_rgb, uint32_t bg_rgb);
/* Register the grid's per-owner byte accounting (ring + archive tags) with
 * the framework's memtag registry, feeding the yctl `memtags` dump. The grid
 * unregisters itself at dispose. */
struct yetty_ycore_void_result yetty_yvterm_grid_register_memtags(struct yetty_yclass_object *obj, struct yetty_ycore_memtag_registry *registry);
/* Current selection rectangle (raw anchor/head; the renderer normalises to a
 * reading-order stream). active=0 → no selection. */
struct yetty_ycore_void_result yetty_yvterm_grid_selection(struct yetty_yclass_object *obj, int *out_active, uint32_t *out_anchor_row, uint32_t *out_anchor_col, uint32_t *out_head_row, uint32_t *out_head_col);
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
struct yetty_yvterm_text_cell_const_ptr_result yetty_yvterm_grid_slot_cells(struct yetty_yclass_object *obj, uint32_t slot);
struct yetty_ycore_int_result yetty_yvterm_grid_slot_dirty(struct yetty_yclass_object *obj, uint32_t slot);

#ifdef __cplusplus
}
#endif

#endif
