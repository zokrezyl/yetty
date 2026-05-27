/* Class runtime — per-domain slot tables.
 *
 * One slot_table per domain (allocated lazily on slot_table_get).
 * method_slot values pack the domain id in bits 27..24 and the
 * per-domain local index in bits 23..0, so two domains can share local
 * names without colliding. The global class_registry stays keyed by
 * the qualified class name; classes carry per-domain dispatch slices,
 * indexed by the same domain id encoded in method_slot. */

#include "class.h"
#include "uthash.h"

#include <stdlib.h>
#include <string.h>

struct class {
    const struct class_descriptor *desc;
    const struct class *parent;
    const struct class **mixins;
    size_t mixin_count;

    /* Per-domain dispatch. dispatch_by_domain[d].impls is a flat array
     * indexed by local_idx (i.e. METHOD_SLOT_INDEX_OF(slot)). count is
     * the array length. Domains the class never touches stay at
     * count=0 / impls=NULL. */
    struct dispatch_slice {
        impl_t *impls;
        size_t count;
    } dispatch_by_domain[METHOD_SLOT_MAX_DOMAINS];

    size_t instance_size;
    UT_hash_handle hh; /* keyed by desc->name */
};

struct slot_entry {
    char *qname;             /* owned: "<domain>_<local_name>" */
    const char *local_name;  /* points into qname after the boundary */
    method_id_t local_id;
    method_slot slot_index;  /* packed (domain_id, local_idx) */
    UT_hash_handle hh_lname; /* by local_name within the per-domain table */
    UT_hash_handle hh_id;    /* by local_id within the per-domain table */
    UT_hash_handle hh_qname; /* by qname in the global qname hash */
};

struct slot_table {
    char *domain;                 /* owned */
    uint8_t domain_id;            /* 1..METHOD_SLOT_MAX_DOMAINS-1 */
    struct slot_entry **by_index; /* local index → entry */
    size_t count;
    size_t cap;
    struct slot_entry *by_local_name; /* uthash root */
    struct slot_entry *by_local_id;   /* uthash root */
    UT_hash_handle hh_dom;            /* by domain string in the registry */
};

struct domain_registry {
    struct slot_table *by_id[METHOD_SLOT_MAX_DOMAINS]; /* domain_id → table */
    struct slot_table *by_name;                        /* uthash by domain */
    uint8_t next_id;                                   /* next free id (1..) */
};

static struct domain_registry *dreg(void)
{
    static struct domain_registry r = {0};
    if (r.next_id == 0) {
        r.next_id = 1; /* domain_id 0 reserved as invalid */
    }
    return &r;
}

static struct slot_entry **global_qname_root(void)
{
    static struct slot_entry *root = NULL;
    return &root;
}

struct slot_table *slot_table_get(const char *domain)
{
    if (!domain) {
        return NULL;
    }
    struct domain_registry *reg = dreg();
    struct slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (tbl) {
        return tbl;
    }
    if (reg->next_id >= METHOD_SLOT_MAX_DOMAINS) {
        return NULL; /* domain cap reached */
    }
    tbl = calloc(1, sizeof(*tbl));
    if (!tbl) {
        return NULL;
    }
    tbl->domain = strdup(domain);
    if (!tbl->domain) {
        free(tbl);
        return NULL;
    }
    tbl->domain_id = reg->next_id++;
    reg->by_id[tbl->domain_id] = tbl;
    HASH_ADD_KEYPTR(hh_dom, reg->by_name, tbl->domain, strlen(tbl->domain), tbl);
    return tbl;
}

method_slot method_slot_register(const char *domain, const char *name, method_id_t id)
{
    if (!domain || !name) {
        return METHOD_SLOT_UNDEFINED;
    }
    struct slot_table *tbl = slot_table_get(domain);
    if (!tbl) {
        return METHOD_SLOT_UNDEFINED;
    }

    struct slot_entry *e = NULL;
    HASH_FIND(hh_lname, tbl->by_local_name, name, strlen(name), e);
    if (e) {
        return e->slot_index;
    }

    e = calloc(1, sizeof(*e));
    if (!e) {
        return METHOD_SLOT_UNDEFINED;
    }

    size_t dom_len = strlen(tbl->domain);
    size_t loc_len = strlen(name);
    e->qname = malloc(dom_len + 1 + loc_len + 1);
    if (!e->qname) {
        free(e);
        return METHOD_SLOT_UNDEFINED;
    }
    memcpy(e->qname, tbl->domain, dom_len);
    e->qname[dom_len] = '_';
    memcpy(e->qname + dom_len + 1, name, loc_len + 1);
    e->local_name = e->qname + dom_len + 1;
    e->local_id = id;

    if (tbl->count >= tbl->cap) {
        size_t ncap = tbl->cap ? tbl->cap * 2 : 32;
        while (ncap <= tbl->count) {
            ncap *= 2;
        }
        struct slot_entry **na = realloc(tbl->by_index, ncap * sizeof(*na));
        if (!na) {
            free(e->qname);
            free(e);
            return METHOD_SLOT_UNDEFINED;
        }
        memset(na + tbl->cap, 0, (ncap - tbl->cap) * sizeof(*na));
        tbl->by_index = na;
        tbl->cap = ncap;
    }
    e->slot_index = METHOD_SLOT_PACK(tbl->domain_id, tbl->count);
    tbl->by_index[tbl->count++] = e;

    HASH_ADD_KEYPTR(hh_lname, tbl->by_local_name, e->local_name, strlen(e->local_name), e);
    HASH_ADD(hh_id, tbl->by_local_id, local_id, sizeof(method_id_t), e);
    HASH_ADD_KEYPTR(hh_qname, *global_qname_root(), e->qname, strlen(e->qname), e);
    return e->slot_index;
}

method_slot method_slot_get(const char *domain, method_id_t id)
{
    if (!domain || !id) {
        return METHOD_SLOT_UNDEFINED;
    }
    struct domain_registry *reg = dreg();
    struct slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (!tbl) {
        return METHOD_SLOT_UNDEFINED;
    }
    struct slot_entry *e = NULL;
    HASH_FIND(hh_id, tbl->by_local_id, &id, sizeof(method_id_t), e);
    return e ? e->slot_index : METHOD_SLOT_UNDEFINED;
}

method_slot method_slot_by_name(const char *domain, const char *name)
{
    if (!domain || !name) {
        return METHOD_SLOT_UNDEFINED;
    }
    struct domain_registry *reg = dreg();
    struct slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (!tbl) {
        return METHOD_SLOT_UNDEFINED;
    }
    struct slot_entry *e = NULL;
    HASH_FIND(hh_lname, tbl->by_local_name, name, strlen(name), e);
    return e ? e->slot_index : METHOD_SLOT_UNDEFINED;
}

method_slot method_slot_by_qname(const char *qname)
{
    if (!qname) {
        return METHOD_SLOT_UNDEFINED;
    }
    struct slot_entry *e = NULL;
    HASH_FIND(hh_qname, *global_qname_root(), qname, strlen(qname), e);
    return e ? e->slot_index : METHOD_SLOT_UNDEFINED;
}

const char *method_slot_name(method_slot slot)
{
    if (slot == METHOD_SLOT_UNDEFINED) {
        return NULL;
    }
    uint8_t dom = METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t idx = METHOD_SLOT_INDEX_OF(slot);
    if (dom == 0 || dom >= METHOD_SLOT_MAX_DOMAINS) {
        return NULL;
    }
    struct slot_table *tbl = dreg()->by_id[dom];
    if (!tbl || idx >= tbl->count) {
        return NULL;
    }
    return tbl->by_index[idx]->qname;
}

impl_t class_dispatch_lookup(const struct class *cls, method_slot slot)
{
    if (!cls || slot == METHOD_SLOT_UNDEFINED) {
        return NULL;
    }
    uint8_t dom = METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t idx = METHOD_SLOT_INDEX_OF(slot);
    if (dom == 0 || dom >= METHOD_SLOT_MAX_DOMAINS) {
        return NULL;
    }
    const struct dispatch_slice *ds = &cls->dispatch_by_domain[dom];
    if (idx >= ds->count) {
        return NULL;
    }
    return ds->impls[idx];
}

const struct class *object_class(const struct object *obj)
{
    return obj ? obj->klass : NULL;
}

/* --- class_registry ----------------------------------------------- */

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
        if (!na) {
            return -1;
        }
        reg->by_index = na;
        reg->cap = ncap;
    }
    reg->by_index[reg->count++] = cls;
    HASH_ADD_KEYPTR(hh, reg->by_name, cls->desc->name, strlen(cls->desc->name), cls);
    return 0;
}

/* --- helpers for class_register ----------------------------------- */

static int dispatch_slice_grow(struct dispatch_slice *ds, size_t needed)
{
    if (needed <= ds->count) {
        return 0;
    }
    impl_t *nd = realloc(ds->impls, needed * sizeof(*nd));
    if (!nd) {
        return -1;
    }
    memset(nd + ds->count, 0, (needed - ds->count) * sizeof(*nd));
    ds->impls = nd;
    ds->count = needed;
    return 0;
}

static int class_inherit_dispatch(struct class *cls, const struct class *src)
{
    for (uint8_t d = 0; d < METHOD_SLOT_MAX_DOMAINS; ++d) {
        const struct dispatch_slice *sd = &src->dispatch_by_domain[d];
        if (sd->count == 0) {
            continue;
        }
        struct dispatch_slice *dd = &cls->dispatch_by_domain[d];
        if (dispatch_slice_grow(dd, sd->count) < 0) {
            return -1;
        }
        for (size_t i = 0; i < sd->count; ++i) {
            if (sd->impls[i]) {
                dd->impls[i] = sd->impls[i];
            }
        }
    }
    return 0;
}

static void class_destroy(struct class *cls)
{
    if (!cls) {
        return;
    }
    for (uint8_t d = 0; d < METHOD_SLOT_MAX_DOMAINS; ++d) {
        free(cls->dispatch_by_domain[d].impls);
    }
    free((void *)cls->mixins);
    free(cls);
}

const struct class *class_register(const struct class_descriptor *desc, const struct op *ops,
                                   size_t ops_count, const struct class *parent,
                                   const struct class *const *mixins, size_t mixin_count)
{
    if (!desc) {
        return NULL;
    }
    struct class *cls = calloc(1, sizeof(*cls));
    if (!cls) {
        return NULL;
    }
    cls->desc = desc;
    cls->parent = parent;

    if (mixin_count > 0) {
        cls->mixins = malloc(mixin_count * sizeof(*cls->mixins));
        if (!cls->mixins) {
            class_destroy(cls);
            return NULL;
        }
        memcpy((void *)cls->mixins, mixins, mixin_count * sizeof(*cls->mixins));
        cls->mixin_count = mixin_count;
    }

    size_t offset = sizeof(struct object);
    for (const struct class *p = parent; p != NULL; p = p->parent) {
        offset += p->desc->data_size;
        for (size_t m = 0; m < p->mixin_count; ++m) {
            offset += p->mixins[m]->desc->data_size;
        }
    }
    offset += desc->data_size;
    for (size_t m = 0; m < mixin_count; ++m) {
        offset += mixins[m]->desc->data_size;
    }
    cls->instance_size = offset;

    /* Inherit dispatch slices from parent, then overlay mixin slices,
     * then apply own ops — exactly the same precedence order the flat
     * design used. */
    if (parent && class_inherit_dispatch(cls, parent) < 0) {
        class_destroy(cls);
        return NULL;
    }
    for (size_t m = 0; m < mixin_count; ++m) {
        if (class_inherit_dispatch(cls, mixins[m]) < 0) {
            class_destroy(cls);
            return NULL;
        }
    }

    for (size_t i = 0; i < ops_count; ++i) {
        method_slot slot = method_slot_register(ops[i].slot_domain, ops[i].name,
                                                ops[i].method_id);
        if (slot == METHOD_SLOT_UNDEFINED) {
            /* Hard fail: skipping leaves the class without a vtable
             * entry the codegen promised. Dispatch and RPC would then
             * silently degrade — refuse to register instead. */
            class_destroy(cls);
            return NULL;
        }
        uint8_t dom = METHOD_SLOT_DOMAIN_OF(slot);
        uint32_t idx = METHOD_SLOT_INDEX_OF(slot);
        struct dispatch_slice *ds = &cls->dispatch_by_domain[dom];
        if (dispatch_slice_grow(ds, (size_t)idx + 1) < 0) {
            class_destroy(cls);
            return NULL;
        }
        ds->impls[idx] = ops[i].impl;
    }

    if (class_registry_add(cls) < 0) {
        class_destroy(cls);
        return NULL;
    }
    return cls;
}

/* --- accessor chain ----------------------------------------------- */

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
    if (!fn) {
        return;
    }
    struct accessor_node *node = calloc(1, sizeof(*node));
    if (!node) {
        return;
    }
    node->fn = fn;
    struct accessor_node **head = accessor_chain_head();
    node->next = *head;
    *head = node;
}

const struct class *class_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    struct class_registry *reg = class_registry_get();
    struct class *cls = NULL;
    HASH_FIND_STR(reg->by_name, name, cls);
    if (cls) {
        return cls;
    }
    for (struct accessor_node *n = *accessor_chain_head(); n; n = n->next) {
        const struct class *fresh = n->fn(name);
        if (fresh) {
            return fresh;
        }
    }
    return NULL;
}

void class_for_each_slot(const struct class *cls,
                         void (*cb)(const char *name, method_slot slot, void *ud),
                         void *userdata)
{
    if (!cls || !cb) {
        return;
    }
    for (uint8_t d = 0; d < METHOD_SLOT_MAX_DOMAINS; ++d) {
        const struct dispatch_slice *ds = &cls->dispatch_by_domain[d];
        for (size_t i = 0; i < ds->count; ++i) {
            if (!ds->impls[i]) {
                continue;
            }
            method_slot slot = METHOD_SLOT_PACK(d, i);
            const char *name = method_slot_name(slot);
            if (name) {
                cb(name, slot, userdata);
            }
        }
    }
}

struct object *object_alloc(const struct class *cls)
{
    if (!cls) {
        return NULL;
    }
    struct object *obj = calloc(1, cls->instance_size);
    if (!obj) {
        return NULL;
    }
    obj->klass = cls;
    return obj;
}

void object_free(struct object *obj)
{
    free(obj);
}
