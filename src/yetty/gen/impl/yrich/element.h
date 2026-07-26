/* GENERATED — do not edit. */
/* Public interface for regular class(es) `element` (module: yrich).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YRICH_ELEMENT_H
#define YETTY_YCLASSGEN_YRICH_ELEMENT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;
struct yetty_yrich_rect;

struct yetty_yclass_ptr_result yetty_yrich_element_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrich_element;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRICH_ELEMENT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRICH_ELEMENT_PTR_RESULT
struct yetty_yrich_element_ptr_result {
    int ok;
    union {
        struct yetty_yrich_element *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrich_element_ptr_result yetty_yrich_element_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrich_element_to(struct yetty_yrich_element *data);

/* element_destroy: free element-specific state, then the object. The base
 * owns no heap state, so the default just frees the object; concrete kinds
 * override, free their own fields and chain up via yetty_yrich_super_void. */
struct yetty_ycore_void_result yetty_yrich_element_destroy(struct yetty_yclass_object * obj);
/* element_bounds: write the axis-aligned bounding box into *out_bounds. */
struct yetty_ycore_void_result yetty_yrich_element_bounds(struct yetty_yclass_object * obj, struct yetty_yrich_rect * out_bounds);
/* element_hit_test: point-in-element; default is point-in-bounds. */
struct yetty_ycore_int_result yetty_yrich_element_hit_test(struct yetty_yclass_object * obj, float x, float y);
/* element_render: emit ydraw primitives at the given base layer. */
struct yetty_ycore_void_result yetty_yrich_element_render(struct yetty_yclass_object * obj, struct yetty_ydraw_drawable_list * drawable_list, uint32_t layer, int selected);
struct yetty_ycore_int_result yetty_yrich_element_is_editable(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_begin_edit(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yrich_element_end_edit(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_yrich_element_is_editing(struct yetty_yclass_object * obj);
/* element_insert_text: insert UTF-8 text at the cursor. Non-editable
 * elements ignore text input. */
struct yetty_ycore_void_result yetty_yrich_element_insert_text(struct yetty_yclass_object * obj, struct yetty_ycore_buffer text);
/* element_delete_sel: delete current selection or character before cursor. */
struct yetty_ycore_void_result yetty_yrich_element_delete_sel(struct yetty_yclass_object * obj);

typedef struct yetty_ycore_void_result (*yetty_yrich_element_destroy_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_bounds_fn)(struct yetty_yclass_object *, struct yetty_yrich_rect *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_hit_test_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_render_fn)(struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *, uint32_t, int);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editable_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_begin_edit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_end_edit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yrich_element_is_editing_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_insert_text_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yrich_element_delete_sel_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yrich_element_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrich_register(void);

struct yetty_yrich_element_id_result yetty_yrich_element_id_value(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_element_set_id(struct yetty_yclass_object *obj, yetty_yrich_element_id id);

#ifdef __cplusplus
}
#endif

#endif
