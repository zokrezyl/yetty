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
};

YETTY_YRESULT_DECLARE(yetty_ydrawlist2_drawable_list_ptr,
                      struct yetty_ydrawlist2_drawable_list *);
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
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_drawable_list_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "list_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "list_from_obj: object_data");
    return slice_r;
}

/* Resolve the wrapped C list, creating it on first use. */
static struct yetty_ydraw_drawable_list_result list_ensure(
    struct yetty_ydrawlist2_drawable_list *wrapper)
{
    if (!wrapper->list) {
        struct yetty_ydraw_drawable_list_result create_r =
            yetty_ydraw_drawable_list_config_buffer_create(NULL);
        YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, create_r,
                            "ydrawlist2: wrapped list create");
        wrapper->list = create_r.value;
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
    struct yetty_yclass_void_ptr_result list_r = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_r, "ydrawlist2 add: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_r.value;
    if (!drawable) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 add: drawable is NULL");
    }
    struct yetty_ydraw_drawable_list_result ensure_r = list_ensure(wrapper);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ensure_r, "ydrawlist2 add: list");
    struct yetty_ycore_void_result pack_r = yetty_ydrawlist2_pack(drawable, ensure_r.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pack_r, "ydrawlist2 add: pack");
    return YETTY_OK_VOID();
}

/* dcs_emit: serialize the list, wrap it in the YETTY_DCS_YDRAW_BIN envelope
 * and write it to stdout — the enclosing yetty renders it, anything else
 * discards it. Flushes so the envelope reaches the terminal immediately. */
YETTY_ANNOTATE("virtual@ydrawlist2:drawable_list:dcs_emit")
YETTY_ANNOTATE("local@ydrawlist2:dcs_emit")
static struct yetty_ycore_void_result list_dcs_emit(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result list_r = list_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, list_r, "ydrawlist2 dcs_emit: object");
    struct yetty_ydrawlist2_drawable_list *wrapper =
        (struct yetty_ydrawlist2_drawable_list *)list_r.value;
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
    struct yetty_ycore_void_result emit_r = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(emit_r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 dcs_emit: yface_emit", emit_r);
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
    struct yetty_yclass_void_ptr_result list_r = list_from_obj(obj);
    if (YETTY_IS_OK(list_r)) {
        struct yetty_ydrawlist2_drawable_list *wrapper =
            (struct yetty_ydrawlist2_drawable_list *)list_r.value;
        if (wrapper->list) {
            yetty_ydraw_drawable_list_destroy(wrapper->list);
            wrapper->list = NULL;
        }
    } else {
        result = YETTY_ERR(yetty_ycore_void, "ydrawlist2 destroy: object", list_r);
    }
    struct yetty_ycore_void_result free_r = yetty_yclass_object_free(obj);
    if (YETTY_IS_OK(result) && YETTY_IS_ERR(free_r)) {
        return YETTY_ERR(yetty_ycore_void, "ydrawlist2 destroy: object_free", free_r);
    }
    if (YETTY_IS_ERR(free_r)) {
        yetty_ycore_error_destroy(free_r.error);
    }
    return result;
}

#include "yetty/gen/impl/ydrawlist2/list.c"
