/*
 * ygui-class.c — class registration, method-slot allocator, dispatch.
 *
 * Framework-wide state (method slot table + registered class list)
 * lives in function-static singletons inside helpers — no file-scope
 * data. The slot table grows append-only; dispatch tables grow
 * eagerly across already-registered classes when a new slot appears.
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Method-slot allocator.
 *
 * One slot is allocated per unique method id (the address of the
 * public stub function). Append-only growing array — slot N has
 * method_id `slots[N]`. Lookup is linear; method counts are small
 * (<100) so a hash table would be overkill.
 *=========================================================================*/

struct slot_table {
    yetty_ygui_method_id_t *ids;
    size_t count;
    size_t cap;
};

static struct slot_table *slot_table_get(void)
{
    static struct slot_table tbl = {0};
    return &tbl;
}

/*===========================================================================
 * Class registry — list of every class registered so far. Used to grow
 * each class's dispatch table when the slot table grows.
 *=========================================================================*/

struct class_registry {
    struct yetty_ygui_class **classes;
    size_t count;
    size_t cap;
};

static struct class_registry *class_registry_get(void)
{
    static struct class_registry reg = {0};
    return &reg;
}

static struct yetty_ycore_void_result class_registry_add(struct yetty_ygui_class *cls)
{
    struct class_registry *reg = class_registry_get();
    if (reg->count == reg->cap) {
        size_t ncap = reg->cap ? reg->cap * 2 : 16;
        struct yetty_ygui_class **na = realloc(reg->classes, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void, "ygui_class: realloc registry failed");
        }
        reg->classes = na;
        reg->cap = ncap;
    }
    reg->classes[reg->count++] = cls;
    return YETTY_OK_VOID();
}

/* Grow every registered class's dispatch table to `new_count`. Existing
 * entries are preserved; new slots are zero-initialised (NULL). */
static struct yetty_ycore_void_result class_registry_grow_dispatch(size_t new_count)
{
    struct class_registry *reg = class_registry_get();
    for (size_t i = 0; i < reg->count; ++i) {
        struct yetty_ygui_class *cls = reg->classes[i];
        if (cls->dispatch_count >= new_count) {
            continue;
        }
        yetty_ygui_impl_t *nd = realloc(cls->dispatch, new_count * sizeof(*nd));
        if (!nd) {
            return YETTY_ERR(yetty_ycore_void, "ygui_class: dispatch realloc failed");
        }
        memset(nd + cls->dispatch_count, 0, (new_count - cls->dispatch_count) * sizeof(*nd));
        cls->dispatch = nd;
        cls->dispatch_count = new_count;
    }
    return YETTY_OK_VOID();
}

yetty_ygui_method_slot yetty_ygui_method_slot_get(yetty_ygui_method_id_t method_id)
{
    struct slot_table *tbl = slot_table_get();
    for (size_t i = 0; i < tbl->count; ++i) {
        if (tbl->ids[i] == method_id) {
            return (yetty_ygui_method_slot)i;
        }
    }
    if (tbl->count == tbl->cap) {
        size_t ncap = tbl->cap ? tbl->cap * 2 : 32;
        yetty_ygui_method_id_t *na = realloc(tbl->ids, ncap * sizeof(*na));
        if (!na) {
            /* Slot allocator can't fail gracefully — the framework
             * relies on it. Caller resolves UNDEFINED → treats as
             * "no override" at dispatch sites; that's strictly worse
             * than aborting, so return UNDEFINED. */
            return YETTY_YGUI_METHOD_SLOT_UNDEFINED;
        }
        tbl->ids = na;
        tbl->cap = ncap;
    }
    tbl->ids[tbl->count] = method_id;
    yetty_ygui_method_slot s = (yetty_ygui_method_slot)tbl->count;
    tbl->count++;

    /* Grow every existing class's dispatch table to fit the new slot. */
    struct yetty_ycore_void_result r = class_registry_grow_dispatch(tbl->count);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        /* Best effort — the slot is allocated; if dispatch growth fails
         * the affected classes return NULL for this slot. */
    }
    return s;
}

yetty_ygui_impl_t yetty_ygui_class_dispatch_lookup(const struct yetty_ygui_class *cls,
                                                   yetty_ygui_method_slot slot)
{
    if (!cls || slot == YETTY_YGUI_METHOD_SLOT_UNDEFINED || slot >= cls->dispatch_count) {
        return NULL;
    }
    return cls->dispatch[slot];
}

yetty_ygui_impl_t yetty_ygui_class_dispatch_lookup_super(const struct yetty_ygui_class *self_class,
                                                         yetty_ygui_method_slot slot)
{
    if (!self_class || !self_class->parent) {
        return NULL;
    }
    return yetty_ygui_class_dispatch_lookup(self_class->parent, slot);
}

/*===========================================================================
 * Class registration — compute layout, build dispatch table, resolve ops.
 *=========================================================================*/

/* Append a (class, offset) slot to a class's slot map, growing the
 * dynamic array as needed. The slot map is searched linearly so no
 * duplicate detection is strictly required — but a duplicate would
 * indicate a registration bug, so we assert via early return on
 * an existing entry for the same class. */
static struct yetty_ycore_void_result class_add_slot(struct yetty_ygui_class *cls,
                                                     const struct yetty_ygui_class *which,
                                                     size_t offset)
{
    for (size_t i = 0; i < cls->slot_count; ++i) {
        if (cls->slots[i].cls == which) {
            return YETTY_ERR(yetty_ycore_void, "ygui_class: duplicate slot during registration");
        }
    }
    struct yetty_ygui_data_slot *ns = realloc(cls->slots, (cls->slot_count + 1) * sizeof(*ns));
    if (!ns) {
        return YETTY_ERR(yetty_ycore_void, "ygui_class: slots realloc failed");
    }
    cls->slots = ns;
    cls->slots[cls->slot_count].cls = which;
    cls->slots[cls->slot_count].offset = offset;
    cls->slot_count++;
    return YETTY_OK_VOID();
}

/* Plant a single op's (method_id → impl) into a dispatch table, growing
 * the table if the slot doesn't fit. */
static struct yetty_ycore_void_result class_plant_op(struct yetty_ygui_class *cls,
                                                     const struct yetty_ygui_op *op)
{
    yetty_ygui_method_slot slot = yetty_ygui_method_slot_get(op->method_id);
    if (slot == YETTY_YGUI_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_ycore_void, "ygui_class: slot allocation failed");
    }
    if (slot >= cls->dispatch_count) {
        size_t ncount = slot + 1;
        yetty_ygui_impl_t *nd = realloc(cls->dispatch, ncount * sizeof(*nd));
        if (!nd) {
            return YETTY_ERR(yetty_ycore_void, "ygui_class: dispatch alloc failed");
        }
        memset(nd + cls->dispatch_count, 0, (ncount - cls->dispatch_count) * sizeof(*nd));
        cls->dispatch = nd;
        cls->dispatch_count = ncount;
    }
    cls->dispatch[slot] = op->impl;
    return YETTY_OK_VOID();
}

static void class_destroy(struct yetty_ygui_class *cls)
{
    if (!cls) {
        return;
    }
    free(cls->dispatch);
    free(cls->slots);
    free((void *)cls->mixins);
    free(cls);
}

struct yetty_ygui_class_ptr_result yetty_ygui_class_register(
    const struct yetty_ygui_class_descriptor *desc, const struct yetty_ygui_op *ops,
    size_t ops_count, const struct yetty_ygui_class *parent,
    const struct yetty_ygui_class *const *mixins, size_t mixin_count)
{
    if (!desc) {
        return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: NULL descriptor");
    }
    if (desc->type == YETTY_YGUI_CLASS_TYPE_MIXIN && parent != NULL) {
        return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: mixin must have NULL parent");
    }

    struct yetty_ygui_class *cls = calloc(1, sizeof(*cls));
    if (!cls) {
        return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: calloc class failed");
    }
    cls->desc = desc;
    cls->parent = parent;

    /* Copy the mixin pointers into an owned array (we drop the trailing
     * NULL sentinel here too in case callers passed one). */
    if (mixin_count > 0) {
        cls->mixins = malloc(mixin_count * sizeof(*cls->mixins));
        if (!cls->mixins) {
            class_destroy(cls);
            return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: malloc mixins failed");
        }
        memcpy((void *)cls->mixins, mixins, mixin_count * sizeof(*cls->mixins));
        cls->mixin_count = mixin_count;
    }

    /* Compute instance layout:
     *   - object header at offset 0
     *   - data slices: walk parent chain root-down, then this class,
     *     then mixins in declaration order. Each slice's offset is
     *     recorded in the slot map; the running offset accumulates the
     *     slice's data_size.
     *
     * Parent chain root-down means: build a list of [root, …, parent],
     * iterate, append slot for each. */
    size_t offset = sizeof(struct yetty_ygui_object);

    /* Walk parent chain root-down. We don't know the depth up front;
     * temporarily collect parents into a small stack array. */
    const struct yetty_ygui_class *chain[64];
    size_t chain_len = 0;
    for (const struct yetty_ygui_class *p = parent; p != NULL; p = p->parent) {
        if (chain_len >= sizeof(chain) / sizeof(chain[0])) {
            class_destroy(cls);
            return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: parent chain too deep");
        }
        chain[chain_len++] = p;
    }
    /* Reverse iterate to go root → parent. */
    for (size_t i = chain_len; i > 0; --i) {
        const struct yetty_ygui_class *p = chain[i - 1];
        if (p->desc->data_size > 0) {
            struct yetty_ycore_void_result r = class_add_slot(cls, p, offset);
            if (YETTY_IS_ERR(r)) {
                class_destroy(cls);
                return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: parent slot add", r);
            }
            offset += p->desc->data_size;
        }
        /* Also expose every mixin of every parent for data_get. */
        for (size_t m = 0; m < p->mixin_count; ++m) {
            const struct yetty_ygui_class *mx = p->mixins[m];
            if (mx->desc->data_size > 0) {
                struct yetty_ycore_void_result r = class_add_slot(cls, mx, offset);
                if (YETTY_IS_ERR(r)) {
                    class_destroy(cls);
                    return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: parent mixin slot add", r);
                }
                offset += mx->desc->data_size;
            }
        }
    }

    /* This class's own slice. */
    if (desc->data_size > 0) {
        struct yetty_ycore_void_result r = class_add_slot(cls, cls, offset);
        if (YETTY_IS_ERR(r)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: own slot add", r);
        }
        offset += desc->data_size;
    }

    /* This class's mixin slices. */
    for (size_t m = 0; m < cls->mixin_count; ++m) {
        const struct yetty_ygui_class *mx = cls->mixins[m];
        if (mx->desc->data_size > 0) {
            struct yetty_ycore_void_result r = class_add_slot(cls, mx, offset);
            if (YETTY_IS_ERR(r)) {
                class_destroy(cls);
                return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: mixin slot add", r);
            }
            offset += mx->desc->data_size;
        }
    }

    cls->instance_size = offset;

    /* Dispatch table: start from parent's table (inheritance), then
     * walk mixin ops in declaration order, then this class's ops
     * (overrides). */
    if (parent && parent->dispatch_count > 0) {
        cls->dispatch = malloc(parent->dispatch_count * sizeof(*cls->dispatch));
        if (!cls->dispatch) {
            class_destroy(cls);
            return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: dispatch alloc failed");
        }
        memcpy(cls->dispatch, parent->dispatch, parent->dispatch_count * sizeof(*cls->dispatch));
        cls->dispatch_count = parent->dispatch_count;
    }

    /* Mixin ops — we don't have the mixin's ops array stored, but we
     * do have its resolved dispatch table. Copy non-NULL entries into
     * ours (overriding what came from parent). */
    for (size_t m = 0; m < cls->mixin_count; ++m) {
        const struct yetty_ygui_class *mx = cls->mixins[m];
        for (size_t s = 0; s < mx->dispatch_count; ++s) {
            if (!mx->dispatch[s]) {
                continue;
            }
            if (s >= cls->dispatch_count) {
                size_t ncount = s + 1;
                yetty_ygui_impl_t *nd = realloc(cls->dispatch, ncount * sizeof(*nd));
                if (!nd) {
                    class_destroy(cls);
                    return YETTY_ERR(yetty_ygui_class_ptr,
                                     "ygui_class: dispatch realloc (mixin) failed");
                }
                memset(nd + cls->dispatch_count, 0, (ncount - cls->dispatch_count) * sizeof(*nd));
                cls->dispatch = nd;
                cls->dispatch_count = ncount;
            }
            cls->dispatch[s] = mx->dispatch[s];
        }
    }

    /* This class's ops (overrides). */
    for (size_t i = 0; i < ops_count; ++i) {
        struct yetty_ycore_void_result r = class_plant_op(cls, &ops[i]);
        if (YETTY_IS_ERR(r)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: op planting failed", r);
        }
    }

    /* Register so the slot allocator can grow this class's table later. */
    struct yetty_ycore_void_result rr = class_registry_add(cls);
    if (YETTY_IS_ERR(rr)) {
        class_destroy(cls);
        return YETTY_ERR(yetty_ygui_class_ptr, "ygui_class: registry add failed", rr);
    }

    return YETTY_OK(yetty_ygui_class_ptr, cls);
}

const struct yetty_ygui_class *yetty_ygui_object_class(const struct yetty_ygui_object *obj)
{
    if (!obj) {
        return NULL;
    }
    return obj->klass;
}
