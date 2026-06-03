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

/* Remote-object proxy. Allocated on the client side by the generated
 * `<class>_create` when ctx->session is set; the embedded `header` is
 * what callers see (so `struct yetty_yclass_object *` keeps working
 * for both local and proxy objects), and `handle` is the server-side
 * id minted by RPC_OP_CREATE.
 *
 * Codegen reaches the handle via container_of(obj, struct
 * yetty_yclass_proxy, header)->handle in the remote-dispatch branch;
 * a dedicated struct keeps the uint64_t naturally aligned (the prior
 * "header byte + raw uint64" layout was misaligned on 32-bit ABIs
 * where sizeof(struct yetty_yclass_object) == 4). */
struct yetty_yclass_proxy {
    struct yetty_yclass_object header;
    uint64_t handle;
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
YETTY_YRESULT_DECLARE(yetty_yclass_impl_t, yetty_yclass_impl_t);
YETTY_YRESULT_DECLARE(yetty_yclass_const_char_ptr, const char *);
YETTY_YRESULT_DECLARE(yetty_yclass_void_ptr, void *); /* opaque ptr (data slice / handle) */

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

/* Look up the impl for a slot on this class's dispatch table. Errors:
 * NULL cls, invalid slot encoding, no impl registered for the slot
 * (callers may treat that as "walk the inheritance chain" if their
 * dispatch model supports it). */
struct yetty_yclass_impl_t_result
yetty_yclass_dispatch_lookup(const struct yetty_yclass *cls, yetty_yclass_method_slot slot);

/* Returns the class pointer the object was minted under. Errors:
 * NULL obj. */
struct yetty_yclass_ptr_result
yetty_yclass_object_class(const struct yetty_yclass_object *obj);

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
struct yetty_ycore_void_result
yetty_yclass_for_each_slot(const struct yetty_yclass *cls,
                           void (*cb)(const char *name, yetty_yclass_method_slot slot,
                                      void *ud),
                           void *userdata);

/* --- Inheritance / layout accessors ------------------------------- */
/*
 * Modules that allocate their own object headers (ygui's `struct
 * yetty_ygui_object` carries parent/sibling/id state alongside the
 * yclass identity) need to walk the class's parent + mixin chain
 * themselves to compute per-instance data-slice offsets. These
 * getters expose the bits previously hidden in the opaque
 * `struct yetty_yclass`.
 *
 * Errors: every getter rejects NULL `cls`; `mixin_at` also rejects
 * out-of-range `index`.
 */
struct yetty_yclass_ptr_result yetty_yclass_parent(const struct yetty_yclass *cls);
struct yetty_ycore_size_result yetty_yclass_mixin_count(const struct yetty_yclass *cls);
struct yetty_yclass_ptr_result yetty_yclass_mixin_at(const struct yetty_yclass *cls, size_t index);
struct yetty_ycore_size_result yetty_yclass_data_size(const struct yetty_yclass *cls);
struct yetty_yclass_const_char_ptr_result yetty_yclass_name(const struct yetty_yclass *cls);
struct yetty_yclass_const_char_ptr_result
yetty_yclass_type_str(const struct yetty_yclass *cls); /* "regular" or "mixin" */

struct yetty_yclass_object_ptr_result yetty_yclass_object_alloc(const struct yetty_yclass *cls);
struct yetty_ycore_void_result yetty_yclass_object_free(struct yetty_yclass_object *obj);

/* Pointer to `cls`'s data slice inside `obj` — the yclass data model
 * (each class in the chain contributes a slice, parent slices first).
 * `cls` must be in `obj`'s class chain. Modules wrap this in a typed,
 * per-class accessor (codegen emits `yetty_<module>_<class>_data`). */
struct yetty_yclass_void_ptr_result yetty_yclass_object_data(struct yetty_yclass_object *obj,
                                                             const struct yetty_yclass *cls);

#endif /* YCLASS_CLASS_H */
