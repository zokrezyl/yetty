/* yclass — tiny class/object runtime with per-domain slot tables.
 *
 * Each module ("domain") owns its own slot_table with its own 0-based
 * local index space. A yetty_yclass_method_slot value is the packed pair
 * (domain_id << 24) | local_idx:
 *
 *     bits 31..28   reserved (always 0 for valid slots; UINT32_MAX
 *                   is the sentinel YETTY_YCLASS_METHOD_SLOT_UNDEFINED).
 *     bits 27..24   domain_id (1..15; 0 is invalid / never returned).
 *     bits 23..0    local index inside that domain's slot_table.
 *
 * The wire treats yetty_yclass_method_slot as opaque — both sides
 * exchange 28-bit ids that round-trip through this packing. The
 * encoding fits inside the rpc header's 28-bit id field.
 *
 * Every fallible runtime entry point returns a Result type from
 * <yetty/ycore/result.h>. Callers check YETTY_IS_ERR and either
 * propagate (YETTY_RETURN_IF_ERR) or absorb at a boundary. */

#ifndef YCLASS_CLASS_H
#define YCLASS_CLASS_H

#include <yetty/ycore/result.h>

#include <stddef.h>
#include <stdint.h>

struct yetty_yclass_object;
struct yetty_yclass;
struct yetty_yclass_slot_table;
struct yetty_yclass_rpc_session;

struct yetty_yclass_ctx {
    struct yetty_yclass_rpc_session *session; /* NULL → local; set → remote */
};

enum yetty_yclass_type {
    YETTY_YCLASS_TYPE_REGULAR = 0,
    YETTY_YCLASS_TYPE_MIXIN = 1,
};

typedef void (*yetty_yclass_method_id_t)(void);
typedef void (*yetty_yclass_impl_t)(void);
typedef uint32_t yetty_yclass_method_slot;
#define YETTY_YCLASS_METHOD_SLOT_UNDEFINED UINT32_MAX

#define YETTY_YCLASS_METHOD_SLOT_DOMAIN_BITS 4
#define YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS (1u << YETTY_YCLASS_METHOD_SLOT_DOMAIN_BITS)
#define YETTY_YCLASS_METHOD_SLOT_INDEX_SHIFT 24
#define YETTY_YCLASS_METHOD_SLOT_INDEX_MASK ((1u << YETTY_YCLASS_METHOD_SLOT_INDEX_SHIFT) - 1)
#define YETTY_YCLASS_METHOD_SLOT_DOMAIN_OF(s)                                                      \
    (((s) >> YETTY_YCLASS_METHOD_SLOT_INDEX_SHIFT) & 0xFu)
#define YETTY_YCLASS_METHOD_SLOT_INDEX_OF(s) ((s) & YETTY_YCLASS_METHOD_SLOT_INDEX_MASK)
#define YETTY_YCLASS_METHOD_SLOT_PACK(dom, idx)                                                    \
    (((uint32_t)(dom) << YETTY_YCLASS_METHOD_SLOT_INDEX_SHIFT) |                                   \
     ((idx) & YETTY_YCLASS_METHOD_SLOT_INDEX_MASK))

struct yetty_yclass_descriptor {
    const char *name; /* qualified, e.g. "yetty_yvehicle_sportscar" */
    enum yetty_yclass_type type;
    size_t data_size;
};

struct yetty_yclass_op {
    const char *slot_domain;            /* slot's owning module, e.g. "yvehicle" */
    const char *name;                   /* slot local name, e.g. "vehicle_describe" */
    yetty_yclass_method_id_t method_id; /* the public-stub fn ptr (vtable key) */
    yetty_yclass_impl_t impl;           /* override for this slot */
};

struct yetty_yclass_object {
    const struct yetty_yclass *klass;
};

/* --- Result types ------------------------------------------------- */
/* Per the project convention, every fallible entry point returns a
 * Result. For pointer types the type identifier suffix is `_ptr`.
 * Slot-domain return types are declared by each using module. */
YETTY_YRESULT_DECLARE(yetty_yclass_slot_table_ptr, struct yetty_yclass_slot_table *);
YETTY_YRESULT_DECLARE(yetty_yclass_ptr, const struct yetty_yclass *);
YETTY_YRESULT_DECLARE(yetty_yclass_object_ptr, struct yetty_yclass_object *);
YETTY_YRESULT_DECLARE(yetty_yclass_method_slot, yetty_yclass_method_slot);
YETTY_YRESULT_DECLARE(yetty_yclass_impl, yetty_yclass_impl_t);
YETTY_YRESULT_DECLARE(yetty_yclass_const_char_ptr, const char *);

/* --- Per-domain slot_table ---------------------------------------- */

/* Return the slot_table for `domain`, allocating one on first sighting
 * (until we run out of domain ids — current cap is
 * YETTY_YCLASS_METHOD_SLOT_MAX_DOMAINS - 1).
 * Errors: NULL domain, cap reached, allocation failure. */
struct yetty_yclass_slot_table_ptr_result yetty_yclass_slot_table_get(const char *domain);

/* --- Registration (one-shot, at yetty_yclass_register) ------------ */

/* Allocate (or look up) the slot for (`domain`, `name`). If first
 * sighting, binds `id` to it. Subsequent registrations with the same
 * (domain, name) return the existing slot. */
struct yetty_yclass_method_slot_result
yetty_yclass_method_slot_register(const char *domain, const char *name,
                                  yetty_yclass_method_id_t id);

/* --- Lookups (per call / per handshake) --------------------------- */

/* Dispatch: only the fn-ptr identity is available at the public stub,
 * but the stub knows its own (compile-time) domain. */
struct yetty_yclass_method_slot_result
yetty_yclass_method_slot_get(const char *domain, yetty_yclass_method_id_t id);

/* Local-name lookup within a domain. */
struct yetty_yclass_method_slot_result
yetty_yclass_method_slot_by_name(const char *domain, const char *name);

/* Wire-side lookup: caller passes the full "<domain>_<local_name>"
 * the remote sent. */
struct yetty_yclass_method_slot_result yetty_yclass_method_slot_by_qname(const char *qname);

/* Reverse: slot → fully qualified name (interned). */
struct yetty_yclass_const_char_ptr_result
yetty_yclass_method_slot_name(yetty_yclass_method_slot slot);

/* --- Dispatch / registry ------------------------------------------ */

/* Dispatch is a NORMAL flow: a class may not override every slot.
 * Returns NULL (not Result-error) when the class does not implement
 * the slot — callers walk inheritance / mixin chains themselves. */
yetty_yclass_impl_t yetty_yclass_dispatch_lookup(const struct yetty_yclass *cls,
                                                 yetty_yclass_method_slot slot);

const struct yetty_yclass *yetty_yclass_object_class(const struct yetty_yclass_object *obj);

struct yetty_yclass_ptr_result yetty_yclass_register(const struct yetty_yclass_descriptor *desc,
                                                     const struct yetty_yclass_op *ops,
                                                     size_t ops_count,
                                                     const struct yetty_yclass *parent,
                                                     const struct yetty_yclass *const *mixins,
                                                     size_t mixin_count);

/* uthash-backed. O(1) on cache hit. On miss, calls the installed lazy
 * accessor lookup (see yetty_yclass_add_accessor_lookup) — that's the
 * path the server uses to discover classes it has never touched. */
struct yetty_yclass_ptr_result yetty_yclass_by_name(const char *name);

/* Server-side lazy class registration hook. Each module's generated
 * `rpc.gen.c` adds its own accessor_lookup at startup via a constructor;
 * yetty_yclass_by_name walks the chain on registry miss until one
 * returns a non-error result with a non-NULL value. */
typedef struct yetty_yclass_ptr_result (*yetty_yclass_accessor_lookup_fn)(const char *name);
struct yetty_ycore_void_result
yetty_yclass_add_accessor_lookup(yetty_yclass_accessor_lookup_fn fn);

/* Walk the class's populated dispatch slots — used by the GET_CLASS
 * handler on the server. */
void yetty_yclass_for_each_slot(const struct yetty_yclass *cls,
                                void (*cb)(const char *name, yetty_yclass_method_slot slot,
                                           void *ud),
                                void *userdata);

struct yetty_yclass_object_ptr_result yetty_yclass_object_alloc(const struct yetty_yclass *cls);
void yetty_yclass_object_free(struct yetty_yclass_object *obj);

#endif /* YCLASS_CLASS_H */
