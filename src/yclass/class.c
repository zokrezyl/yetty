/* Class runtime — per-domain slot tables.
 *
 * One slot_table per domain (allocated lazily on slot_table_get).
 * yetty_yclass_method_slot values pack the domain id in bits 27..24
 * and the per-domain local index in bits 23..0, so two domains can
 * share local names without colliding. The global class_registry
 * stays keyed by the qualified class name; classes carry per-domain
 * dispatch slices, indexed by the same domain id encoded in
 * yetty_yclass_method_slot.
 *
 * Every fallible entry point returns a Result. Internal helpers do
 * too, propagating via YETTY_RETURN_IF_ERR. */

#include <yclass/class.h>

#include <ut/uthash.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <stdlib.h>
#include <string.h>

struct yetty_yclass {
    const struct yetty_yclass_descriptor *desc;
    const struct yetty_yclass *parent;
    const struct yetty_yclass **mixins;
    size_t mixin_count;

    /* Per-domain dispatch. dispatch_by_domain[d].impls is a flat array
     * indexed by local_idx (i.e. YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot)).
     * count is the array length. Domains the class never touches stay at
     * count=0 / impls=NULL. */
    struct dispatch_slice {
        yetty_yclass_impl_t *impls;
        size_t count;
    } dispatch_by_domain[YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS];

    size_t instance_size;
    UT_hash_handle hh; /* keyed by desc->name */
};

struct slot_entry {
    char *qname;                       /* owned: "<domain>_<local_name>" */
    const char *local_name;            /* points into qname after the boundary */
    yetty_yclass_method_id_t local_id;
    yetty_yclass_method_slot slot_index; /* packed (domain_id, local_idx) */
    UT_hash_handle hh_lname;           /* by local_name within the per-domain table */
    UT_hash_handle hh_id;              /* by local_id within the per-domain table */
    UT_hash_handle hh_qname;           /* by qname in the global qname hash */
};

struct yetty_yclass_slot_table {
    char *domain;                     /* owned */
    uint8_t domain_id;                /* 1..YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS-1 */
    struct slot_entry **by_index;     /* local index → entry */
    size_t count;
    size_t cap;
    struct slot_entry *by_local_name; /* uthash root */
    struct slot_entry *by_local_id;   /* uthash root */
    UT_hash_handle hh_dom;            /* by domain string in the registry */
};

struct domain_registry {
    struct yetty_yclass_slot_table *by_id[YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS];
    struct yetty_yclass_slot_table *by_name; /* uthash by domain */
    uint8_t next_id;                         /* next free id (1..) */
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

struct yetty_yclass_slot_table_ptr_result yetty_yclass_slot_table_get(const char *domain)
{
    ydebug("domain=%s", domain ? domain : "(null)");
    if (!domain) {
        return YETTY_ERR(yetty_yclass_slot_table_ptr, "slot_table_get: NULL domain");
    }
    struct domain_registry *reg = dreg();
    struct yetty_yclass_slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (tbl) {
        return YETTY_OK(yetty_yclass_slot_table_ptr, tbl);
    }
    if (reg->next_id >= YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS) {
        return YETTY_ERR(yetty_yclass_slot_table_ptr,
                         "slot_table_get: domain id capacity exhausted");
    }
    tbl = calloc(1, sizeof(*tbl));
    if (!tbl) {
        return YETTY_ERR(yetty_yclass_slot_table_ptr,
                         "slot_table_get: calloc(slot_table) failed");
    }
    tbl->domain = strdup(domain);
    if (!tbl->domain) {
        free(tbl);
        return YETTY_ERR(yetty_yclass_slot_table_ptr,
                         "slot_table_get: strdup(domain) failed");
    }
    tbl->domain_id = reg->next_id++;
    reg->by_id[tbl->domain_id] = tbl;
    HASH_ADD_KEYPTR(hh_dom, reg->by_name, tbl->domain, strlen(tbl->domain), tbl);
    return YETTY_OK(yetty_yclass_slot_table_ptr, tbl);
}

struct yetty_yclass_method_slot_result
yetty_yclass_method_slot_register(const char *domain, const char *name,
                                  yetty_yclass_method_id_t id)
{
    ydebug("domain=%s name=%s id=%p", domain ? domain : "(null)", name ? name : "(null)",
           (void *)id);
    if (!domain || !name) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_register: NULL domain or name");
    }
    struct yetty_yclass_slot_table_ptr_result tbl_r = yetty_yclass_slot_table_get(domain);
    YETTY_RETURN_IF_ERR(yetty_yclass_method_slot, tbl_r,
                        "method_slot_register: slot_table_get failed");
    struct yetty_yclass_slot_table *tbl = tbl_r.value;

    struct slot_entry *e = NULL;
    HASH_FIND(hh_lname, tbl->by_local_name, name, strlen(name), e);
    if (e) {
        return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
    }

    e = calloc(1, sizeof(*e));
    if (!e) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_register: calloc(slot_entry) failed");
    }

    size_t dom_len = strlen(tbl->domain);
    size_t loc_len = strlen(name);
    e->qname = malloc(dom_len + 1 + loc_len + 1);
    if (!e->qname) {
        free(e);
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_register: malloc(qname) failed");
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
            return YETTY_ERR(yetty_yclass_method_slot,
                             "method_slot_register: realloc(by_index) failed");
        }
        memset(na + tbl->cap, 0, (ncap - tbl->cap) * sizeof(*na));
        tbl->by_index = na;
        tbl->cap = ncap;
    }
    e->slot_index = YETTY_YCLASS_METHOD_SLOT_PACK(tbl->domain_id, tbl->count);
    tbl->by_index[tbl->count++] = e;

    HASH_ADD_KEYPTR(hh_lname, tbl->by_local_name, e->local_name, strlen(e->local_name), e);
    HASH_ADD(hh_id, tbl->by_local_id, local_id, sizeof(yetty_yclass_method_id_t), e);
    HASH_ADD_KEYPTR(hh_qname, *global_qname_root(), e->qname, strlen(e->qname), e);
    return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
}

struct yetty_yclass_method_slot_result
yetty_yclass_method_slot_get(const char *domain, yetty_yclass_method_id_t id)
{
    ydebug("domain=%s id=%p", domain ? domain : "(null)", (void *)id);
    if (!domain || !id) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_get: NULL domain or id");
    }
    struct domain_registry *reg = dreg();
    struct yetty_yclass_slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (!tbl) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_get: domain not registered");
    }
    struct slot_entry *e = NULL;
    HASH_FIND(hh_id, tbl->by_local_id, &id, sizeof(yetty_yclass_method_id_t), e);
    if (!e) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_get: id not in domain's slot table");
    }
    return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
}

struct yetty_yclass_method_slot_result
yetty_yclass_method_slot_by_name(const char *domain, const char *name)
{
    ydebug("domain=%s name=%s", domain ? domain : "(null)", name ? name : "(null)");
    if (!domain || !name) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_by_name: NULL domain or name");
    }
    struct domain_registry *reg = dreg();
    struct yetty_yclass_slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (!tbl) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_by_name: domain not registered");
    }
    struct slot_entry *e = NULL;
    HASH_FIND(hh_lname, tbl->by_local_name, name, strlen(name), e);
    if (!e) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_by_name: name not in domain's slot table");
    }
    return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
}

struct yetty_yclass_method_slot_result yetty_yclass_method_slot_by_qname(const char *qname)
{
    ydebug("qname=%s", qname ? qname : "(null)");
    if (!qname) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_by_qname: NULL qname");
    }
    struct slot_entry *e = NULL;
    HASH_FIND(hh_qname, *global_qname_root(), qname, strlen(qname), e);
    if (!e) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_by_qname: qname not registered");
    }
    return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
}

struct yetty_yclass_const_char_ptr_result
yetty_yclass_method_slot_name(yetty_yclass_method_slot slot)
{
    ydebug("slot=0x%08x", slot);
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_yclass_const_char_ptr,
                         "method_slot_name: METHOD_SLOT_UNDEFINED");
    }
    uint8_t dom = YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t idx = YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot);
    if (dom == 0 || dom >= YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "method_slot_name: invalid domain id");
    }
    struct yetty_yclass_slot_table *tbl = dreg()->by_id[dom];
    if (!tbl || idx >= tbl->count) {
        return YETTY_ERR(yetty_yclass_const_char_ptr,
                         "method_slot_name: slot index out of range");
    }
    return YETTY_OK(yetty_yclass_const_char_ptr, tbl->by_index[idx]->qname);
}

yetty_yclass_impl_t yetty_yclass_dispatch_lookup(const struct yetty_yclass *cls,
                                                 yetty_yclass_method_slot slot)
{
    /* Not Result — "this class does not override this slot" is a
     * normal flow, not an error. Returns NULL to indicate that. */
    if (!cls || slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return NULL;
    }
    uint8_t dom = YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t idx = YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot);
    if (dom == 0 || dom >= YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS) {
        return NULL;
    }
    const struct dispatch_slice *ds = &cls->dispatch_by_domain[dom];
    if (idx >= ds->count) {
        return NULL;
    }
    return ds->impls[idx];
}

const struct yetty_yclass *yetty_yclass_object_class(const struct yetty_yclass_object *obj)
{
    return obj ? obj->klass : NULL;
}

/* --- class_registry ----------------------------------------------- */

struct class_registry {
    struct yetty_yclass **by_index;
    size_t count;
    size_t cap;
    struct yetty_yclass *by_name;
};

static struct class_registry *class_registry_get(void)
{
    static struct class_registry reg = {0};
    return &reg;
}

static struct yetty_ycore_void_result class_registry_add(struct yetty_yclass *cls)
{
    ydebug("cls=%s", cls && cls->desc ? cls->desc->name : "(null)");
    struct class_registry *reg = class_registry_get();
    if (reg->count == reg->cap) {
        size_t ncap = reg->cap ? reg->cap * 2 : 16;
        struct yetty_yclass **na = realloc(reg->by_index, ncap * sizeof(*na));
        if (!na) {
            return YETTY_ERR(yetty_ycore_void, "class_registry_add: realloc failed");
        }
        reg->by_index = na;
        reg->cap = ncap;
    }
    reg->by_index[reg->count++] = cls;
    HASH_ADD_KEYPTR(hh, reg->by_name, cls->desc->name, strlen(cls->desc->name), cls);
    return YETTY_OK_VOID();
}

/* --- helpers for yetty_yclass_register ---------------------------- */

static struct yetty_ycore_void_result dispatch_slice_grow(struct dispatch_slice *ds,
                                                          size_t needed)
{
    if (needed <= ds->count) {
        return YETTY_OK_VOID();
    }
    yetty_yclass_impl_t *nd = realloc(ds->impls, needed * sizeof(*nd));
    if (!nd) {
        return YETTY_ERR(yetty_ycore_void, "dispatch_slice_grow: realloc failed");
    }
    memset(nd + ds->count, 0, (needed - ds->count) * sizeof(*nd));
    ds->impls = nd;
    ds->count = needed;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result class_inherit_dispatch(struct yetty_yclass *cls,
                                                             const struct yetty_yclass *src)
{
    for (uint8_t d = 0; d < YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS; ++d) {
        const struct dispatch_slice *sd = &src->dispatch_by_domain[d];
        if (sd->count == 0) {
            continue;
        }
        struct dispatch_slice *dd = &cls->dispatch_by_domain[d];
        struct yetty_ycore_void_result g = dispatch_slice_grow(dd, sd->count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, g, "class_inherit_dispatch: grow failed");
        for (size_t i = 0; i < sd->count; ++i) {
            if (sd->impls[i]) {
                dd->impls[i] = sd->impls[i];
            }
        }
    }
    return YETTY_OK_VOID();
}

static void class_destroy(struct yetty_yclass *cls)
{
    if (!cls) {
        return;
    }
    for (uint8_t d = 0; d < YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS; ++d) {
        free(cls->dispatch_by_domain[d].impls);
    }
    free((void *)cls->mixins);
    free(cls);
}

struct yetty_yclass_ptr_result yetty_yclass_register(const struct yetty_yclass_descriptor *desc,
                                                     const struct yetty_yclass_op *ops,
                                                     size_t ops_count,
                                                     const struct yetty_yclass *parent,
                                                     const struct yetty_yclass *const *mixins,
                                                     size_t mixin_count)
{
    ydebug("class=%s ops=%zu parent=%s mixins=%zu",
           desc && desc->name ? desc->name : "(null)", ops_count,
           parent && parent->desc ? parent->desc->name : "(none)", mixin_count);
    if (!desc) {
        return YETTY_ERR(yetty_yclass_ptr, "class_register: NULL descriptor");
    }
    struct yetty_yclass *cls = calloc(1, sizeof(*cls));
    if (!cls) {
        return YETTY_ERR(yetty_yclass_ptr, "class_register: calloc(class) failed");
    }
    cls->desc = desc;
    cls->parent = parent;

    if (mixin_count > 0) {
        cls->mixins = malloc(mixin_count * sizeof(*cls->mixins));
        if (!cls->mixins) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: malloc(mixins) failed");
        }
        memcpy((void *)cls->mixins, mixins, mixin_count * sizeof(*cls->mixins));
        cls->mixin_count = mixin_count;
    }

    size_t offset = sizeof(struct yetty_yclass_object);
    for (const struct yetty_yclass *p = parent; p != NULL; p = p->parent) {
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

    if (parent) {
        struct yetty_ycore_void_result inh = class_inherit_dispatch(cls, parent);
        if (YETTY_IS_ERR(inh)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr,
                             "class_register: inherit parent dispatch failed", inh);
        }
    }
    for (size_t m = 0; m < mixin_count; ++m) {
        struct yetty_ycore_void_result inh = class_inherit_dispatch(cls, mixins[m]);
        if (YETTY_IS_ERR(inh)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr,
                             "class_register: inherit mixin dispatch failed", inh);
        }
    }

    for (size_t i = 0; i < ops_count; ++i) {
        struct yetty_yclass_method_slot_result sr = yetty_yclass_method_slot_register(
            ops[i].slot_domain, ops[i].name, ops[i].method_id);
        if (YETTY_IS_ERR(sr)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr,
                             "class_register: method_slot_register failed", sr);
        }
        yetty_yclass_method_slot slot = sr.value;
        uint8_t dom = YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(slot);
        uint32_t idx = YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot);
        struct dispatch_slice *ds = &cls->dispatch_by_domain[dom];
        struct yetty_ycore_void_result g = dispatch_slice_grow(ds, (size_t)idx + 1);
        if (YETTY_IS_ERR(g)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr,
                             "class_register: dispatch slice grow failed", g);
        }
        ds->impls[idx] = ops[i].impl;
    }

    struct yetty_ycore_void_result a = class_registry_add(cls);
    if (YETTY_IS_ERR(a)) {
        class_destroy(cls);
        return YETTY_ERR(yetty_yclass_ptr, "class_register: class_registry_add failed", a);
    }
    return YETTY_OK(yetty_yclass_ptr, cls);
}

/* --- accessor chain ----------------------------------------------- */

struct accessor_node {
    yetty_yclass_accessor_lookup_fn fn;
    struct accessor_node *next;
};

static struct accessor_node **accessor_chain_head(void)
{
    static struct accessor_node *head = NULL;
    return &head;
}

struct yetty_ycore_void_result
yetty_yclass_add_accessor_lookup(yetty_yclass_accessor_lookup_fn fn)
{
    ydebug("fn=%p", (void *)(uintptr_t)fn);
    if (!fn) {
        return YETTY_ERR(yetty_ycore_void, "class_add_accessor_lookup: NULL fn");
    }
    struct accessor_node *node = calloc(1, sizeof(*node));
    if (!node) {
        return YETTY_ERR(yetty_ycore_void, "class_add_accessor_lookup: calloc failed");
    }
    node->fn = fn;
    struct accessor_node **head = accessor_chain_head();
    node->next = *head;
    *head = node;
    return YETTY_OK_VOID();
}

struct yetty_yclass_ptr_result yetty_yclass_by_name(const char *name)
{
    ydebug("name=%s", name ? name : "(null)");
    if (!name) {
        return YETTY_ERR(yetty_yclass_ptr, "class_by_name: NULL name");
    }
    struct class_registry *reg = class_registry_get();
    struct yetty_yclass *cls = NULL;
    HASH_FIND_STR(reg->by_name, name, cls);
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    /* Registry miss → walk the per-module accessor chain. Each hook
     * returns a Result; an OK-value-NULL means "not mine, try next".
     * Hook errors propagate immediately. */
    for (struct accessor_node *n = *accessor_chain_head(); n; n = n->next) {
        struct yetty_yclass_ptr_result r = n->fn(name);
        if (YETTY_IS_ERR(r)) {
            return YETTY_ERR(yetty_yclass_ptr, "class_by_name: accessor hook failed", r);
        }
        if (r.value) {
            return r;
        }
    }
    return YETTY_ERR(yetty_yclass_ptr,
                     "class_by_name: class not found in registry or any hook");
}

void yetty_yclass_for_each_slot(const struct yetty_yclass *cls,
                                void (*cb)(const char *name, yetty_yclass_method_slot slot,
                                           void *ud),
                                void *userdata)
{
    ydebug("cls=%s", cls && cls->desc ? cls->desc->name : "(null)");
    if (!cls || !cb) {
        return;
    }
    for (uint8_t d = 0; d < YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS; ++d) {
        const struct dispatch_slice *ds = &cls->dispatch_by_domain[d];
        for (size_t i = 0; i < ds->count; ++i) {
            if (!ds->impls[i]) {
                continue;
            }
            yetty_yclass_method_slot slot = YETTY_YCLASS_METHOD_SLOT_PACK(d, i);
            struct yetty_yclass_const_char_ptr_result nr =
                yetty_yclass_method_slot_name(slot);
            if (YETTY_IS_OK(nr) && nr.value) {
                cb(nr.value, slot, userdata);
            } else if (YETTY_IS_ERR(nr)) {
                yetty_ycore_error_destroy(nr.error);
            }
        }
    }
}

struct yetty_yclass_object_ptr_result yetty_yclass_object_alloc(const struct yetty_yclass *cls)
{
    ydebug("cls=%s size=%zu", cls && cls->desc ? cls->desc->name : "(null)",
           cls ? cls->instance_size : 0);
    if (!cls) {
        return YETTY_ERR(yetty_yclass_object_ptr, "object_alloc: NULL class");
    }
    struct yetty_yclass_object *obj = calloc(1, cls->instance_size);
    if (!obj) {
        return YETTY_ERR(yetty_yclass_object_ptr, "object_alloc: calloc failed");
    }
    obj->klass = cls;
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

void yetty_yclass_object_free(struct yetty_yclass_object *obj)
{
    ydebug("obj=%p", (void *)obj);
    free(obj);
}
