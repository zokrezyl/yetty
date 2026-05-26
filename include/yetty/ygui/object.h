/*
 * ygui-object.h — runtime widget instance API.
 *
 * An object is one runtime instance of some class. The header is opaque
 * to user code; per-class data slices live in a contiguous block
 * following the header (allocated by yetty_ygui_add as one block).
 */
#ifndef YETTY_YGUI_OBJECT_H
#define YETTY_YGUI_OBJECT_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create one instance of `cls` and link it under `parent` (NULL for the
 * root). Allocates one block sized to (header + sum of data slices).
 * Walks the class chain top-down invoking yetty_ygui_constructor on the
 * leaf class; each ctor is expected to chain via yetty_ygui_super_void
 * before its own work. On any ctor failure, already-constructed slices
 * are torn down (destructors run leaf-first) and the block freed. */
struct yetty_ygui_object_ptr_result yetty_ygui_add(const struct yetty_ygui_class *cls,
                                                   struct yetty_ygui_object *parent);

/* Destroy `obj`. Runs destructors leaf-first, detaches from parent,
 * cascades to children, and frees the block. NULL-safe. */
struct yetty_ycore_void_result yetty_ygui_del(struct yetty_ygui_object *obj);

/* Return a typed pointer to `cls`'s data slice inside `obj`. Caller
 * casts to the slice struct type. Infallible: `cls` must be in `obj`'s
 * class chain (own class, parent chain, or any mixin). Debug builds
 * assert; release builds trust the call site. */
void *yetty_ygui_data_get(struct yetty_ygui_object *obj, const struct yetty_ygui_class *cls);

/* Parent / first-child / next-sibling access. NULL when no relation. */
struct yetty_ygui_object *yetty_ygui_object_parent(struct yetty_ygui_object *obj);
struct yetty_ygui_object *yetty_ygui_object_first_child(struct yetty_ygui_object *obj);
struct yetty_ygui_object *yetty_ygui_object_next_sibling(struct yetty_ygui_object *obj);

/* The engine that owns this widget tree. Resolves up the parent chain
 * to the root object, then returns the engine pointer associated with
 * the root. Returns NULL if the root has no engine attached. */
struct yetty_ygui_runtime *yetty_ygui_object_engine(struct yetty_ygui_object *obj);

/* The wire id allocated for this widget by the engine at construction
 * time. Used as a CMD_GROUP id (figure_kind==0) or a CREATE_CHILD
 * figure_id (figure_kind!=0). */
uint32_t yetty_ygui_object_id(const struct yetty_ygui_object *obj);

/* Mark this widget as dirty — content changed without geometry move.
 * The engine ORs dirty flags during emission and clears them after. */
struct yetty_ycore_void_result yetty_ygui_object_set_dirty(struct yetty_ygui_object *obj);

int yetty_ygui_object_is_dirty(const struct yetty_ygui_object *obj);

/* True when this widget is the deepest hit under the mouse pointer.
 * Maintained by the framework's pointer-tracking pass — widgets read
 * the flag from their paint method to render a hover variant. */
int yetty_ygui_object_is_hovered(const struct yetty_ygui_object *obj);

/* Lifecycle method ids (declared as public stubs). Other code may pass
 * these to super invokers / dispatch helpers using their address. */
YETTY_YGUI_DECLARE_METHOD(yetty_ygui_constructor, struct yetty_ycore_void_result,
                          (struct yetty_ygui_object * obj));

YETTY_YGUI_DECLARE_METHOD(yetty_ygui_destructor, struct yetty_ycore_void_result,
                          (struct yetty_ygui_object * obj));

/*-----------------------------------------------------------------------------
 * Super invokers — one per Result type.
 *
 * Inside an overriding method, call the corresponding super_<type> with
 * (obj, MY_CLASS, method_id) to invoke the next implementation up the
 * inheritance chain (parent class → grandparent → ...). Mixins are
 * resolved at registration time and merged into the leaf class's
 * dispatch table; super always means "the parent's slot value".
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_ygui_object *obj,
                                                     const struct yetty_ygui_class *self_class,
                                                     yetty_ygui_method_id_t method_id);

struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_ygui_object *obj,
                                                   const struct yetty_ygui_class *self_class,
                                                   yetty_ygui_method_id_t method_id);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OBJECT_H */
