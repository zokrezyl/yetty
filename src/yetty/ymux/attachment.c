/*
 * attachment.c — one client's view of a pane: class@ymux:attachment (#695
 * phase 2).
 *
 * Per-client state only — the canonical pane knows nothing of it: the
 * viewport anchor (follow-live, or a STABLE history coordinate), selection
 * in stable coordinates, the viewport geometry the client presents, and the
 * generation/ack bookkeeping the projector drives. Stable coordinates are
 * (logical_line_id, cell offset) resolved against the pane per use, so
 * reflow or floor movement never leaves a dangling screen-relative anchor.
 *
 * Phase-2 scope: view anchoring + resolution + generation counters.
 * Controller/permission flags live on the session (phase 5); the projector
 * (phase 4) consumes this object.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#include <yetty/api/ymux/engine.h>
#include <yetty/api/ymux/history.h>
#include <yetty/api/ymux/pane.h>

/* The attachment — the yclass data block. */
struct YETTY_ANNOTATE("class@ymux:attachment") yetty_ymux_attachment {
    /* The viewed pane. Borrowed — the session owns pane lifetimes and
     * detaches attachments before disposing panes. */
    struct yetty_yclass_object *pane;

    /* Viewport geometry the CLIENT presents (may differ from the pane's
     * canonical geometry; non-controllers crop/pad). */
    uint32_t view_rows;
    uint32_t view_cols;

    /* Follow-live (the tmux default), or anchored at a stable history
     * coordinate: the logical line id + the timeline hint where it was
     * last seen (the hint accelerates resolution; identity wins when the
     * two disagree after floor movement). */
    int follow_live;
    uint64_t anchor_logical_id;
    uint64_t anchor_timeline_hint;

    /* Selection in stable coordinates; inactive when anchor id is 0. */
    uint64_t selection_anchor_logical_id;
    uint32_t selection_anchor_offset;
    uint64_t selection_head_logical_id;
    uint32_t selection_head_offset;

    /* Projector bookkeeping: the last generation published to this client
     * and the last one it acknowledged. */
    uint64_t published_generation;
    uint64_t acked_generation;
};

/* Provided by the generated impl glue (foot include). */
struct yetty_yclass_ptr_result yetty_ymux_attachment_class_get(void);
struct yetty_ymux_attachment_ptr_result yetty_ymux_attachment_from(struct yetty_yclass_object *obj);
YETTY_YRESULT_DECLARE(yetty_ymux_attachment_ptr, struct yetty_ymux_attachment *);

YETTY_ANNOTATE("expose")
struct yetty_yclass_object_ptr_result yetty_ymux_attachment_make(struct yetty_yclass_object *pane,
                                                                 uint32_t view_rows,
                                                                 uint32_t view_cols)
{
    if (!pane || view_rows == 0 || view_cols == 0) {
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux attachment_make: invalid arguments");
    }
    struct yetty_yclass_ptr_result class_res = yetty_ymux_attachment_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res, "ymux attachment_make: class");
    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res, "ymux attachment_make: alloc");
    struct yetty_ymux_attachment_ptr_result attachment_res =
        yetty_ymux_attachment_from(object_res.value);
    if (YETTY_IS_ERR(attachment_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(object_res.value);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "ymux attachment_make: from_obj", attachment_res);
    }
    struct yetty_ymux_attachment *attachment = attachment_res.value;
    attachment->pane = pane;
    attachment->view_rows = view_rows;
    attachment->view_cols = view_cols;
    attachment->follow_live = 1;
    return YETTY_OK(yetty_yclass_object_ptr, object_res.value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_dispose(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_dispose: from_obj");
    return yetty_yclass_object_free(obj);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_set_view_size(struct yetty_yclass_object *obj,
                                                                   uint32_t view_rows,
                                                                   uint32_t view_cols)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_set_view_size");
    if (view_rows == 0 || view_cols == 0) {
        return YETTY_ERR(yetty_ycore_void, "ymux attachment_set_view_size: invalid size");
    }
    attachment_res.value->view_rows = view_rows;
    attachment_res.value->view_cols = view_cols;
    return YETTY_OK_VOID();
}

/* Enter follow-live (the default) — the viewport tracks the live bottom. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_follow(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_follow: from_obj");
    attachment_res.value->follow_live = 1;
    attachment_res.value->anchor_logical_id = 0;
    return YETTY_OK_VOID();
}

/* Anchor the viewport top at a timeline row: the row's STABLE logical id is
 * captured (with the timeline as a hint), so the anchor survives floor
 * movement and later re-resolution. Anchoring past the live top clamps to
 * follow-live. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_anchor(struct yetty_yclass_object *obj,
                                                            uint64_t timeline_idx)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_anchor: from_obj");
    struct yetty_ymux_attachment *attachment = attachment_res.value;
    uint64_t live_top = 0;
    struct yetty_ycore_void_result timeline_res =
        yetty_ymux_pane_timeline(attachment->pane, NULL, &live_top);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, timeline_res, "ymux attachment_anchor: timeline");
    if (timeline_idx >= live_top) {
        attachment->follow_live = 1;
        attachment->anchor_logical_id = 0;
        return YETTY_OK_VOID();
    }
    struct yetty_ymux_history_row_result row_res =
        yetty_ymux_pane_resolve_row(attachment->pane, timeline_idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, row_res, "ymux attachment_anchor: resolve");
    attachment->follow_live = 0;
    attachment->anchor_logical_id = row_res.value.logical_line_id;
    attachment->anchor_timeline_hint = timeline_idx;
    return YETTY_OK_VOID();
}

/* Resolve the viewport's TOP timeline index right now: follow-live tracks
 * the live screen; an anchored view re-locates its logical id around the
 * hint (identity wins over the hint), clamping to the floor when the
 * anchored row was dropped. */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_attachment_view_top(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, attachment_res, "ymux attachment_view_top: from_obj");
    struct yetty_ymux_attachment *attachment = attachment_res.value;
    uint64_t floor_value = 0, live_top = 0;
    struct yetty_ycore_void_result timeline_res =
        yetty_ymux_pane_timeline(attachment->pane, &floor_value, &live_top);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, timeline_res, "ymux attachment_view_top: timeline");
    if (attachment->follow_live) {
        return YETTY_OK(yetty_ycore_uint64, live_top);
    }
    /* Identity check at the hint; walk outward a bounded distance when the
     * hint no longer matches (floor drops shift nothing — timeline indices
     * are stable — but reflow re-derivation in later phases may move a
     * logical line by a few rows). */
    uint64_t hint = attachment->anchor_timeline_hint;
    if (hint < floor_value) {
        hint = floor_value;
    }
    if (hint >= live_top && live_top > 0) {
        hint = live_top - 1;
    }
    enum { YMUX_ANCHOR_SEARCH_RADIUS = 64 };
    for (uint64_t distance = 0; distance <= YMUX_ANCHOR_SEARCH_RADIUS; ++distance) {
        uint64_t candidates[2] = {hint + distance,
                                  hint >= distance ? hint - distance : (uint64_t)0 - 1};
        for (int side = 0; side < 2; ++side) {
            uint64_t candidate = candidates[side];
            if (candidate == (uint64_t)0 - 1 || candidate < floor_value || candidate >= live_top) {
                continue;
            }
            struct yetty_ymux_history_row_result row_res =
                yetty_ymux_pane_resolve_row(attachment->pane, candidate);
            if (YETTY_IS_ERR(row_res)) {
                yetty_ycore_error_destroy(row_res.error);
                continue;
            }
            if (row_res.value.logical_line_id == attachment->anchor_logical_id) {
                attachment->anchor_timeline_hint = candidate;
                return YETTY_OK(yetty_ycore_uint64, candidate);
            }
            if (side == 0 && distance == 0) {
                break; /* hint mismatch: try both directions from now on */
            }
        }
    }
    /* The anchored row is gone (dropped past the floor): clamp. */
    attachment->anchor_timeline_hint = floor_value;
    return YETTY_OK(yetty_ycore_uint64, floor_value);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_int_result yetty_ymux_attachment_is_following(struct yetty_yclass_object *obj)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, attachment_res, "ymux attachment_is_following");
    return YETTY_OK(yetty_ycore_int, attachment_res.value->follow_live);
}

/* Selection endpoints in stable coordinates (0 anchor id = inactive). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_set_selection(struct yetty_yclass_object *obj,
                                                                   uint64_t anchor_logical_id,
                                                                   uint32_t anchor_offset,
                                                                   uint64_t head_logical_id,
                                                                   uint32_t head_offset)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_set_selection");
    struct yetty_ymux_attachment *attachment = attachment_res.value;
    attachment->selection_anchor_logical_id = anchor_logical_id;
    attachment->selection_anchor_offset = anchor_offset;
    attachment->selection_head_logical_id = head_logical_id;
    attachment->selection_head_offset = head_offset;
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_selection(struct yetty_yclass_object *obj,
                                                               uint64_t *out_anchor_logical_id,
                                                               uint32_t *out_anchor_offset,
                                                               uint64_t *out_head_logical_id,
                                                               uint32_t *out_head_offset)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_selection");
    struct yetty_ymux_attachment *attachment = attachment_res.value;
    if (out_anchor_logical_id) {
        *out_anchor_logical_id = attachment->selection_anchor_logical_id;
    }
    if (out_anchor_offset) {
        *out_anchor_offset = attachment->selection_anchor_offset;
    }
    if (out_head_logical_id) {
        *out_head_logical_id = attachment->selection_head_logical_id;
    }
    if (out_head_offset) {
        *out_head_offset = attachment->selection_head_offset;
    }
    return YETTY_OK_VOID();
}

/* Generation bookkeeping (the projector publishes; the wire acks). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_uint64_result yetty_ymux_attachment_next_generation(
    struct yetty_yclass_object *obj)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_uint64, attachment_res, "ymux attachment_next_generation");
    return YETTY_OK(yetty_ycore_uint64, ++attachment_res.value->published_generation);
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_ack(struct yetty_yclass_object *obj,
                                                         uint64_t generation)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_ack");
    struct yetty_ymux_attachment *attachment = attachment_res.value;
    if (generation > attachment->published_generation) {
        return YETTY_ERR(yetty_ycore_void, "ymux attachment_ack: unpublished generation");
    }
    if (generation > attachment->acked_generation) {
        attachment->acked_generation = generation;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_generations(struct yetty_yclass_object *obj,
                                                                 uint64_t *out_published,
                                                                 uint64_t *out_acked)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_generations");
    if (out_published) {
        *out_published = attachment_res.value->published_generation;
    }
    if (out_acked) {
        *out_acked = attachment_res.value->acked_generation;
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ymux_attachment_view_size(struct yetty_yclass_object *obj,
                                                               uint32_t *out_rows,
                                                               uint32_t *out_cols)
{
    struct yetty_ymux_attachment_ptr_result attachment_res = yetty_ymux_attachment_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, attachment_res, "ymux attachment_view_size");
    if (out_rows) {
        *out_rows = attachment_res.value->view_rows;
    }
    if (out_cols) {
        *out_cols = attachment_res.value->view_cols;
    }
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ymux/attachment.c"
