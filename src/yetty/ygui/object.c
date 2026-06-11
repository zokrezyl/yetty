/*
 * ygui-object.c — runtime instance lifecycle + data slice access +
 * super invokers + lifecycle method stubs.
 *
 * Built on top of <yetty/yclass/class.h> — ygui owns no class system. An
 * ygui object IS a plain `struct yetty_yclass_object`; the yclass runtime
 * owns instance allocation (yetty_yclass_object_alloc) and data-slice
 * resolution (yetty_yclass_object_data). The widget tree + per-widget
 * framework state live in the `class@ygui:widget` base-class data slice
 * (struct yetty_ygui_tree); ygui_tree() resolves it from any object.
 */

#include "internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

/*===========================================================================
 * Lifecycle method stubs.
 *
 * Like the widget-level stubs in widget.c, these resolve the slot
 * once, look up the impl in the leaf class's dispatch table, and
 * invoke it. Absence of an override is treated as a no-op (the
 * default object lifecycle is "do nothing extra").
 *=========================================================================*/

/* Public stubs `yetty_ygui_constructor` and `yetty_ygui_destructor`
 * are emitted by yclass codegen from the override annotations on
 * widget.c's `widget_default_constructor` / `_destructor`. The
 * generated stubs live in methods.gen.c with the canonical yclass
 * slot signature `(struct yetty_yclass_ctx *, struct yetty_yclass_object *)`. */

/*===========================================================================
 * Super invokers.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_yclass_object *obj,
                                                     const struct yetty_yclass *self_class,
                                                     yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: NULL arg");
    }
    struct yetty_yclass_method_slot_result slot_result = yetty_ygui_method_slot_get(method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, slot_result, "yetty_ygui_super_void: slot lookup");
    yetty_yclass_method_slot slot = slot_result.value;
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: slot lookup failed");
    }
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(self_class, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, impl_result, "yetty_ygui_super_void: dispatch lookup");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_ctx *,
                                                   struct yetty_yclass_object *);
    return ((fn_t)impl)(NULL, (struct yetty_yclass_object *)obj);
}

struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_yclass_object *obj,
                                                   const struct yetty_yclass *self_class,
                                                   yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: NULL arg");
    }
    struct yetty_yclass_method_slot_result slot_result = yetty_ygui_method_slot_get(method_id);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, slot_result, "yetty_ygui_super_int: slot lookup");
    yetty_yclass_method_slot slot = slot_result.value;
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: slot lookup failed");
    }
    struct yetty_yclass_impl_t_result impl_result =
        yetty_ygui_dispatch_lookup_super(self_class, slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, impl_result, "yetty_ygui_super_int: dispatch lookup");
    yetty_yclass_impl_t impl = impl_result.value;
    if (!impl) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_ctx *,
                                                  struct yetty_yclass_object *);
    return ((fn_t)impl)(NULL, (struct yetty_yclass_object *)obj);
}

/*===========================================================================
 * Instance lifecycle.
 *
 * ygui objects are plain `struct yetty_yclass_object`s: slice resolution
 * goes through the yclass runtime (yetty_yclass_object_data) and the typed
 * generated `<class>_from(obj)` accessors directly — ygui carries no
 * data-get wrapper of its own.
 *=========================================================================*/

static void object_unlink_from_parent(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_object *p = ygui_tree(obj)->parent;
    if (!p) {
        return;
    }
    struct yetty_yclass_object **slot = &ygui_tree(p)->first_child;
    while (*slot && *slot != obj) {
        slot = &ygui_tree(*slot)->next_sibling;
    }
    if (*slot == obj) {
        *slot = ygui_tree(obj)->next_sibling;
    }
    ygui_tree(obj)->next_sibling = NULL;
    ygui_tree(obj)->parent = NULL;
}

static void object_link_to_parent(struct yetty_yclass_object *obj,
                                  struct yetty_yclass_object *parent)
{
    ygui_tree(obj)->parent = parent;
    if (!parent) {
        return;
    }
    if (!ygui_tree(parent)->first_child) {
        ygui_tree(parent)->first_child = obj;
        return;
    }
    struct yetty_yclass_object *t = ygui_tree(parent)->first_child;
    while (ygui_tree(t)->next_sibling) {
        t = ygui_tree(t)->next_sibling;
    }
    ygui_tree(t)->next_sibling = obj;
}

const struct yetty_yclass *yetty_ygui_class_expect(struct yetty_yclass_ptr_result class_result,
                                                   const char *name)
{
    if (YETTY_IS_ERR(class_result)) {
        yerror("yetty_ygui_class_expect: %s failed: %s", name ? name : "(class)",
               class_result.error.msg);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    return class_result.value;
}

struct yetty_ygui_object_ptr_result yetty_ygui_add(const struct yetty_yclass *cls,
                                                   struct yetty_yclass_object *parent)
{
    if (!cls) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: NULL class");
    }
    /* Reject mixin direct instantiation — mixins contribute data via
     * `uses@`, they're not concrete classes you can `add`. */
    struct yetty_yclass_const_char_ptr_result tr = yetty_yclass_type_str(cls);
    if (YETTY_IS_OK(tr) && strcmp(tr.value, "mixin") == 0) {
        return YETTY_ERR(yetty_ygui_object_ptr,
                         "yetty_ygui_add: cannot instantiate a mixin class directly");
    }
    if (YETTY_IS_ERR(tr)) {
        yetty_ycore_error_destroy(tr.error);
    }

    /* The yclass runtime owns instance layout + sizing: the object is a
     * plain yetty_yclass_object whose data slices (widget tree first, then
     * each subclass / mixin slice) follow the header. */
    struct yetty_yclass_object_ptr_result objr = yetty_yclass_object_alloc(cls);
    if (YETTY_IS_ERR(objr)) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: object_alloc failed", objr);
    }
    struct yetty_yclass_object *obj = objr.value;
    object_link_to_parent(obj, parent);

    struct yetty_ygui_framework *framework = yetty_ygui_object_framework(obj);
    if (framework) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(framework);
        if (YETTY_IS_ERR(idr)) {
            object_unlink_from_parent(obj);
            free(obj);
            return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: id alloc failed", idr);
        }
        ygui_tree(obj)->id = idr.value;
    }

    struct yetty_ycore_void_result cr =
        yetty_ygui_constructor(NULL, (struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(cr)) {
        if (framework && ygui_tree(obj)->id) {
            struct yetty_ycore_void_result fr =
                yetty_ygui_framework_free_id(framework, ygui_tree(obj)->id);
            if (YETTY_IS_ERR(fr)) {
                yetty_ycore_error_destroy(fr.error);
            }
        }
        object_unlink_from_parent(obj);
        free(obj);
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: constructor failed", cr);
    }

    return YETTY_OK(yetty_ygui_object_ptr, obj);
}

struct yetty_ycore_void_result yetty_ygui_del(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }
    while (ygui_tree(obj)->first_child) {
        struct yetty_ycore_void_result cr = yetty_ygui_del(ygui_tree(obj)->first_child);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    struct yetty_ygui_event_subscription *sub = ygui_tree(obj)->subscriptions;
    while (sub) {
        struct yetty_ygui_event_subscription *next = sub->next;
        free(sub);
        sub = next;
    }
    ygui_tree(obj)->subscriptions = NULL;
    struct yetty_ycore_void_result dr =
        yetty_ygui_destructor(NULL, (struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    struct yetty_ygui_framework *framework = yetty_ygui_object_framework(obj);
    if (framework && ygui_tree(obj)->id != 0) {
        struct yetty_ycore_void_result fr =
            yetty_ygui_framework_free_id(framework, ygui_tree(obj)->id);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    if (framework && framework->hovered_obj == obj) {
        framework->hovered_obj = NULL;
    }
    if (framework && framework->pressed_obj == obj) {
        framework->pressed_obj = NULL;
    }
    object_unlink_from_parent(obj);
    free(obj);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Hierarchy + id + dirty accessors.
 *=========================================================================*/

struct yetty_yclass_object *yetty_ygui_object_parent(struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->parent : NULL;
}

struct yetty_yclass_object *yetty_ygui_object_first_child(struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->first_child : NULL;
}

struct yetty_yclass_object *yetty_ygui_object_next_sibling(struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->next_sibling : NULL;
}

void yetty_ygui_object_raise(struct yetty_yclass_object *obj)
{
    if (!obj || !ygui_tree(obj)->parent) {
        return;
    }
    /* Move to the end of the sibling list so the framework's widget
     * hit-test (last-match-wins) prefers this widget over earlier
     * siblings it overlaps. The render side is ordered by figure z
     * separately; raising bumps both so they agree. */
    struct yetty_yclass_object *parent = ygui_tree(obj)->parent;
    object_unlink_from_parent(obj);
    object_link_to_parent(obj, parent);
}

struct yetty_ygui_framework *yetty_ygui_object_framework(struct yetty_yclass_object *obj)
{
    while (obj) {
        if (ygui_tree(obj)->framework) {
            return ygui_tree(obj)->framework;
        }
        obj = ygui_tree(obj)->parent;
    }
    return NULL;
}

uint32_t yetty_ygui_object_id(const struct yetty_yclass_object *obj)
{
    return obj ? ygui_tree(obj)->id : 0;
}

struct yetty_ycore_void_result yetty_ygui_object_set_dirty(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_object_set_dirty: NULL obj");
    }
    ygui_tree(obj)->dirty = 1;
    struct yetty_ygui_framework *framework = yetty_ygui_object_framework(obj);
    if (framework) {
        yetty_ygui_framework_mark_dirty(framework);
    }
    return YETTY_OK_VOID();
}

int yetty_ygui_object_is_dirty(const struct yetty_yclass_object *obj)
{
    return obj && ygui_tree(obj)->dirty;
}

int yetty_ygui_object_is_hovered(const struct yetty_yclass_object *obj)
{
    return obj && ygui_tree(obj)->hovered;
}

const struct yetty_yclass *yetty_ygui_object_class(const struct yetty_yclass_object *obj)
{
    return obj ? obj->klass : NULL;
}
