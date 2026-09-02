/*
 * shadertoy.c — yclass class `ycomplex2:shadertoy`: one yshadertoy complex
 * record as a v2 drawable. The payload IS the WGSL shader text (mainImage
 * contract, see yshadertoy/prim.h); the receiving factory compiles a
 * per-instance pipeline around it. Source arrives via set_wgsl_path (a
 * file, the primary content) or set_source (inline text) — inline storage,
 * no owned heap, no destructor.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yshadertoy/prim.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "complex2-internal.h"

enum {
    YCOMPLEX2_WGSL_MAX = 16384,
};

struct YETTY_ANNOTATE("class@ycomplex2:shadertoy") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ycomplex2_shadertoy {
    YETTY_ANNOTATE("property") float x;
    YETTY_ANNOTATE("property") float y;
    YETTY_ANNOTATE("property") float width;
    YETTY_ANNOTATE("property") float height;
    /* Stacking depth (z-order), uniform with every drawable's `layer`. */
    YETTY_ANNOTATE("property") int32_t layer;
    /* Addressable id (0 = anonymous). Nonzero makes the complex ITSELF the
     * addressable node at (enclosing path . id). yshadertoy does not implement
     * CMD_UPDATE (.update is NULL) — update at its path is a graceful no-op. */
    YETTY_ANNOTATE("property") uint32_t id;
    uint32_t wgsl_len;
    char wgsl[YCOMPLEX2_WGSL_MAX]; /* mainImage WGSL; empty = receiver default */
};

YETTY_YRESULT_DECLARE(yetty_ycomplex2_shadertoy_ptr, struct yetty_ycomplex2_shadertoy *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_SHADERTOY_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ycomplex2_shadertoy_class_get(void);

static struct yetty_yclass_void_ptr_result shadertoy_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_ycomplex2_shadertoy_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "shadertoy_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_res, "shadertoy_from_obj: object_data");
    return slice_res;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* set_source: the WGSL shader source, inline. Empty/NULL selects the
 * receiver's built-in default shader (animated gradient). */
YETTY_ANNOTATE("virtual@ycomplex2:shadertoy:set_source")
YETTY_ANNOTATE("local@ycomplex2:set_source")
static struct yetty_ycore_void_result shadertoy_set_source(struct yetty_yclass_object *obj,
                                                           const char *wgsl YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result shader_res = shadertoy_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shader_res, "ycomplex2 set_wgsl: object");
    struct yetty_ycomplex2_shadertoy *shader = (struct yetty_ycomplex2_shadertoy *)shader_res.value;
    if (!wgsl) {
        shader->wgsl[0] = '\0';
        shader->wgsl_len = 0;
        return YETTY_OK_VOID();
    }
    size_t wgsl_len = strlen(wgsl);
    if (wgsl_len >= sizeof(shader->wgsl)) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 set_wgsl: shader too long");
    }
    memcpy(shader->wgsl, wgsl, wgsl_len + 1);
    shader->wgsl_len = (uint32_t)wgsl_len;
    return YETTY_OK_VOID();
}

/* set_wgsl_path: read the shader source from a file — the class's primary
 * content (Shadertoy("plasma.wgsl", …)); set_wgsl takes inline source. */
YETTY_ANNOTATE("virtual@ycomplex2:shadertoy:set_wgsl_path")
YETTY_ANNOTATE("primary@ycomplex2:set_wgsl_path")
YETTY_ANNOTATE("local@ycomplex2:set_wgsl_path")
static struct yetty_ycore_void_result shadertoy_set_wgsl_path(struct yetty_yclass_object *obj,
                                                              const char *path YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result shader_res = shadertoy_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shader_res, "ycomplex2 set_wgsl_path: object");
    struct yetty_ycomplex2_shadertoy *shader = (struct yetty_ycomplex2_shadertoy *)shader_res.value;
    if (!path || !path[0]) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 set_wgsl_path: missing path");
    }
    uint8_t *bytes = NULL;
    struct yetty_ycore_size_result read_res = ycomplex2_read_file(path, &bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "ycomplex2 set_wgsl_path: read");
    if (read_res.value >= sizeof(shader->wgsl)) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 set_wgsl_path: shader too long");
    }
    memcpy(shader->wgsl, bytes, read_res.value);
    shader->wgsl[read_res.value] = '\0';
    shader->wgsl_len = (uint32_t)read_res.value;
    free(bytes);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result shadertoy_pack(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result shader_res = shadertoy_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shader_res, "ycomplex2 shadertoy pack: object");
    struct yetty_ycomplex2_shadertoy *shader = (struct yetty_ycomplex2_shadertoy *)shader_res.value;
    struct yetty_yshadertoy_prim_config config = {
        .bounds_x = shader->x,
        .bounds_y = shader->y,
        .bounds_w = shader->width > 0.0f ? shader->width : 560.0f,
        .bounds_h = shader->height > 0.0f ? shader->height : 240.0f,
    };
    size_t record_size = yetty_yshadertoy_prim_serialized_size(shader->wgsl_len);
    uint8_t *record = malloc(record_size);
    if (!record) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 shadertoy pack: alloc");
    }
    struct yetty_ycore_size_result pack_res = yetty_yshadertoy_prim_serialize(
        &config, shader->wgsl, shader->wgsl_len, record, record_size);
    if (YETTY_IS_ERR(pack_res)) {
        free(record);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 shadertoy pack: serialize", pack_res);
    }
    struct yetty_ycore_void_result paint_open_res = ycomplex2_paint_z_open(list, shader->layer);
    if (YETTY_IS_ERR(paint_open_res)) {
        free(record);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 shadertoy pack: paint_z open",
                         paint_open_res);
    }
    struct yetty_ycore_void_result node_id_res = ycomplex2_node_id(list, shader->id);
    if (YETTY_IS_ERR(node_id_res)) {
        free(record);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 shadertoy pack: node_id", node_id_res);
    }
    struct yetty_ydraw_id_result add_res =
        yetty_ydraw_drawable_list_add_prim(list, record, pack_res.value);
    free(record);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_res, "ycomplex2 shadertoy pack: add_prim");
    struct yetty_ycore_void_result paint_close_res = ycomplex2_paint_z_close(list, shader->layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paint_close_res,
                        "ycomplex2 shadertoy pack: paint_z close");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ycomplex2/shadertoy.c"
