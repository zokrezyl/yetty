/* GENERATED — do not edit. */
/* Public interface for regular class(es) `attachment` (module: ymux).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YMUX_ATTACHMENT_H
#define YETTY_YCLASSGEN_YMUX_ATTACHMENT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The attachment — the yclass data block. */
struct yetty_yclass_ptr_result yetty_ymux_attachment_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymux_attachment;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMUX_ATTACHMENT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMUX_ATTACHMENT_PTR_RESULT
struct yetty_ymux_attachment_ptr_result {
    int ok;
    union {
        struct yetty_ymux_attachment *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymux_attachment_ptr_result yetty_ymux_attachment_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymux_attachment_to(struct yetty_ymux_attachment *data);

struct yetty_yclass_object_ptr_result yetty_ymux_attachment_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymux_register(void);

struct yetty_yclass_object_ptr_result yetty_ymux_attachment_make(struct yetty_yclass_object *pane,
                                                                 uint32_t view_rows,
                                                                 uint32_t view_cols);
struct yetty_ycore_void_result yetty_ymux_attachment_dispose(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_attachment_set_view_size(struct yetty_yclass_object *obj,
                                                                   uint32_t view_rows,
                                                                   uint32_t view_cols);
/* Enter follow-live (the default) — the viewport tracks the live bottom. */
struct yetty_ycore_void_result yetty_ymux_attachment_follow(struct yetty_yclass_object *obj);
/* Anchor the viewport top at a timeline row: the row's STABLE logical id is
 * captured (with the timeline as a hint), so the anchor survives floor
 * movement and later re-resolution. Anchoring past the live top clamps to
 * follow-live. */
struct yetty_ycore_void_result yetty_ymux_attachment_anchor(struct yetty_yclass_object *obj,
                                                            uint64_t timeline_idx);
/* Resolve the viewport's TOP timeline index right now: follow-live tracks
 * the live screen; an anchored view re-locates its logical id around the
 * hint (identity wins over the hint), clamping to the floor when the
 * anchored row was dropped. */
struct yetty_ycore_uint64_result yetty_ymux_attachment_view_top(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ymux_attachment_is_following(struct yetty_yclass_object *obj);
/* Selection endpoints in stable coordinates (0 anchor id = inactive). */
struct yetty_ycore_void_result yetty_ymux_attachment_set_selection(struct yetty_yclass_object *obj,
                                                                   uint64_t anchor_logical_id,
                                                                   uint32_t anchor_offset,
                                                                   uint64_t head_logical_id,
                                                                   uint32_t head_offset);
struct yetty_ycore_void_result yetty_ymux_attachment_selection(struct yetty_yclass_object *obj,
                                                               uint64_t *out_anchor_logical_id,
                                                               uint32_t *out_anchor_offset,
                                                               uint64_t *out_head_logical_id,
                                                               uint32_t *out_head_offset);
/* Generation bookkeeping (the projector publishes; the wire acks). */
struct yetty_ycore_uint64_result yetty_ymux_attachment_next_generation(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymux_attachment_ack(struct yetty_yclass_object *obj,
                                                         uint64_t generation);
struct yetty_ycore_void_result yetty_ymux_attachment_generations(struct yetty_yclass_object *obj,
                                                                 uint64_t *out_published,
                                                                 uint64_t *out_acked);
struct yetty_ycore_void_result yetty_ymux_attachment_view_size(struct yetty_yclass_object *obj,
                                                               uint32_t *out_rows,
                                                               uint32_t *out_cols);

#ifdef __cplusplus
}
#endif

#endif
