/* Tiny class/object runtime — per-domain slot tables.
 *
 * Each module ("domain") owns its own slot_table with its own 0-based
 * local index space. A method_slot value is the packed pair
 * (domain_id << 24) | local_idx:
 *
 *     bits 31..28   reserved (always 0 for valid slots; UINT32_MAX
 *                   is the sentinel METHOD_SLOT_UNDEFINED).
 *     bits 27..24   domain_id (1..15; 0 is invalid / never returned).
 *     bits 23..0    local index inside that domain's slot_table.
 *
 * The wire treats method_slot as opaque — both sides exchange 28-bit
 * ids that round-trip through this packing. The encoding fits inside
 * the rpc header's 28-bit id field.
 *
 * Every fallible runtime entry point returns a Result type from
 * result.h. Callers check YETTY_IS_ERR and either propagate
 * (YETTY_RETURN_IF_ERR) or absorb at a boundary. */

#ifndef POC_CLASS_H
#define POC_CLASS_H

#include "result.h"

#include <stddef.h>
#include <stdint.h>

struct object;
struct class;
struct slot_table;
struct rpc_session;

struct ctx {
    struct rpc_session *session; /* NULL → local; set → remote */
};

enum class_type {
    CLASS_TYPE_REGULAR = 0,
    CLASS_TYPE_MIXIN = 1,
};

typedef void (*method_id_t)(void);
typedef void (*impl_t)(void);
typedef uint32_t method_slot;
#define METHOD_SLOT_UNDEFINED UINT32_MAX

#define METHOD_SLOT_DOMAIN_BITS 4
#define METHOD_SLOT_MAX_DOMAINS (1u << METHOD_SLOT_DOMAIN_BITS)

/* Deepest inheritance chain object_data_offset walks (root → derived).
 * The PoC hierarchies are shallow; this is a generous safety cap. */
#define OBJECT_MAX_DEPTH 32

#define METHOD_SLOT_INDEX_SHIFT 24
#define METHOD_SLOT_INDEX_MASK ((1u << METHOD_SLOT_INDEX_SHIFT) - 1)
#define METHOD_SLOT_DOMAIN_OF(s) (((s) >> METHOD_SLOT_INDEX_SHIFT) & 0xFu)
#define METHOD_SLOT_INDEX_OF(s) ((s) & METHOD_SLOT_INDEX_MASK)
#define METHOD_SLOT_PACK(dom, idx)                                                                 \
    (((uint32_t)(dom) << METHOD_SLOT_INDEX_SHIFT) | ((idx) & METHOD_SLOT_INDEX_MASK))

struct class_descriptor {
    const char *name; /* qualified, e.g. "yvehicle_sportscar" */
    enum class_type type;
    size_t data_size;
};

struct op {
    const char *slot_domain; /* slot's owning module, e.g. "yvehicle" */
    const char *name;        /* slot local name, e.g. "vehicle_describe" */
    method_id_t method_id;   /* the public-stub fn ptr (vtable key) */
    impl_t impl;             /* override for this slot */
};

struct object {
    const struct class *klass;
};

struct str {
    char buf[128];
};

/* --- Result types ------------------------------------------------- */
/* Per the project convention, every fallible entry point returns a
 * Result. For pointer types the type identifier suffix is `_ptr`.
 * Slot-domain return types (struct str, etc.) are declared here once
 * so generated methods.gen.h files don't redeclare them per module. */
YETTY_YRESULT_DECLARE(slot_table_ptr, struct slot_table *);
YETTY_YRESULT_DECLARE(class_ptr, const struct class *);
YETTY_YRESULT_DECLARE(object_ptr, struct object *);
YETTY_YRESULT_DECLARE(method_slot, method_slot);
YETTY_YRESULT_DECLARE(impl, impl_t);
YETTY_YRESULT_DECLARE(const_char_ptr, const char *);
YETTY_YRESULT_DECLARE(str, struct str);

/* --- Per-domain slot_table ---------------------------------------- */

/* Return the slot_table for `domain`, allocating one on first sighting
 * (until we run out of domain ids — current cap is METHOD_SLOT_MAX_DOMAINS - 1).
 * Errors: NULL domain, cap reached, allocation failure. */
struct slot_table_ptr_result slot_table_get(const char *domain);

/* --- Registration (one-shot, at class_register) ------------------- */

/* Allocate (or look up) the slot for (`domain`, `name`). If first
 * sighting, binds `id` to it. Subsequent registrations with the same
 * (domain, name) return the existing slot. */
struct method_slot_result method_slot_register(const char *domain, const char *name,
                                               method_id_t id);

/* --- Lookups (per call / per handshake) --------------------------- */

/* Dispatch: only the fn-ptr identity is available at the public stub,
 * but the stub knows its own (compile-time) domain. */
struct method_slot_result method_slot_get(const char *domain, method_id_t id);

/* Local-name lookup within a domain. */
struct method_slot_result method_slot_by_name(const char *domain, const char *name);

/* Wire-side lookup: caller passes the full "<domain>_<local_name>"
 * the remote sent. */
struct method_slot_result method_slot_by_qname(const char *qname);

/* Reverse: slot → fully qualified name (interned). */
struct const_char_ptr_result method_slot_name(method_slot slot);

/* --- Dispatch / registry ------------------------------------------ */

/* Dispatch is a NORMAL flow: a class may not override every slot.
 * Returns NULL (not Result-error) when the class does not implement
 * the slot — callers walk inheritance / mixin chains themselves. */
impl_t class_dispatch_lookup(const struct class *cls, method_slot slot);

const struct class *object_class(const struct object *obj);

struct class_ptr_result class_register(const struct class_descriptor *desc, const struct op *ops,
                                       size_t ops_count, const struct class *parent,
                                       const struct class *const *mixins, size_t mixin_count);

/* uthash-backed. O(1) on cache hit. On miss, calls the installed lazy
 * accessor lookup (see class_add_accessor_lookup) — that's the path
 * the server uses to discover classes it has never touched. */
struct class_ptr_result class_by_name(const char *name);

/* Server-side lazy class registration hook. Each module's generated
 * `rpc.gen.c` adds its own accessor_lookup at startup via a constructor;
 * class_by_name walks the chain on registry miss until one returns a
 * non-error result with a non-NULL value. */
typedef struct class_ptr_result (*accessor_lookup_fn)(const char *name);
struct yetty_ycore_void_result class_add_accessor_lookup(accessor_lookup_fn fn);

/* Walk the class's populated dispatch slots — used by the GET_CLASS
 * handler on the server. */
void class_for_each_slot(const struct class *cls,
                         void (*cb)(const char *name, method_slot slot, void *ud), void *userdata);

struct object_ptr_result object_alloc(const struct class *cls);
void object_free(struct object *obj);

/* --- Data-member access ------------------------------------------- */

/* Byte offset (from the object header) of `target`'s data block inside an
 * instance whose most-derived class is `obj_class`. `target` may be
 * `obj_class` itself, any ancestor, or any mixin used along the chain. The
 * walk reproduces the layout object_alloc reserves: object header, then each
 * class root → derived, each immediately followed by that class's mixins.
 * Errors if `target` is not in `obj_class`'s layout. The generated per-class
 * data handle (`<domain>_<class>_data`) and the member getters/setters are
 * thin typed wrappers over this. */
struct yetty_ycore_size_result object_data_offset(const struct class *obj_class,
                                                  const struct class *target);

#endif /* POC_CLASS_H */
