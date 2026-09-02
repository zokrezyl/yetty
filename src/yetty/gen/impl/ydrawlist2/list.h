/* GENERATED — do not edit. */
/* Public interface for regular class(es) `drawable_list` (module: ydrawlist2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YDRAWLIST2_LIST_H
#define YETTY_YCLASSGEN_YDRAWLIST2_LIST_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_list_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ydrawlist2_drawable_list;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_LIST_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_LIST_PTR_RESULT
struct yetty_ydrawlist2_drawable_list_ptr_result {
    int ok;
    union {
        struct yetty_ydrawlist2_drawable_list *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ydrawlist2_drawable_list_ptr_result yetty_ydrawlist2_drawable_list_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_to(
    struct yetty_ydrawlist2_drawable_list *data);

/* add: pack `drawable`'s record into this list, immediately, in call order. */
struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *drawable);
/* begin_group: open a named entity scope — drawables added until the
 * matching end_group land inside GROUP(group_id)'s payload. On a receiver
 * with an entity model (the terminal's rolling rich store, scene-canvas) a
 * live group id can later be replaced in place by re-emitting GROUP(id, …)
 * or removed with delete_group(id). Groups nest. */
struct yetty_ycore_void_result yetty_ydrawlist2_begin_group(struct yetty_yclass_object *obj,
                                                            uint32_t group_id);
/* end_group: close the innermost open group (back-patches its payload
 * size). Every begin_group needs its end_group before emit. */
struct yetty_ycore_void_result yetty_ydrawlist2_end_group(struct yetty_yclass_object *obj);
/* delete_group: append DELETE(group_id) — the receiver removes the named
 * live group's whole subtree (figures included). The terminal keeps the
 * block's reserved rows; content sealed into scrollback is out of reach
 * and reports nothing. */
struct yetty_ycore_void_result yetty_ydrawlist2_delete_group(struct yetty_yclass_object *obj,
                                                             uint32_t group_id);
/* update: append CMD_UPDATE(id, payload) — deliver an opaque payload to the
 * addressable complex bound to `id` (the figure created with that id, e.g.
 * Plot(id=7) → update(7, …)). The payload schema belongs to the target
 * figure; for a yplot data buffer it is
 *   [buffer_index u32][sample_offset u32][count u32][f32 samples...]
 * — the streaming-plot path. Emit it in its own envelope after the figure's
 * creation envelope. */
struct yetty_ycore_void_result yetty_ydrawlist2_update(struct yetty_yclass_object *obj, uint32_t id,
                                                       struct yetty_ycore_buffer payload);
/* path: the ABSOLUTE ancestor path for the NEXT update/delete in this list —
 * the command's own id is the final path component (CMD_PATH on the wire).
 * `prefix` is the packed little-endian u32 id sequence, outermost first
 * (e.g. struct.pack("<II", 7, 2) addresses under path 7.2). Not needed for
 * depth-1 targets. */
struct yetty_ycore_void_result yetty_ydrawlist2_path(struct yetty_yclass_object *obj,
                                                     struct yetty_ycore_buffer prefix);
/* reserve: declare this batch's insertion row span (a VIEWPORT) from a pixel
 * height. Content taller than the declared span never extends the
 * reservation — it clips; pan it with the enclosing group's offset. */
struct yetty_ycore_void_result yetty_ydrawlist2_reserve(struct yetty_yclass_object *obj,
                                                        uint32_t height_px);
/* dcs_emit: serialize the list, wrap it in the YETTY_DCS_YDRAW_BIN envelope
 * and write it to stdout — the enclosing yetty renders it, anything else
 * discards it. Flushes so the envelope reaches the terminal immediately. */
struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *obj);
/* destroy: release the wrapped list and the object. */
struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_add_fn)(struct yetty_yclass_object *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_begin_group_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_end_group_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_delete_group_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_update_fn)(struct yetty_yclass_object *,
                                                                     uint32_t,
                                                                     struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_path_fn)(struct yetty_yclass_object *,
                                                                   struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_reserve_fn)(struct yetty_yclass_object *,
                                                                      uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_dcs_emit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ydrawlist2_register(void);

/* Transfer the wrapped RAW drawable list out: ownership moves to the
 * caller (e.g. ygui2's ydraw_embed adoption) and the v2 object is left
 * empty — the next add() lazily creates a fresh list, so the object stays
 * reusable. Errors when nothing was ever added (no list to hand over). */
struct yetty_ydraw_drawable_list_result yetty_ydrawlist2_drawable_list_release_raw(
    struct yetty_yclass_object *obj);
/* Adopt a RAW drawable list INTO the wrapper (ownership moves in). The
 * inverse of release_raw — the restore path when a consumer (ygui2 embed
 * adoption) rejects a transferred list and hands it back. Errors when the
 * wrapper already holds a list (nothing is silently dropped). */
struct yetty_ycore_void_result yetty_ydrawlist2_drawable_list_adopt_raw(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *raw);

#ifdef __cplusplus
}
#endif

#endif
