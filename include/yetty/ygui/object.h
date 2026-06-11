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

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_object;
YETTY_YRESULT_DECLARE(yetty_ygui_object_ptr, struct yetty_yclass_object *);

/* Create one instance of `cls` and link it under `parent` (NULL for the
 * root). Allocates one block sized to (header + sum of data slices).
 * Walks the class chain top-down invoking yetty_ygui_constructor on the
 * leaf class; each ctor is expected to chain via yetty_ygui_super_void
 * before its own work. On any ctor failure, already-constructed slices
 * are torn down (destructors run leaf-first) and the block freed. */
struct yetty_ygui_object_ptr_result yetty_ygui_add(const struct yetty_yclass *cls,
                                                   struct yetty_yclass_object *parent);

/* Unwrap a `*_class_get()` result for direct use as the `cls` argument
 * of yetty_ygui_add / add helpers. Class registration is deterministic
 * setup, so a failure is a configuration bug: log it (with `name` for
 * context) and return NULL. yetty_ygui_add rejects a NULL class with a
 * clear error, so the failure still propagates through the caller's
 * Result chain rather than crashing. */
const struct yetty_yclass *yetty_ygui_class_expect(struct yetty_yclass_ptr_result class_result,
                                                   const char *name);

/* Destroy `obj`. Runs destructors leaf-first, detaches from parent,
 * cascades to children, and frees the block. NULL-safe. */
struct yetty_ycore_void_result yetty_ygui_del(struct yetty_yclass_object *obj);

/* Return a typed pointer to `cls`'s data slice inside `obj`. Caller
 * casts to the slice struct type. Infallible: `cls` must be in `obj`'s
 * class chain (own class, parent chain, or any mixin). Debug builds
 * assert; release builds trust the call site. */
YETTY_YRESULT_DECLARE(yetty_ygui_void_ptr, void *);

struct yetty_ygui_void_ptr_result yetty_ygui_data_get_result(struct yetty_yclass_object *obj,
                                                             const struct yetty_yclass *cls);

YETTY_EXTERNAL_CALLBACK
void *yetty_ygui_data_get(struct yetty_yclass_object *obj, const struct yetty_yclass *cls);

/* Parent / first-child / next-sibling access. NULL when no relation. */
struct yetty_yclass_object *yetty_ygui_object_parent(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_object_first_child(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_object_next_sibling(struct yetty_yclass_object *obj);

/* Move `obj` to the end of its parent's child list so the framework's
 * widget hit-test (last sibling wins) prefers it over overlapping
 * earlier siblings. Paired with a figure-z bump for floating windows so
 * render order and hit order agree. No-op for a root / parentless obj. */
void yetty_ygui_object_raise(struct yetty_yclass_object *obj);

/* The framework that owns this widget tree. Resolves up the parent chain
 * to the root object, then returns the framework pointer associated with
 * the root. Returns NULL if the root has no framework attached. */
struct yetty_ygui_framework *yetty_ygui_object_framework(struct yetty_yclass_object *obj);

/* The wire id allocated for this widget by the framework at construction
 * time. Used as a CMD_GROUP id (figure_kind==0) or a CREATE_CHILD
 * figure_id (figure_kind!=0). */
uint32_t yetty_ygui_object_id(const struct yetty_yclass_object *obj);

/* Mark this widget as dirty — content changed without geometry move.
 * The framework ORs dirty flags during emission and clears them after. */
struct yetty_ycore_void_result yetty_ygui_object_set_dirty(struct yetty_yclass_object *obj);

int yetty_ygui_object_is_dirty(const struct yetty_yclass_object *obj);

/* True when this widget is the deepest hit under the mouse pointer.
 * Maintained by the framework's pointer-tracking pass — widgets read
 * the flag from their paint method to render a hover variant. */
int yetty_ygui_object_is_hovered(const struct yetty_yclass_object *obj);

/* `yetty_ygui_constructor` / `yetty_ygui_destructor` public stubs are
 * emitted by yclass codegen from the override annotations in widget.c;
 * they live in the module-wide methods.gen.h. This header deliberately
 * does NOT pull in <yetty/ygui/widget.h>: object.h is included (via
 * internal.h) by widget.c itself, and a class TU must not transitively
 * see its own generated header. Widgets needing the base-widget API
 * include <yetty/ygui/widget.h> directly. */
#include <yetty/ygui/methods.gen.h>

/*-----------------------------------------------------------------------------
 * Super invokers — one per Result type.
 *
 * Inside an overriding method, call the corresponding super_<type> with
 * (obj, MY_CLASS, method_id) to invoke the next implementation up the
 * inheritance chain (parent class → grandparent → ...). Mixins are
 * resolved at registration time and merged into the leaf class's
 * dispatch table; super always means "the parent's slot value".
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_yclass_object *obj,
                                                     const struct yetty_yclass *self_class,
                                                     yetty_yclass_method_id_t method_id);

struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_yclass_object *obj,
                                                   const struct yetty_yclass *self_class,
                                                   yetty_yclass_method_id_t method_id);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OBJECT_H */
