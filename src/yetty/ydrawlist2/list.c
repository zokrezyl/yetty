/*
 * list.c — yclass class `ydrawlist2:drawable_list`: the drawable list of the
 * version-2 client interface.
 *
 * Semantics are EXACTLY the ydraw-list producer model: one list, immediate
 * appends in call order. add(drawable) dispatches the drawable's virtual
 * pack slot against the wrapped yetty_ydraw_drawable_list right there — it
 * manages nothing and returns nothing; ids (record ids, font ids) are user
 * data inside the records. dcs_emit() wraps the serialized list in the same
 * YETTY_DCS_YDRAW_BIN envelope every C producer tool emits, on stdout, and
 * flushes — "emit" means "draw it now".
 *
 * The wrapped C list is created lazily on first use with a NULL config (the
 * documented default: no scene bounds declared — the receiver measures
 * content from record AABBs and reserves scroll rows itself).
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yface/yface.h>
#include <yetty/yterminal/dcs-codes.h>

#include <stdio.h>

struct YETTY_ANNOTATE("class@ydrawlist2:drawable_list") yetty_ydrawlist2_drawable_list {
    struct yetty_ydraw_drawable_list *list; /* owned; lazy-created on first use */
    /* Open-group marker stack: begin_group pushes the header byte offset the
     * C builder returns; end_group pops it to back-patch the payload size.
     * Script authors never see byte offsets. */
    uint32_t group_markers[8];
    uint32_t group_marker_depth;
};

YETTY_YRESULT_DECLARE(yetty_ydrawlist2_drawable_list_ptr, struct yetty_ydrawlist2_drawable_list *);
/* Keep the impl glue's guarded re-emission out of this TU (C23 tag-compat
 * vs the annotate-attributed class tag). */
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_DRAWABLE_LIST_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_list_class_get(void);
struct yetty_ydrawlist2_drawable_list_ptr_result yetty_ydrawlist2_drawable_list_from(
    struct yetty_yclass_object *obj);

/* The module's pack stub (generated into the api layer from drawable.c's
 * virtual slot): dispatches to the drawable object's concrete override. */
struct yetty_ycore_void_result yetty_ydrawlist2_pack(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list);

static struct yetty_yclass_void_ptr_result list_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_ydrawlist2_drawable_list_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "list_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_res, "list_from_obj: object_data");
    return slice_res;
}

/* Resolve the wrapped C list, creating it on first use. */
static struct yetty_ydraw_drawable_list_result list_ensure(
    struct yetty_ydrawlist2_drawable_list *wrapper)
{
    if (!wrapper->list) {
        struct yetty_ydraw_drawable_list_result create_res =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, create_res,
                            "ydrawlist2: wrapped list create");
        wrapper->list = create_res.value;
    }
    return YETTY_OK(yetty_ydraw_drawable_list, wrapper->list);
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* add: pack `drawable`'s record into this list, immediately, in call order. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:add")
YETTY_ANNOTATE("local@ydrawlist2:add")
static struct yetty_ycore_void_result list_add(struct yetty_yclass_object *obj,
                                               struct yetty_yclass_object *drawable)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 add: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    if (!drawable) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 add: drawable is NULL");
    }
    struct yetty_ydraw_drawable_list_result ensure_res = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ydrawlist2 add: list");
    struct yetty_ycore_void_result pack_res = yetty_ydrawlist2_pack(drawable, ensure_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pack_res, "ydrawlist2 add: pack");
    return YETTY_OK_VOID();
}

/* begin_group: open a named entity scope — drawables added until the
 * matching end_group land inside GROUP(group_id)'s payload. On a receiver
 * with an entity model (the terminal's rolling rich store, scene-canvas) a
 * live group id can later be replaced in place by re-emitting GROUP(id, …)
 * or removed with delete_group(id). Groups nest. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:begin_group")
YETTY_ANNOTATE("local@ydrawlist2:begin_group")
static struct yetty_ycore_void_result list_begin_group(struct yetty_yclass_object *obj,
                                                       uint32_t group_id)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 begin_group: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    struct yetty_ydraw_drawable_list_result ensure_res = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ydrawlist2 begin_group: list");
    if (wrapper->group_marker_depth >=
        sizeof(wrapper->group_markers) / sizeof(wrapper->group_markers[0])) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 begin_group: nesting too deep");
    }
    struct yetty_ydraw_id_result marker_res =
        yetty_ydraw_drawable_list_begin_group(ensure_res.value, group_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, marker_res, "ydrawlist2 begin_group");
    wrapper->group_markers[wrapper->group_marker_depth++] = marker_res.value;
    return YETTY_OK_VOID();
}

/* end_group: close the innermost open group (back-patches its payload
 * size). Every begin_group needs its end_group before emit. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:end_group")
YETTY_ANNOTATE("local@ydrawlist2:end_group")
static struct yetty_ycore_void_result list_end_group(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 end_group: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    if (!wrapper->list || wrapper->group_marker_depth == 0) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 end_group: no open group");
    }
    uint32_t marker = wrapper->group_markers[--wrapper->group_marker_depth];
    return yetty_ydraw_drawable_list_end_group(wrapper->list, marker);
}

/* delete_group: append DELETE(group_id) — the receiver removes the named
 * live group's whole subtree (figures included). The terminal keeps the
 * block's reserved rows; content sealed into scrollback is out of reach
 * and reports nothing. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:delete_group")
YETTY_ANNOTATE("local@ydrawlist2:delete_group")
static struct yetty_ycore_void_result list_delete_group(struct yetty_yclass_object *obj,
                                                        uint32_t group_id)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 delete_group: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    struct yetty_ydraw_drawable_list_result ensure_res = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ydrawlist2 delete_group: list");
    return yetty_ydraw_drawable_list_add_cmd_delete(ensure_res.value, group_id);
}

/* update: append CMD_UPDATE(id, payload) — deliver an opaque payload to the
 * addressable complex bound to `id` (the figure created with that id, e.g.
 * Plot(id=7) → update(7, …)). The payload schema belongs to the target
 * figure; for a yplot data buffer it is
 *   [buffer_index u32][sample_offset u32][count u32][f32 samples...]
 * — the streaming-plot path. Emit it in its own envelope after the figure's
 * creation envelope. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:update")
YETTY_ANNOTATE("local@ydrawlist2:update")
static struct yetty_ycore_void_result list_update(struct yetty_yclass_object *obj, uint32_t id,
                                                  struct yetty_ycore_buffer payload)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 update: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    struct yetty_ydraw_drawable_list_result ensure_res = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ydrawlist2 update: list");
    return yetty_ydraw_drawable_list_add_cmd_update(ensure_res.value, id, payload.data,
                                                    payload.size);
}

/* path: the ABSOLUTE ancestor path for the NEXT update/delete in this list —
 * the command's own id is the final path component (CMD_PATH on the wire).
 * `prefix` is the packed little-endian u32 id sequence, outermost first
 * (e.g. struct.pack("<II", 7, 2) addresses under path 7.2). Not needed for
 * depth-1 targets. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:path")
YETTY_ANNOTATE("local@ydrawlist2:path")
static struct yetty_ycore_void_result list_path(struct yetty_yclass_object *obj,
                                                struct yetty_ycore_buffer prefix)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 path: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    struct yetty_ydraw_drawable_list_result ensure_res = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ydrawlist2 path: list");
    if (prefix.size == 0 || (prefix.size % sizeof(uint32_t)) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 path: prefix must be packed u32 ids");
    }
    return yetty_ydraw_drawable_list_add_cmd_path(ensure_res.value, (const uint32_t *)prefix.data,
                                                  (uint32_t)(prefix.size / sizeof(uint32_t)));
}

/* reserve: declare this batch's insertion row span (a VIEWPORT) from a pixel
 * height. Content taller than the declared span never extends the
 * reservation — it clips; pan it with the enclosing group's offset. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:reserve")
YETTY_ANNOTATE("local@ydrawlist2:reserve")
static struct yetty_ycore_void_result list_reserve(struct yetty_yclass_object *obj,
                                                   uint32_t height_px)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 reserve: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    struct yetty_ydraw_drawable_list_result ensure_res = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_res, "ydrawlist2 reserve: list");
    return yetty_ydraw_drawable_list_add_cmd_reserve(ensure_res.value, height_px);
}

/* dcs_emit: serialize the list, wrap it in the YETTY_DCS_YDRAW_BIN envelope
 * and write it to stdout — the enclosing yetty renders it, anything else
 * discards it. Flushes so the envelope reaches the terminal immediately. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:dcs_emit")
YETTY_ANNOTATE("local@ydrawlist2:dcs_emit")
static struct yetty_ycore_void_result list_dcs_emit(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_res, "ydrawlist2 dcs_emit: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_res.value;
    if (!wrapper->list) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 dcs_emit: empty list — nothing added");
    }
    const uint8_t *raw = NULL;
    size_t raw_size = yetty_ydraw_drawable_list_serialize(wrapper->list, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 dcs_emit: empty serialize");
    }
    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result emit_res = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 dcs_emit: yface_emit", emit_res);
    }
    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, stdout);
    }
    size_t expected = envelope.size;
    yetty_ycore_buffer_destroy(&envelope);
    if (written != expected) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 dcs_emit: short write");
    }
    if (fflush(stdout) != 0) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 dcs_emit: fflush");
    }
    return YETTY_OK_VOID();
}

/* destroy: release the wrapped list and the object. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:destroy")
YETTY_ANNOTATE("local@ydrawlist2:destroy")
static struct yetty_ycore_void_result list_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_ycore_void_result result = YETTY_OK_VOID();
    struct yetty_yclass_void_ptr_result list_res = list_from_obj(obj);
    if (YETTY_IS_OK(list_res)) {
        struct yetty_ydrawlist2_drawable_list *wrapper =
            (struct yetty_ydrawlist2_drawable_list *)list_res.value;
        if (wrapper->list) {
            yetty_ydraw_drawable_list_destroy(wrapper->list);
            wrapper->list = NULL;
        }
    } else {
        result = YETTY_ERR(yetty_ycore_void, "ydrawlist2 destroy: object", list_res);
    }
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_OK(result) && YETTY_IS_ERR(free_res)) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 destroy: object_free", free_res);
    }
    if (YETTY_IS_ERR(free_res)) {
        yetty_ycore_error_destroy(free_res.error);
    }
    return result;
}

/* Transfer the wrapped RAW drawable list out: ownership moves to the
 * caller (e.g. ygui2's ydraw_embed adoption) and the v2 object is left
 * empty — the next add() lazily creates a fresh list, so the object stays
 * reusable. Errors when nothing was ever added (no list to hand over). */
YETTY_ANNOTATE("expose")
struct yetty_ydraw_drawable_list_result yetty_ydrawlist2_drawable_list_release_raw(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result slice_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, slice_res,
                        "ydrawlist2 release_raw: object data");
    struct yetty_ydrawlist2_drawable_list *wrapper = slice_res.value;
    if (!wrapper->list) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "ydrawlist2 release_raw: empty list");
    }
    if (wrapper->group_marker_depth != 0u) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "ydrawlist2 release_raw: unbalanced open group");
    }
    struct yetty_ydraw_drawable_list *raw = wrapper->list;
    wrapper->list = NULL;
    return YETTY_OK(yetty_ydraw_drawable_list, raw);
}

/* Adopt a RAW drawable list INTO the wrapper (ownership moves in). The
 * inverse of release_raw — the restore path when a consumer (ygui2 embed
 * adoption) rejects a transferred list and hands it back. Errors when the
 * wrapper already holds a list (nothing is silently dropped). */
YETTY_ANNOTATE("expose")
struct yetty_ycore_void_result yetty_ydrawlist2_drawable_list_adopt_raw(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list *raw)
{
    struct yetty_yclass_void_ptr_result slice_res = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slice_res, "ydrawlist2 adopt_raw: object data");
    struct yetty_ydrawlist2_drawable_list *wrapper = slice_res.value;
    if (!raw) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 adopt_raw: NULL list");
    }
    if (wrapper->list) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 adopt_raw: wrapper already holds a list");
    }
    wrapper->list = raw;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ydrawlist2/list.c"
