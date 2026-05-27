/* Class runtime — slot allocator, registry, dispatch.
 * uthash-indexed name + id lookups. Slot 0 is reserved. */

#include "class.h"
#include "uthash.h"

#include <stdlib.h>
#include <string.h>

struct class {
    const struct class_descriptor *desc;
    const struct class *parent;
    const struct class **mixins;
    size_t mixin_count;
    impl_t *dispatch;
    size_t dispatch_count;
    size_t instance_size;
    UT_hash_handle hh;     /* keyed by desc->name */
};

struct slot_entry {
    const char *name;          /* uthash key — name view (default `hh`) */
    method_id_t local_id;      /* uthash key — id view (`hh_id`)        */
    method_slot slot_index;
    UT_hash_handle hh;         /* by_name */
    UT_hash_handle hh_id;      /* by_id   */
};

struct slot_table {
    struct slot_entry **by_index;
    size_t count;              /* count of populated entries — index 0 reserved */
    size_t cap;
    struct slot_entry *by_name;
    struct slot_entry *by_id;
};

static struct slot_table *slot_table_get(void)
{
    static struct slot_table tbl = {0};
    return &tbl;
}

struct class_registry {
    struct class **by_index;
    size_t count;
    size_t cap;
    struct class *by_name;
};

static struct class_registry *class_registry_get(void)
{
    static struct class_registry reg = {0};
    return &reg;
}

static int class_registry_add(struct class *cls)
{
    struct class_registry *reg = class_registry_get();
    if (reg->count == reg->cap) {
        size_t ncap = reg->cap ? reg->cap * 2 : 16;
        struct class **na = realloc(reg->by_index, ncap * sizeof(*na));
        if (!na) return -1;
        reg->by_index = na;
        reg->cap = ncap;
    }
    reg->by_index[reg->count++] = cls;
    HASH_ADD_KEYPTR(hh, reg->by_name, cls->desc->name,
                    strlen(cls->desc->name), cls);
    return 0;
}

static int class_registry_grow_dispatch(size_t new_count)
{
    struct class_registry *reg = class_registry_get();
    for (size_t i = 0; i < reg->count; ++i) {
        struct class *cls = reg->by_index[i];
        if (cls->dispatch_count >= new_count) continue;
        impl_t *nd = realloc(cls->dispatch, new_count * sizeof(*nd));
        if (!nd) return -1;
        memset(nd + cls->dispatch_count, 0,
               (new_count - cls->dispatch_count) * sizeof(*nd));
        cls->dispatch = nd;
        cls->dispatch_count = new_count;
    }
    return 0;
}

method_slot method_slot_register(const char *name, method_id_t id)
{
    if (!name) return METHOD_SLOT_UNDEFINED;
    struct slot_table *tbl = slot_table_get();

    struct slot_entry *e = NULL;
    HASH_FIND_STR(tbl->by_name, name, e);
    if (e) return e->slot_index;

    /* Allocate a new entry. */
    e = calloc(1, sizeof(*e));
    if (!e) return METHOD_SLOT_UNDEFINED;
    e->name = name;
    e->local_id = id;

    if (tbl->count >= tbl->cap) {
        size_t ncap = tbl->cap ? tbl->cap * 2 : 32;
        while (ncap <= tbl->count) ncap *= 2;
        struct slot_entry **na = realloc(tbl->by_index, ncap * sizeof(*na));
        if (!na) { free(e); return METHOD_SLOT_UNDEFINED; }
        memset(na + tbl->cap, 0, (ncap - tbl->cap) * sizeof(*na));
        tbl->by_index = na;
        tbl->cap = ncap;
    }
    e->slot_index = (method_slot)tbl->count;
    tbl->by_index[tbl->count++] = e;
    HASH_ADD_KEYPTR(hh, tbl->by_name, e->name, strlen(e->name), e);
    HASH_ADD(hh_id, tbl->by_id, local_id, sizeof(method_id_t), e);

    class_registry_grow_dispatch(tbl->count);
    return e->slot_index;
}

method_slot method_slot_get(method_id_t id)
{
    if (!id) return METHOD_SLOT_UNDEFINED;
    struct slot_table *tbl = slot_table_get();
    struct slot_entry *e = NULL;
    HASH_FIND(hh_id, tbl->by_id, &id, sizeof(method_id_t), e);
    return e ? e->slot_index : METHOD_SLOT_UNDEFINED;
}

method_slot method_slot_by_name(const char *name)
{
    if (!name) return METHOD_SLOT_UNDEFINED;
    struct slot_table *tbl = slot_table_get();
    struct slot_entry *e = NULL;
    HASH_FIND_STR(tbl->by_name, name, e);
    return e ? e->slot_index : METHOD_SLOT_UNDEFINED;
}

const char *method_slot_name(method_slot slot)
{
    struct slot_table *tbl = slot_table_get();
    if (slot == METHOD_SLOT_UNDEFINED || slot >= tbl->count) return NULL;
    return tbl->by_index[slot] ? tbl->by_index[slot]->name : NULL;
}

impl_t class_dispatch_lookup(const struct class *cls, method_slot slot)
{
    if (!cls || slot == METHOD_SLOT_UNDEFINED || slot >= cls->dispatch_count)
        return NULL;
    return cls->dispatch[slot];
}

const struct class *object_class(const struct object *obj)
{
    return obj ? obj->klass : NULL;
}

const struct class *class_register(
    const struct class_descriptor *desc, const struct op *ops,
    size_t ops_count, const struct class *parent,
    const struct class *const *mixins, size_t mixin_count)
{
    if (!desc) return NULL;
    struct class *cls = calloc(1, sizeof(*cls));
    if (!cls) return NULL;
    cls->desc = desc;
    cls->parent = parent;

    if (mixin_count > 0) {
        cls->mixins = malloc(mixin_count * sizeof(*cls->mixins));
        if (!cls->mixins) { free(cls); return NULL; }
        memcpy((void *)cls->mixins, mixins, mixin_count * sizeof(*cls->mixins));
        cls->mixin_count = mixin_count;
    }

    size_t offset = sizeof(struct object);
    for (const struct class *p = parent; p != NULL; p = p->parent) {
        offset += p->desc->data_size;
        for (size_t m = 0; m < p->mixin_count; ++m)
            offset += p->mixins[m]->desc->data_size;
    }
    offset += desc->data_size;
    for (size_t m = 0; m < mixin_count; ++m)
        offset += mixins[m]->desc->data_size;
    cls->instance_size = offset;

    if (parent && parent->dispatch_count > 0) {
        cls->dispatch = malloc(parent->dispatch_count * sizeof(*cls->dispatch));
        if (!cls->dispatch) {
            free(cls->mixins); free(cls); return NULL;
        }
        memcpy(cls->dispatch, parent->dispatch,
               parent->dispatch_count * sizeof(*cls->dispatch));
        cls->dispatch_count = parent->dispatch_count;
    }
    for (size_t m = 0; m < mixin_count; ++m) {
        const struct class *mx = mixins[m];
        for (size_t s = 0; s < mx->dispatch_count; ++s) {
            if (!mx->dispatch[s]) continue;
            if (s >= cls->dispatch_count) {
                size_t ncount = s + 1;
                impl_t *nd = realloc(cls->dispatch, ncount * sizeof(*nd));
                if (!nd) {
                    free(cls->dispatch); free(cls->mixins); free(cls); return NULL;
                }
                memset(nd + cls->dispatch_count, 0,
                       (ncount - cls->dispatch_count) * sizeof(*nd));
                cls->dispatch = nd;
                cls->dispatch_count = ncount;
            }
            cls->dispatch[s] = mx->dispatch[s];
        }
    }

    for (size_t i = 0; i < ops_count; ++i) {
        method_slot slot = method_slot_register(ops[i].name, ops[i].method_id);
        if (slot == METHOD_SLOT_UNDEFINED) continue;
        if (slot >= cls->dispatch_count) {
            size_t ncount = slot + 1;
            impl_t *nd = realloc(cls->dispatch, ncount * sizeof(*nd));
            if (!nd) {
                free(cls->dispatch); free(cls->mixins); free(cls); return NULL;
            }
            memset(nd + cls->dispatch_count, 0,
                   (ncount - cls->dispatch_count) * sizeof(*nd));
            cls->dispatch = nd;
            cls->dispatch_count = ncount;
        }
        cls->dispatch[slot] = ops[i].impl;
    }

    if (class_registry_add(cls) < 0) {
        free(cls->dispatch);
        free((void *)cls->mixins);
        free(cls);
        return NULL;
    }
    return cls;
}

/* Per-module accessor hooks chained linearly. Module count is tiny;
 * linear walk dominates only on registry miss anyway. */
struct accessor_node {
    accessor_lookup_fn fn;
    struct accessor_node *next;
};

static struct accessor_node **accessor_chain_head(void)
{
    static struct accessor_node *head = NULL;
    return &head;
}

void class_add_accessor_lookup(accessor_lookup_fn fn)
{
    if (!fn) return;
    struct accessor_node *node = calloc(1, sizeof(*node));
    if (!node) return;
    node->fn = fn;
    struct accessor_node **head = accessor_chain_head();
    node->next = *head;
    *head = node;
}

const struct class *class_by_name(const char *name)
{
    if (!name) return NULL;
    struct class_registry *reg = class_registry_get();
    struct class *cls = NULL;
    HASH_FIND_STR(reg->by_name, name, cls);
    if (cls) return cls;

    /* Registry miss → walk the per-module lookup chain. First hook that
     * recognises the name calls the accessor, which routes through
     * class_register and lands the class in the registry. */
    for (struct accessor_node *n = *accessor_chain_head(); n; n = n->next) {
        const struct class *fresh = n->fn(name);
        if (fresh) return fresh;
    }
    return NULL;
}

void class_for_each_slot(const struct class *cls,
                         void (*cb)(const char *name, method_slot slot, void *ud),
                         void *userdata)
{
    if (!cls || !cb) return;
    for (size_t s = 0; s < cls->dispatch_count; ++s) {
        if (!cls->dispatch[s]) continue;
        const char *name = method_slot_name((method_slot)s);
        if (name) cb(name, (method_slot)s, userdata);
    }
}

struct object *object_alloc(const struct class *cls)
{
    if (!cls) return NULL;
    struct object *obj = calloc(1, cls->instance_size);
    if (!obj) return NULL;
    obj->klass = cls;
    return obj;
}

void object_free(struct object *obj)
{
    free(obj);
}
