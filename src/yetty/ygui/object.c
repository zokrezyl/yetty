/*
 * ygui-object.c — runtime instance lifecycle + data slice access +
 * super invokers + lifecycle method stubs.
 */

#include "internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Lifecycle method stubs.
 *
 * Each stub's address is its method id. The body resolves the slot
 * lazily and invokes the implementation found in the leaf class's
 * dispatch table.
 *
 * Constructor / destructor are special: when no override exists, the
 * framework treats the call as a no-op (the default object lifecycle
 * is "do nothing extra"). For other methods, the absence of an
 * implementation is an error — the framework's base widget class
 * provides defaults so this only fires when a method is invoked on
 * an object that doesn't inherit from the widget base.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_ygui_object *obj)
{
    static yetty_ygui_method_slot slot = YETTY_YGUI_METHOD_SLOT_UNDEFINED;
    if (slot == YETTY_YGUI_METHOD_SLOT_UNDEFINED) {
        slot = yetty_ygui_method_slot_get((yetty_ygui_method_id_t)yetty_ygui_constructor);
    }
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_constructor: NULL obj");
    }
    yetty_ygui_impl_t impl = yetty_ygui_class_dispatch_lookup(obj->klass, slot);
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_ygui_object *);
    return ((fn_t)impl)(obj);
}

struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_ygui_object *obj)
{
    static yetty_ygui_method_slot slot = YETTY_YGUI_METHOD_SLOT_UNDEFINED;
    if (slot == YETTY_YGUI_METHOD_SLOT_UNDEFINED) {
        slot = yetty_ygui_method_slot_get((yetty_ygui_method_id_t)yetty_ygui_destructor);
    }
    if (!obj) {
        return YETTY_OK_VOID();
    }
    yetty_ygui_impl_t impl = yetty_ygui_class_dispatch_lookup(obj->klass, slot);
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_ygui_object *);
    return ((fn_t)impl)(obj);
}

/*===========================================================================
 * Super invokers.
 *=========================================================================*/

struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_ygui_object *obj,
                                                     const struct yetty_ygui_class *self_class,
                                                     yetty_ygui_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: NULL arg");
    }
    yetty_ygui_method_slot slot = yetty_ygui_method_slot_get(method_id);
    if (slot == YETTY_YGUI_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: slot allocation failed");
    }
    yetty_ygui_impl_t impl = yetty_ygui_class_dispatch_lookup_super(self_class, slot);
    if (!impl) {
        /* No parent implementation; treated as no-op (typical for ctor/
         * dtor at the root). */
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_ygui_object *);
    return ((fn_t)impl)(obj);
}

struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_ygui_object *obj,
                                                   const struct yetty_ygui_class *self_class,
                                                   yetty_ygui_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: NULL arg");
    }
    yetty_ygui_method_slot slot = yetty_ygui_method_slot_get(method_id);
    if (slot == YETTY_YGUI_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: slot allocation failed");
    }
    yetty_ygui_impl_t impl = yetty_ygui_class_dispatch_lookup_super(self_class, slot);
    if (!impl) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    /* Caller does the actual call with their typed signature when the
     * method takes args beyond `obj`. This generic helper covers the
     * (obj)-only case; for methods with more args, the caller writes
     * a method-specific super invoker. */
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_ygui_object *);
    return ((fn_t)impl)(obj);
}

/*===========================================================================
 * Data slice access.
 *=========================================================================*/

void *yetty_ygui_data_get(struct yetty_ygui_object *obj, const struct yetty_ygui_class *cls)
{
    assert(obj && cls);
    const struct yetty_ygui_class *leaf = obj->klass;
    for (size_t i = 0; i < leaf->slot_count; ++i) {
        if (leaf->slots[i].cls == cls) {
            return (char *)obj + leaf->slots[i].offset;
        }
    }
    /* Programming error — class not in object's chain. Debug build asserts. */
    assert(0 && "yetty_ygui_data_get: class not in object's class chain");
    return NULL;
}

/*===========================================================================
 * Instance lifecycle.
 *=========================================================================*/

static void object_unlink_from_parent(struct yetty_ygui_object *obj)
{
    struct yetty_ygui_object *p = obj->parent;
    if (!p) {
        return;
    }
    struct yetty_ygui_object **slot = &p->first_child;
    while (*slot && *slot != obj) {
        slot = &(*slot)->next_sibling;
    }
    if (*slot == obj) {
        *slot = obj->next_sibling;
    }
    obj->next_sibling = NULL;
    obj->parent = NULL;
}

static void object_link_to_parent(struct yetty_ygui_object *obj, struct yetty_ygui_object *parent)
{
    obj->parent = parent;
    if (!parent) {
        return;
    }
    /* Append at tail. Children appear in insertion order during the
     * layout walk — first widget added sits first on the layout
     * cursor (left in a row, top in a column), matching every flexbox
     * convention. Linear scan to tail is fine here: widget trees are
     * shallow and add is one-shot per widget. */
    if (!parent->first_child) {
        parent->first_child = obj;
        return;
    }
    struct yetty_ygui_object *t = parent->first_child;
    while (t->next_sibling) {
        t = t->next_sibling;
    }
    t->next_sibling = obj;
}

struct yetty_ygui_object_ptr_result yetty_ygui_add(const struct yetty_ygui_class *cls,
                                                   struct yetty_ygui_object *parent)
{
    if (!cls) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: NULL class");
    }
    if (cls->desc->type != YETTY_YGUI_CLASS_TYPE_REGULAR) {
        return YETTY_ERR(yetty_ygui_object_ptr,
                         "yetty_ygui_add: cannot instantiate a mixin class directly");
    }
    struct yetty_ygui_object *obj = calloc(1, cls->instance_size);
    if (!obj) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: calloc instance failed");
    }
    obj->klass = cls;
    object_link_to_parent(obj, parent);

    /* Allocate a wire id if an engine is reachable. The root widget is
     * added before set_root, so there's no engine yet — the engine
     * resolves the root's id later in set_root. Non-root widgets see
     * an engine via parent->engine walk. */
    struct yetty_ygui_runtime *engine = yetty_ygui_object_engine(obj);
    if (engine) {
        struct uint32_result idr = yetty_ygui_framework_alloc_id(engine);
        if (YETTY_IS_ERR(idr)) {
            object_unlink_from_parent(obj);
            free(obj);
            return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: id alloc failed", idr);
        }
        obj->id = idr.value;
    }

    /* Drive the constructor chain. */
    struct yetty_ycore_void_result cr = yetty_ygui_constructor(obj);
    if (YETTY_IS_ERR(cr)) {
        if (engine && obj->id) {
            struct yetty_ycore_void_result fr = yetty_ygui_framework_free_id(engine, obj->id);
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

struct yetty_ycore_void_result yetty_ygui_del(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_OK_VOID();
    }

    /* Cascade to children first (leaf-first destruction). */
    while (obj->first_child) {
        struct yetty_ycore_void_result cr = yetty_ygui_del(obj->first_child);
        if (YETTY_IS_ERR(cr)) {
            /* Best-effort cleanup — log and continue. */
            yetty_ycore_error_destroy(cr.error);
        }
    }

    /* Free event subscriptions. */
    struct yetty_ygui_event_subscription *sub = obj->subscriptions;
    while (sub) {
        struct yetty_ygui_event_subscription *next = sub->next;
        free(sub);
        sub = next;
    }
    obj->subscriptions = NULL;

    /* Drive the destructor chain (leaf-first). */
    struct yetty_ycore_void_result dr = yetty_ygui_destructor(obj);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }

    /* Free the wire id (queues a DELETE for next envelope). */
    struct yetty_ygui_runtime *engine = yetty_ygui_object_engine(obj);
    if (engine && obj->id != 0) {
        struct yetty_ycore_void_result fr = yetty_ygui_framework_free_id(engine, obj->id);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }

    object_unlink_from_parent(obj);
    free(obj);
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Hierarchy + id + dirty accessors.
 *=========================================================================*/

struct yetty_ygui_object *yetty_ygui_object_parent(struct yetty_ygui_object *obj)
{
    return obj ? obj->parent : NULL;
}

struct yetty_ygui_object *yetty_ygui_object_first_child(struct yetty_ygui_object *obj)
{
    return obj ? obj->first_child : NULL;
}

struct yetty_ygui_object *yetty_ygui_object_next_sibling(struct yetty_ygui_object *obj)
{
    return obj ? obj->next_sibling : NULL;
}

struct yetty_ygui_runtime *yetty_ygui_object_engine(struct yetty_ygui_object *obj)
{
    while (obj) {
        if (obj->engine) {
            return obj->engine;
        }
        obj = obj->parent;
    }
    return NULL;
}

uint32_t yetty_ygui_object_id(const struct yetty_ygui_object *obj)
{
    return obj ? obj->id : 0;
}

struct yetty_ycore_void_result yetty_ygui_object_set_dirty(struct yetty_ygui_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_object_set_dirty: NULL obj");
    }
    obj->dirty = 1;
    return YETTY_OK_VOID();
}

int yetty_ygui_object_is_dirty(const struct yetty_ygui_object *obj)
{
    return obj && obj->dirty;
}
