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

#include <yetty/yclass/class.h>

#include <ut/uthash.h>
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Saturating add — returns true on overflow without writing *out.
 * Used in every place where a size_t accumulator could wrap and end
 * up smaller than the constituent values (instance_size summation,
 * qname buffer sizing). Cheaper than __builtin_add_overflow gates
 * across compilers. */
static inline _Bool size_add_check(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return 1;
    }
    *out = a + b;
    return 0;
}

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
    struct data_slice {
        const struct yetty_yclass *cls;
        size_t offset;
    } *data_slices;
    size_t data_slice_count;
    UT_hash_handle hh; /* keyed by desc->name */
};

struct slot_entry {
    char *qname;            /* owned: "<domain>_<local_name>" */
    const char *local_name; /* points into qname after the boundary */
    yetty_yclass_method_id_t local_id;
    yetty_yclass_method_slot slot_index; /* packed (domain_id, local_idx) */
    UT_hash_handle hh_lname;             /* by local_name within the per-domain table */
    UT_hash_handle hh_id;                /* by local_id within the per-domain table */
    UT_hash_handle hh_qname;             /* by qname in the global qname hash */
};

struct yetty_yclass_slot_table {
    char *domain;                 /* owned */
    uint8_t domain_id;            /* 1..YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS-1 */
    struct slot_entry **by_index; /* local index → entry */
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
        return YETTY_ERR(yetty_yclass_slot_table_ptr, "slot_table_get: calloc(slot_table) failed");
    }
    tbl->domain = strdup(domain);
    if (!tbl->domain) {
        free(tbl);
        return YETTY_ERR(yetty_yclass_slot_table_ptr, "slot_table_get: strdup(domain) failed");
    }
    tbl->domain_id = reg->next_id++;
    reg->by_id[tbl->domain_id] = tbl;
    HASH_ADD_KEYPTR(hh_dom, reg->by_name, tbl->domain, strlen(tbl->domain), tbl);
    return YETTY_OK(yetty_yclass_slot_table_ptr, tbl);
}

struct yetty_yclass_method_slot_result yetty_yclass_method_slot_register(
    const char *domain, const char *name, yetty_yclass_method_id_t id)
{
    ydebug("domain=%s name=%s id=%p", domain ? domain : "(null)", name ? name : "(null)",
           (void *)id);
    if (!domain || !name) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_register: NULL domain or name");
    }
    /* `id` is the public-stub fn-ptr used as the by_local_id hash key.
     * method_slot_get rejects NULL ids, so accepting NULL here would
     * create a slot that can't be located through the dispatch path —
     * silently broken at first call. Reject at registration time. */
    if (!id) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_register: NULL method_id_t");
    }
    struct yetty_yclass_slot_table_ptr_result tbl_r = yetty_yclass_slot_table_get(domain);
    YETTY_RETURN_IF_ERR(yetty_yclass_method_slot, tbl_r,
                        "method_slot_register: slot_table_get failed");
    struct yetty_yclass_slot_table *tbl = tbl_r.value;

    struct slot_entry *e = NULL;
    HASH_FIND(hh_lname, tbl->by_local_name, name, strlen(name), e);
    if (e) {
        /* (domain, name) is registered idempotently — calling twice
         * with the same id returns the same slot. But a SECOND call
         * with a DIFFERENT method_id_t is a contract violation: the
         * by_local_id table still only contains the original id, so
         * a later method_slot_get(domain, new_id) would silently miss
         * while dispatch through the old id keeps working. Reject so
         * the build / startup fails loudly instead. */
        if (e->local_id != id) {
            return YETTY_ERR(yetty_yclass_method_slot,
                             "method_slot_register: (domain,name) already bound to a "
                             "different method_id_t — stale generated file or duplicate "
                             "registration?");
        }
        return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
    }

    e = calloc(1, sizeof(*e));
    if (!e) {
        return YETTY_ERR(yetty_yclass_method_slot,
                         "method_slot_register: calloc(slot_entry) failed");
    }

    size_t dom_len = strlen(tbl->domain);
    size_t loc_len = strlen(name);
    /* Checked size for qname buffer: dom_len + 1('_') + loc_len + 1(NUL).
     * Without this, pathological inputs could wrap size_t and trigger
     * a tiny malloc followed by a huge memcpy. */
    size_t qname_size = 0;
    if (size_add_check(dom_len, 1, &qname_size) ||
        size_add_check(qname_size, loc_len, &qname_size) ||
        size_add_check(qname_size, 1, &qname_size)) {
        free(e);
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_register: qname size overflow");
    }
    e->qname = malloc(qname_size);
    if (!e->qname) {
        free(e);
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_register: malloc(qname) failed");
    }
    memcpy(e->qname, tbl->domain, dom_len);
    e->qname[dom_len] = '_';
    memcpy(e->qname + dom_len + 1, name, loc_len + 1);
    e->local_name = e->qname + dom_len + 1;
    e->local_id = id;

    if (tbl->count >= tbl->cap) {
        size_t ncap = tbl->cap ? tbl->cap * 2 : 32;
        /* Doubling can overflow size_t for pathological tbl->cap.
         * Also gate the eventual ncap * sizeof so we don't realloc(0). */
        if (tbl->cap && ncap / 2 != tbl->cap) {
            free(e->qname);
            free(e);
            return YETTY_ERR(yetty_yclass_method_slot,
                             "method_slot_register: by_index cap doubling overflowed");
        }
        while (ncap <= tbl->count) {
            size_t next = ncap * 2;
            if (next / 2 != ncap) {
                free(e->qname);
                free(e);
                return YETTY_ERR(yetty_yclass_method_slot,
                                 "method_slot_register: by_index cap growth overflowed");
            }
            ncap = next;
        }
        if (ncap > SIZE_MAX / sizeof(struct slot_entry *)) {
            free(e->qname);
            free(e);
            return YETTY_ERR(yetty_yclass_method_slot,
                             "method_slot_register: by_index byte size overflow");
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

struct yetty_yclass_method_slot_result yetty_yclass_method_slot_get(const char *domain,
                                                                    yetty_yclass_method_id_t id)
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

struct yetty_yclass_method_slot_result yetty_yclass_method_slot_by_name(const char *domain,
                                                                        const char *name)
{
    ydebug("domain=%s name=%s", domain ? domain : "(null)", name ? name : "(null)");
    if (!domain || !name) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_by_name: NULL domain or name");
    }
    struct domain_registry *reg = dreg();
    struct yetty_yclass_slot_table *tbl = NULL;
    HASH_FIND(hh_dom, reg->by_name, domain, strlen(domain), tbl);
    if (!tbl) {
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_by_name: domain not registered");
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
        return YETTY_ERR(yetty_yclass_method_slot, "method_slot_by_qname: qname not registered");
    }
    return YETTY_OK(yetty_yclass_method_slot, e->slot_index);
}

struct yetty_yclass_const_char_ptr_result yetty_yclass_method_slot_name(
    yetty_yclass_method_slot slot)
{
    ydebug("slot=0x%08x", slot);
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "method_slot_name: METHOD_SLOT_UNDEFINED");
    }
    uint8_t dom = YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t idx = YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot);
    if (dom == 0 || dom >= YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "method_slot_name: invalid domain id");
    }
    struct yetty_yclass_slot_table *tbl = dreg()->by_id[dom];
    if (!tbl || idx >= tbl->count) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "method_slot_name: slot index out of range");
    }
    return YETTY_OK(yetty_yclass_const_char_ptr, tbl->by_index[idx]->qname);
}

struct yetty_yclass_impl_t_result yetty_yclass_dispatch_lookup(const struct yetty_yclass *cls,
                                                               yetty_yclass_method_slot slot)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_impl_t, "dispatch_lookup: NULL class");
    }
    if (slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        return YETTY_ERR(yetty_yclass_impl_t, "dispatch_lookup: METHOD_SLOT_UNDEFINED");
    }
    uint8_t dom = YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(slot);
    uint32_t idx = YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot);
    if (dom == 0 || dom >= YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS) {
        return YETTY_ERR(yetty_yclass_impl_t, "dispatch_lookup: invalid slot domain id");
    }
    const struct dispatch_slice *ds = &cls->dispatch_by_domain[dom];
    if (idx >= ds->count) {
        return YETTY_ERR(yetty_yclass_impl_t,
                         "dispatch_lookup: slot index out of class's dispatch range");
    }
    if (!ds->impls[idx]) {
        return YETTY_ERR(yetty_yclass_impl_t,
                         "dispatch_lookup: class does not implement this slot");
    }
    return YETTY_OK(yetty_yclass_impl_t, ds->impls[idx]);
}

struct yetty_yclass_ptr_result yetty_yclass_object_class(const struct yetty_yclass_object *obj)
{
    if (!obj) {
        return YETTY_ERR(yetty_yclass_ptr, "object_class: NULL object");
    }
    if (!obj->klass) {
        return YETTY_ERR(yetty_yclass_ptr, "object_class: object has no class");
    }
    return YETTY_OK(yetty_yclass_ptr, obj->klass);
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
    /* Reject duplicate qualified names. Generated accessors gate
     * themselves with a static cache and won't repeat, but direct API
     * use or two modules accidentally minting the same qualified name
     * would otherwise leave duplicate hash entries and ambiguous
     * yetty_yclass_by_name results. */
    struct yetty_yclass *existing = NULL;
    HASH_FIND_STR(reg->by_name, cls->desc->name, existing);
    if (existing) {
        return YETTY_ERR(yetty_ycore_void, "class_registry_add: class with this qualified name "
                                           "is already registered");
    }
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

static struct yetty_ycore_void_result dispatch_slice_grow(struct dispatch_slice *ds, size_t needed)
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
    free(cls->data_slices);
    free(cls);
}

static struct yetty_ycore_void_result class_add_data_slice(struct yetty_yclass *cls,
                                                           const struct yetty_yclass *slice_cls,
                                                           size_t offset)
{
    struct data_slice *slices =
        realloc(cls->data_slices, (cls->data_slice_count + 1) * sizeof(*slices));
    if (!slices) {
        return YETTY_ERR(yetty_ycore_void, "class_add_data_slice: realloc failed");
    }
    cls->data_slices = slices;
    cls->data_slices[cls->data_slice_count++] = (struct data_slice){
        .cls = slice_cls,
        .offset = offset,
    };
    return YETTY_OK_VOID();
}

struct yetty_yclass_ptr_result yetty_yclass_register(
    const struct yetty_yclass_descriptor *desc, const struct yetty_yclass_op *ops, size_t ops_count,
    const struct yetty_yclass *parent, const struct yetty_yclass *const *mixins, size_t mixin_count)
{
    ydebug("class=%s ops=%zu parent=%s mixins=%zu", desc && desc->name ? desc->name : "(null)",
           ops_count, parent && parent->desc ? parent->desc->name : "(none)", mixin_count);
    if (!desc) {
        return YETTY_ERR(yetty_yclass_ptr, "class_register: NULL descriptor");
    }
    /* desc->name is used as the registry hash key (class_registry_add)
     * and printed in error messages. ops/mixins arrays are dereferenced
     * up to their count below. Reject NULL-with-count combos here
     * rather than crashing in the loops further down — this is the
     * exported runtime API and must not assume generator discipline. */
    if (!desc->name) {
        return YETTY_ERR(yetty_yclass_ptr, "class_register: descriptor missing name");
    }
    if (ops_count > 0 && !ops) {
        return YETTY_ERR(yetty_yclass_ptr, "class_register: ops_count > 0 but ops is NULL");
    }
    if (mixin_count > 0 && !mixins) {
        return YETTY_ERR(yetty_yclass_ptr, "class_register: mixin_count > 0 but mixins is NULL");
    }
    /* NULL mixin element would be dereferenced later in instance_size
     * accumulation and class_inherit_dispatch — reject up front. */
    for (size_t m = 0; m < mixin_count; ++m) {
        if (!mixins[m]) {
            return YETTY_ERR(yetty_yclass_ptr, "class_register: NULL mixin element");
        }
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

    /* Sum the per-frame data sizes (parent chain + their mixins + own
     * data + own mixins) into instance_size. Each += is overflow-
     * checked so a pathological data_size doesn't wrap and produce a
     * too-small object_alloc later. */
    size_t offset = sizeof(struct yetty_yclass_object);
    for (const struct yetty_yclass *p = parent; p != NULL; p = p->parent) {
        struct yetty_ycore_void_result so = class_add_data_slice(cls, p, offset);
        if (YETTY_IS_ERR(so)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: add parent data slice failed", so);
        }
        if (size_add_check(offset, p->desc->data_size, &offset)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr,
                             "class_register: instance_size overflow (parent data)");
        }
        for (size_t m = 0; m < p->mixin_count; ++m) {
            so = class_add_data_slice(cls, p->mixins[m], offset);
            if (YETTY_IS_ERR(so)) {
                class_destroy(cls);
                return YETTY_ERR(yetty_yclass_ptr,
                                 "class_register: add parent mixin data slice failed", so);
            }
            if (size_add_check(offset, p->mixins[m]->desc->data_size, &offset)) {
                class_destroy(cls);
                return YETTY_ERR(yetty_yclass_ptr,
                                 "class_register: instance_size overflow (parent mixin)");
            }
        }
    }
    struct yetty_ycore_void_result so = class_add_data_slice(cls, cls, offset);
    if (YETTY_IS_ERR(so)) {
        class_destroy(cls);
        return YETTY_ERR(yetty_yclass_ptr, "class_register: add own data slice failed", so);
    }
    if (size_add_check(offset, desc->data_size, &offset)) {
        class_destroy(cls);
        return YETTY_ERR(yetty_yclass_ptr, "class_register: instance_size overflow (own data)");
    }
    for (size_t m = 0; m < mixin_count; ++m) {
        so = class_add_data_slice(cls, mixins[m], offset);
        if (YETTY_IS_ERR(so)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: add own mixin data slice failed",
                             so);
        }
        if (size_add_check(offset, mixins[m]->desc->data_size, &offset)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr,
                             "class_register: instance_size overflow (own mixin)");
        }
    }
    cls->instance_size = offset;

    if (parent) {
        struct yetty_ycore_void_result inh = class_inherit_dispatch(cls, parent);
        if (YETTY_IS_ERR(inh)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: inherit parent dispatch failed",
                             inh);
        }
    }
    for (size_t m = 0; m < mixin_count; ++m) {
        struct yetty_ycore_void_result inh = class_inherit_dispatch(cls, mixins[m]);
        if (YETTY_IS_ERR(inh)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: inherit mixin dispatch failed",
                             inh);
        }
    }

    for (size_t i = 0; i < ops_count; ++i) {
        /* `op` means "this class overrides this slot with this impl".
         * A NULL impl is a contract violation — it would register the
         * slot, then dispatch as "no impl" (indistinguishable from
         * pure inheritance) and silently drop out of for_each_slot.
         * Reject up front. */
        if (!ops[i].impl) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: op has NULL impl");
        }
        struct yetty_yclass_method_slot_result sr =
            yetty_yclass_method_slot_register(ops[i].slot_domain, ops[i].name, ops[i].method_id);
        if (YETTY_IS_ERR(sr)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: method_slot_register failed", sr);
        }
        yetty_yclass_method_slot slot = sr.value;
        uint8_t dom = YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(slot);
        uint32_t idx = YETTY_YCLASS_METHOD_SLOT_INDEX_OF(slot);
        struct dispatch_slice *ds = &cls->dispatch_by_domain[dom];
        struct yetty_ycore_void_result g = dispatch_slice_grow(ds, (size_t)idx + 1);
        if (YETTY_IS_ERR(g)) {
            class_destroy(cls);
            return YETTY_ERR(yetty_yclass_ptr, "class_register: dispatch slice grow failed", g);
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

struct yetty_ycore_void_result yetty_yclass_add_accessor_lookup(yetty_yclass_accessor_lookup_fn fn)
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
    return YETTY_ERR(yetty_yclass_ptr, "class_by_name: class not found in registry or any hook");
}

struct yetty_ycore_void_result yetty_yclass_for_each_slot(
    const struct yetty_yclass *cls,
    void (*cb)(const char *name, yetty_yclass_method_slot slot, void *ud), void *userdata)
{
    ydebug("cls=%s", cls && cls->desc ? cls->desc->name : "(null)");
    if (!cls) {
        return YETTY_ERR(yetty_ycore_void, "for_each_slot: NULL cls");
    }
    if (!cb) {
        return YETTY_ERR(yetty_ycore_void, "for_each_slot: NULL cb");
    }
    for (uint8_t d = 0; d < YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS; ++d) {
        const struct dispatch_slice *ds = &cls->dispatch_by_domain[d];
        for (size_t i = 0; i < ds->count; ++i) {
            if (!ds->impls[i]) {
                continue;
            }
            yetty_yclass_method_slot slot = YETTY_YCLASS_METHOD_SLOT_PACK(d, i);
            struct yetty_yclass_const_char_ptr_result nr = yetty_yclass_method_slot_name(slot);
            /* Bail on first slot-name lookup failure rather than
             * silently skipping the slot. Previously, GET_CLASS
             * could ship a class map missing entries and report
             * success, leaving the client with no remote-id mapping
             * for the dropped slots — only recoverable via per-slot
             * RESOLVE_SLOT round-trips (slow) or never (silent). */
            YETTY_RETURN_IF_ERR(yetty_ycore_void, nr,
                                "for_each_slot: method_slot_name failed mid-walk");
            if (!nr.value) {
                return YETTY_ERR(yetty_ycore_void,
                                 "for_each_slot: method_slot_name returned OK with NULL");
            }
            cb(nr.value, slot, userdata);
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_yclass_ptr_result yetty_yclass_parent(const struct yetty_yclass *cls)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_ptr, "yclass_parent: NULL cls");
    }
    return YETTY_OK(yetty_yclass_ptr, cls->parent);
}

struct yetty_ycore_size_result yetty_yclass_mixin_count(const struct yetty_yclass *cls)
{
    if (!cls) {
        return YETTY_ERR(yetty_ycore_size, "yclass_mixin_count: NULL cls");
    }
    return YETTY_OK(yetty_ycore_size, cls->mixin_count);
}

struct yetty_yclass_ptr_result yetty_yclass_mixin_at(const struct yetty_yclass *cls, size_t index)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_ptr, "yclass_mixin_at: NULL cls");
    }
    if (index >= cls->mixin_count) {
        return YETTY_ERR(yetty_yclass_ptr, "yclass_mixin_at: index out of range");
    }
    return YETTY_OK(yetty_yclass_ptr, cls->mixins[index]);
}

struct yetty_ycore_size_result yetty_yclass_data_size(const struct yetty_yclass *cls)
{
    if (!cls) {
        return YETTY_ERR(yetty_ycore_size, "yclass_data_size: NULL cls");
    }
    if (!cls->desc) {
        return YETTY_ERR(yetty_ycore_size, "yclass_data_size: class has no descriptor");
    }
    return YETTY_OK(yetty_ycore_size, cls->desc->data_size);
}

struct yetty_yclass_const_char_ptr_result yetty_yclass_name(const struct yetty_yclass *cls)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "yclass_name: NULL cls");
    }
    if (!cls->desc || !cls->desc->name) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "yclass_name: missing name");
    }
    return YETTY_OK(yetty_yclass_const_char_ptr, cls->desc->name);
}

struct yetty_yclass_const_char_ptr_result yetty_yclass_type_str(const struct yetty_yclass *cls)
{
    if (!cls) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "yclass_type_str: NULL cls");
    }
    if (!cls->desc) {
        return YETTY_ERR(yetty_yclass_const_char_ptr, "yclass_type_str: NULL descriptor");
    }
    const char *s = (cls->desc->type == YETTY_YCLASS_TYPE_MIXIN) ? "mixin" : "regular";
    return YETTY_OK(yetty_yclass_const_char_ptr, s);
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

struct yetty_ycore_void_result yetty_yclass_object_free(struct yetty_yclass_object *obj)
{
    ydebug("obj=%p", (void *)obj);
    free(obj);
    return YETTY_OK_VOID();
}

static size_t class_data_offset(const struct yetty_yclass *leaf, const struct yetty_yclass *target)
{
    if (!leaf || !target) {
        return SIZE_MAX;
    }
    for (size_t i = 0; i < leaf->data_slice_count; ++i) {
        if (leaf->data_slices[i].cls == target) {
            return leaf->data_slices[i].offset;
        }
    }
    return SIZE_MAX;
}

struct yetty_ycore_size_result yetty_yclass_object_data_offset(const struct yetty_yclass *leaf,
                                                               const struct yetty_yclass *cls)
{
    size_t offset = class_data_offset(leaf, cls);
    if (offset == SIZE_MAX) {
        return YETTY_ERR(yetty_ycore_size,
                         "yclass_object_data_offset: class not in leaf object's chain");
    }
    return YETTY_OK(yetty_ycore_size, offset);
}
struct yetty_yclass_void_ptr_result yetty_yclass_object_data(struct yetty_yclass_object *obj,
                                                             const struct yetty_yclass *cls)
{
    if (!obj || !obj->klass) {
        return YETTY_ERR(yetty_yclass_void_ptr, "yclass_object_data: NULL object");
    }
    if (!cls) {
        return YETTY_ERR(yetty_yclass_void_ptr, "yclass_object_data: NULL class");
    }
    size_t offset = class_data_offset(obj->klass, cls);
    if (offset == SIZE_MAX) {
        return YETTY_ERR(yetty_yclass_void_ptr, "yclass_object_data: class not in object's chain");
    }
    if (offset > obj->klass->instance_size ||
        cls->desc->data_size > obj->klass->instance_size - offset) {
        return YETTY_ERR(yetty_yclass_void_ptr, "yclass_object_data: data slice outside object");
    }
    return YETTY_OK(yetty_yclass_void_ptr, (char *)obj + offset);
}
