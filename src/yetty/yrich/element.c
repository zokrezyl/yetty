/*
 * yrich:element base-class host TU.
 *
 * `struct yetty_yrich_element` is the yclass base class for every concrete
 * document element (paragraph, inline_image, shape, cell). Concrete kinds
 * inherit via `parent@yrich:element` and override the element_* slots; all
 * element dispatch goes through yclass — there is no ops vtable.
 *
 * This TU defines the element-base polymorphic slots with their default
 * impls. Geometry/draw slots take raw pointers (rect out-param, drawable
 * list), so they are `local@`; the editing/input slots carry only scalars
 * or a by-value buffer and stay wire-marshallable.
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yrich/element.h` — that header is a generated artifact for other
 * modules. The accessor/downcast the appended element.gen.c defines are
 * forward-declared below.
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#include <stddef.h>

struct yetty_ydraw_drawable_list;

/* Accessor / downcast defined in the appended element.gen.c. */
YETTY_YRESULT_DECLARE(yetty_yrich_element_ptr, struct yetty_yrich_element *);
struct yetty_yclass_ptr_result yetty_yrich_element_class_get(void);
struct yetty_yrich_element_ptr_result yetty_yrich_element_from(struct yetty_yclass_object *obj);

/* Public stubs this TU calls before the foot include (generated in
 * methods.gen.c; the generated element.h publishes them for consumers). */
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);

/*===========================================================================
 * Element base data slice — the first slice in every element object.
 *=========================================================================*/

struct [[clang::annotate("class@yrich:element"),
         clang::annotate("include@yetty/yrich/yrich-types.h")]] yetty_yrich_element {
    yetty_yrich_element_id id;
};

/*===========================================================================
 * Module constructor slot — owned by yrich:document (the create() factories
 * auto-call it). The element hierarchy is a second class root in the same
 * domain, so it installs its own impl for the shared slot.
 *=========================================================================*/

[[clang::annotate("override@yrich:element:constructor")]]
static struct yetty_ycore_void_result element_constructor(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    /* Object data is calloc-zeroed; the creating factory assigns the id. */
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Element slots — defaults model the old NULL-vtable-op semantics.
 *=========================================================================*/

/* element_destroy: free element-specific state, then the object. The base
 * owns no heap state, so the default just frees the object; concrete kinds
 * override, free their own fields and chain up via yetty_yrich_super_void. */
[[clang::annotate("virtual@yrich:element:element_destroy")]] [[clang::annotate(
    "local@yrich:element_destroy")]]
static struct yetty_ycore_void_result element_default_destroy(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj)
{
    (void)ctx;
    return yetty_yclass_object_free(obj);
}

/* element_bounds: write the axis-aligned bounding box into *out_bounds. */
[[clang::annotate("virtual@yrich:element:element_bounds")]] [[clang::annotate(
    "local@yrich:element_bounds")]]
static struct yetty_ycore_void_result element_default_bounds(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_rect *out_bounds)
{
    (void)ctx;
    (void)obj;
    if (!out_bounds) {
        return YETTY_ERR(yetty_ycore_void, "element_bounds: NULL out_bounds");
    }
    *out_bounds = (struct yetty_yrich_rect){0};
    return YETTY_OK_VOID();
}

/* element_hit_test: point-in-element; default is point-in-bounds. */
[[clang::annotate("virtual@yrich:element:element_hit_test")]]
static struct yetty_ycore_int_result element_default_hit_test(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              float x, float y)
{
    (void)ctx;
    struct yetty_yrich_rect bounds;
    struct yetty_ycore_void_result bounds_res = yetty_yrich_element_bounds(NULL, obj, &bounds);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, bounds_res, "element_hit_test: bounds failed");
    return YETTY_OK(yetty_ycore_int, yetty_yrich_rect_contains(&bounds, x, y) ? 1 : 0);
}

/* element_render: emit ydraw primitives at the given base layer. */
[[clang::annotate("virtual@yrich:element:element_render")]] [[clang::annotate(
    "local@yrich:element_render")]]
static struct yetty_ycore_void_result element_default_render(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ydraw_drawable_list *drawable_list, uint32_t layer, int selected)
{
    (void)ctx;
    (void)obj;
    (void)drawable_list;
    (void)layer;
    (void)selected;
    return YETTY_ERR(yetty_ycore_void, "yrich element: render not implemented by this element");
}

[[clang::annotate("virtual@yrich:element:element_is_editable")]]
static struct yetty_ycore_int_result element_default_is_editable(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("virtual@yrich:element:element_begin_edit")]]
static struct yetty_ycore_void_result element_default_begin_edit(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@yrich:element:element_end_edit")]]
static struct yetty_ycore_void_result element_default_end_edit(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

[[clang::annotate("virtual@yrich:element:element_is_editing")]]
static struct yetty_ycore_int_result element_default_is_editing(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK(yetty_ycore_int, 0);
}

/* element_insert_text: insert UTF-8 text at the cursor. Non-editable
 * elements ignore text input. */
[[clang::annotate("virtual@yrich:element:element_insert_text")]]
static struct yetty_ycore_void_result element_default_insert_text(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text)
{
    (void)ctx;
    (void)obj;
    (void)text;
    return YETTY_OK_VOID();
}

/* element_delete_sel: delete current selection or character before cursor. */
[[clang::annotate("virtual@yrich:element:element_delete_sel")]]
static struct yetty_ycore_void_result element_default_delete_sel(struct yetty_yclass_ctx *ctx,
                                                                 struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Exposed element API — id access (the id field stays private; uint64
 * has no generated property accessor).
 *=========================================================================*/

[[clang::annotate("expose")]]
struct yetty_yrich_element_id_result yetty_yrich_element_id_value(struct yetty_yclass_object *obj)
{
    struct yetty_yrich_element_ptr_result data_res = yetty_yrich_element_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yrich_element_id, data_res, "element_id_value: data_get");
    return YETTY_OK(yetty_yrich_element_id, data_res.value->id);
}

[[clang::annotate("expose")]]
struct yetty_ycore_void_result yetty_yrich_element_set_id(struct yetty_yclass_object *obj,
                                                          yetty_yrich_element_id id)
{
    struct yetty_yrich_element_ptr_result data_res = yetty_yrich_element_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data_res, "element_set_id: data_get");
    data_res.value->id = id;
    return YETTY_OK_VOID();
}

#include "element.gen.c"
