/* Tiny class/object runtime.
 *
 * Two translations live in this PoC; this header owns ONE of them:
 *
 *   T1 (name <-> local slot id) — slot_table, populated when class_register
 *      walks its ops. uthash-indexed by BOTH name and id. Used on every
 *      side independently.
 *
 *   T2 (local id <-> remote id) — lives in the RPC layer (see rpc.h),
 *      only on the client. Populated by the server's RESOLVE/GET_CLASS
 *      handshakes. */

#ifndef POC_CLASS_H
#define POC_CLASS_H

#include <stddef.h>
#include <stdint.h>

struct object;
struct class;
struct rpc_session;

struct ctx {
    struct rpc_session *session;   /* NULL → local; set → remote */
};

enum class_type {
    CLASS_TYPE_REGULAR = 0,
    CLASS_TYPE_MIXIN = 1,
};

typedef void (*method_id_t)(void);
typedef void (*impl_t)(void);
typedef uint32_t method_slot;
#define METHOD_SLOT_UNDEFINED  UINT32_MAX

struct class_descriptor {
    const char *name;
    enum class_type type;
    size_t data_size;
};

struct op {
    const char *name;       /* slot identity — same on every side */
    method_id_t method_id;  /* the public-stub fn ptr (vtable key) */
    impl_t      impl;       /* override for this slot */
};

struct object {
    const struct class *klass;
};

struct str {
    char buf[128];
};

/* --- Registration (one-shot, at class_register) ------------------- */

/* Allocate (or look up) the slot identified by `name`. If first sighting,
 * binds `id` to it. Subsequent registrations with the same name return
 * the existing slot. Indexed in slot_table by name AND id. O(1). */
method_slot method_slot_register(const char *name, method_id_t id);

/* --- Lookups (per call / per handshake) --------------------------- */

/* Dispatch: only the fn-ptr identity is available at the public stub.
 * Returns the slot or METHOD_SLOT_UNDEFINED. O(1). */
method_slot method_slot_get(method_id_t id);

/* Handshake interpretation (client) / RESOLVE handling (server). O(1). */
method_slot method_slot_by_name(const char *name);

/* Reverse: slot → name (interned). NULL if unknown. */
const char *method_slot_name(method_slot slot);

/* --- Dispatch / registry ------------------------------------------ */

impl_t class_dispatch_lookup(const struct class *cls, method_slot slot);

const struct class *object_class(const struct object *obj);

const struct class *class_register(
    const struct class_descriptor *desc, const struct op *ops,
    size_t ops_count, const struct class *parent,
    const struct class *const *mixins, size_t mixin_count);

/* uthash-backed. O(1) on cache hit. On miss, calls the installed lazy
 * accessor lookup (see class_set_accessor_lookup) — that's the path the
 * server uses to discover classes it has never touched. */
const struct class *class_by_name(const char *name);

/* Server-side lazy class registration hook. Each module's generated
 * `rpc.gen.c` adds its own accessor_lookup at startup via a constructor;
 * class_by_name walks the chain on registry miss until one returns a
 * non-NULL class. */
typedef const struct class *(*accessor_lookup_fn)(const char *name);
void class_add_accessor_lookup(accessor_lookup_fn fn);


/* Walk the class's populated dispatch slots — used by the GET_CLASS
 * handler on the server. */
void class_for_each_slot(const struct class *cls,
                         void (*cb)(const char *name, method_slot slot, void *ud),
                         void *userdata);

struct object *object_alloc(const struct class *cls);
void object_free(struct object *obj);

#endif /* POC_CLASS_H */
