/*
 * mesh.c — yclass class `ycomplex2:mesh`: one ymesh complex record as a v2
 * drawable. pack() parses the glTF 2.0 binary (.glb) client-side and
 * appends the record at the bounds properties with the camera posed by the
 * same parameters the ymesh tool's orbit drag mutates. Source is referenced
 * by PATH (inline, no owned heap, no destructor).
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ymesh/ymesh.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "complex2-internal.h"

struct YETTY_ANNOTATE("class@ycomplex2:mesh") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ycomplex2_mesh {
    YETTY_ANNOTATE("property") float x;
    YETTY_ANNOTATE("property") float y;
    YETTY_ANNOTATE("property") float width;
    YETTY_ANNOTATE("property") float height;
    /* Camera; zeros mean the engine defaults (azimuth 0.7, elevation 0.5,
     * zoom 3.0). wireframe: 0 = solid, 1 = wireframe. */
    YETTY_ANNOTATE("property") float azimuth;
    YETTY_ANNOTATE("property") float elevation;
    YETTY_ANNOTATE("property") float zoom; /* camera dolly (the config's dist_factor) */
    YETTY_ANNOTATE("property") uint32_t wireframe;
    /* Stacking depth (z-order), uniform with every drawable's `layer`. */
    YETTY_ANNOTATE("property") int32_t layer;
    /* Addressable id (0 = anonymous). Nonzero makes the complex ITSELF the
     * addressable node at (enclosing path . id). ymesh does not implement
     * CMD_UPDATE (.update is NULL) — update at its path is a graceful no-op. */
    YETTY_ANNOTATE("property") uint32_t id;
    char path[YCOMPLEX2_PATH_LIMIT];
};

YETTY_YRESULT_DECLARE(yetty_ycomplex2_mesh_ptr, struct yetty_ycomplex2_mesh *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_MESH_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ycomplex2_mesh_class_get(void);

static struct yetty_yclass_void_ptr_result mesh_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_ycomplex2_mesh_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "mesh_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_res, "mesh_from_obj: object_data");
    return slice_res;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* set_glb: the glTF 2.0 binary file. */
YETTY_ANNOTATE("virtual@ycomplex2:mesh:set_glb")
YETTY_ANNOTATE("primary@ycomplex2:set_glb")
YETTY_ANNOTATE("local@ycomplex2:set_glb")
static struct yetty_ycore_void_result mesh_set_glb(struct yetty_yclass_object *obj,
                                                   const char *path YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result mesh_res = mesh_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mesh_res, "ycomplex2 set_glb: object");
    struct yetty_ycomplex2_mesh *mesh = (struct yetty_ycomplex2_mesh *)mesh_res.value;
    if (!path || strlen(path) >= sizeof(mesh->path)) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 set_glb: missing or too-long path");
    }
    snprintf(mesh->path, sizeof(mesh->path), "%s", path);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result mesh_pack(struct yetty_yclass_object *obj,
                                                struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result mesh_res = mesh_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mesh_res, "ycomplex2 mesh pack: object");
    struct yetty_ycomplex2_mesh *mesh = (struct yetty_ycomplex2_mesh *)mesh_res.value;
    if (mesh->path[0] == '\0') {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 mesh pack: glb path not set");
    }
    struct yetty_ymesh_render_config config = {
        .bounds_x = mesh->x,
        .bounds_y = mesh->y,
        .bounds_w = mesh->width,
        .bounds_h = mesh->height,
        .azimuth = mesh->azimuth,
        .elevation = mesh->elevation,
        .dist_factor = mesh->zoom,
        .mode = mesh->wireframe,
    };
    struct yetty_ydraw_drawable_list_result rendered_res =
        yetty_ymesh_render_path(mesh->path, &config);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rendered_res, "ycomplex2 mesh pack: render");
    struct yetty_ycore_void_result open_res = ycomplex2_paint_z_open(list, mesh->layer);
    if (YETTY_IS_ERR(open_res)) {
        yetty_ydraw_drawable_list_destroy(rendered_res.value);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 mesh pack: paint_z open", open_res);
    }
    struct yetty_ycore_void_result node_id_res = ycomplex2_node_id(list, mesh->id);
    if (YETTY_IS_ERR(node_id_res)) {
        yetty_ydraw_drawable_list_destroy(rendered_res.value);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 mesh pack: node_id", node_id_res);
    }
    struct yetty_ycore_void_result append_res = ycomplex2_append_rendered(list, rendered_res.value);
    yetty_ydraw_drawable_list_destroy(rendered_res.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "ycomplex2 mesh pack: append");
    struct yetty_ycore_void_result close_res = ycomplex2_paint_z_close(list, mesh->layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "ycomplex2 mesh pack: paint_z close");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ycomplex2/mesh.c"
