/*
 * image.c — yclass class `ycomplex2:image`: one yimage complex record as a
 * v2 drawable. pack() reads the image file (PNG/JPG/…, decoded client-side
 * by yimage's stb path) and appends the record into the caller's drawable
 * list at the bounds properties — dlist.add(Image(path, …)).
 *
 * The source is referenced by PATH (inline, no owned heap, no destructor —
 * the binding's generic object free reclaims the slice). width/height 0
 * means the image's natural pixel size.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yimage/yimage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "complex2-internal.h"

struct YETTY_ANNOTATE("class@ycomplex2:image") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ycomplex2_image {
    YETTY_ANNOTATE("property") float x;
    YETTY_ANNOTATE("property") float y;
    YETTY_ANNOTATE("property") float width;
    YETTY_ANNOTATE("property") float height;
    /* Stacking depth (z-order), uniform with every drawable's `layer`.
     * Client-side only; pack() brackets the record in a paint-z scope. */
    YETTY_ANNOTATE("property") int32_t layer;
    /* Addressable id (0 = anonymous). A nonzero id makes the complex ITSELF
     * the addressable node at (enclosing path . id) — delete(path) and
     * replacement re-insert reach it. yimage does not implement CMD_UPDATE
     * (.update is NULL) — update at its path is a graceful no-op. */
    YETTY_ANNOTATE("property") uint32_t id;
    char path[YCOMPLEX2_PATH_LIMIT];
};

YETTY_YRESULT_DECLARE(yetty_ycomplex2_image_ptr, struct yetty_ycomplex2_image *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YCOMPLEX2_IMAGE_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ycomplex2_image_class_get(void);

static struct yetty_yclass_void_ptr_result image_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_ycomplex2_image_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "image_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_res = yetty_yclass_object_data(obj, class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_res, "image_from_obj: object_data");
    return slice_res;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* set_path: the image file (PNG/JPG/… — whatever stb decodes). */
YETTY_ANNOTATE("virtual@ycomplex2:image:set_path")
YETTY_ANNOTATE("primary@ycomplex2:set_path")
YETTY_ANNOTATE("local@ycomplex2:set_path")
static struct yetty_ycore_void_result image_set_path(struct yetty_yclass_object *obj,
                                                     const char *path YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result image_res = image_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, image_res, "ycomplex2 set_path: object");
    struct yetty_ycomplex2_image *image = (struct yetty_ycomplex2_image *)image_res.value;
    if (!path || strlen(path) >= sizeof(image->path)) {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 set_path: missing or too-long path");
    }
    snprintf(image->path, sizeof(image->path), "%s", path);
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result image_pack(struct yetty_yclass_object *obj,
                                                 struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result image_res = image_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, image_res, "ycomplex2 image pack: object");
    struct yetty_ycomplex2_image *image = (struct yetty_ycomplex2_image *)image_res.value;
    if (image->path[0] == '\0') {
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 image pack: path not set");
    }
    uint8_t *bytes = NULL;
    struct yetty_ycore_size_result read_res = ycomplex2_read_file(image->path, &bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, read_res, "ycomplex2 image pack: read");
    struct yetty_yimage_render_config config = {
        .bounds_x = image->x,
        .bounds_y = image->y,
        .bounds_w = image->width,
        .bounds_h = image->height,
    };
    struct yetty_ycore_void_result paint_open_res = ycomplex2_paint_z_open(list, image->layer);
    if (YETTY_IS_ERR(paint_open_res)) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 image pack: paint_z open", paint_open_res);
    }
    struct yetty_ycore_void_result node_id_res = ycomplex2_node_id(list, image->id);
    if (YETTY_IS_ERR(node_id_res)) {
        free(bytes);
        return YETTY_ERR(yetty_ycore_void, "ycomplex2 image pack: node_id", node_id_res);
    }
    struct yetty_ycore_void_result emit_res =
        yetty_yimage_emit_into(list, bytes, read_res.value, &config);
    free(bytes);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "ycomplex2 image pack: emit_into");
    struct yetty_ycore_void_result paint_close_res = ycomplex2_paint_z_close(list, image->layer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, paint_close_res, "ycomplex2 image pack: paint_z close");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ycomplex2/image.c"
