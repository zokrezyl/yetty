/*
 * ygui-object.c — runtime instance lifecycle + data slice access +
 * super invokers + lifecycle method stubs.
 *
 * Now built on top of <yclass/class.h> — no ygui-side class system.
 * The per-instance memory layout ygui needs (object header followed
 * by every reachable class's data slice in inheritance order) is
 * computed on the fly by walking the yclass parent / mixin chain.
 *
 * Per-instance layout:
 *   [ struct yetty_ygui_object header                              ]
 *   [ root data | …  | direct parent data | parent mixins data … ]
 *   [ own data | own mixins data …                                ]
 *
 * Order matches yetty_yclass_register's layout (root-down through
 * parent chain, with each level's mixins immediately after that
 * level's own data, then own + own mixins last).
 */

#include "internal.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>

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

struct yetty_ycore_void_result yetty_ygui_super_void(struct yetty_ygui_object *obj,
                                                     const struct yetty_yclass *self_class,
                                                     yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: NULL arg");
    }
    yetty_yclass_method_slot slot = yetty_ygui_method_slot_get(method_id);
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_super_void: slot lookup failed");
    }
    yetty_yclass_impl_t impl = yetty_ygui_dispatch_lookup_super(self_class, slot);
    if (!impl) {
        return YETTY_OK_VOID();
    }
    typedef struct yetty_ycore_void_result (*fn_t)(struct yetty_yclass_ctx *,
                                                   struct yetty_yclass_object *);
    return ((fn_t)impl)(NULL, (struct yetty_yclass_object *)obj);
}

struct yetty_ycore_int_result yetty_ygui_super_int(struct yetty_ygui_object *obj,
                                                   const struct yetty_yclass *self_class,
                                                   yetty_yclass_method_id_t method_id)
{
    if (!obj || !self_class || !method_id) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: NULL arg");
    }
    yetty_yclass_method_slot slot = yetty_ygui_method_slot_get(method_id);
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ygui_super_int: slot lookup failed");
    }
    yetty_yclass_impl_t impl = yetty_ygui_dispatch_lookup_super(self_class, slot);
    if (!impl) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    typedef struct yetty_ycore_int_result (*fn_t)(struct yetty_yclass_ctx *,
                                                  struct yetty_yclass_object *);
    return ((fn_t)impl)(NULL, (struct yetty_yclass_object *)obj);
}

/*===========================================================================
 * Data slice access — walks the leaf class's parent / mixin chain
 * (via the yclass getters) and returns a typed pointer to the
 * requested class's slice within the object's body.
 *
 * Layout order (matches yetty_yclass_register's instance layout):
 *   sizeof(yetty_ygui_object)
 *   then root → parent (each class's own data + its mixins' data)
 *   then leaf own data + leaf own mixins data
 *
 * Walk is O(chain length × mixin count) per data_get call. Widget
 * trees are shallow (typically ≤3 levels with ≤2 mixins each) so the
 * linear search is cache-friendly. No caching today; if profiling
 * later shows it matters, a per-(leaf,target) → offset hash is the
 * obvious cache.
 *=========================================================================*/

/* If `cls` matches `target`, write `offset` to `*out_offset` and
 * return 1. Otherwise advance `offset` by `cls`'s data_size and
 * return 0. NULL `cls` is silently skipped. */
static int try_match(const struct yetty_yclass *cls, const struct yetty_yclass *target,
                     size_t *offset, size_t *out_offset)
{
    if (!cls) {
        return 0;
    }
    if (cls == target) {
        *out_offset = *offset;
        return 1;
    }
    struct yetty_ycore_size_result ds = yetty_yclass_data_size(cls);
    if (YETTY_IS_ERR(ds)) {
        yetty_ycore_error_destroy(ds.error);
        return 0;
    }
    *offset += ds.value;
    return 0;
}

/* Walk `cls`'s mixins, checking each against `target`. Returns 1 + sets
 * `*out_offset` when found. */
static int walk_mixins(const struct yetty_yclass *cls, const struct yetty_yclass *target,
                       size_t *offset, size_t *out_offset)
{
    struct yetty_ycore_size_result mc = yetty_yclass_mixin_count(cls);
    if (YETTY_IS_ERR(mc)) {
        yetty_ycore_error_destroy(mc.error);
        return 0;
    }
    for (size_t i = 0; i < mc.value; ++i) {
        struct yetty_yclass_ptr_result mxr = yetty_yclass_mixin_at(cls, i);
        if (YETTY_IS_ERR(mxr)) {
            yetty_ycore_error_destroy(mxr.error);
            continue;
        }
        if (try_match(mxr.value, target, offset, out_offset)) {
            return 1;
        }
    }
    return 0;
}

/* Compute the byte offset of `target`'s data slice within an instance
 * of `leaf`'s class. Returns SIZE_MAX if `target` isn't in the chain
 * (programmer error). */
static size_t data_offset(const struct yetty_yclass *leaf, const struct yetty_yclass *target)
{
    size_t offset = sizeof(struct yetty_ygui_object);
    size_t out_offset = 0;

    /* Walk the parent chain root-down. Collect parents into a tiny
     * stack so we can iterate root → leaf without recursion. */
    const struct yetty_yclass *chain[64];
    size_t chain_len = 0;
    {
        const struct yetty_yclass *cur = leaf;
        for (;;) {
            struct yetty_yclass_ptr_result pr = yetty_yclass_parent(cur);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
                break;
            }
            if (!pr.value) {
                break;
            }
            if (chain_len >= sizeof(chain) / sizeof(chain[0])) {
                return SIZE_MAX;
            }
            chain[chain_len++] = pr.value;
            cur = pr.value;
        }
    }

    /* Iterate root → direct parent: each level's data first, then its
     * mixins. */
    for (size_t i = chain_len; i > 0; --i) {
        const struct yetty_yclass *p = chain[i - 1];
        if (try_match(p, target, &offset, &out_offset)) {
            return out_offset;
        }
        if (walk_mixins(p, target, &offset, &out_offset)) {
            return out_offset;
        }
    }

    /* Leaf's own data, then leaf's mixins. */
    if (try_match(leaf, target, &offset, &out_offset)) {
        return out_offset;
    }
    if (walk_mixins(leaf, target, &offset, &out_offset)) {
        return out_offset;
    }
    return SIZE_MAX;
}

void *yetty_ygui_data_get(struct yetty_ygui_object *obj, const struct yetty_yclass *cls)
{
    assert(obj && cls);
    size_t off = data_offset(obj->klass, cls);
    assert(off != SIZE_MAX && "yetty_ygui_data_get: class not in object's chain");
    if (off == SIZE_MAX) {
        return NULL;
    }
    return (char *)obj + off;
}

/* Total per-instance size (object header + every data slice) for a
 * class. Used by yetty_ygui_add to size the calloc. Sums via the
 * same walk data_offset does — root-down through parent + parent
 * mixins, then leaf own + leaf mixins. */
static size_t instance_size_of(const struct yetty_yclass *leaf)
{
    size_t total = sizeof(struct yetty_ygui_object);
    const struct yetty_yclass *chain[64];
    size_t chain_len = 0;
    {
        const struct yetty_yclass *cur = leaf;
        for (;;) {
            struct yetty_yclass_ptr_result pr = yetty_yclass_parent(cur);
            if (YETTY_IS_ERR(pr)) {
                yetty_ycore_error_destroy(pr.error);
                break;
            }
            if (!pr.value) {
                break;
            }
            if (chain_len >= sizeof(chain) / sizeof(chain[0])) {
                return total;
            }
            chain[chain_len++] = pr.value;
            cur = pr.value;
        }
    }
    for (size_t i = chain_len; i > 0; --i) {
        const struct yetty_yclass *p = chain[i - 1];
        struct yetty_ycore_size_result ds = yetty_yclass_data_size(p);
        if (YETTY_IS_OK(ds)) {
            total += ds.value;
        } else {
            yetty_ycore_error_destroy(ds.error);
        }
        struct yetty_ycore_size_result mc = yetty_yclass_mixin_count(p);
        if (YETTY_IS_OK(mc)) {
            for (size_t j = 0; j < mc.value; ++j) {
                struct yetty_yclass_ptr_result mxr = yetty_yclass_mixin_at(p, j);
                if (YETTY_IS_OK(mxr)) {
                    struct yetty_ycore_size_result mds = yetty_yclass_data_size(mxr.value);
                    if (YETTY_IS_OK(mds)) {
                        total += mds.value;
                    } else {
                        yetty_ycore_error_destroy(mds.error);
                    }
                } else {
                    yetty_ycore_error_destroy(mxr.error);
                }
            }
        } else {
            yetty_ycore_error_destroy(mc.error);
        }
    }
    /* Leaf own. */
    struct yetty_ycore_size_result ds = yetty_yclass_data_size(leaf);
    if (YETTY_IS_OK(ds)) {
        total += ds.value;
    } else {
        yetty_ycore_error_destroy(ds.error);
    }
    struct yetty_ycore_size_result mc = yetty_yclass_mixin_count(leaf);
    if (YETTY_IS_OK(mc)) {
        for (size_t j = 0; j < mc.value; ++j) {
            struct yetty_yclass_ptr_result mxr = yetty_yclass_mixin_at(leaf, j);
            if (YETTY_IS_OK(mxr)) {
                struct yetty_ycore_size_result mds = yetty_yclass_data_size(mxr.value);
                if (YETTY_IS_OK(mds)) {
                    total += mds.value;
                } else {
                    yetty_ycore_error_destroy(mds.error);
                }
            } else {
                yetty_ycore_error_destroy(mxr.error);
            }
        }
    } else {
        yetty_ycore_error_destroy(mc.error);
    }
    return total;
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

struct yetty_ygui_object_ptr_result yetty_ygui_add(const struct yetty_yclass *cls,
                                                   struct yetty_ygui_object *parent)
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

    size_t inst_size = instance_size_of(cls);
    struct yetty_ygui_object *obj = calloc(1, inst_size);
    if (!obj) {
        return YETTY_ERR(yetty_ygui_object_ptr, "yetty_ygui_add: calloc instance failed");
    }
    obj->klass = cls;
    object_link_to_parent(obj, parent);

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

    struct yetty_ycore_void_result cr = yetty_ygui_constructor(NULL, (struct yetty_yclass_object *)obj);
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
    while (obj->first_child) {
        struct yetty_ycore_void_result cr = yetty_ygui_del(obj->first_child);
        if (YETTY_IS_ERR(cr)) {
            yetty_ycore_error_destroy(cr.error);
        }
    }
    struct yetty_ygui_event_subscription *sub = obj->subscriptions;
    while (sub) {
        struct yetty_ygui_event_subscription *next = sub->next;
        free(sub);
        sub = next;
    }
    obj->subscriptions = NULL;
    struct yetty_ycore_void_result dr = yetty_ygui_destructor(NULL, (struct yetty_yclass_object *)obj);
    if (YETTY_IS_ERR(dr)) {
        yetty_ycore_error_destroy(dr.error);
    }
    struct yetty_ygui_runtime *engine = yetty_ygui_object_engine(obj);
    if (engine && obj->id != 0) {
        struct yetty_ycore_void_result fr = yetty_ygui_framework_free_id(engine, obj->id);
        if (YETTY_IS_ERR(fr)) {
            yetty_ycore_error_destroy(fr.error);
        }
    }
    if (engine && engine->hovered_obj == obj) {
        engine->hovered_obj = NULL;
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
    struct yetty_ygui_runtime *engine = yetty_ygui_object_engine(obj);
    if (engine) {
        yetty_ygui_framework_mark_dirty(engine);
    }
    return YETTY_OK_VOID();
}

int yetty_ygui_object_is_dirty(const struct yetty_ygui_object *obj)
{
    return obj && obj->dirty;
}

int yetty_ygui_object_is_hovered(const struct yetty_ygui_object *obj)
{
    return obj && obj->hovered;
}

const struct yetty_yclass *yetty_ygui_object_class(const struct yetty_ygui_object *obj)
{
    return obj ? obj->klass : NULL;
}
