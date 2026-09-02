/*
 * shape.c — yclass class `ydrawlist2:shape`: the SDF paint prefix shared by
 * every shape record: [type|id][z_order][fill][stroke][stroke_width] — the
 * geometry fields live on the generated per-shape classes in the `ysdf2`
 * module, which derive this class.
 *
 * Lives in its own translation unit (not drawable.c) because cross-module
 * subclasses resolve their parent's impl header by class name — the source
 * stem must equal the class name, the convention every split-layout module
 * follows. The pack override machinery is in the geometry classes; this
 * class carries the paint properties plus the color-STRING setters (the
 * one C-side "#RRGGBB[AA]" parser — kwargs in every language route
 * through them; the u32 properties remain for numeric callers).
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include "list2-internal.h"

/* `id` follows the wire rule: 0 = anonymous record, nonzero = addressable
 * (the type word gains the HAS_ID flag and the id word follows it). Colors
 * are 0xAARRGGBB words. */
struct YETTY_ANNOTATE("class@ydrawlist2:shape") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_ydrawlist2_shape {
    YETTY_ANNOTATE("property") uint32_t id;
    /* Stacking depth (z-order), uniform and SIGNED across every drawable's
     * `layer` — negative layers paint below the default plane. Serialized as
     * raw 32-bit bits; the receiver decodes the paint key as signed. */
    YETTY_ANNOTATE("property") int32_t layer;
    YETTY_ANNOTATE("property") uint32_t fill;
    YETTY_ANNOTATE("property") uint32_t stroke;
    YETTY_ANNOTATE("property") float stroke_width;
};

/* Result wrapper declared here (this TU does not include its own generated
 * headers); the guard define keeps the impl glue's guarded re-emission out
 * of this TU (C23 tag-compat vs the annotate-attributed class tag). */
YETTY_YRESULT_DECLARE(yetty_ydrawlist2_shape_ptr, struct yetty_ydrawlist2_shape *);
#define YETTY_YCLASSGEN_TYPE_YETTY_YDRAWLIST2_SHAPE_PTR_RESULT

struct yetty_yclass_ptr_result yetty_ydrawlist2_shape_class_get(void);

static struct yetty_yclass_void_ptr_result shape_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_shape_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "shape_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "shape_from_obj: object_data");
    return slice_r;
}

/*=============================================================================
 * Method slots
 *===========================================================================*/

/* set_fill: fill color as "#RRGGBB" / "#RRGGBBAA". */
YETTY_ANNOTATE("virtual@ydrawlist2:shape:set_fill")
YETTY_ANNOTATE("local@ydrawlist2:set_fill")
static struct yetty_ycore_void_result shape_set_fill(struct yetty_yclass_object *obj,
                                                     const char *color YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result shape_r = shape_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_r, "ydrawlist2 set_fill: object");
    struct yetty_ycore_uint32_result word_r = ydrawlist2_color_parse(color);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, word_r, "ydrawlist2 set_fill: color");
    ((struct yetty_ydrawlist2_shape *)shape_r.value)->fill = word_r.value;
    return YETTY_OK_VOID();
}

/* set_stroke: stroke color as "#RRGGBB" / "#RRGGBBAA". */
YETTY_ANNOTATE("virtual@ydrawlist2:shape:set_stroke")
YETTY_ANNOTATE("local@ydrawlist2:set_stroke")
static struct yetty_ycore_void_result shape_set_stroke(struct yetty_yclass_object *obj,
                                                       const char *color YETTY_ANNOT_CSTRING)
{
    struct yetty_yclass_void_ptr_result shape_r = shape_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, shape_r, "ydrawlist2 set_stroke: object");
    struct yetty_ycore_uint32_result word_r = ydrawlist2_color_parse(color);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, word_r, "ydrawlist2 set_stroke: color");
    ((struct yetty_ydrawlist2_shape *)shape_r.value)->stroke = word_r.value;
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/ydrawlist2/shape.c"
