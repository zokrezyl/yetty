/* GENERATED — do not edit. */
/* Public interface for regular class(es) `element` (module: yrich).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YRICH_ELEMENT_H
#define YETTY_YCLASSGEN_YRICH_ELEMENT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yrich_element_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_element;
YETTY_YRESULT_DECLARE(yetty_yrich_element_ptr, struct yetty_yrich_element *);
struct yetty_yrich_element_ptr_result yetty_yrich_element_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrich_element_to(struct yetty_yrich_element *data);

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;

struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          struct yetty_yrich_rect *out_bounds);
struct yetty_ycore_int_result yetty_yrich_element_hit_test(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj, float x,
                                                           float y);
struct yetty_ycore_void_result yetty_yrich_element_render(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ydraw_drawable_list *drawable_list, uint32_t layer, int selected);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               struct yetty_ycore_buffer text);
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_hit_test_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *,
    uint32_t, int);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yrich_element_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_register(void);

/* Header-destined: the shared yrich value types (rect, element id Result)
 * the public element API above references. */
#include <yetty/yrich/yrich-types.h>
struct yetty_yrich_element_id_result yetty_yrich_element_id_value(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_set_id(struct yetty_yclass_object *obj,
                                                          yetty_yrich_element_id id);

#endif
