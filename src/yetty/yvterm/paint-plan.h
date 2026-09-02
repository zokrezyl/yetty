/*
 * paint-plan.h — the screen-wide sorted render plan of the yvterm rich
 * layer (module-private; grid.c builds it, vterm.c executes it).
 *
 * One plan covers every render-leaf record of a screen's resident rich
 * blocks (the primary screen's plan additionally covers blocks
 * materialized in the archive view cache), sorted lexicographically by
 * the total paint key (paint_z, paint_sequence, record_ordinal). Lower
 * keys render first; later keys composite on top.
 *
 * A leaf caches ONLY the generation-checked block handle, the stable
 * record index, the record kind and the complete key. It never caches an
 * arena pointer, creation-envelope pointer or complex runtime pointer:
 * the per-frame walk resolves bytes, rolling anchors and runtimes through
 * the handle and verifies the record is still alive, so arena moves and
 * hot-tier runtime eviction cannot dangle a plan entry.
 *
 * Validity is stamp-checked against the store's per-screen paint
 * generations (membership/key changes only). Ordinary scrolling, block
 * anchor movement, sealing, runtime eviction, journal traffic and view
 * origin changes do not touch those generations and therefore reuse the
 * cached order unchanged.
 */
#ifndef YETTY_YVTERM_PAINT_PLAN_H
#define YETTY_YVTERM_PAINT_PLAN_H

#include <stdint.h>

#include <yetty/ycore/result.h>

#include "rich-store.h"

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_object;

struct yetty_yvterm_paint_leaf {
    struct yetty_yvterm_rich_handle block;
    uint32_t record_index;
    enum yetty_yvterm_rich_record_kind kind;
    int32_t paint_z;
    uint64_t paint_sequence;
    uint32_t record_ordinal;
};

/* A screen's cached sorted leaf plan. `built_stamp` snapshots the sum of
 * the store paint generations the plan covers (each is monotone, so the
 * sum changes whenever any constituent does); `built` distinguishes an
 * empty valid plan from a never-built one. */
struct yetty_yvterm_paint_plan {
    struct yetty_yvterm_paint_leaf *leaves;
    uint32_t leaf_count;
    uint32_t leaf_capacity;
    uint64_t built_stamp;
    int built;
    /* Rebuild counter — cache-reuse observability for tests. */
    uint64_t build_count;
};

/* The ACTIVE screen's plan, rebuilt first if its stamp went stale. The
 * returned pointer stays valid until the next grid mutation; the caller
 * walks it within the current frame only. */
struct yetty_ycore_void_result yetty_yvterm_grid_paint_plan_current(
    struct yetty_yclass_object *grid_obj, const struct yetty_yvterm_paint_plan **out_plan);

/* Per-frame leaf resolution: bytes, runtime and placement re-resolve
 * through the generation-checked handle every frame. A stale handle or a
 * dead/replaced record reports *out_alive = 0 (skip the leaf) — never an
 * error. Outputs may be NULL when the caller does not need them. */
void yetty_yvterm_grid_paint_leaf_resolve(struct yetty_yclass_object *grid_obj,
                                          const struct yetty_yvterm_paint_leaf *leaf,
                                          const uint32_t **out_words, uint32_t *out_word_count,
                                          struct yetty_ydraw_complex **out_complex,
                                          uint32_t *out_span_rows, uint64_t *out_bottom_owner_row,
                                          int *out_alive, float *out_offset_x, float *out_offset_y);

/* The leaf's accumulated ancestor CLIP (intersection, block-content space);
 * *out_valid 0 = unclipped. */
void yetty_yvterm_grid_paint_leaf_clip(struct yetty_yclass_object *grid_obj,
                                       const struct yetty_yvterm_paint_leaf *leaf, int *out_valid,
                                       float *out_x, float *out_y, float *out_w, float *out_h);

/* Timeline row currently at viewport row 0 of the ACTIVE screen (the
 * scrollback view origin when a view is active, else the live top). Leaf
 * placement: viewport_bottom = bottom_owner_row − this. */
uint64_t yetty_yvterm_grid_paint_view_top(struct yetty_yclass_object *grid_obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVTERM_PAINT_PLAN_H */
