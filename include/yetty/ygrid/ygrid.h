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
 *   - Glyph / TEXT_SPAN records (flyweight tier)
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

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygrid_grid;

YETTY_YRESULT_DECLARE(yetty_ygrid_grid_ptr, struct yetty_ygrid_grid *);

/* Create an empty ygrid figure with the given AABB in absolute target
 * pixel space. The grid cell layout (cols × rows) controls how the GPU
 * pipeline will eventually bucket prims for batch dispatch. The
 * context supplies the GPU device / queue / surface_format used to
 * build the pipeline and per-instance bind group. */
struct yetty_ygrid_grid_ptr_result yetty_ygrid_create(
    struct yetty_ycore_rectangle rect, uint32_t grid_cols, uint32_t grid_rows,
    const struct yetty_context *context);

/* Upcast helper — a ygrid is a figure. */
struct yetty_yfigure_figure *yetty_ygrid_as_figure(struct yetty_ygrid_grid *grid);

/* Append one wire record to the grid. `record_bytes` points at the
 * full u32-type | u32-payload_size | bytes block; `record_len` is the
 * total size of that block (8 + payload_size). Coordinates inside the
 * record are interpreted as LOCAL to the grid's origin.
 *
 * Storage is opaque at this layer: the bytes are copied verbatim and
 * parsed lazily by the render path via the ydraw-core flyweight
 * registry. */
struct yetty_ycore_void_result yetty_ygrid_add_record(
    struct yetty_ygrid_grid *grid,
    const uint8_t *record_bytes, size_t record_len);

/* Drop every record. The figure stays alive at its current rect with
 * an empty payload (renders nothing). Marks the figure dirty so the
 * compositor exposes the previously-covered region. */
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_ygrid_grid *grid);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGRID_YGRID_H */
