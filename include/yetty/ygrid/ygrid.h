/*
 * ygrid — figure kind: spatial-bucketed batch of SDF primitives + glyphs.
 *
 * A ygrid is one concrete `yetty_yfigure_figure` implementation. It owns
 * a wire-format buffer of prim records (the same compact encoding used
 * by ycompositor's OSC envelope: u32 type | u32 payload_size | bytes),
 * partitions them by terminal grid cell at render time, and dispatches
 * the GPU pipeline that handles SDF + glyph rendering.
 *
 * Coordinate system: every record's coordinates are LOCAL to the
 * ygrid's own origin. The compositor (or enclosing group) passes the
 * ygrid's absolute rect at render time; ygrid translates record coords
 * by abs_rect.min before submitting GPU work. Moving a ygrid is a
 * single rect update — children records don't move.
 *
 * Scope:
 *   - SDF primitives (type-id range 0x10000000–0x1FFFFFFF)
 *   - Glyph / TEXT_DRAWABLE_LIST records (drawable-list tier)
 *   - Implicit ADD is the default decode action (no opcode byte).
 *
 * Figures that are not naturally rendered by a grid (yplot, yimage,
 * yvideo, ymgui cards, yrdawn surfaces) are NOT inside ygrid — they
 * each become their own compositor figure with their own pipeline.
 *
 * State of v1: storage + API + figure ops scaffold. The render op is
 * a no-op pending the GPU pipeline (shader, atlas, instanced draws).
 */
#ifndef YETTY_YGRID_YGRID_H
#define YETTY_YGRID_YGRID_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
/* The generated per-class header publishes the canonical class accessor,
 * the opaque `struct yetty_ygrid_grid` forward decl, and the
 * `YETTY_YRESULT_DECLARE(yetty_ygrid_grid_ptr, …)` wrapper this header's
 * own declarations return. Pull it in so the result type has a single
 * definition shared by every consumer — this header no longer declares
 * its own (which would redefine the struct in any TU that included
 * both). */
#include <yetty/ygrid/grid.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygrid_grid;
struct yetty_yfont_font;
struct yetty_yfigure_registry;
struct yetty_ydraw_composite_factory;

/* Bundle of host-owned pointers handed to every ygrid the factory mints.
 * Both fields are borrowed — the host (terminal / yui) keeps the actual
 * font and composite factory alive for the lifetime of every ygrid
 * the registry might still be holding.
 *
 *   default_font   slot-0 font for GLYPH/TEXT_DRAWABLE_LIST expansion. NULL → no
 *                  default font, glyph records silently drop.
 *   composite_factory composite renderer (yplot / yimage / yvideo / …).
 *                  NULL → composite records silently drop, same as
 *                  the v1 behaviour. */
struct yetty_ygrid_factory_args {
    struct yetty_yfont_font *default_font;
    struct yetty_ydraw_composite_factory *composite_factory;
};

/* Register the ygrid factory under YETTY_YFIGURE_KIND_YGRID with the
 * given registry. Subsequent admin CREATE_CHILD records with kind=YGRID
 * land in the factory, which mints a ygrid at the supplied rect using
 * default grid dims (1x1 — the ygrid is for flat prims; cell bucketing
 * isn't useful for arbitrary widget rects).
 *
 * `args` is borrowed and must outlive every ygrid the factory will ever
 * mint. The host should keep it as part of its own state. */
struct yetty_ycore_void_result yetty_ygrid_register_factory(
    struct yetty_yfigure_registry *registry, const struct yetty_ygrid_factory_args *args);

/* Register the ygrid factory under an arbitrary kind code. Used by
 * ygui's complex producer widgets (yplot/yimage/yvideo/yzoo/yjungle)
 * so their content lands in dedicated kind slots on the wire — same
 * underlying renderer but a distinct kind tag that proxies and
 * analyzers can route on. Args has the same lifetime contract. */
struct yetty_ycore_void_result yetty_ygrid_register_factory_for_kind(
    struct yetty_yfigure_registry *registry, uint32_t kind,
    const struct yetty_ygrid_factory_args *args);

/* Attach a composite figure factory. Borrowed; lifetime must
 * outlive the ygrid. Complex prims (yplot / yimage / etc.) arriving
 * via process_bytes after this call mint a figure instance through
 * the factory and are rendered alongside the SDF / glyph pass. */
void yetty_ygrid_set_composite_factory(struct yetty_ygrid_grid *grid,
                                       struct yetty_ydraw_composite_factory *factory);

/* Create an empty ygrid figure with the given AABB in absolute target
 * pixel space. The grid cell layout (cols × rows) controls how the GPU
 * pipeline will eventually bucket prims for batch dispatch. The
 * context supplies the GPU device / queue / surface_format used to
 * build the pipeline and per-instance bind group. */
struct yetty_ygrid_grid_ptr_result yetty_ygrid_create(struct yetty_ycore_rectangle rect,
                                                      uint32_t grid_cols, uint32_t grid_rows,
                                                      const struct yetty_context *context);

/* Upcast helper — a ygrid is a figure. */
struct yetty_yfigure_figure *yetty_ygrid_as_figure(struct yetty_ygrid_grid *grid);

/* Append one wire record to the grid. `record_bytes` points at the
 * full u32-type | u32-payload_size | bytes block; `record_len` is the
 * total size of that block (8 + payload_size). Coordinates inside the
 * record are interpreted as LOCAL to the grid's origin.
 *
 * Storage is opaque at this layer: the bytes are copied verbatim and
 * parsed lazily by the render path via the ydraw-core drawable-list entry
 * registry.
 *
 * NAMING: this is the in-process, raw-bytes entry point — the legacy
 * surface tools and tests still drive directly. The canonical yclass
 * slot is `yetty_ygrid_add_record(ctx, obj, struct yetty_ycore_buffer)`
 * (emitted by codegen in `<yetty/ygrid/grid.h>`); it dispatches
 * via the registered class and works for local AND remote callers.
 * This `_local` variant exists only until every caller migrates. */
struct yetty_ycore_void_result yetty_ygrid_add_record_local(struct yetty_ygrid_grid *grid,
                                                            const uint8_t *record_bytes,
                                                            size_t record_len);

/* Drop every record. The figure stays alive at its current rect with
 * an empty payload (renders nothing). Marks the figure dirty so the
 * compositor exposes the previously-covered region.
 *
 * NAMING: see `yetty_ygrid_add_record_local` — the unsuffixed name is
 * the yclass slot stub; this is the in-process variant. */
struct yetty_ycore_void_result yetty_ygrid_clear_local(struct yetty_ygrid_grid *grid);

/* Attach a font at the given slot. Slot 0 is the default font (the slot
 * a GLYPH record addresses when its packed `font_id` field is 0); higher
 * slots are for alternate fonts (e.g. PDF-embedded). Pass `font = NULL`
 * to clear a slot.
 *
 * The font's gpu_resource_set is attached as a child of the ygrid's rs,
 * its namespaced helpers (<ns>_glyph_size, <ns>_glyph_sample, …) are
 * merged into the compiled shader, and a per-slot dispatcher routes
 * `slot` to the right helper. Changing the font set after the first
 * render triggers a shader recompile via the binder's hash-change path.
 *
 * Caller retains ownership of the font — ygrid borrows the pointer and
 * does NOT destroy it on ygrid_destroy. */
struct yetty_ycore_void_result yetty_ygrid_set_font(struct yetty_ygrid_grid *grid, uint32_t slot,
                                                    struct yetty_yfont_font *font);

/* Content extent in px. By default a ygrid's content fills its on-screen
 * rect; set a larger extent to make it a scroll viewport — the cell grid
 * and prim bucketing then span the content while the rect stays the
 * visible window. Pass 0 for an axis to mean "same as the rect". Records
 * are still authored in content-local coords. */
void yetty_ygrid_set_content_size(struct yetty_ygrid_grid *grid, float content_w, float content_h);

/* Scroll offset in px: the content coordinate shown at the rect's
 * top-left. The shader maps the rect onto the content window starting
 * here; the per-figure scissor clips. Records don't move — scrolling is a
 * single offset update + redraw, no re-emit. */
void yetty_ygrid_set_scroll(struct yetty_ygrid_grid *grid, float scroll_x, float scroll_y);

/* Rolling-row scroll for terminal scrollback content. `set_insert_rolling_row`
 * stamps the absolute output row onto every record added afterwards (call it
 * before add_record with the cursor's absolute row); `set_rolling_row_0` sets
 * the absolute row currently at the top of the view. The shader draws each prim
 * at (its rolling_row - rolling_row_0) * cell_h, so figures scroll in lockstep
 * with the terminal text. Both default 0 (compositor figures don't scroll). */
void yetty_ygrid_set_insert_rolling_row(struct yetty_ygrid_grid *grid, uint32_t rolling_row);
void yetty_ygrid_set_rolling_row_0(struct yetty_ygrid_grid *grid, uint32_t rolling_row_0);

/* Per-row pixel height used for rolling-row figure placement. The host sets
 * this to the authoritative text cell height so anchored composite figures
 * line up with the terminal's text rows independent of the grid's bucketing
 * geometry. 0 (default) falls back to content_extent / grid_rows. */
void yetty_ygrid_set_rolling_cell_height(struct yetty_ygrid_grid *grid, float cell_h);

/* Resize the cell grid to cols×rows, re-bucketing existing prims. The caller
 * must also update the figure's rect (via the figure rect setter) so the cell
 * height (content_extent / rows) tracks the terminal's text cell height — this
 * keeps rolling-row figure placement aligned with the text after a window
 * resize. No-op when the dims are unchanged. */
struct yetty_ycore_void_result yetty_ygrid_resize(struct yetty_ygrid_grid *grid, uint32_t cols,
                                                  uint32_t rows);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGRID_YGRID_H */
